/*  Батарея (1.0.36, 22.11.2026). Власник: «добавляй функцию мониторинга бмс через esp 32 c3», «с3 держит постоянную
    связь с бмс, а сторож лишь читает эти данные из с3», «в ha эта работа ранее была организована».

    Міст батареї (ESP32-C3 біля JK BMS, firmware/avr-bms) постійно під'єднаний до BMS по Bluetooth і кожні 5 с шле
    Сторожу UDP «BMS1 <MAC мосту> ключ=значення …» на порт маячків (watch.cpp передає сюди). Сторож лише читає.

    Сповіщення — як у HA власника (notify/automations.json), лише з галочкою «Сповіщати про батарею» (S.bmsOn):
      • «🔋 Заряд батареї N %» — заряд опустився нижче 50, 30, 20, 10 %, коли міського світла немає;
      • «⏳ Батареї вистачить менше ніж на три години / на годину» — за середнім розрядом за 10 хв, 2 хв поспіль, без світла;
      • «🔋 BMS повідомляє: …» — тривога BMS (чи BMS вимкнула розряд) тримається 1 хв і змінилась;
      • 15 хв без даних від BMS — «пропала» адміністраторам без звуку, одне повідомлення на одну втрату (журнал HA, Т119),
        і «знову на зв'язку» після хвилини даних; після запуску Сторожа без жодних даних — лише через годину.
    Отримують: у кого в «🔔 Сповіщення» ввімкнено «🔋 Батарея», і адміністратори.  */
#include "bms.h"
#include "settings.h"
#include "tg.h"
#include "util.h"
#include "watch.h"

