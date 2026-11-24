/*  Батарея (1.0.36, 22.11.2026). Власник: «добавляй функцию мониторинга бмс через esp 32 c3», «с3 держит постоянную
    связь с бмс, а сторож лишь читает эти данные из с3».

    Міст батареї (ESP32-C3 біля JK BMS, firmware/avr-bms) під'єднаний до BMS по Bluetooth і кожні 5 с шле Сторожу
    UDP «BMS1 <MAC мосту> ключ=значення …» на порт маячків (watch.cpp передає сюди). Сторож лише читає.

    1.0.48 (23.11.2026): міст 1.2.0 слухає й кілька BMS — по черзі, раз на період (власник: «поддержку подключения к
    нескольким бмс … циклично раз в выбранный период их опрашивать, чтобы они постоянно не пищали»). Кожна батарея —
    окремий пакет: bi — номер з 0, bn — скільки батарей, per — період опитування, с (0 — постійне з'єднання), lbl —
    назва від власника (пробіли — «_»). Тут до 4 батарей, у кожної свої сповіщення; «дані свіжі» й «пропала» — з
    урахуванням періоду. Міст до 1.2.0 (без bi/bn) — одна батарея, усе як було.

    Сповіщення — як у Home Assistant (notify/automations.json), лише з галочкою «Сповіщати про батарею» (S.bmsOn):
      • «🔋 Заряд батареї N %» — заряд опустився нижче 50, 30, 20, 10 %, коли міського світла немає;
      • «⏳ Батареї вистачить менше ніж на три години / на годину» — за середнім розрядом за 10 хв, 2 хв поспіль, без світла;
      • «🔋 BMS повідомляє: …» — тривога BMS (чи BMS вимкнула розряд) тримається 1 хв і змінилась;
      • 15 хв без даних від BMS («по черзі» — щонайменше два періоди й 5 хв) — «пропала» адміністраторам без звуку,
        одне повідомлення на одну втрату (як у Home Assistant), і «знову на зв'язку» після хвилини даних; мовчить сам міст —
        одне повідомлення на всі батареї; після запуску Сторожа без жодних даних — лише через годину.
      Кілька батарей — у заголовку котра: «Батарея 2 «Гараж»: заряд 30 %».
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
constexpr int64_t LOST_AFTER = 15 * 60 * SEC;  // як у Home Assistant: «плата BMS пропала» — 15 хв
constexpr int64_t LOST_BOOT = 60 * 60 * SEC;   // після запуску Сторожа, якщо даних так і не було
constexpr int64_t BACK_AFTER = 60 * SEC;       // «знову на зв'язку» — після хвилини даних (Home Assistant: 1 хв)
constexpr int64_t BACK_MAX = 15 * 60 * SEC;    // міст повернувся, а якась BMS — ні: сказати за 15 хв однаково
constexpr int64_t ERR_HOLD = 60 * SEC;         // тривога BMS — 1 хв (як у Home Assistant)
constexpr int64_t RT_HOLD = 120 * SEC;         // «вистачить менше ніж…» — 2 хв (як у Home Assistant)
constexpr int64_t AVG_TAU = 600 * SEC;         // середній розряд — за 10 хв (Home Assistant: battery_discharge_power_10min)
constexpr uint32_t DIS_OFF = 1u << 31;         // наш біт «BMS вимкнула розряд» (біти 29–31 BMS не тривожні — їх зняли)
constexpr int MAXB = 4;                        // батарей на одному мості (міст 1.2.0: до 4 по черзі)

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

struct Bridge {
  bool seen = false;                 // міст озивався від запуску
  uint8_t mac[6] = {0};
  int64_t lastUs = 0;                // останній пакет від мосту
  int64_t aliveSince = 0;            // з якого часу міст знову озивається
  char ver[12] = "";
  int rssi = 0;
  uint32_t ip = 0, up = 0, heap = 0;  // 1.0.39: адреса мосту в мережі Сторожа, скільки секунд працює, вільна пам'ять
  bool lcd = false;                  // на мосту ввімкнено дисплей
  uint8_t rst = 0;                   // причина останнього запуску мосту (esp_reset_reason; міст 1.1.0+), 0 — невідомо
  uint32_t rc = 0;                   // з'єднань із BMS від запуску мосту («по черзі» міст шле 0)
  uint8_t bn = 1;                    // 1.0.48: скільки батарей шле міст
  uint32_t per = 0;                  // 1.0.48: період опитування «по черзі», с; 0 — постійне з'єднання
  bool lostSaid = false;             // сказали «від мосту нічого не приходить» (чи «від запуску немає даних»)
};
struct Bat {
  bool used = false;                 // міст шле цю батарею
  int64_t firstUs = 0;               // коли вперше почули про неї
  int64_t dataUs = 0;                // коли BMS дала останній кадр (пакет мінус вік кадру)
  char name[24] = "", bmac[18] = "", model[17] = "", sw[9] = "", lbl[24] = "";
  int brssi = 0;
  uint8_t st = 0;                    // стан: 0 шукає BMS, 1 під'єднується, 2 на зв'язку, 3 без кадрів, 4 пауза; по черзі: 5 чекає, 6 не чути, 7 не відповіла
  uint32_t pl = 0, pf = 0;           // «по черзі»: опитувань і невдалих
  bool has = false;                  // дані BMS були
  uint8_t soc = 0, soh = 0, n = 0, lox = 0, hix = 0, nc = 0;
  uint32_t mv = 0, rem = 0, full = 0, cyc = 0, err = 0;
  int32_t ma = 0;
  uint16_t lo = 0, hi = 0;
  int16_t t1 = 0, t2 = 0, tm = 0;
  bool hT1 = false, hT2 = false, hTm = false, chg = false, dis = false, bal = false, heat = false;
  uint16_t cells[32] = {0};
  //  сповіщення
  double avgMa = 0;                  // середній струм за ~10 хв, мА (− розряд)
  int64_t avgUs = 0;
  bool socArmed[4] = {false, false, false, false}, socInit = false;
  bool rtArmed[2] = {true, true};
  int64_t rtBelow[2] = {0, 0};
  uint32_t errShown = 0, errCand = 0;
  int64_t errCandUs = 0;
  bool lostSaid = false, logOk = false;
  int64_t okSince = 0;
};
Bridge Br;
Bat P[MAXB];
const uint8_t SOC_L[4] = {50, 30, 20, 10};
const int RT_L[2] = {180, 60};        // хвилин
int64_t loopUs = 0;                  // останній прохід bmsLoop (раз на секунду)

void resetAll() { Br = Bridge(); for (auto& b : P) b = Bat(); loopUs = 0; }   // для перевірки на комп'ютері (tests/bat)

String obj() { return S.objName.length() ? S.objName : String("Об'єкт"); }
String head(const char* icon, const String& title) { return String(icon) + " <b>" + title + "</b>\n" + objIco() + htmlEsc(obj()) + " · %T"; }

//  «53,25» з 53250 (мВ, 2 знаки) — коми, як у повідомленнях Home Assistant
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

bool bridgeAlive(int64_t now) { return Br.seen && now - Br.lastUs < BRIDGE_FRESH; }
//  Дані свіжі: BMS шле кадр щосекунди — хвилина; «по черзі» — період і ще півтори хвилини
int64_t freshFor() { int64_t p = (int64_t)Br.per * SEC + 90 * SEC; return p > DATA_FRESH ? p : DATA_FRESH; }
//  «Пропала»: 15 хв; «по черзі» — щонайменше два періоди й 5 хв (одне невдале опитування — ще не біда)
int64_t lostAfter() { int64_t p = 2 * (int64_t)Br.per * SEC + 5 * 60 * SEC; return p > LOST_AFTER ? p : LOST_AFTER; }
bool fresh(const Bat& b, int64_t now) { return bridgeAlive(now) && b.used && b.has && now - b.dataUs < freshFor(); }
bool multi() { return Br.bn > 1; }
String tag(int k) { String t = "Батарея " + String(k + 1); if (P[k].lbl[0]) t += " «" + String(P[k].lbl) + "»"; return t; }
//  Заголовок: одна батарея — як було; кілька — «Батарея 2 «Гараж»: …»
String ttl(int k, const String& one, const String& many) { return multi() ? tag(k) + ": " + many : one; }
String jl(int k) { return multi() ? tag(k) : String("Батарея"); }   // початок рядка журналу

uint32_t alarms(const Bat& b) { uint32_t a = b.err & ~ERR_INFO; if (!b.dis) a |= DIS_OFF; return a; }
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
int runtimeMin(const Bat& b) {
  if (!b.has || b.avgMa > -300) return -1;
  double m = (double)b.rem / -b.avgMa * 60.0;
  return m > 99999 ? 99999 : (int)m;
}
String stText(uint8_t st) {
  switch (st) {
    case 0: return "міст шукає BMS — її не чути поруч";
    case 1: return "міст під'єднується до BMS";
    case 3: return "міст під'єднаний, але стану від BMS немає";
    case 4: return "міст чекає перед новою спробою під'єднатись";
    case 5: return "міст чекає черги, щоб опитати BMS";
    case 6: return "міст не чує BMS під час опитування — її не чути поруч";
    case 7: return "BMS не відповіла мосту на опитування";
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
String cellsText(const Bat& b) {
  return "найнижча комірка " + fix(b.lo, 1000, 2) + " В, найвища " + fix(b.hi, 1000, 2) + " В";
}
String perText(uint32_t s) { return s >= 120 ? "раз на " + String((unsigned long)(s / 60)) + " хв" : String("щохвилини"); }

void parseKV(char* kv, Bridge& r, Bat& d, long& age, bool& gotCells) {
  char* eq = strchr(kv, '=');
  if (!eq) return;
  *eq = 0;
  const char* k = kv;
  const char* v = eq + 1;
  long l = strtol(v, nullptr, 10);
  unsigned long u = strtoul(v, nullptr, 10);
  auto is = [&](const char* x) { return !strcmp(k, x); };
  if (is("ver")) strlcpy(r.ver, v, sizeof r.ver);
  else if (is("up")) r.up = (uint32_t)u;
  else if (is("heap")) r.heap = (uint32_t)u;
  else if (is("lcd")) r.lcd = u != 0;
  else if (is("rst")) r.rst = (uint8_t)u;
  else if (is("rssi")) r.rssi = (int)l;
  else if (is("rc")) r.rc = (uint32_t)u;
  else if (is("st")) d.st = (uint8_t)u;
  else if (is("pl")) d.pl = (uint32_t)u;
  else if (is("pf")) d.pf = (uint32_t)u;
  else if (is("lbl")) { strlcpy(d.lbl, v, sizeof d.lbl); for (char* p = d.lbl; *p; p++) if (*p == '_') *p = ' '; }
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
String socLine(const Bat& b) { return "Заряд " + String((int)b.soc) + " %, " + fix(b.mv, 1000, 2) + " В."; }

//  Сповіщення однієї батареї, поки її дані свіжі: тривоги BMS, рівні заряду й «вистачить на…» без світла
void batAlerts(int k, int64_t now, bool gridOff) {
  Bat& b = P[k];
  //  Тривоги BMS: набір змінився й тримається хвилину; оголошуємо лише непорожній (як у Home Assistant)
  uint32_t a = alarms(b);
  if (a != b.errCand) { b.errCand = a; b.errCandUs = now; }
  if (a != b.errShown && now - b.errCandUs >= ERR_HOLD) {
    if (a) {
      logEvent(jl(k) + ": BMS повідомляє — " + alarmText(a));
      if (S.bmsOn) {
        String m = head("🔋", ttl(k, "BMS повідомляє: " + alarmText(a), "BMS повідомляє: " + alarmText(a))) + "\nЗаряд " + String((int)b.soc) +
                   " %, напруга " + fix(b.mv, 1000, 2) + " В, " + cellsText(b);
        if (b.hTm) m += ", температура ключів " + fix(b.tm, 10, 0) + " °C";
        send(m + ".", false);
      }
    } else logEvent(jl(k) + ": тривог BMS більше немає");
    b.errShown = a;
  }

  //  Заряд нижче 50/30/20/10 % — без міського світла. Рівень «перезводиться», коли заряд піднявся на 3 % вище нього.
  if (!b.socInit) { for (int i = 0; i < 4; i++) b.socArmed[i] = b.soc >= SOC_L[i]; b.socInit = true; }
  for (int i = 0; i < 4; i++) if (!b.socArmed[i] && b.soc >= SOC_L[i] + 3) b.socArmed[i] = true;
  int fire = -1;
  for (int i = 0; i < 4; i++) if (b.socArmed[i] && b.soc < SOC_L[i]) { b.socArmed[i] = false; fire = i; }   // найнижчий перетнутий
  int rt = runtimeMin(b);
  if (fire >= 0 && gridOff && S.bmsOn) {
    String s = String((int)b.soc) + " %";
    String m = head("🔋", ttl(k, "Заряд батареї " + s, "заряд " + s)) + "\nНапруга " + fix(b.mv, 1000, 1) + " В.";
    if (rt >= 0) m += " Вистачить приблизно на " + fmtDur((int64_t)rt * 60) + " — до повного розряду, за середнім розрядом " +
                      fix((long)(-b.avgMa * (double)b.mv / 1e6), 1, 0) + " Вт за 10 хв.";
    send(m, false);
  }

  //  «Вистачить менше ніж на 3 год / 1 год» — 2 хв поспіль, без міського світла (як у Home Assistant); 20 % запасу на «перезвести»
  for (int i = 1; i >= 0; i--) {
    if (rt >= 0 && rt < RT_L[i]) { if (!b.rtBelow[i]) b.rtBelow[i] = now | 1; }
    else b.rtBelow[i] = 0;
    if (!b.rtArmed[i] && (rt < 0 || rt >= RT_L[i] * 6 / 5)) b.rtArmed[i] = true;
    if (b.rtArmed[i] && b.rtBelow[i] && now - b.rtBelow[i] >= RT_HOLD) {
      b.rtArmed[i] = false;
      if (i == 1) b.rtArmed[0] = false;          // вже «менше години» — «менше трьох» після нього не потрібне
      if (gridOff && S.bmsOn) {
        String w = String("вистачить менше ніж на ") + (i == 1 ? "годину" : "три години");
        send(head("⏳", ttl(k, "Батареї " + w, w)) + "\nПриблизно на " + fmtDur((int64_t)rt * 60) + " — до повного розряду, за середнім розрядом " +
             fix((long)(-b.avgMa * (double)b.mv / 1e6), 1, 0) + " Вт за 10 хв. Заряд " + String((int)b.soc) + " %. Вимкніть зайве.", false);
      }
    }
  }
}

}  // namespace

void bmsPacket(char* msg, uint32_t ip) {
  char* save = nullptr;
  char* tok = strtok_r(msg, " ", &save);
  if (!tok || strcmp(tok, "BMS1")) return;
  tok = strtok_r(nullptr, " ", &save);
  uint8_t m[6];
  if (!tok || !strToMac(String(tok), m)) return;
  char* kv[80];
  int nk = 0;
  while (nk < 80 && (tok = strtok_r(nullptr, " ", &save))) kv[nk++] = tok;
  //  1.0.48: котра батарея (міст до 1.2.0 цих ключів не шле — одна батарея, постійне з'єднання)
  int bi = 0, bn = 1;
  long per = 0;
  const char* nbmac = nullptr;
  for (int i = 0; i < nk; i++) {
    if (!strncmp(kv[i], "bi=", 3)) bi = atoi(kv[i] + 3);
    else if (!strncmp(kv[i], "bn=", 3)) bn = atoi(kv[i] + 3);
    else if (!strncmp(kv[i], "per=", 4)) per = atol(kv[i] + 4);
    else if (!strncmp(kv[i], "bmac=", 5)) nbmac = kv[i] + 5;
  }
  if (bn < 1) bn = 1;
  if (bn > MAXB) bn = MAXB;
  if (bi < 0 || bi >= bn) return;
  if (per < 0 || per > 86400) per = 0;
  int64_t now = nowUs();
  bool other = !Br.seen || memcmp(Br.mac, m, 6);
  if (Br.seen && other) for (auto& b : P) b = Bat();     // інший міст — батареї з чистого аркуша
  Bridge r = Br;
  //  на цьому місці переліку інша BMS — її сповіщення з чистого аркуша
  bool otherBms = nbmac && P[bi].bmac[0] && strcasecmp(nbmac, P[bi].bmac);
  Bat d = otherBms ? Bat() : P[bi];                      // нові значення поверх старих; дані BMS — лише якщо є «age»
  d.hT1 = d.hT2 = d.hTm = false;
  //  1.0.52: назву й лічильники опитувань міст шле в кожному пакеті «по черзі»; немає — отже, їх і немає (23.11 після
  //  перевірки «по черзі» назва «Перевірка» лишалась у Сторожа, хоч міст повернувся до однієї BMS)
  d.lbl[0] = 0;
  d.pl = d.pf = 0;
  long age = -1;
  bool gotCells = false;
  for (int i = 0; i < nk; i++) parseKV(kv[i], r, d, age, gotCells);
  if (other)
    logEvent("Міст батареї на зв'язку: " + macToStr(m) + (r.ver[0] ? ", версія " + String(r.ver) : String()) +
             (d.name[0] ? ", BMS " + String(d.name) : String()) + (bn > 1 ? ", батарей " + String(bn) + " по черзі " + perText((uint32_t)per) : String()));
  else {
    //  1.0.40: перезапуск мосту й кожне нове під'єднання до BMS — у журнал: 22.11 о 19:13 міст перезапустився, за 8 хв
    //  під'єднався до BMS 5 разів, і вона перестала оголошувати себе — а в журналі Сторожа про це не було ні рядка.
    //  1.0.48: «по черзі» кожне опитування — нове з'єднання (міст шле rc=0) — їх не пишемо.
    if (r.up + 30 < Br.up) {
      String t = rstText(r.rst);
      logEvent("Міст батареї перезапустився" + (t.length() ? " (" + t + ")" : String()) + ": перед тим працював " + fmtDur(Br.up));
    } else if (!per && r.rc > Br.rc)
      logEvent("Міст батареї: нове під'єднання до BMS — " + String((unsigned long)r.rc) + "-те від запуску мосту");
    if (bn != Br.bn || (uint32_t)per != Br.per)
      logEvent("Міст батареї: " + (bn > 1 || per ? "батарей " + String(bn) + ", опитує по черзі " + perText((uint32_t)per) : String("одна батарея, постійне з'єднання")));
    if (otherBms) logEvent(String(bn > 1 ? "Батарея " + String(bi + 1) : String("Батарея")) + ": тепер BMS " + String(d.name) + " (" + String(d.bmac) + ")");
  }
  if (bn < Br.bn) for (int k = bn; k < MAXB; k++) P[k] = Bat();   // батарей поменшало — зайві прибрати
  if (!bridgeAlive(now)) r.aliveSince = now;
  memcpy(r.mac, m, 6);
  r.seen = true;
  r.ip = ip;
  r.lastUs = now;
  r.bn = (uint8_t)bn;
  r.per = (uint32_t)per;
  d.used = true;
  if (!d.firstUs) d.firstUs = now;
  if (age >= 0 && age < 3600) {
    d.has = true;
    d.dataUs = now - (int64_t)age * SEC;
    if (!gotCells) d.nc = 0;
    //  середній струм за ~10 хв (для «вистачить на …»): кожен пакет — крок, вага за часом від попереднього
    double w = d.avgUs ? (double)(now - d.avgUs) / AVG_TAU : 1.0;
    if (w > 1.0) w = 1.0;
    if (d.ma <= -300 && d.avgMa > -300) d.avgMa = d.ma;     // розряд щойно почався — середнє з теперішнього струму, не з нуля
    else d.avgMa += (d.ma - d.avgMa) * w;
    d.avgUs = now;
  } else if (!otherBms) {                        // кадрів немає — числа BMS лишаються старі (вони «застарілі» за dataUs)
    const Bat& o = P[bi];
    d.mv = o.mv; d.ma = o.ma; d.soc = o.soc; d.err = o.err; d.dis = o.dis; d.chg = o.chg;
    d.hT1 = o.hT1; d.hT2 = o.hT2; d.hTm = o.hTm;
  }
  Br = r;
  P[bi] = d;
}

int bmsSoc() {                                   // найменший заряд серед батарей зі свіжими даними
  int64_t now = nowUs();
  int s = -1;
  for (auto& b : P) if (fresh(b, now) && (s < 0 || b.soc < s)) s = b.soc;
  return s;
}
//  1.0.39: для сторінки мосту через Сторожа (/bms/…) — адреса мосту, поки він на зв'язку; і його MAC
uint32_t bmsBridgeIp() { return bridgeAlive(nowUs()) ? Br.ip : 0; }
bool bmsBridgeMac(uint8_t mac[6]) { if (!Br.seen) return false; memcpy(mac, Br.mac, 6); return true; }

void bmsLoop() {
  int64_t now = nowUs();
  if (now - loopUs < SEC) return;
  loopUs = now;
  bool alive = bridgeAlive(now);
  bool gridOff = watchGrid() == 0;

  //  Міст мовчить (чи від запуску Сторожа його не було годину) — одне повідомлення на всі батареї
  if (!Br.lostSaid && !alive) {
    bool due = Br.seen ? now - Br.lastUs >= lostAfter() : now >= LOST_BOOT;
    if (due && S.bmsOn) {
      Br.lostSaid = true;
      for (auto& b : P) b.lostSaid = false;
      String since = Br.seen ? "Понад " + String((long)(lostAfter() / (60 * SEC))) + " хв немає даних від BMS. " : String("Від запуску Сторожа (годину) немає даних від BMS. ");
      send(head("🔋", "BMS не на зв'язку") + "\n" + since + "Від мосту батареї (ESP32-C3 біля BMS) нічого не приходить: плата без живлення чи поза мережею Сторожа.", true);
    }
  }
  //  Міст повернувся: коли всі його батареї хвилину зі свіжими даними (або за 15 хв однаково) — одне повідомлення
  if (Br.lostSaid && alive) {
    bool all = Br.bn > 0;
    int64_t okFrom = 0;
    for (int k = 0; k < Br.bn; k++) {
      if (!fresh(P[k], now) || !P[k].okSince) { all = false; break; }
      if (P[k].okSince > okFrom) okFrom = P[k].okSince;
    }
    if ((all && now - okFrom >= BACK_AFTER) || now - Br.aliveSince >= BACK_MAX) {
      Br.lostSaid = false;
      if (S.bmsOn) {
        if (!multi()) {
          const Bat& b = P[0];
          send(head("🔋", "BMS знову на зв'язку") + "\n" + (fresh(b, now) ? socLine(b) : "Міст батареї на зв'язку, але " + stText(b.st) + "."), true);
        } else {
          String t = head("🔋", "Міст батареї знову на зв'язку");
          for (int k = 0; k < Br.bn; k++) t += "\n" + tag(k) + ": " + (fresh(P[k], now) ? socLine(P[k]) : "даних немає — " + stText(P[k].st) + ".");
          send(t, true);
        }
      }
    }
  }

  for (int k = 0; k < MAXB; k++) {
    Bat& b = P[k];
    if (!b.used) continue;
    bool ok = fresh(b, now);
    //  Журнал: дані BMS з'явились / зникли (два строки свіжості без даних — щоб не засмічувати журнал перепідключеннями)
    if (ok && !b.logOk) { b.logOk = true; logEvent(jl(k) + ": дані BMS є — заряд " + String((int)b.soc) + " %, " + fix(b.mv, 1000, 2) + " В"); }
    else if (!ok && b.logOk && b.has && now - b.dataUs > 2 * freshFor()) {
      b.logOk = false;
      logEvent(jl(k) + ": даних BMS немає " + (alive ? "— " + stText(b.st) : String("— міст не на зв'язку")));
    }
    if (ok) { if (!b.okSince) b.okSince = now | 1; } else b.okSince = 0;
    //  Міст на зв'язку, а від цієї BMS даних немає: одне «пропала» на одну втрату; хвилина даних — «знову на зв'язку»
    if (!ok && !b.lostSaid && !Br.lostSaid && alive && S.bmsOn) {
      int64_t lastGood = b.has ? b.dataUs : b.firstUs;
      if (now - lastGood >= lostAfter()) {
        b.lostSaid = true;
        String since = b.has ? "Понад " + String((long)(lostAfter() / (60 * SEC))) + " хв немає даних від BMS. " : String("Даних від BMS ще не було. ");
        send(head("🔋", ttl(k, "BMS не на зв'язку", "BMS не на зв'язку")) + "\n" + since + "Міст батареї на зв'язку, але " + stText(b.st) +
                 ". Можливо, до BMS під'єднано застосунок JK на телефоні — тоді міст чекає, доки її відпустять.", true);
      }
    }
    if (ok && b.lostSaid && now - b.okSince >= BACK_AFTER) {
      b.lostSaid = false;
      if (S.bmsOn) send(head("🔋", ttl(k, "BMS знову на зв'язку", "BMS знову на зв'язку")) + "\n" + socLine(b), true);
    }
    if (ok) batAlerts(k, now, gridOff);
  }
}

String bmsJson() {
  if (!Br.seen && !S.bmsOn) return String();
  int64_t now = nowUs();
  auto b = [](bool v) { return String(v ? "true" : "false"); };
  bool any = false;
  for (auto& x : P) if (fresh(x, now)) any = true;
  String o = "{\"on\":" + b(S.bmsOn) + ",\"seen\":" + b(Br.seen) + ",\"ok\":" + b(any) + ",\"bridge\":" + b(bridgeAlive(now)) +
             ",\"bn\":" + String((int)Br.bn) + ",\"per\":" + String((unsigned long)Br.per);
  if (Br.seen)
    o += ",\"ago\":" + String((long)((now - Br.lastUs) / SEC)) + ",\"mac\":\"" + macToStr(Br.mac) + "\",\"ver\":\"" + jsonEsc(Br.ver) +
         "\",\"rssi\":" + String(Br.rssi) + ",\"rc\":" + String((unsigned long)Br.rc) +
         ",\"ip\":\"" + String(Br.ip & 0xFF) + "." + String((Br.ip >> 8) & 0xFF) + "." + String((Br.ip >> 16) & 0xFF) + "." + String(Br.ip >> 24) +
         "\",\"up\":" + String((unsigned long)Br.up) + ",\"rst\":" + String((int)Br.rst) + ",\"bheap\":" + String((unsigned long)Br.heap) + ",\"lcd\":" + b(Br.lcd);
  o += ",\"bats\":[";
  bool first = true;
  for (int k = 0; k < MAXB; k++) {
    const Bat& x = P[k];
    if (!x.used) continue;
    o += String(first ? "" : ",") + "{\"k\":" + String(k) + ",\"lbl\":\"" + jsonEsc(x.lbl) + "\",\"ok\":" + b(fresh(x, now)) + ",\"st\":" + String((int)x.st) +
         ",\"name\":\"" + jsonEsc(x.name) + "\",\"bmac\":\"" + jsonEsc(x.bmac) + "\",\"brssi\":" + String(x.brssi) + ",\"model\":\"" + jsonEsc(x.model) +
         "\",\"sw\":\"" + jsonEsc(x.sw) + "\",\"pl\":" + String((unsigned long)x.pl) + ",\"pf\":" + String((unsigned long)x.pf);
    first = false;
    if (x.has) {
      o += ",\"age\":" + String((long)((now - x.dataUs) / SEC)) + ",\"soc\":" + String((int)x.soc) + ",\"v\":" + String((unsigned long)x.mv) +
           ",\"i\":" + String((long)x.ma) + ",\"avg\":" + String((long)x.avgMa) + ",\"rem\":" + String((unsigned long)x.rem) +
           ",\"full\":" + String((unsigned long)x.full) + ",\"cyc\":" + String((unsigned long)x.cyc) + ",\"soh\":" + String((int)x.soh) +
           ",\"n\":" + String((int)x.n) + ",\"lo\":" + String((unsigned)x.lo) + ",\"lox\":" + String((int)x.lox) + ",\"hi\":" + String((unsigned)x.hi) +
           ",\"hix\":" + String((int)x.hix) + ",\"chg\":" + b(x.chg) + ",\"dis\":" + b(x.dis) + ",\"bal\":" + b(x.bal) + ",\"heat\":" + b(x.heat) +
           ",\"left\":" + String(runtimeMin(x));
      if (x.hT1) o += ",\"t1\":" + String((int)x.t1);
      if (x.hT2) o += ",\"t2\":" + String((int)x.t2);
      if (x.hTm) o += ",\"tm\":" + String((int)x.tm);
      o += ",\"alarm\":\"" + jsonEsc(alarmText(alarms(x))) + "\",\"cells\":[";
      for (int i = 0; i < x.nc; i++) o += (i ? "," : "") + String((unsigned)x.cells[i]);
      o += "]";
    }
    o += "}";
  }
  return o + "]}";
}

String bmsStatusLine() {
  if (!Br.seen && !S.bmsOn) return String();
  int64_t now = nowUs();
  bool none = true;
  for (auto& x : P) if (x.used) none = false;
  if (none) return String("\n🔋 Батарея: даних від BMS ще немає") + (Br.seen ? " — " + stText(P[0].st) : String(" — міст не на зв'язку"));
  String r;
  for (int k = 0; k < MAXB; k++) {
    const Bat& b = P[k];
    if (!b.used) continue;
    String who = multi() ? tag(k) : String("Батарея");
    if (!fresh(b, now)) {
      if (!b.has) r += "\n🔋 " + who + ": даних від BMS ще немає" + (bridgeAlive(now) ? " — " + stText(b.st) : String(" — міст не на зв'язку"));
      else r += "\n🔋 " + who + ": даних від BMS немає " + fmtDur((now - b.dataUs) / SEC) + " (було " + String((int)b.soc) + " %)";
      continue;
    }
    r += "\n🔋 " + who + " " + String((int)b.soc) + " % · " + fix(b.mv, 1000, 1) + " В · ";
    int rt = runtimeMin(b);
    if (b.ma <= -300) r += "розряд " + fix(-b.ma, 1000, 1) + " А" + (rt >= 0 ? ", вистачить ~" + fmtDur((int64_t)rt * 60) : String());
    else if (b.ma >= 300) r += "заряд " + fix(b.ma, 1000, 1) + " А";
    else r += "спокій";
    uint32_t a = alarms(b);
    if (a) r += "\n⚠️ BMS: " + htmlEsc(alarmText(a));
  }
  return r;
}

String bmsDiagLine() {
  if (!Br.seen) return S.bmsOn ? String("\n🔋 Міст батареї: від запуску не озивався") : String();
  int64_t now = nowUs();
  if (!bridgeAlive(now)) return "\n🔋 Міст батареї: немає зв'язку " + fmtDur((now - Br.lastUs) / SEC);
  String r = "\n🔋 Міст батареї " + htmlEsc(Br.ver) + ": Wi-Fi " + String(Br.rssi) + " дБм; ";
  if (Br.per) r += "по черзі " + perText(Br.per) + "; ";
  for (int k = 0; k < MAXB; k++) {
    const Bat& b = P[k];
    if (!b.used) continue;
    if (multi()) r += String(k ? "; " : "") + String(k + 1) + ") ";
    if ((b.st == 2 || fresh(b, now)) && b.name[0])
      r += "BMS " + htmlEsc(b.name) + (b.model[0] && strcmp(b.model, b.name) ? " (" + htmlEsc(b.model) + ")" : String()) + ", " + String(b.brssi) + " дБм";
    else r += stText(b.st);
    if (Br.per) r += ", опитувань " + String((unsigned long)b.pl) + (b.pf ? ", невдалих " + String((unsigned long)b.pf) : String());
  }
  if (!Br.per) r += ", з'єднань " + String((unsigned long)Br.rc);
  return r;
}
