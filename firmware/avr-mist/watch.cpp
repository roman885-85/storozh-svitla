#include "watch.h"
#include "stats.h"
#include "signals.h"
#include "settings.h"
#include "bridge.h"
#include "tg.h"
#include "util.h"
#include "yasno.h"
#include "version.h"
#include "lcd.h"
#include "box.h"
#include "rtc_watch.h"
#include "bms.h"
#include <WiFi.h>
#include <WiFiUdp.h>

//  Маячки (1.0.23, власник: «добавление маячков»). Список — S.beacons, рядки «MAC роль назва»: g — генератор, l — лінія
//  чи фаза, v — лише показувати. Розбирається в bc[] під час запуску й після змін на сторінці. Чуті маячки — bs[], лише в
//  пам'яті: невідомий маячок з'являється в таблиці на сторінці з кнопкою «Додати». Список порожній, а «маячок
//  встановлено» (mayakOn, до 1.0.23) — будь-який маячок вважається маячком генератора, як і раніше.
constexpr int MAXB = 6, MAXSEEN = 8;
struct BSeen { bool used = false; uint8_t mac[6] = {0}; uint32_t ip = 0; int64_t firstUs = 0, lastUs = 0; uint32_t up = 0; int rssi = 0; char ver[16] = ""; };
struct BCfg { uint8_t mac[6] = {0}; char role = 'v'; String name; Track t; };
struct LChg { bool on; int64_t at, fromSince; bool fromExact; };   // зміна на лінії, що чекає надсилання

static Track grid, line;
static BSeen bs[MAXSEEN];
static BCfg bc[MAXB];
static int nbc = 0;
static LChg lpend[MAXB];
static bool lhas[MAXB];
static int64_t lbatchUs = 0;                       // коли перша зміна на лініях чекає; 0 — нічого не чекає
static bool rwGridTried = false, rwLineTried = false;  // відновлення з RTC — лише при першому визначенні після запуску
static bool rwGridSame = false;                         // місто після перезапуску — як і до нього (повідомлення про запуск — тихе)
static bool gridChecked = true, lineChecked = true;     // 1.0.29: стан, відновлений одразу після запуску, ще не звірено з АВР/маячком
static bool bootRestoreDone = false;
static WiFiUDP udp;
static int64_t bootUs = 0;
static bool startAnnounced = false;
static int64_t bothSince = 0, lastRemindUs = 0;     // місто є, а генератор ще працює: відколи й коли востаннє нагадували
static int reminds = 0;
static const int64_t BOOT_GRACE = 90LL * 1000000;    // після запуску АВР має час знайти точку
static const int64_t PING_FRESH = 2500000;           // 1.0.29: ping щосекунди; 2 пропуски поспіль — «мовчить» (було 2 с і 8 с)
static const int64_t JOIN_WINDOW = 15LL * 1000000;   // щойно під'єднався — ще отримує адресу
static const int64_t BEAT_FRESH = 7LL * 1000000;     // маячок шле кожні 2 с
static const uint16_t UDP_PORT = 47801;

static String obj() { return S.objName.length() ? S.objName : String("Об'єкт"); }
static bool avrAssigned() { uint8_t m[6]; return strToMac(S.avrMac, m); }
//  Маячок (1.0.17, власник 18.11.2026): «подключён к выходу напряжения генератора и активен только когда работает
//  генератор». Отже маячок на зв'язку = генератор працює. Рахується лише з галочкою «Маячок встановлено»: до неї
//  маячок на столі (від USB комп'ютера) не повинен казати «генератор працює».
static bool legacy() { return nbc == 0 && S.mayakOn; }
static bool hasRole(char r) { for (int i = 0; i < nbc; i++) if (bc[i].role == r) return true; return false; }
static bool lineEnabled() { return legacy() || hasRole('g'); }
static BSeen* seenFor(const uint8_t* m) { for (auto& b : bs) if (b.used && !memcmp(b.mac, m, 6)) return &b; return nullptr; }
static bool aliveB(const BSeen* b, int64_t now) { return b && now - b->lastUs < BEAT_FRESH; }
static int cfgIdx(const uint8_t* m) { for (int i = 0; i < nbc; i++) if (!memcmp(bc[i].mac, m, 6)) return i; return -1; }
static String bname(int i) { return bc[i].name.length() ? bc[i].name : "маячок " + macToStr(bc[i].mac).substring(9); }

//  Розбір S.beacons. Стан лінії маячка, що лишився з тією самою роллю, не скидається (перейменування не дає «визначається»).
static void parseBeacons() {
  BCfg old[MAXB];
  int nold = nbc;
  for (int i = 0; i < nbc; i++) old[i] = bc[i];
  nbc = 0;
  const String& s = S.beacons;
  int from = 0;
  while (from < (int)s.length() && nbc < MAXB) {
    int e = s.indexOf('\n', from);
    if (e < 0) e = s.length();
    String l = s.substring(from, e);
    from = e + 1;
    l.trim();
    uint8_t m[6];
    if (l.length() < 19 || !strToMac(l.substring(0, 17), m) || cfgIdx(m) >= 0) continue;
    BCfg& b = bc[nbc];
    b = BCfg();
    memcpy(b.mac, m, 6);
    b.role = (l[18] == 'g' || l[18] == 'l') ? l[18] : 'v';
    b.name = l.length() > 20 ? l.substring(20) : String();
    b.name.trim();
    for (int k = 0; k < nold; k++) if (!memcmp(old[k].mac, m, 6) && old[k].role == b.role) b.t = old[k].t;
    nbc++;
  }
  for (int i = 0; i < MAXB; i++) lhas[i] = false;   // номери в списку могли змінитися — незіслані зміни ліній відкидаємо
  lbatchUs = 0;
}
void watchReload() { parseBeacons(); }
static bool genOn() { return lineEnabled() && line.st == T_ON; }
static void bothStart() { bothSince = lastRemindUs = nowUs(); reminds = 0; }