namespace {

constexpr int64_t SEC = 1000000;
constexpr int64_t BRIDGE_FRESH = 30 * SEC;     // міст шле кожні 5 с
constexpr int64_t DATA_FRESH = 60 * SEC;       // BMS шле кадр щосекунди; міст передає вік кадру
constexpr int64_t LOST_AFTER = 15 * 60 * SEC;  // як у HA: «плата BMS пропала» — 15 хв
constexpr int64_t LOST_BOOT = 60 * 60 * SEC;   // після запуску Сторожа, якщо даних так і не було
constexpr int64_t BACK_AFTER = 60 * SEC;       // «знову на зв'язку» — після хвилини даних (HA: 1 хв)
constexpr int64_t ERR_HOLD = 60 * SEC;         // тривога BMS — 1 хв (HA)
constexpr int64_t RT_HOLD = 120 * SEC;         // «вистачить менше ніж…» — 2 хв (HA)
constexpr int64_t AVG_TAU = 600 * SEC;         // середній розряд — за 10 хв (HA: battery_discharge_power_10min)
constexpr uint32_t DIS_OFF = 1u << 31;         // наш біт «BMS вимкнула розряд» (біти 29–31 BMS не тривожні — їх зняли)

//  Тривоги JK — ті самі назви, що в мосту (firmware/avr-bms/jk_core.h; tests/bat звіряє).
constexpr int ERR_BITS = 29;
const char* const ERR_NAME[ERR_BITS] = {
  "опір з'єднань комірок", "перегрів ключів BMS", "комірок не стільки, як у налаштуваннях", "",
  "батарея повністю заряджена", "перенапруга батареї", "завеликий струм заряду", "коротке замикання під час заряду",
  "перегрів під час заряду", "холодно для заряду", "збій зв'язку всередині BMS", "низька напруга комірки",
  "низька напруга батареї", "завеликий струм розряду", "коротке замикання під час розряду", "перегрів під час розряду",
  "несправний ключ заряду", "несправний ключ розряду", "GPS від'єднано", "час змінити пароль BMS",
  "розряд не вмикається", "перегрів батареї", "несправний датчик температури", "несправний модуль PL",
  "захист від замикання не знімається", "завеликий струм розряду (II)", "завеликий струм розряду (III)",
  "холодно для розряду", "GPS: віддалене блокування",
};
constexpr uint32_t ERR_INFO = (1u << 3) | (1u << 4) | (1u << 18) | (1u << 19) | (1u << 28) | (7u << 29);

struct Bat {
  bool seen = false;                 // міст озивався від запуску
  uint8_t mac[6] = {0};
  int64_t lastUs = 0;                // останній пакет від мосту
  int64_t dataUs = 0;                // коли BMS дала останній кадр (пакет мінус вік кадру)
  char ver[12] = "", name[24] = "", bmac[18] = "", model[17] = "", sw[9] = "";
  int rssi = 0, brssi = 0;
  uint8_t st = 0;                    // стан мосту: 0 шукає BMS, 1 під'єднується, 2 на зв'язку, 3 без кадрів, 4 пауза
  uint32_t ip = 0, up = 0, heap = 0;  // 1.0.39: адреса мосту в мережі Сторожа, скільки секунд працює, вільна пам'ять
  bool lcd = false;                  // на мосту ввімкнено дисплей
  uint8_t rst = 0;                   // причина останнього запуску мосту (esp_reset_reason; міст 1.1.0+), 0 — невідомо
  uint32_t rc = 0;                   // з'єднань із BMS від запуску мосту
  bool has = false;                  // дані BMS були
  uint8_t soc = 0, soh = 0, n = 0, lox = 0, hix = 0, nc = 0;
  uint32_t mv = 0, rem = 0, full = 0, cyc = 0, err = 0;
  int32_t ma = 0;
  uint16_t lo = 0, hi = 0;
  int16_t t1 = 0, t2 = 0, tm = 0;
  bool hT1 = false, hT2 = false, hTm = false, chg = false, dis = false, bal = false, heat = false;
  uint16_t cells[32] = {0};
};
Bat B;

double avgMa = 0;                     // середній струм за ~10 хв, мА (− розряд)
int64_t avgUs = 0;
const uint8_t SOC_L[4] = {50, 30, 20, 10};
bool socArmed[4] = {false, false, false, false}, socInit = false;
const int RT_L[2] = {180, 60};        // хвилин
bool rtArmed[2] = {true, true};
int64_t rtBelow[2] = {0, 0};
uint32_t errShown = 0, errCand = 0;
int64_t errCandUs = 0;
bool lostSaid = false, logOk = false;
int64_t okSince = 0, badSince = 0;
int64_t loopUs = 0;                  // останній прохід bmsLoop (раз на секунду)

String obj() { return S.objName.length() ? S.objName : String("Об'єкт"); }
String head(const char* icon, const String& title) { return String(icon) + " <b>" + title + "</b>\n⛪ " + htmlEsc(obj()) + " · %T"; }

//  «53,25» з 53250 (мВ, 2 знаки) — коми, як у повідомленнях HA
String fix(long v, int div, int dec) {
  bool neg = v < 0;
  unsigned long a = neg ? -v : v;
  unsigned long ip = a / div, fp = a % div;
  char b[24];
  if (dec <= 0) snprintf(b, sizeof b, "%s%lu", neg ? "−" : "", (a + div / 2) / div);
  else {
    unsigned long p = dec == 1 ? 10 : 100;
    unsigned long f = (fp * p + div / 2) / div;
    if (f >= p) { ip++; f -= p; }
    snprintf(b, sizeof b, dec == 1 ? "%s%lu,%01lu" : "%s%lu,%02lu", neg ? "−" : "", ip, f);
  }
  return String(b);
}

bool bridgeAlive(int64_t now) { return B.seen && now - B.lastUs < BRIDGE_FRESH; }
bool fresh(int64_t now) { return bridgeAlive(now) && B.has && now - B.dataUs < DATA_FRESH; }

uint32_t alarms() { uint32_t a = B.err & ~ERR_INFO; if (!B.dis) a |= DIS_OFF; return a; }
String alarmText(uint32_t a) {
  String t;
  for (int b = 0; b < 32; b++) {
    if (!(a >> b & 1)) continue;
    String nm = b == 31 ? String("BMS вимкнула розряд") : b < ERR_BITS && *ERR_NAME[b] ? String(ERR_NAME[b]) : "тривога " + String(b);
    t += (t.length() ? ", " : "") + nm;
  }
  return t;
}
//  Скільки хвилин вистачить до повного розряду за середнім струмом; -1 — не розряджається
int runtimeMin() {
  if (!B.has || avgMa > -300) return -1;
  double m = (double)B.rem / -avgMa * 60.0;
  return m > 99999 ? 99999 : (int)m;
}
String stText(uint8_t st) {
  switch (st) {
    case 0: return "міст шукає BMS — її не чути поруч";
    case 1: return "міст під'єднується до BMS";
    case 3: return "міст під'єднаний, але стану від BMS немає";
    case 4: return "міст чекає перед новою спробою під'єднатись";
    default: return "кадрів від BMS немає";
  }
}
//  Причина запуску мосту (esp_reset_reason) — словами
String rstText(uint8_t r) {
  switch (r) {
    case 1: return "увімкнення живлення";
    case 3: return "програмний перезапуск";
    case 4: return "збій програми";
    case 5: case 6: case 7: return "сторож — програма зависла";
    case 9: return "просідання живлення";
    case 11: case 12: return "USB";
    default: return r ? "код " + String((int)r) : String();
  }
}
String cellsText() {
  return "найнижча комірка " + fix(B.lo, 1000, 2) + " В, найвища " + fix(B.hi, 1000, 2) + " В";
}

void parseKV(char* kv, Bat& d, long& age, bool& gotCells) {
  char* eq = strchr(kv, '=');
  if (!eq) return;
  *eq = 0;
  const char* k = kv;
  const char* v = eq + 1;
  long l = strtol(v, nullptr, 10);
  unsigned long u = strtoul(v, nullptr, 10);
  auto is = [&](const char* x) { return !strcmp(k, x); };
  if (is("ver")) strlcpy(d.ver, v, sizeof d.ver);
  else if (is("up")) d.up = (uint32_t)u;
  else if (is("heap")) d.heap = (uint32_t)u;
  else if (is("lcd")) d.lcd = u != 0;
  else if (is("rst")) d.rst = (uint8_t)u;
  else if (is("rssi")) d.rssi = (int)l;
  else if (is("st")) d.st = (uint8_t)u;
  else if (is("rc")) d.rc = (uint32_t)u;
  else if (is("bmac")) strlcpy(d.bmac, v, sizeof d.bmac);
  else if (is("name")) strlcpy(d.name, v, sizeof d.name);
  else if (is("brssi")) d.brssi = (int)l;
  else if (is("model")) strlcpy(d.model, v, sizeof d.model);
  else if (is("sw")) strlcpy(d.sw, v, sizeof d.sw);
  else if (is("age")) age = l;
  else if (is("soc")) d.soc = (uint8_t)(u > 100 ? 100 : u);
  else if (is("v")) d.mv = (uint32_t)u;
  else if (is("i")) d.ma = (int32_t)l;
  else if (is("rem")) d.rem = (uint32_t)u;
  else if (is("full")) d.full = (uint32_t)u;
  else if (is("cyc")) d.cyc = (uint32_t)u;
  else if (is("soh")) d.soh = (uint8_t)u;
  else if (is("n")) d.n = (uint8_t)u;
  else if (is("lo")) d.lo = (uint16_t)u;
  else if (is("lox")) d.lox = (uint8_t)u;
  else if (is("hi")) d.hi = (uint16_t)u;
  else if (is("hix")) d.hix = (uint8_t)u;
  else if (is("t1")) { d.t1 = (int16_t)l; d.hT1 = true; }
  else if (is("t2")) { d.t2 = (int16_t)l; d.hT2 = true; }
  else if (is("tm")) { d.tm = (int16_t)l; d.hTm = true; }
  else if (is("err")) d.err = (uint32_t)strtoul(v, nullptr, 16);
  else if (is("chg")) d.chg = u != 0;
  else if (is("dis")) d.dis = u != 0;
  else if (is("bal")) d.bal = u != 0;
  else if (is("heat")) d.heat = u != 0;
  else if (is("c")) {
    gotCells = true;
    d.nc = 0;
    const char* p = v;
    while (*p && d.nc < 32) {
      d.cells[d.nc++] = (uint16_t)strtoul(p, nullptr, 10);
      const char* c = strchr(p, ',');
      if (!c) break;
      p = c + 1;
    }
  }
}

void send(const String& text, bool admin) {
  tgBroadcast(nowUs(), text, admin, admin ? C_ADMIN : (uint16_t)(RF_BAT | C_ADMIN));   // технічне — без звуку
}

}  // namespace

