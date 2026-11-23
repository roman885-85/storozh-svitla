// Перевірка батареї Сторожа (bms.cpp, 1.0.36) на Mac: справжній bms.cpp, а міст, Telegram, годинник і стан міста —
// замінники. Запуск: tests/bat/run.sh. Пакети «BMS1 …» — такі самі, як шле міст батареї (firmware/avr-bms).
#include <Arduino.h>
#include "../../firmware/avr-mist/bms.cpp"
#include "../../firmware/avr-bms/jk_core.h"       // назви тривог мають збігатися з мостом
#include "util_part.cpp"                           // strToMac, htmlEsc, jsonEsc, macToStr — витягнуті з util.cpp (run.sh)
#include <vector>
#include <string>
#include <fstream>

Settings S;
static int64_t T = 0;
int64_t esp_timer_get_time() { return T; }
static int gridState = 1;
int watchGrid() { return gridState; }
static std::string hm(int64_t us) { long s = (long)(us / 1000000); char b[16]; snprintf(b, sizeof b, "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60); return b; }
String fmtDur(int64_t sec) {
  char b[40];
  if (sec >= 3600) snprintf(b, sizeof b, "%lld год %lld хв", (long long)sec / 3600, (long long)sec % 3600 / 60);
  else if (sec >= 60) snprintf(b, sizeof b, "%lld хв", (long long)sec / 60);
  else snprintf(b, sizeof b, "%lld с", (long long)sec);
  return String(b);
}
static std::vector<std::string> g_log;
void logEvent(const String& t) { g_log.push_back(t.s); printf("    журнал %s: %s\n", hm(T).c_str(), t.c_str()); }

struct Sent { int64_t at; std::string text; bool silent; uint16_t cat; };
static std::vector<Sent> g_tg;
bool tgBroadcast(int64_t, const String& text, bool silent, uint16_t cat) {
  g_tg.push_back({T, text.s, silent, cat});
  std::string one = text.s;
  for (auto& c : one) if (c == '\n') c = '|';
  printf("  >>> TG %s [%s, кат %03x]: %s\n", hm(T).c_str(), silent ? "без звуку" : "ЗІ ЗВУКОМ", cat, one.c_str());
  return true;
}

// --- міст: що «бачить» BMS ---
struct Sim {
  bool on = true;            // міст живий і в мережі Сторожа
  bool bms = true;           // BMS на зв'язку з мостом (є кадри)
  int st = 2;
  int soc = 60, n = 8, rc = 1, rst = -1;
  long long upBase = 0;      // «коли запустився міст» — зсув часу роботи
  long mv = 26500, ma = 0, rem = 60000, full = 100000;
  unsigned long err = 0;
  int dis = 1, chg = 1;
  int64_t lastPkt = -10000000;
} sim;
static void pkt() {
  char b[700];
  snprintf(b, sizeof b, "BMS1 10:20:30:40:50:20 up=%lld rssi=-58 ver=1.0.0 st=%d rc=%d bad=0 bmac=c8:47:80:00:00:02 name=JK_B1A8S10P brssi=-71",
           (long long)(T / 1000000) - sim.upBase, sim.bms ? 2 : sim.st, sim.rc);
  std::string s = b;
  if (sim.rst >= 0) { snprintf(b, sizeof b, " rst=%d", sim.rst); s += b; }
  if (sim.bms) {
    snprintf(b, sizeof b, " age=1 fmt=32 soc=%d v=%ld i=%ld rem=%ld full=%ld cyc=12 soh=100 n=%d lo=3300 lox=3 hi=3312 hix=7 t1=215 tm=263 err=%lx chg=%d dis=%d bal=0 heat=0 c=3301,3305,3300,3310,3308,3304,3312,3306 model=JK_B1A8S10P hw=11.XW sw=11.26",
             sim.soc, sim.mv, sim.ma, sim.rem, sim.full, sim.n, sim.err, sim.chg, sim.dis);
    s += b;
  }
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back(0);
  bmsPacket(buf.data(), 0);
}
static void run(int64_t sec) {                   // крок — 1 с, міст шле кожні 5 с
  int64_t until = T + sec * 1000000LL;
  while (T < until) {
    T += 1000000;
    if (sim.on && T - sim.lastPkt >= 5000000) { sim.lastPkt = T; pkt(); }
    bmsLoop();
  }
}
static void reset() {                            // новий запуск Сторожа: стан bms.cpp з нуля
  B = Bat(); avgMa = 0; avgUs = 0; socInit = false;
  for (bool& a : socArmed) a = false;
  rtArmed[0] = rtArmed[1] = true; rtBelow[0] = rtBelow[1] = 0;
  errShown = errCand = 0; errCandUs = 0; lostSaid = false; logOk = false; okSince = badSince = 0; loopUs = 0;
  T = 0; sim = Sim(); g_tg.clear(); g_log.clear();
}

static int fails = 0;
static void check(bool ok, const char* what) { printf("%s %s\n", ok ? "  ✅" : "  ❌", what); if (!ok) fails++; }
static size_t mark = 0;
static std::vector<Sent> since() { std::vector<Sent> v(g_tg.begin() + mark, g_tg.end()); mark = g_tg.size(); return v; }
static bool has(const std::string& t, const char* x) { return t.find(x) != std::string::npos; }
static void jsonOut(const char* name) { std::ofstream(std::string("/tmp/bat-test-") + name + ".json") << bmsJson().s; }

int main() {
  S.objName = "Церква";
  S.bmsOn = true;

  puts("0) Назви тривог — ті самі, що в мосту (jk_core.h)");
  bool same = true;
  for (int b = 0; b < ERR_BITS; b++) if (strcmp(ERR_NAME[b], jk::errName(b))) { same = false; printf("    біт %d: «%s» ≠ «%s»\n", b, ERR_NAME[b], jk::errName(b)); }
  check(same && ERR_BITS == jk::ERR_BITS && ERR_INFO == jk::ERR_INFO, "29 назв і «не тривожні» біти збігаються");

  puts("1) Міст на зв'язку, світло є: даних досить, повідомлень немає");
  reset(); mark = 0;
  run(60);
  check(since().empty(), "жодного повідомлення");
  check(bmsSoc() == 60, "заряд 60 % видно");
  check(has(bmsStatusLine().s, "🔋 Батарея 60 % · 26,5 В · спокій"), "рядок «📊 Стан»");
  check(has(bmsDiagLine().s, "Міст батареї 1.0.0: Wi-Fi -58 дБм; BMS JK_B1A8S10P, -71 дБм, з'єднань 1"), "рядок «🛠 Сторож»");
  jsonOut("fresh");

  puts("2) Світла немає, розряд: 60 → 49 % — одне «Заряд батареї 49 %»; 48 — нічого");
  gridState = 0; sim.ma = -12000; sim.soc = 52; run(30); since();
  sim.soc = 49; run(30);
  auto v = since();
  check(v.size() == 1 && has(v[0].text, "Заряд батареї 49 %") && !v[0].silent && v[0].cat == (RF_BAT | C_ADMIN), "одне, зі звуком, «Батарея» + адміністратори");
  check(v.size() == 1 && has(v[0].text, "Напруга 26,5 В.") && has(v[0].text, "Вистачить приблизно на"), "напруга й «вистачить на»");
  sim.soc = 48; run(30);
  check(since().empty(), "48 % — тиша");

  puts("3) Рівень 30: 29 → 30 → 29 (коливання) — одне повідомлення; перезвести лише з 33 %");
  sim.soc = 29; run(20); v = since();
  check(v.size() == 1 && has(v[0].text, "Заряд батареї 29 %"), "29 % — одне");
  for (int k = 0; k < 4; k++) { sim.soc = 30; run(20); sim.soc = 29; run(20); }
  check(since().empty(), "коливання 29/30 — тиша");

  puts("4) Стрибок 29 → 8 % (пройдено 20 і 10) — одне повідомлення з 8 %");
  sim.soc = 8; run(20); v = since();
  check(v.size() == 1 && has(v[0].text, "Заряд батареї 8 %"), "одне, «8 %»");

  puts("5) Світло є — пороги не тривожать");
  reset(); mark = 0; gridState = 1; sim.ma = -5000; run(30); since();
  sim.soc = 45; run(30); sim.soc = 25; run(30);
  check(since().empty(), "зі світлом — тиша");

  puts("6) «Вистачить менше ніж на 3 год» (2 хв поспіль), далі «менше ніж на годину» — по одному");
  reset(); mark = 0; gridState = 0; sim.soc = 70; sim.rem = 30000; sim.ma = -12000;    // 30 А·год / 12 А = 150 хв
  run(60); check(since().empty(), "першу хвилину — тиша (тримається 2 хв)");
  run(90); v = since();
  check(v.size() == 1 && has(v[0].text, "Батареї вистачить менше ніж на три години") && has(v[0].text, "Вимкніть зайве"), "«менше ніж на три години»");
  run(300); check(since().empty(), "не повторюється");
  sim.rem = 10000; run(150); v = since();                                           // 10 / 12 = 50 хв
  check(v.size() == 1 && has(v[0].text, "менше ніж на годину"), "«менше ніж на годину»");
  check(v.size() == 1 && has(v[0].text, "318 Вт"), "середній розряд 12 А × 26,5 В = 318 Вт");

  puts("7) Різкий стрибок навантаження: одразу 40 хв — лише «менше ніж на годину»");
  reset(); mark = 0; gridState = 0; sim.soc = 80; sim.rem = 8000; sim.ma = -12000;
  run(200); v = since();
  check(v.size() == 1 && has(v[0].text, "менше ніж на годину"), "одне, «на годину», без «на три години»");

  puts("8) Тривоги BMS: 30 с — тиша; 70 с — одне «BMS повідомляє»; лише «повністю заряджена» — тиша");
  reset(); mark = 0; gridState = 1; run(30); since();
  sim.err = 1u << 11; run(30); sim.err = 0; run(90);
  check(since().empty(), "коротка тривога (30 с) — тиша");
  sim.err = 1u << 11; run(70); v = since();
  check(v.size() == 1 && has(v[0].text, "BMS повідомляє: низька напруга комірки") && has(v[0].text, "найнижча комірка 3,30 В") &&
        has(v[0].text, "температура ключів 26 °C") && v[0].cat == (RF_BAT | C_ADMIN), "одне, з комірками й температурою ключів");
  run(300); check(since().empty(), "триває — не повторюється");
  sim.err = (1u << 11) | (1u << 13); run(70); v = since();
  check(v.size() == 1 && has(v[0].text, "низька напруга комірки, завеликий струм розряду"), "додалась тривога — нове повідомлення");
  sim.err = 1u << 4; run(120);
  check(since().empty(), "лише «повністю заряджена» — тиша");
  check(!g_log.empty() && has(g_log.back(), "тривог BMS більше немає"), "у журналі — «тривог більше немає»");
  sim.dis = 0; run(70); v = since();
  check(v.size() == 1 && has(v[0].text, "BMS повідомляє: BMS вимкнула розряд"), "BMS вимкнула розряд");
  check(has(bmsStatusLine().s, "⚠️ BMS: BMS вимкнула розряд"), "і в «📊 Стан»");
  jsonOut("alarm");

  puts("9) Міст замовк: 15 хв — одне «BMS не на зв'язку» адміністраторам без звуку; годину — без повторів; повернувся — «знову»");
  reset(); mark = 0; run(60); since();
  sim.on = false; run(14 * 60);
  check(since().empty(), "14 хв — тиша");
  run(90); v = since();
  check(v.size() == 1 && has(v[0].text, "BMS не на зв'язку") && has(v[0].text, "плата без живлення чи поза мережею") && v[0].silent && v[0].cat == C_ADMIN,
        "одне, без звуку, лише адміністраторам, «міст без живлення»");
  run(3600); check(since().empty(), "годину — без повторів");
  check(has(bmsStatusLine().s, "даних від BMS немає") && has(bmsDiagLine().s, "немає зв'язку"), "«📊 Стан» і «🛠 Сторож» кажуть, що даних немає");
  jsonOut("lost");
  sim.on = true; run(30); check(since().empty(), "30 с даних — ще тиша");
  run(60); v = since();
  check(v.size() == 1 && has(v[0].text, "BMS знову на зв'язку") && has(v[0].text, "Заряд 60 %") && v[0].silent, "«знову на зв'язку», без звуку");

  puts("10) Міст живий, але BMS не чує (застосунок JK на телефоні) — 15 хв: «Міст батареї на зв'язку, але …»");
  sim.bms = false; sim.st = 0; run(16 * 60); v = since();
  check(v.size() == 1 && has(v[0].text, "Міст батареї на зв'язку, але міст шукає BMS") && has(v[0].text, "застосунок JK"), "пояснення — міст шукає BMS");
  sim.bms = true; run(90); v = since();
  check(v.size() == 1 && has(v[0].text, "знову на зв'язку"), "BMS повернулась — одне");

  puts("11) Сторож запустився, а мосту немає: годину тиша, потім одне повідомлення");
  reset(); mark = 0; sim.on = false; run(59 * 60);
  check(since().empty(), "59 хв — тиша");
  check(has(bmsStatusLine().s, "даних від BMS ще немає — міст не на зв'язку"), "«📊 Стан»: даних ще немає");
  run(120); v = since();
  check(v.size() == 1 && has(v[0].text, "Від запуску Сторожа (годину) немає даних від BMS"), "одне — «від запуску Сторожа»");

  puts("12) Сповіщення про батарею вимкнено: даних видно, повідомлень немає ніколи");
  reset(); mark = 0; S.bmsOn = false; gridState = 0; sim.ma = -12000; sim.rem = 8000; run(60);
  sim.soc = 9; sim.err = 1u << 12; run(400); sim.on = false; run(2 * 3600);
  check(since().empty(), "жодного повідомлення");
  check(bmsJson().length() > 0, "картка на сторінці є (міст озивався)");
  reset(); S.bmsOn = false; sim.on = false; run(7200);
  check(bmsJson().length() == 0 && bmsStatusLine().length() == 0 && bmsDiagLine().length() == 0, "мосту не було й вимкнено — нічого не показуємо");
  S.bmsOn = true;

  puts("14) Перезапуск мосту й нове під'єднання до BMS — рядки в журналі (1.0.40)");
  reset(); mark = 0; S.bmsOn = true; gridState = 1; run(600);
  size_t l0 = g_log.size();
  sim.rc = 2; run(10);
  check(g_log.size() == l0 + 1 && has(g_log.back(), "нове під'єднання до BMS — 2-те"), "rc 1 → 2: «нове під'єднання … 2-те»");
  sim.upBase = T / 1000000; sim.rc = 1; sim.rst = 4; run(10);
  check(g_log.size() == l0 + 2 && has(g_log.back(), "Міст батареї перезапустився (збій програми): перед тим працював 10 хв"), "час роботи впав — «перезапустився (збій програми)»");
  run(60);
  check(g_log.size() == l0 + 2, "далі — без повторів");
  check(since().empty(), "у Telegram про це нічого");
  sim.rst = -1; sim.upBase = 0; sim.rc = 1;

  puts("13) Сміття в пакетах не ламає розбір");
  reset();
  for (const char* g : {"BMS1", "BMS1 ", "BMS1 zz:zz", "BMS1 10:20:30:40:50:20 soc", "BMS1 10:20:30:40:50:20 =5 soc=150 age=1 c=,,,", "MAYAK1 x"}) {
    std::string s = g; std::vector<char> b(s.begin(), s.end()); b.push_back(0); bmsPacket(b.data(), 0);
  }
  check(B.seen && B.soc == 100 && B.nc <= 32, "лише справжній MAC прийнято; заряд обрізано до 100 %");

  printf("\n%s: помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", fails);
  return fails ? 1 : 0;
}