void watchBegin() {
  bootUs = nowUs();
  parseBeacons();
  udp.begin(UDP_PORT);
}

static void pollUdp() {
  for (int k = 0; k < 8; k++) {
    int n = udp.parsePacket();
    if (n <= 0) break;
    char b[600];                                     // 1.0.36: пакет мосту батареї — до ~450 байт
    int len = udp.read(b, sizeof(b) - 1);
    if (len < 0) len = 0;
    b[len] = 0;
    IPAddress rip = udp.remoteIP();
    if (rip[0] != 10 || rip[1] != 77 || rip[2] != 0) continue;          // лише з мережі моста
    if (!strncmp(b, "BMS1 ", 5)) { bmsPacket(b, (uint32_t)rip); continue; }   // 1.0.36: міст батареї
    if (strncmp(b, "MAYAK1 ", 7)) continue;
    char macs[24] = {0}, ver[16] = {0};
    unsigned long up = 0;
    int rssi = 0;
    if (sscanf(b + 7, "%23s up=%lu rssi=%d ver=%15s", macs, &up, &rssi, ver) < 1) continue;
    uint8_t m[6];
    if (!strToMac(macs, m)) continue;
    BSeen* bp = seenFor(m);
    if (!bp) {                                       // новий: вільне місце або найдавніше чутий
      bp = &bs[0];
      for (auto& x : bs) {
        if (!x.used) { bp = &x; break; }
        if (x.lastUs < bp->lastUs) bp = &x;
      }
      *bp = BSeen();
      bp->used = true;
      memcpy(bp->mac, m, 6);
      bp->firstUs = nowUs();
      int ci = cfgIdx(m);
      logEvent(String("Маячок на зв'язку: ") + macs + (ci >= 0 ? " («" + bname(ci) + "»)" : nbc ? String(" — його немає в списку маячків") : String()));
    }
    bp->ip = (uint32_t)rip;
    bp->lastUs = nowUs();
    bp->up = up;
    bp->rssi = rssi;
    strlcpy(bp->ver, ver, sizeof bp->ver);
  }
}

/*  Затримка на обидва боки. Момент події: поява — коли вперше побачили;
    зникнення — коли востаннє бачили живим.  */
struct Change { Tri from; int64_t fromSince; bool fromExact; };
static bool step(Track& t, bool raw, int64_t now, int64_t onUs, int64_t offUs, bool mayOff, Change& ch) {
  if (raw) t.lastTrueUs = now;
  if (raw != t.raw || t.rawSince == 0) { t.raw = raw; t.rawSince = now; }
  int64_t held = now - t.rawSince;
  Tri want = t.st;
  if (raw && t.st != T_ON && held >= onUs) want = T_ON;
  if (!raw && t.st != T_OFF && held >= offUs && mayOff) want = T_OFF;
  if (want == t.st) return false;
  ch.from = t.st;
  ch.fromSince = t.since;
  ch.fromExact = t.exact;
  int64_t at = (want == T_ON) ? t.rawSince : (t.lastTrueUs ? t.lastTrueUs : t.rawSince);
  t.exact = (t.st != T_UNK);
  t.since = t.exact ? at : bootUs;
  t.st = want;
  return true;
}

static String durSince(const Change& c, int64_t at) {
  String d = fmtHours((at - c.fromSince) / 1000000);
  return c.fromExact ? d : "щонайменше " + d;
}

//  Оформлення повідомлень (1.0.6, власник: «оформи красиво оповещения»). Перший рядок — значок і суть жирним: його
//  видно вже у сповіщенні на екрані телефона. Другий — об'єкт і час події, далі подробиці. Режим HTML Telegram,
//  усе змінне — через htmlEsc (не прийме оформлення — tg.cpp надішле той самий текст без нього).
static String head(const char* icon, const char* title, const String& when) {
  return String(icon) + " <b>" + title + "</b>\n" + objIco() + htmlEsc(obj()) + (when.length() ? " · " + when : String());
}
static String statusIconText() {
  if (!avrAssigned()) return "⚪ " + statusShort();
  if (grid.st == T_UNK) return "⏳ " + statusShort();
  if (grid.st == T_ON && genOn()) return "⚠️ " + statusShort();
  return String(grid.st == T_ON ? "🟢 " : "🔴 ") + statusShort();
}
String testMessage() { return head("✅", "Перевірка зв'язку", "%T") + "\nЗараз: " + statusIconText(); }
String pwResetMessage() {
  return head("🔑", "Пароль сторінки скинуто", "%T") + "\nНа Сторожі утримали кнопку скидання пароля. Пароль сторінки тепер типовий — "
         "увійдіть з ним і задайте свій: «Налаштування → Пароль сторінки». Якщо це були не ви — зробіть це якнайшвидше.";
}
String factoryMessage(const String& who) {
  return head("⚠️", "Сторожа скинуто до заводських налаштувань", "%T") + "\nЗі сторінки, пристрій " + htmlEsc(who) +
         ". Сповіщення з цього Сторожа припиняються, доки його не налаштують знову.";
}