void bmsPacket(char* msg, uint32_t ip) {
  char* save = nullptr;
  char* tok = strtok_r(msg, " ", &save);
  if (!tok || strcmp(tok, "BMS1")) return;
  tok = strtok_r(nullptr, " ", &save);
  uint8_t m[6];
  if (!tok || !strToMac(String(tok), m)) return;
  int64_t now = nowUs();
  Bat d = B;                                     // нові значення поверх старих; дані BMS — лише якщо є «age»
  d.hT1 = d.hT2 = d.hTm = false;
  long age = -1;
  bool gotCells = false;
  while ((tok = strtok_r(nullptr, " ", &save))) parseKV(tok, d, age, gotCells);
  if (!B.seen || memcmp(B.mac, m, 6))
    logEvent("Міст батареї на зв'язку: " + macToStr(m) + (d.ver[0] ? ", версія " + String(d.ver) : String()) +
             (d.name[0] ? ", BMS " + String(d.name) : String()));
  //  1.0.40: перезапуск мосту й кожне нове під'єднання до BMS — у журнал: 22.11 о 19:13 міст перезапустився, за 8 хв
  //  під'єднався до BMS 5 разів, і вона перестала оголошувати себе — а в журналі Сторожа про це не було ні рядка.
  if (B.seen && !memcmp(B.mac, m, 6)) {
    if (d.up + 30 < B.up) {
      String r = rstText(d.rst);
      logEvent("Міст батареї перезапустився" + (r.length() ? " (" + r + ")" : String()) + ": перед тим працював " + fmtDur(B.up));
    } else if (d.rc > B.rc)
      logEvent("Міст батареї: нове під'єднання до BMS — " + String((unsigned long)d.rc) + "-те від запуску мосту");
  }
  memcpy(d.mac, m, 6);
  d.seen = true;
  d.ip = ip;
  d.lastUs = now;
  if (age >= 0 && age < 3600) {
    d.has = true;
    d.dataUs = now - (int64_t)age * SEC;
    if (!gotCells) d.nc = 0;
    //  середній струм за ~10 хв (для «вистачить на …»): кожен пакет — крок, вага за часом від попереднього
    double w = avgUs ? (double)(now - avgUs) / AVG_TAU : 1.0;
    if (w > 1.0 || !B.has) w = 1.0;
    if (d.ma <= -300 && avgMa > -300) avgMa = d.ma;     // розряд щойно почався — середнє з теперішнього струму, не з нуля
    else avgMa += (d.ma - avgMa) * w;
    avgUs = now;
  } else {                                       // кадрів немає — числа BMS лишаються старі (вони «застарілі» за dataUs)
    d.mv = B.mv; d.ma = B.ma; d.soc = B.soc; d.err = B.err; d.dis = B.dis; d.chg = B.chg;
    d.hT1 = B.hT1; d.hT2 = B.hT2; d.hTm = B.hTm;
  }
  B = d;
}

int bmsSoc() { return fresh(nowUs()) ? B.soc : -1; }
//  1.0.39: для сторінки мосту через Сторожа (/bms/…) — адреса мосту, поки він на зв'язку; і його MAC
uint32_t bmsBridgeIp() { return bridgeAlive(nowUs()) ? B.ip : 0; }
bool bmsBridgeMac(uint8_t mac[6]) { if (!B.seen) return false; memcpy(mac, B.mac, 6); return true; }

void bmsLoop() {
  int64_t now = nowUs();
  if (now - loopUs < SEC) return;
  loopUs = now;
  bool ok = fresh(now);
  bool gridOff = watchGrid() == 0;

  //  Журнал: дані BMS з'явились / зникли (хвилина без даних — щоб не засмічувати журнал перепідключеннями)
  if (ok && !logOk) { logOk = true; logEvent("Батарея: дані BMS є — заряд " + String((int)B.soc) + " %, " + fix(B.mv, 1000, 2) + " В"); }
  else if (!ok && logOk && B.has && now - B.dataUs > 2 * DATA_FRESH) {
    logOk = false;
    logEvent(String("Батарея: даних BMS немає ") + (bridgeAlive(now) ? "— " + stText(B.st) : String("— міст не на зв'язку")));
  }

  //  Зв'язок: 15 хв без даних — «пропала» (одне на одну втрату); хвилина даних після того — «знову на зв'язку»
  if (ok) { if (!okSince) okSince = now | 1; badSince = 0; }
  else { okSince = 0; if (!badSince) badSince = now | 1; }
  if (!ok && !lostSaid) {
    int64_t lastGood = B.has ? B.dataUs : B.seen ? B.lastUs : 0;
    bool due = lastGood ? now - lastGood >= LOST_AFTER : now >= LOST_BOOT;
    if (due && S.bmsOn) {
      lostSaid = true;
      String why = bridgeAlive(now) ? "Міст батареї на зв'язку, але " + stText(B.st) +
                                          ". Можливо, до BMS під'єднано застосунок JK на телефоні — тоді міст чекає, доки її відпустять."
                                    : String("Від мосту батареї (ESP32-C3 біля BMS) нічого не приходить: плата без живлення чи поза мережею Сторожа.");
      String since = lastGood ? "Понад 15 хв немає даних від BMS. " : "Від запуску Сторожа (годину) немає даних від BMS. ";
      send(head("🔋", "BMS не на зв'язку") + "\n" + since + why, true);
    }
  }
  if (ok && lostSaid && now - okSince >= BACK_AFTER) {
    lostSaid = false;
    if (S.bmsOn) send(head("🔋", "BMS знову на зв'язку") + "\nЗаряд " + String((int)B.soc) + " %, " + fix(B.mv, 1000, 2) + " В.", true);
  }
  if (!ok) return;

  //  Тривоги BMS: набір змінився й тримається хвилину; оголошуємо лише непорожній (як у HA)
  uint32_t a = alarms();
  if (a != errCand) { errCand = a; errCandUs = now; }
  if (a != errShown && now - errCandUs >= ERR_HOLD) {
    if (a) {
      logEvent("Батарея: BMS повідомляє — " + alarmText(a));
      if (S.bmsOn) {
        String m = head("🔋", "BMS повідомляє: " + alarmText(a)) + "\nЗаряд " + String((int)B.soc) + " %, напруга " + fix(B.mv, 1000, 2) + " В, " + cellsText();
        if (B.hTm) m += ", температура ключів " + fix(B.tm, 10, 0) + " °C";
        send(m + ".", false);
      }
    } else logEvent("Батарея: тривог BMS більше немає");
    errShown = a;
  }

  //  Заряд нижче 50/30/20/10 % — без міського світла. Рівень «перезводиться», коли заряд піднявся на 3 % вище нього.
  if (!socInit) { for (int k = 0; k < 4; k++) socArmed[k] = B.soc >= SOC_L[k]; socInit = true; }
  for (int k = 0; k < 4; k++) if (!socArmed[k] && B.soc >= SOC_L[k] + 3) socArmed[k] = true;
  int fire = -1;
  for (int k = 0; k < 4; k++) if (socArmed[k] && B.soc < SOC_L[k]) { socArmed[k] = false; fire = k; }   // найнижчий перетнутий
  int rt = runtimeMin();
  if (fire >= 0 && gridOff && S.bmsOn) {
    String m = head("🔋", "Заряд батареї " + String((int)B.soc) + " %") + "\nНапруга " + fix(B.mv, 1000, 1) + " В.";
    if (rt >= 0) m += " Вистачить приблизно на " + fmtDur((int64_t)rt * 60) + " — до повного розряду, за середнім розрядом " +
                      fix((long)(-avgMa * (double)B.mv / 1e6), 1, 0) + " Вт за 10 хв.";
    send(m, false);
  }

  //  «Вистачить менше ніж на 3 год / 1 год» — 2 хв поспіль, без міського світла (як у HA); 20 % запасу на «перезвести»
  for (int k = 1; k >= 0; k--) {
    if (rt >= 0 && rt < RT_L[k]) { if (!rtBelow[k]) rtBelow[k] = now | 1; }
    else rtBelow[k] = 0;
    if (!rtArmed[k] && (rt < 0 || rt >= RT_L[k] * 6 / 5)) rtArmed[k] = true;
    if (rtArmed[k] && rtBelow[k] && now - rtBelow[k] >= RT_HOLD) {
      rtArmed[k] = false;
      if (k == 1) rtArmed[0] = false;            // уже «менше години» — «менше трьох» після нього не потрібне
      if (gridOff && S.bmsOn)
        send(head("⏳", String("Батареї вистачить менше ніж на ") + (k == 1 ? "годину" : "три години")) + "\nПриблизно на " +
             fmtDur((int64_t)rt * 60) + " — до повного розряду, за середнім розрядом " + fix((long)(-avgMa * (double)B.mv / 1e6), 1, 0) +
             " Вт за 10 хв. Заряд " + String((int)B.soc) + " %. Вимкніть зайве.", false);
    }
  }
}