// ---------- стан і відколи — у RTC (1.0.26) ----------
static const uint32_t RW_MAGIC = 0x52574131;           // «RWA1»
static uint32_t rwSum(const RtcWatchData& d) {         // FNV-1a — без ROM-функцій, щоб перевірялось і на Mac
  const uint8_t* p = (const uint8_t*)&d;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof d; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
//  Записати поточний стан. Поки щось ще визначається після запуску — ні: інакше «невідомо» затерло б те, що ще не відновили.
static void rwSave() {
  if (!clockValid()) return;
  if (avrAssigned() && grid.st == T_UNK) return;
  if (lineEnabled() && line.st == T_UNK) return;
  int64_t ep = (int64_t)time(nullptr), now = nowUs();
  RtcWatchData d;
  memset(&d, 0, sizeof d);
  d.gst = grid.st;
  d.lst = line.st;
  d.gex = grid.exact;
  d.lex = line.exact;
  d.gsince = ep - (now - grid.since) / 1000000;
  d.lsince = ep - (now - line.since) / 1000000;
  d.savedAt = ep;
  RW.d = d;
  RW.sum = rwSum(RW.d);
  RW.magic = RW_MAGIC;
}
//  Чи можна вірити записаному: ціле, годинник є і записано недавно (≤ 10 хв тому — перезапуск, а не довга перерва)
static bool rwUsable() {
  if (RW.magic != RW_MAGIC || RW.sum != rwSum(RW.d) || !clockValid()) return false;
  int64_t ep = (int64_t)time(nullptr);
  return RW.d.savedAt <= ep + 5 && ep - RW.d.savedAt <= 600;
}
static int64_t usOfEpoch(int64_t ep) { return nowUs() - ((int64_t)time(nullptr) - ep) * 1000000; }
//  Перше визначення після запуску: той самий стан, що до перезапуску, — відновити «відколи» (і не рахувати нове відключення);
//  інший — це справжня зміна за час перезапуску: повідомити, як про звичайну. true — стан той самий, відновлено.
static bool rwRestore(Track& t, Change& c, int8_t st, uint8_t ex, int64_t since, bool& tried) {
  if (tried || c.from != T_UNK) return false;
  tried = true;
  if (!rwUsable() || (st != T_ON && st != T_OFF)) return false;
  if (st == t.st) {
    t.since = usOfEpoch(since);
    t.exact = ex;
    return true;
  }
  c.from = (Tri)st;
  c.fromSince = usOfEpoch(since);
  c.fromExact = ex;
  return false;
}

//  1.0.29 (власник 20.11.2026: «обновление состояния происходит долго и с лишними переходами — свет отключили, пауза,
//  потом инициализация, потом через две минуты состояние отключенного света»). Після перезапуску стан і «відколи» з RTC
//  ставимо одразу, щойно є годинник: дисплей, стрічка й облік не проходять через «визначається» (раніше — до 90 с).
//  Відновлене звіряємо з тим, що бачимо: АВР з'явився — «є» підтверджено; не з'явився за 90 с — справжня зміна
//  «Зникло» з повідомленням; так само «немає» → «Повернулось», щойно АВР на зв'язку.
static void rwBootRestore() {
  if (!rwUsable()) return;
  if (avrAssigned() && grid.st == T_UNK && (RW.d.gst == T_ON || RW.d.gst == T_OFF)) {
    grid.st = (Tri)RW.d.gst;
    grid.since = usOfEpoch(RW.d.gsince);
    grid.exact = RW.d.gex;
    rwGridTried = rwGridSame = true;
    gridChecked = false;
    logEvent(String("Місто: «") + (grid.st == T_ON ? "є" : "немає") + "» " + (grid.exact ? "з " : "щонайменше з ") + fmtHM(grid.since) +
             " — пам'ятав до перезапуску, звіряю з АВР");
  }
  if (lineEnabled() && line.st == T_UNK && (RW.d.lst == T_ON || RW.d.lst == T_OFF)) {
    line.st = (Tri)RW.d.lst;
    line.since = usOfEpoch(RW.d.lsince);
    line.exact = RW.d.lex;
    rwLineTried = true;
    lineChecked = false;
    logEvent(String("Генератор: «") + (line.st == T_ON ? "працює" : "не працює") + "» " + (line.exact ? "з " : "щонайменше з ") + fmtHM(line.since) +
             " — пам'ятав до перезапуску, звіряю з маячком");
  }
}

static void onGrid(const Change& c, bool restored = false) {
  if (!restored) statsTransition(c.from, grid.st, grid.since, !grid.exact);   // відновлено — відключення те саме, не нове
  sigEvent(c.from, grid.st);
  logEvent(grid.st == T_ON ? "Місто: є (АВР на зв'язку)" : "Місто: немає (АВР зник із мережі)");
  if (restored) logEvent(String("Місто: «") + (grid.st == T_ON ? "є" : "немає") + "» " + (grid.exact ? "з " : "щонайменше з ") + fmtHM(grid.since) + " — пам'ятав до перезапуску");
  if (c.from == T_UNK) return;                   // перше визначення після запуску — скаже повідомлення про запуск
  lcdAlert(grid.st == T_ON ? LCD_A_BACK : LCD_A_LOST);   // 1.0.26: спалахи й миготіння на дисплеї
  if (grid.st == T_OFF) {
    String m = head("🔴", "Зникло міське світло", "%T") + "\n⏱ Світло було " + durSince(c, grid.since);
    if (genOn()) m += "\n🟡 Генератор працює";
    else if (lineEnabled() && line.st == T_OFF) m += "\n⚫ Генератор не працює";
    m += schLostNote();                          // 1.0.20: за графіком YASNO чи ні
    tgBroadcast(grid.since, m, false, RF_LIGHT);
  } else {
    String m = head("🟢", "Міське світло повернулось", "%T") + "\n⏱ Не було " + durSince(c, grid.since);
    if (genOn()) { m += "\n⚠️ <b>Генератор ще працює — вимкніть його</b>"; bothStart(); }
    m += schBackNote();                          // 1.0.20: наступне відключення за графіком
    tgBroadcast(grid.since, m, false, genOn() ? (RF_LIGHT | RF_GEN) : RF_LIGHT);
  }
}

static void onLine(const Change& c, bool restored = false) {
  logEvent(line.st == T_ON ? "Генератор: працює (маячок на зв'язку)" : "Генератор: не працює (маячок мовчить)");
  if (restored) logEvent(String("Генератор: «") + (line.st == T_ON ? "працює" : "не працює") + "» " + (line.exact ? "з " : "щонайменше з ") + fmtHM(line.since) + " — пам'ятав до перезапуску");
  if (c.from == T_UNK) return;
  lcdAlert(LCD_A_GEN);                           // 1.0.26: миготить стан на дисплеї
  if (line.st == T_ON) {
    if (grid.st == T_OFF)
      tgBroadcast(line.since, head("🟡", "Працює генератор", "%T") + "\n🔴 Міського світла немає " +
                              (grid.exact ? "з " + fmtHM(grid.since) : "щонайменше " + fmtDur((nowUs() - grid.since) / 1000000)), false, RF_GEN);
    else if (grid.st == T_ON) {
      tgBroadcast(line.since, head("⚠️", "Генератор працює, а міське світло є", "%T") + "\nВимкніть генератор, якщо його не перевіряють.", false, RF_GEN);
      bothStart();
    }
  } else {
    if (grid.st == T_OFF)
      tgBroadcast(line.since, head("⚫", "Генератор зупинився", "%T") + "\n⏱ Працював " + durSince(c, line.since) + "\n⚠️ Світла немає ні з міста, ні від генератора", false, RF_GEN | RF_LIGHT);
    else if (grid.st == T_ON)
      tgBroadcast(line.since, head("✅", "Генератор вимкнено", "%T") + "\n⏱ Працював " + durSince(c, line.since) + "\n🟢 Міське світло є", false, RF_GEN);
  }
}

//  Маячки на лініях (1.0.23). Зміну одразу пишемо в журнал, а в Telegram — одним повідомленням на всі лінії, що змінилися
//  за 10 с. Лінія зникла (з'явилась) разом із містом — АВР у ту саму хвилину замовк (озвався) — повідомлення без звуку:
//  про місто вже було зі звуком.
static void onBLine(int i, const Change& c) {
  const Track& t = bc[i].t;
  logEvent("Лінія «" + bname(i) + "»: " + (t.st == T_ON ? "є напруга (маячок на зв'язку)" : "немає напруги (маячок мовчить)"));
  if (c.from == T_UNK) return;
  lpend[i] = {t.st == T_ON, t.since, c.fromSince, c.fromExact};
  lhas[i] = true;
  if (!lbatchUs) lbatchUs = nowUs();
}

static void flushLines(int64_t now) {
  if (!lbatchUs || now - lbatchUs < 10LL * 1000000) return;
  //  Разом із містом (АВР замовк чи озвався в ту саму хвилину), а повідомлення про місто ще не пішло — його затримка довша
  //  (типово 30 с проти 15): чекаємо його, щоб «🔌» прийшло після «Зникло міське світло», але не довше затримки міста + 15 с.
  bool any = false, allGrid = true, gridSaid = true;
  for (int i = 0; i < nbc; i++) {
    if (!lhas[i] || bc[i].role != 'l') continue;
    any = true;
    const LChg& x = lpend[i];
    int64_t dt = grid.rawSince - x.at;
    if (!avrAssigned() || grid.raw != x.on || dt > 90LL * 1000000 || dt < -90LL * 1000000) allGrid = false;
    if (grid.st != (x.on ? T_ON : T_OFF)) gridSaid = false;
  }
  int64_t waitMax = ((int64_t)(S.offSec > S.onSec ? S.offSec : S.onSec) + 15) * 1000000;
  if (any && allGrid && !gridSaid && now - lbatchUs < waitMax) return;
  lbatchUs = 0;
  int n = 0, first = -1;
  int64_t ev = 0;
  String body;
  for (int i = 0; i < nbc; i++) {
    if (!lhas[i]) continue;
    lhas[i] = false;
    if (bc[i].role != 'l') continue;
    const LChg& x = lpend[i];
    n++;
    if (first < 0) first = i;
    if (!ev || x.at < ev) ev = x.at;
    String d = fmtHours((x.at - x.fromSince) / 1000000);
    if (!x.fromExact) d = "щонайменше " + d;
    body += String(x.on ? "\n🟢 " : "\n🔴 ") + "<b>" + htmlEsc(bname(i)) + "</b> — " + (x.on ? "є напруга · не було " : "немає напруги · була ") + d;
  }
  if (!n) return;
  String m = head("🔌", n > 1 ? "Зміни на лініях" : lpend[first].on ? "Напруга на лінії є" : "Немає напруги на лінії", "%T") + body;
  if (allGrid) m += "\nРазом із міським світлом";
  else if (genOn()) m += "\n🟡 Працює генератор";
  tgBroadcast(ev, m, allGrid, RF_LINE);
}

//  Місто є, а генератор ще працює: нагадувати «вимкніть» кожні S.genRemMin хв, до 6 разів (1.0.17). Перше повідомлення —
//  у «Міське світло повернулось» чи «Генератор працює, а міське світло є»; після запуску Сторожа в такому стані —
//  перше нагадування за S.genRemMin хв (повідомлення про запуск і так каже «генератор ще працює»).
static void checkGenRemind(int64_t now) {
  if (grid.st != T_ON || !genOn()) { bothSince = 0; return; }
  if (!bothSince) { bothStart(); return; }
  if (!S.genRemMin || reminds >= 6 || now - lastRemindUs < (int64_t)S.genRemMin * 60 * 1000000) return;
  lastRemindUs = now;
  reminds++;
  tgBroadcast(now, head("⚠️", "Генератор досі працює", "%T") + "\nМіське світло є " + (grid.exact ? "з " + fmtHM(grid.since) : "щонайменше " + fmtDur((now - grid.since) / 1000000)) +
                   ", генератор працює " + fmtDur((now - line.since) / 1000000) + " — вимкніть його." +
                   (reminds >= 6 ? "\nБільше не нагадую. Якщо генератор вимкнений, а повідомлення приходять — маячок не в розетці від генератора." : ""), false, RF_GEN);
}

static void announceStart(bool afterGrace) {
  if (startAnnounced) return;
  bool gridKnown = !avrAssigned() || (grid.st != T_UNK && gridChecked);
  bool lineKnown = !lineEnabled() || (line.st != T_UNK && lineChecked);
  if (!(gridKnown && lineKnown) && !afterGrace) return;
  startAnnounced = true;
  logEvent("Сторож світла увімкнувся (" + resetReasonText() + "). Зараз: " + statusShort());
  //  1.0.29 (власник: «в бот приходят частые отчеты о перезапуске»): лише адміністраторам, без звуку, і лише коли перезапуск
  //  несподіваний — збій, сторож, зникнення живлення. Оновлення прошивки й перезапуск зі сторінки — дія людини: мовчимо.
  //  Зміна світла за час перезапуску й так приходить окремим «Зникло» / «Повернулось».
  if (S.notifyStart && !bootAfterUpdate() && !boxPlanned())
    tgBroadcast(bootUs, head("ℹ️", "Сторож світла перезапустився", "%T") + "\nПричина: " + htmlEsc(resetReasonText()) +
                            "\nЗараз: " + statusIconText() + "\n<i>Що було перед тим — «📋 Журнал».</i>", true, C_ADMIN);
}

void watchLoop() {
  pollUdp();
  int64_t now = nowUs();
  static int64_t last = 0;
  if (now - last < 250000) return;
  last = now;
  bool afterGrace = now - bootUs > BOOT_GRACE;
  Change c;
  if (!bootRestoreDone && (clockValid() || afterGrace)) { bootRestoreDone = true; rwBootRestore(); }

  // --- АВР: місто ---
  uint8_t am[6];
  if (strToMac(S.avrMac, am)) {
    bool raw = false;
    ApDev cl;
    bool found = findClient(am, cl);
    bool present = false;
    if (found && cl.assoc) {
      bool fresh = cl.lastPingOkUs && now - cl.lastPingOkUs < PING_FRESH;
      bool joining = now - cl.assocUs < JOIN_WINDOW;
      present = cl.pingWorks ? (fresh || joining) : true;   // не відповідає на ping — віримо самому під'єднанню
    }
    //  1.0.35 (власник 22.11.2026: «сделай подключение АВР как функцию маячка — есть связь — работает, отключился от сети —
    //  нет»). Здогадки 1.0.30–1.0.34 («вийшов сам — живий, чекаю 3 хв», «шукає мережу — живий», «живий, але не під'єднаний —
    //  світло є») прибрано: вони робили поведінку непередбачуваною. Тепер як у маячка: АВР у мережі Сторожа й відповідає —
    //  місто є; випав із мережі чи замовк — «міста немає» за S.offSec від останньої відповіді; знову в мережі S.onSec — «є».
    //  Запити пошуку мережі від АВР — лише рядок у журналі, на стан не впливають.
    raw = present;
    static bool probeSaid = false;
    if (!present && found && cl.lastProbeUs && cl.lastProbeUs > cl.lastPingOkUs + 1500000) {
      if (!probeSaid) {
        probeSaid = true;
        logEvent("АВР не в мережі Сторожа, але шукає її (сигнал " + String(cl.probeRssi) + " дБм) — для журналу, на стан не впливає");
      }
    } else if (present) probeSaid = false;
    //  1.0.29 (власник: «скорость сработки отсутствия света … не больше 15»): «АВР мовчить N с» — від останньої відповіді,
    //  а не від миті, коли мовчання помітили (раніше до S.offSec додавалось ще 8 с: 30 с → 38 с).
    int64_t offUs = (int64_t)S.offSec * 1000000 - PING_FRESH;
    if (offUs < 1000000) offUs = 1000000;
    //  Точка Сторожа сама змінила канал (за роутером) — АВР шукає її на новому: 2 хв на це (дія Сторожа, не здогадка про АВР)
    int64_t chg = apChannelChangedUs();
    if (chg && now - chg < 180LL * 1000000 && offUs < 120LL * 1000000) offUs = 120LL * 1000000;
    if (step(grid, raw, now, (int64_t)S.onSec * 1000000, offUs, afterGrace, c)) {
      //  1.0.33 (власник 22.11.2026: «ложное срабатывание отсутствия света» — а журнал до того стерся з відключенням
      //  живлення). Тепер кожне «міста немає» пише в журнал, як саме зник АВР, а коротке відключення — як він повернувся:
      //  так хибне спрацьовування видно й без моєї присутності.
      if (grid.st == T_OFF && c.from == T_ON) {
        String w;
        if (!found) w = "АВР у точці не значиться";
        else if (cl.assoc) w = "АВР у точці, але не відповідає на ping" + (cl.lastPingOkUs ? " " + String((long)((now - cl.lastPingOkUs) / 1000000)) + " с" : String());
        else w = "АВР вийшов із точки " + String((long)((now - cl.leaveUs) / 1000000)) + " с тому (код " + String(cl.leaveReason) +
                 (cl.leftAlive ? ", за мить до того відповідав" : "") + ")";
        if (found && cl.lastProbeUs) w += "; останній запит пошуку мережі від нього — " + String((long)((now - cl.lastProbeUs) / 1000000)) + " с тому";
        logEvent("Чому «міста немає»: " + w + "; затримка " + String(S.offSec) + " с, сигнал АВР був " + String(found ? cl.rssi : 0) + " дБм");
      }
      if (grid.st == T_ON && c.from == T_OFF && grid.exact && c.fromExact) {
        long dur = (long)((grid.since - c.fromSince) / 1000000);
        if (dur >= 0 && dur < 180)
          logEvent("Коротке відключення — " + String(dur) + " с: АВР " +
                   (found && cl.assocUs && now - cl.assocUs < 120000000LL ? String("знову під'єднався до точки — виходив із мережі")
                                                                          : String("із точки не виходив — лише мовчав")) +
                   ". Якщо світло насправді не зникало, це обрив зв'язку з АВР.");
      }
      //  Момент зникнення — остання відповідь АВР (точність ~1 с), а не мить, коли помітили мовчання
      if (grid.st == T_OFF && grid.exact && found && cl.lastPingOkUs && cl.lastPingOkUs < grid.since)
        grid.since = cl.lastPingOkUs;                // момент — остання відповідь АВР
      bool rs = rwRestore(grid, c, RW.d.gst, RW.d.gex, RW.d.gsince, rwGridTried);
      if (rs) rwGridSame = true;
      if (!gridChecked) rwGridSame = false;      // відновлене не підтвердилось — за час перезапуску світло змінилось
      gridChecked = true;
      onGrid(c, rs);
      rwSave();
    } else if (!gridChecked && ((grid.st == T_ON && raw) || afterGrace)) {
      gridChecked = true;
      logEvent(String("Місто: «") + (grid.st == T_ON ? "є" : "немає") + "» підтверджено після перезапуску");
    }
  } else {
    grid = Track();
  }

  // --- маячок: генератор (маячок живиться від виходу генератора) ---
  //  1.0.23: маячків генератора може бути кілька (про запас) — генератор працює, коли на зв'язку хоч один
  if (lineEnabled()) {
    bool raw = false;
    if (legacy()) { for (auto& b : bs) if (b.used && aliveB(&b, now)) raw = true; }
    else for (int i = 0; i < nbc; i++) if (bc[i].role == 'g' && aliveB(seenFor(bc[i].mac), now)) raw = true;
    if (step(line, raw, now, (int64_t)S.lineOnSec * 1000000, (int64_t)S.lineOffSec * 1000000, afterGrace, c)) {
      bool rs = rwRestore(line, c, RW.d.lst, RW.d.lex, RW.d.lsince, rwLineTried);
      lineChecked = true;
      onLine(c, rs);
      rwSave();
    } else if (!lineChecked && ((line.st == T_ON && raw) || afterGrace)) lineChecked = true;
  } else {
    line = Track();
  }

  // --- маячки на лініях і фазах (1.0.23) ---
  for (int i = 0; i < nbc; i++) {
    if (bc[i].role != 'l') continue;
    if (step(bc[i].t, aliveB(seenFor(bc[i].mac), now), now, (int64_t)S.lineOnSec * 1000000, (int64_t)S.lineOffSec * 1000000, afterGrace, c)) onBLine(i, c);
  }
  flushLines(now);

  checkGenRemind(now);
  announceStart(afterGrace);
  static int64_t rwLast = 0;                         // раз на хвилину — «записано недавно» (≤ 10 хв) для перевірки після перезапуску
  if (now - rwLast > 60LL * 1000000) { rwLast = now; rwSave(); }
  statsTick(avrAssigned() ? grid.st : -1);
}

int watchGrid() { return avrAssigned() ? (int)grid.st : -1; }
bool watchAvrSet() { return avrAssigned(); }
int64_t watchGridSince(bool& exact) { exact = grid.exact; return grid.since; }
int64_t watchLineSince(bool& exact) { exact = line.exact; return line.since; }
bool watchAvrRssi(int& rssi) {
  uint8_t m[6];
  ApDev c;
  if (!strToMac(S.avrMac, m) || !findClient(m, c) || !c.assoc) return false;
  rssi = c.rssi;
  return true;
}
//  Для дисплея: спершу маячок генератора, далі ліній; списку немає — будь-який чутий (як до 1.0.23)
bool watchMayakRssi(int& rssi) {
  int64_t now = nowUs();
  const BSeen* best = nullptr;
  for (char r : {'g', 'l'})
    for (int i = 0; i < nbc && !best; i++)
      if (bc[i].role == r && aliveB(seenFor(bc[i].mac), now)) best = seenFor(bc[i].mac);
  if (!best && !nbc)
    for (auto& b : bs) if (aliveB(&b, now) && (!best || b.lastUs > best->lastUs)) best = &b;
  if (!best) return false;
  ApDev c;
  rssi = findClient(best->mac, c) && c.assoc ? c.rssi : best->rssi;   // як чує Сторож; немає в списку — як чує маячок
  return true;
}

//  Зміни в списку маячків зі сторінки (1.0.23): act 'd' — прибрати, інакше додати чи змінити роль і назву.
//  Лише переписує S.beacons і mayakOn у пам'яті; у флеш пише web.cpp (settingsSaveBeacons) — це дія власника.
bool watchBeaconEdit(const uint8_t* mac, char act, char role, const String& name, String& err) {
  int i = cfgIdx(mac);
  uint8_t am[6];
  if (act == 'd') {
    if (i < 0) { err = "Такого маячка немає в списку"; return false; }
  } else {
    if (role != 'g' && role != 'l' && role != 'v') { err = "Невідома роль маячка"; return false; }
    if (i < 0 && nbc >= MAXB) { err = "У списку вже " + String(MAXB) + " маячків — приберіть непотрібний"; return false; }
    if (strToMac(S.avrMac, am) && !memcmp(am, mac, 6)) { err = "Це адреса АВР, а не маячка"; return false; }
  }
  String t;
  for (int k = 0; k < nbc; k++) {
    if (k == i && act == 'd') continue;
    char r = k == i ? role : bc[k].role;
    String n = k == i ? name : bc[k].name;
    t += macToStr(bc[k].mac) + " " + r + (n.length() ? " " + n : String()) + "\n";
  }
  if (i < 0 && act != 'd') t += macToStr(mac) + " " + role + (name.length() ? " " + name : String()) + "\n";
  t.trim();
  S.beacons = t;
  parseBeacons();
  S.mayakOn = hasRole('g');
  return true;
}

//  Маячки для «🛠 Сторож» у боті (1.0.23)
String watchBeaconsText() {
  int64_t now = nowUs();
  String r;
  auto one = [&](const uint8_t* mac, int ci) {
    const BSeen* b = seenFor(mac);
    r += "\n• " + (ci >= 0 ? htmlEsc(bname(ci)) + " (" + (bc[ci].role == 'g' ? "генератор" : bc[ci].role == 'l' ? "лінія" : "показ") + ")"
                            : macToStr(mac) + " (немає в списку)") + ": ";
    if (aliveB(b, now)) {
      ApDev c;
      r += "на зв'язку, " + String(findClient(mac, c) && c.assoc ? c.rssi : b->rssi) + " дБм, працює " + fmtDur(b->up) + (b->ver[0] ? ", " + String(b->ver) : String());
    } else if (b) r += "мовчить " + fmtDur((now - b->lastUs) / 1000000);
    else r += "не чув від запуску Сторожа";
  };
  for (int i = 0; i < nbc; i++) one(bc[i].mac, i);
  for (auto& b : bs) if (b.used && cfgIdx(b.mac) < 0) one(b.mac, -1);
  if (!r.length()) return "\n• жодного";
  if (legacy()) r += "\n<i>Списку маячків немає: будь-який вважається маячком генератора (як до 1.0.23).</i>";
  return r;
}
int watchLine() { return lineEnabled() ? (int)line.st : -1; }

String statusShort() {
  if (!avrAssigned()) return "АВР ще не призначено";
  switch (grid.st) {
    case T_UNK: return "стан визначається";
    case T_ON:
      return genOn() ? "міське світло є, а генератор ще працює — вимкніть його" : "міське світло є";
    default:
      if (lineEnabled() && line.st == T_ON) return "міського світла немає, працює генератор";
      if (lineEnabled() && line.st == T_OFF) return "міського світла немає, генератор не працює";
      return "міського світла немає";
  }
}

static String sinceText(const Track& t) {
  int64_t now = nowUs();
  if (t.exact) return "з " + fmtHM(t.since) + ", " + fmtDur((now - t.since) / 1000000);
  return "щонайменше " + fmtDur((now - t.since) / 1000000);
}

String statusReport() {
  int64_t now = nowUs();
  String r = "📊 <b>Стан зараз</b>\n" + objIco() + htmlEsc(obj()) + " · " + fmtHM(now);
  if (!avrAssigned()) r += "\n⚪ АВР ще не призначено";
  else if (grid.st == T_UNK) r += "\n⏳ Стан визначається";
  else r += String(grid.st == T_ON ? "\n🟢 Міське світло є · " : "\n🔴 Міського світла немає · ") + sinceText(grid);
  if (genOn())
    r += String(grid.st == T_ON ? "\n⚠️ Генератор ще працює — вимкніть · " : "\n🟡 Працює генератор · ") + sinceText(line);
  else if (lineEnabled() && line.st == T_OFF && grid.st != T_ON)
    r += "\n⚫ Генератор не працює · " + sinceText(line);
  for (int i = 0; i < nbc; i++) {                // 1.0.23: маячки на лініях
    if (bc[i].role != 'l') continue;
    const Track& t = bc[i].t;
    r += "\n🔌 " + htmlEsc(bname(i)) + ": " + (t.st == T_ON ? "є напруга · " + sinceText(t) : t.st == T_OFF ? "немає напруги · " + sinceText(t) : String("визначається"));
  }
  r += schStatusLine();                          // 1.0.20: графік YASNO
  r += bmsStatusLine();                          // 1.0.36: батарея
  r += "\n📡 Сторож працює " + fmtDur(now / 1000000) + (staUp() ? ", Wi-Fi " + String(staRssi()) + " дБм" : ", роутер не відповідає");
  return r;
}

static String trackJson(const Track& t) {
  int64_t now = nowUs();
  return "{\"st\":" + String((int)t.st) + ",\"exact\":" + (t.exact ? "true" : "false") + ",\"ago\":" +
         String((long)((now - t.since) / 1000000)) + ",\"at\":\"" + jsonEsc(fmtClock(t.since)) + "\",\"raw\":" + (t.raw ? "true" : "false") + "}";
}

String watchJson() {
  int64_t now = nowUs();
  String o = "{\"obj\":\"" + jsonEsc(obj()) + "\",\"status\":\"" + jsonEsc(statusShort()) + "\",\"grid\":" + trackJson(grid) +
             ",\"line\":" + trackJson(line).substring(0, trackJson(line).length() - 1) + ",\"enabled\":" + (lineEnabled() ? "true" : "false") + "}" +
             ",\"avr\":{\"assigned\":" + (avrAssigned() ? "true" : "false") + ",\"mac\":\"" + jsonEsc(S.avrMac) + "\"}" +
             ",\"bcn\":{\"legacy\":" + (legacy() ? "true" : "false") + ",\"max\":" + String(MAXB) + ",\"list\":[";
  //  1.0.23: маячки зі списку (cfg) і чуті, яких у списку немає
  bool firstItem = true;
  auto item = [&](const uint8_t* mac, int ci) {
    const BSeen* b = seenFor(mac);
    String x = "{\"mac\":\"" + macToStr(mac) + "\",\"cfg\":" + (ci >= 0 ? "true" : "false");
    if (ci >= 0) {
      x += ",\"role\":\"" + String(bc[ci].role) + "\",\"name\":\"" + jsonEsc(bc[ci].name) + "\"";
      if (bc[ci].role == 'l') x += ",\"t\":" + trackJson(bc[ci].t);
    }
    if (b) {
      ApDev c;
      bool as = findClient(mac, c) && c.assoc;
      x += ",\"seen\":true,\"alive\":" + String(aliveB(b, now) ? "true" : "false") + ",\"ago\":" + String((long)((now - b->lastUs) / 1000000)) +
           ",\"rssi\":" + String(as ? c.rssi : b->rssi) + ",\"up\":" + String((unsigned long)b->up) + ",\"ver\":\"" + jsonEsc(b->ver) + "\"";
    } else {
      x += ",\"seen\":false";
    }
    if (!firstItem) o += ',';
    o += x;
    o += '}';
    firstItem = false;
  };
  for (int i = 0; i < nbc; i++) item(bc[i].mac, i);
  for (auto& b : bs) if (b.used && cfgIdx(b.mac) < 0) item(b.mac, -1);
  return o + "]}}";
}