String bmsJson() {
  if (!B.seen && !S.bmsOn) return String();
  int64_t now = nowUs();
  auto b = [](bool v) { return String(v ? "true" : "false"); };
  String o = "{\"on\":" + b(S.bmsOn) + ",\"seen\":" + b(B.seen) + ",\"ok\":" + b(fresh(now)) + ",\"bridge\":" + b(bridgeAlive(now));
  if (B.seen) {
    o += ",\"ago\":" + String((long)((now - B.lastUs) / SEC)) + ",\"mac\":\"" + macToStr(B.mac) + "\",\"ver\":\"" + jsonEsc(B.ver) +
         "\",\"rssi\":" + String(B.rssi) + ",\"st\":" + String((int)B.st) + ",\"rc\":" + String((unsigned long)B.rc) +
         ",\"name\":\"" + jsonEsc(B.name) + "\",\"bmac\":\"" + jsonEsc(B.bmac) + "\",\"brssi\":" + String(B.brssi) +
         ",\"model\":\"" + jsonEsc(B.model) + "\",\"sw\":\"" + jsonEsc(B.sw) + "\"" +
         ",\"ip\":\"" + String(B.ip & 0xFF) + "." + String((B.ip >> 8) & 0xFF) + "." + String((B.ip >> 16) & 0xFF) + "." + String(B.ip >> 24) +
         "\",\"up\":" + String((unsigned long)B.up) + ",\"rst\":" + String((int)B.rst) + ",\"bheap\":" + String((unsigned long)B.heap) + ",\"lcd\":" + b(B.lcd);
  }
  if (B.has) {
    o += ",\"age\":" + String((long)((now - B.dataUs) / SEC)) + ",\"soc\":" + String((int)B.soc) + ",\"v\":" + String((unsigned long)B.mv) +
         ",\"i\":" + String((long)B.ma) + ",\"avg\":" + String((long)avgMa) + ",\"rem\":" + String((unsigned long)B.rem) +
         ",\"full\":" + String((unsigned long)B.full) + ",\"cyc\":" + String((unsigned long)B.cyc) + ",\"soh\":" + String((int)B.soh) +
         ",\"n\":" + String((int)B.n) + ",\"lo\":" + String((unsigned)B.lo) + ",\"lox\":" + String((int)B.lox) + ",\"hi\":" + String((unsigned)B.hi) +
         ",\"hix\":" + String((int)B.hix) + ",\"chg\":" + b(B.chg) + ",\"dis\":" + b(B.dis) + ",\"bal\":" + b(B.bal) + ",\"heat\":" + b(B.heat) +
         ",\"left\":" + String(runtimeMin());
    if (B.hT1) o += ",\"t1\":" + String((int)B.t1);
    if (B.hT2) o += ",\"t2\":" + String((int)B.t2);
    if (B.hTm) o += ",\"tm\":" + String((int)B.tm);
    o += ",\"alarm\":\"" + jsonEsc(alarmText(alarms())) + "\",\"cells\":[";
    for (int i = 0; i < B.nc; i++) o += (i ? "," : "") + String((unsigned)B.cells[i]);
    o += "]";
  }
  return o + "}";
}

String bmsStatusLine() {
  if (!B.seen && !S.bmsOn) return String();
  int64_t now = nowUs();
  if (!fresh(now)) {
    if (!B.has) return String("\n🔋 Батарея: даних від BMS ще немає") + (B.seen ? " — " + stText(B.st) : String(" — міст не на зв'язку"));
    return "\n🔋 Батарея: даних від BMS немає " + fmtDur((now - B.dataUs) / SEC) + " (було " + String((int)B.soc) + " %)";
  }
  String r = "\n🔋 Батарея " + String((int)B.soc) + " % · " + fix(B.mv, 1000, 1) + " В · ";
  int rt = runtimeMin();
  if (B.ma <= -300) r += "розряд " + fix(-B.ma, 1000, 1) + " А" + (rt >= 0 ? ", вистачить ~" + fmtDur((int64_t)rt * 60) : String());
  else if (B.ma >= 300) r += "заряд " + fix(B.ma, 1000, 1) + " А";
  else r += "спокій";
  uint32_t a = alarms();
  if (a) r += "\n⚠️ BMS: " + htmlEsc(alarmText(a));
  return r;
}

String bmsDiagLine() {
  if (!B.seen) return S.bmsOn ? String("\n🔋 Міст батареї: від запуску не озивався") : String();
  int64_t now = nowUs();
  if (!bridgeAlive(now)) return "\n🔋 Міст батареї: немає зв'язку " + fmtDur((now - B.lastUs) / SEC);
  String r = "\n🔋 Міст батареї " + htmlEsc(B.ver) + ": Wi-Fi " + String(B.rssi) + " дБм; ";
  if (B.st == 2 && B.name[0]) r += "BMS " + htmlEsc(B.name) + (B.model[0] && strcmp(B.model, B.name) ? " (" + htmlEsc(B.model) + ")" : String()) +
                                   ", " + String(B.brssi) + " дБм";
  else r += stText(B.st);
  return r + ", з'єднань " + String((unsigned long)B.rc);
}
