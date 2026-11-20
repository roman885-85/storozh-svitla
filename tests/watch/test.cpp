// Перевірка маячків Сторожа (1.0.23) на Mac: справжній watch.cpp, а мережа, Telegram і годинник — замінники.
// Запуск: tests/watch/run.sh. Пакети маячків «MAYAK1 …» ідуть так само, як від справжніх плат (кожні 2 с з 10.77.0.x).
//  1.0.26: стан у RTC зберігається в секундах від 1970 р. — тут годинник теж учбовий, щоб перевірка не залежала від Mac
#include <ctime>
#include <Arduino.h>
time_t fakeTime(time_t*);
#define time(x) fakeTime(x)
#include "../../firmware/avr-mist/watch.cpp"
#undef time
RtcWatch RW;                                   // у прошивці — zrtc.cpp (RTC_NOINIT)
#include "util_part.cpp"          // strToMac, htmlEsc, jsonEsc, macToStr — витягнуті з util.cpp (run.sh)
#include <vector>
#include <deque>

Settings S;
std::deque<FakePkt> g_pkts;
static int64_t T = 0;
int64_t esp_timer_get_time() { return T; }
time_t fakeTime(time_t*) { return (time_t)(1795000000LL + T / 1000000); }

// --- util ---
bool clockValid() { return true; }
static String hm(int64_t us) { long s = (long)(us / 1000000); char b[16]; snprintf(b, sizeof b, "%02ld:%02ld", (s / 3600) % 24, (s / 60) % 60); return String(b); }
String fmtClock(int64_t us) { return "о " + hm(us); }
String fmtHM(int64_t us) { return hm(us); }
String fmtDur(int64_t sec) { char b[40]; if (sec >= 3600) snprintf(b, sizeof b, "%lld год %lld хв", (long long)sec / 3600, (long long)sec % 3600 / 60); else if (sec >= 60) snprintf(b, sizeof b, "%lld хв", (long long)sec / 60); else snprintf(b, sizeof b, "%lld с", (long long)sec); return String(b); }
String fmtHours(int64_t sec) { return fmtDur(sec); }
String resetReasonText() { return "перевірка"; }
bool bootAfterUpdate() { return false; }      // 1.0.25: тихий запуск після оновлення — тут звичайний запуск
bool boxPlanned() { return false; }           // 1.0.29: перезапуск не з волі людини — повідомлення адміністраторам
static std::vector<std::string> g_log;
void logEvent(const String& t) { g_log.push_back(t.s); printf("    журнал %s: %s\n", hm(T).c_str(), t.c_str()); }

// --- міст: АВР у точці, поки є місто; без живлення ще 30 с «під'єднаний», але ping не відповідає ---
static bool avrUp = true;
static int64_t avrDownAt = 0, avrUpAt = 0;
static bool avrGlitch = false;                  // 1.0.29: АВР сам вийшов із точки живим (перепідключає Wi-Fi)
static int64_t glitchAt = 0;
//  1.0.31: АВР не на зв'язку, але шукає точку — запити пошуку мережі кожні 5 с, перший — через probeDelay після glitchAt
static bool avrProbing = false, avrLeftAlive = true;
static int64_t probeDelay = 4000000;
static int64_t glitchLastPing = -1;             // ≥ 0 — коли АВР востаннє відповідав (до вимкнення), а не glitchAt
bool staUp() { return true; }
int staRssi() { return -50; }
int64_t apChannelChangedUs() { return 0; }
bool findClient(const uint8_t mac[6], ApDev& out) {
  uint8_t am[6];
  if (!strToMac(S.avrMac, am) || memcmp(mac, am, 6)) return false;
  if (avrGlitch) {                               // вийшов сам (код 3), за мить до того відповідав на ping
    out = ApDev();
    out.used = true; memcpy(out.mac, mac, 6); out.assoc = false; out.pingWorks = true;
    out.leftAlive = avrLeftAlive; out.leaveReason = avrLeftAlive ? 3 : 0; out.leaveUs = glitchAt; out.lastPingOkUs = glitchLastPing >= 0 ? glitchLastPing : glitchAt;
    if (avrProbing && T - glitchAt >= probeDelay) {
      int64_t k = (T - glitchAt - probeDelay) / 5000000;
      out.lastProbeUs = glitchAt + probeDelay + k * 5000000; out.probeRssi = -80;
    }
    return true;
  }
  if (!avrUp && T - avrDownAt > 30LL * 1000000) return false;
  out = ApDev();
  out.used = true; memcpy(out.mac, mac, 6); out.assoc = true; out.pingWorks = true; out.rssi = -80;
  out.lastPingOkUs = avrUp ? T : avrDownAt;
  out.assocUs = avrUpAt;
  return true;
}
static int g_outStarts = 0;                     // скільки разів облік почав нове відключення
void statsTransition(int from, int to, int64_t, bool) { if (to == 0) g_outStarts++; (void)from; }
void statsTick(int) {}
void sigEvent(int, int) {}
void lcdAlert(int, const String&, int) {}   // 1.0.26: ефекти дисплея — тут не перевіряємо
String schLostNote() { return String(); }
String schBackNote() { return String(); }
String schStatusLine() { return String(); }

// --- Telegram ---
struct Sent { int64_t at; std::string text; bool silent; uint16_t cat; };
static std::vector<Sent> g_tg;
bool tgBroadcast(int64_t evUs, const String& text, bool silent, uint16_t cat) {
  std::string t = text.s;
  size_t p = t.find("%T");
  if (p != std::string::npos) t.replace(p, 2, fmtClock(evUs).s);
  g_tg.push_back({T, t, silent, cat});
  std::string one = t;
  for (auto& c : one) if (c == '\n') c = '|';
  printf("  >>> TG %s [%s, кат %03x]: %s\n", hm(T).c_str(), silent ? "без звуку" : "ЗІ ЗВУКОМ", cat, one.c_str());
  return true;
}

// --- симуляція ---
struct SimB { uint8_t mac[6]; uint8_t ip; bool on; int64_t last; };
static std::vector<SimB> sims;
static SimB& sim(const char* mac, uint8_t ip) { SimB b{}; strToMac(mac, b.mac); b.ip = ip; b.on = false; b.last = -10000000; sims.push_back(b); return sims.back(); }
static void run(int64_t untilSec) {
  while (T < untilSec * 1000000LL) {
    T += 250000;
    for (auto& b : sims)
      if (b.on && T - b.last >= 2000000) {
        b.last = T;
        char t[96];
        snprintf(t, sizeof t, "MAYAK1 %s up=%lld rssi=-60 ver=1.0.17", macToStr(b.mac).c_str(), (long long)(T / 1000000));
        g_pkts.push_back({T, IPAddress(10, 77, 0, b.ip), t});
      }
    watchLoop();
  }
}
static int fails = 0;
static void check(bool ok, const char* what) { printf("%s %s\n", ok ? "  ✅" : "  ❌", what); if (!ok) fails++; }
static size_t mark = 0;
static std::vector<Sent> since() { std::vector<Sent> v(g_tg.begin() + mark, g_tg.end()); mark = g_tg.size(); return v; }
static bool has(const std::string& t, const char* x) { return t.find(x) != std::string::npos; }

int main() {
  S.objName = "Церква";
  S.avrMac = "10:20:30:40:50:73";
  S.offSec = 12; S.onSec = 10; S.lineOffSec = 15; S.lineOnSec = 10; S.genRemMin = 10;   // 1.0.29: типово 12 с
  S.notifyStart = true; S.mayakOn = false;
  S.beacons = "10:20:30:40:50:20 g Генератор\n80:65:99:11:22:33 l Фаза B\nце не маячок\n80:65:99:11:22:33 v повтор\n80:65:99:44:55:66 l Котельня <2>";
  SimB& G = sim("10:20:30:40:50:20", 21);
  SimB& L1 = sim("80:65:99:11:22:33", 22);
  SimB& L2 = sim("80:65:99:44:55:66", 23);
  SimB* pG = &G; SimB* pL1 = &L1; SimB* pL2 = &L2;   // (вектор більше не росте — посилання дійсні)
  (void)pG; (void)pL1; (void)pL2;
  watchBegin();

  puts("1) Розбір списку");
  check(nbc == 3, "у списку 3 маячки: рядок без MAC і повтор MAC пропущено");
  check(bc[0].role == 'g' && bc[1].role == 'l' && bc[2].role == 'l', "ролі g, l, l");
  check(bc[2].name == String("Котельня <2>"), "назва з пробілами й знаками");
  check(lineEnabled() && !legacy(), "генератор за списком, не «будь-який маячок»");

  puts("2) Запуск: місто є, обидві лінії під напругою, генератор стоїть");
  sims[1].on = sims[2].on = true;
  run(120);
  auto v = since();
  check(v.size() == 1 && has(v[0].text, "Сторож світла перезапустився") && v[0].cat == C_ADMIN && v[0].silent,
        "лише повідомлення про запуск — адміністраторам, без звуку; перше визначення ліній без повідомлень");
  check(grid.st == T_ON && line.st == T_OFF && bc[1].t.st == T_ON && bc[2].t.st == T_ON, "місто є, генератор стоїть, обидві лінії є");

  puts("3) О 200 с зникло місто: АВР і обидві лінії разом");
  avrUp = false; avrDownAt = T; sims[1].on = sims[2].on = false;
  run(320);
  v = since();
  check(v.size() == 2, "два повідомлення");
  check(v.size() == 2 && has(v[0].text, "Зникло міське світло") && !v[0].silent, "спершу «Зникло міське світло» зі звуком");
  check(v.size() == 2 && has(v[1].text, "Зміни на лініях") && has(v[1].text, "Фаза B") && has(v[1].text, "Котельня &lt;2&gt;") &&
        has(v[1].text, "Разом із міським світлом") && v[1].silent && v[1].cat == RF_LINE, "потім «🔌 Зміни на лініях» без звуку, «Разом із міським світлом», категорія «Маячки»");

  puts("4) О 400 с запустили генератор: він живить лише Фазу B");
  sims[0].on = true; sims[1].on = true;
  run(500);
  v = since();
  check(v.size() == 2, "два повідомлення");
  bool gen = false, l1 = false;
  for (auto& x : v) {
    if (has(x.text, "Працює генератор") && x.cat == RF_GEN) gen = true;
    if (has(x.text, "Напруга на лінії є") && has(x.text, "Фаза B") && !x.silent && has(x.text, "🟡 Працює генератор") && x.cat == RF_LINE) l1 = true;
  }
  check(gen, "«🟡 Працює генератор»");
  check(l1, "«🔌 Напруга на лінії є: Фаза B» зі звуком і «Працює генератор» — це не місто");

  puts("5) О 600 с повернулось місто; генератор ще працює; Котельня знову під напругою");
  avrUp = true; avrUpAt = T; sims[2].on = true;
  run(700);
  v = since();
  bool back = false, l2 = false;
  for (auto& x : v) {
    if (has(x.text, "Міське світло повернулось") && has(x.text, "Генератор ще працює")) back = true;
    if (has(x.text, "Напруга на лінії є") && has(x.text, "Котельня") && x.silent && has(x.text, "Разом із міським світлом")) l2 = true;
  }
  check(back, "«Міське світло повернулось … Генератор ще працює — вимкніть його»");
  check(l2, "«🔌 Котельня: є напруга» без звуку, «Разом із міським світлом»");
  check(v.size() >= 2 && has(v[0].text, "Міське світло повернулось"), "місто — першим");

  puts("6) О 800 с генератор вимкнули");
  sims[0].on = false;
  run(900);
  v = since();
  check(v.size() == 1 && has(v[0].text, "Генератор вимкнено"), "«✅ Генератор вимкнено», про лінії нічого (Фаза B тепер від міста й не зникала)");

  puts("7) «📊 Стан» і стан для сторінки");
  String st = statusReport();
  printf("%s\n", st.c_str());
  check(has(st.s, "🔌 Фаза B: є напруга") && has(st.s, "🔌 Котельня &lt;2&gt;: є напруга"), "рядки ліній у «📊 Стан»");
  FILE* f = fopen("/tmp/watch-test.json", "w"); fputs(watchJson().c_str(), f); fclose(f);
  String bt = watchBeaconsText();
  printf("%s\n", bt.c_str());
  check(has(bt.s, "Генератор (генератор): мовчить") && has(bt.s, "Фаза B (лінія): на зв'язку"), "«🛠 Сторож»: маячки");

  puts("8) Зміни списку");
  uint8_t m[6]; String err;
  strToMac("80:65:99:11:22:33", m);
  check(watchBeaconEdit(m, 'a', 'l', "Фаза Б", err) && bc[1].name == String("Фаза Б") && bc[1].t.st == T_ON, "перейменування не скидає стан лінії");
  strToMac("10:20:30:40:50:73", m);
  check(!watchBeaconEdit(m, 'a', 'v', "", err) && has(err.s, "АВР"), "MAC АВР — відмова");
  strToMac("02:00:00:00:00:09", m);
  check(!watchBeaconEdit(m, 'a', 'x', "", err), "невідома роль — відмова");
  check(!watchBeaconEdit(m, 'd', 0, "", err), "прибрати те, чого немає, — відмова");
  for (int k = 1; k <= 3; k++) { strToMac(("02:00:00:00:00:0" + String(k)).c_str(), m); watchBeaconEdit(m, 'a', 'v', "", err); }
  strToMac("02:00:00:00:00:07", m);
  check(nbc == 6 && !watchBeaconEdit(m, 'a', 'v', "", err), "сьомий — відмова, у списку щонайбільше 6");
  strToMac("10:20:30:40:50:20", m);
  check(S.mayakOn, "є маячок генератора — mayakOn стоїть");
  check(watchBeaconEdit(m, 'd', 0, "", err) && !S.mayakOn && !lineEnabled(), "прибрали генератор — mayakOn знято, генератора немає");
  printf("    список: %s\n", S.beacons.c_str());

  puts("9) Чужий маячок, коли список є");
  sim("80:65:99:aa:bb:cc", 30);
  sims.back().on = true;
  size_t lg = g_log.size();
  run(960);
  bool said = false;
  for (size_t k = lg; k < g_log.size(); k++) if (has(g_log[k], "80:65:99:aa:bb:cc") && has(g_log[k], "немає в списку")) said = true;
  check(said && line.st == T_UNK && since().empty(), "у журналі «його немає в списку маячків», генератором не вважається, повідомлень немає");

  puts("10) Як до 1.0.23: список порожній, «маячок встановлено» — будь-який маячок = генератор");
  S.beacons = ""; S.mayakOn = true; watchReload();
  run(1000);
  check(legacy() && lineEnabled() && line.st == T_ON, "генератор працює за будь-яким маячком");
  v = since();
  check(v.empty(), "перше визначення після переходу — без повідомлення");

  //  ---------- 1.0.26: стан і «відколи» переживають перезапуск ----------
  auto reboot = [&]() {                          // що зникає з оперативною пам'яттю (RTC — ні)
    grid = Track(); line = Track();
    for (auto& b : bs) b = BSeen();
    for (int k = 0; k < nbc; k++) bc[k].t = Track();
    bootUs = T; startAnnounced = false; bothSince = 0; lbatchUs = 0;
    rwGridTried = rwLineTried = rwGridSame = false;
    gridChecked = lineChecked = true; bootRestoreDone = false;   // 1.0.29
    g_pkts.clear();
    for (auto& b : sims) b.last = -10000000;
  };
  puts("11) Перезапуск під час відключення: міста як не було, так і немає");
  for (auto& b : sims) b.on = false;
  S.beacons = "10:20:30:40:50:20 g Генератор"; S.mayakOn = true; watchReload();
  avrUp = true; avrUpAt = T;
  run(T / 1000000 + 120);
  avrUp = false; avrDownAt = T;
  int64_t offAt = T;
  run(T / 1000000 + 1800);                       // пів години без міста
  since();
  bool ex0; int64_t s0 = watchGridSince(ex0);
  check(grid.st == T_OFF && ex0, "до перезапуску: міста немає, момент відомий");
  int outs = g_outStarts;
  reboot();
  run(T / 1000000 + 2);
  check(grid.st == T_OFF && line.st == T_OFF, "1.0.29: за 2 с після запуску — уже «немає», без «визначається»");
  run(T / 1000000 + 148);
  v = since();
  bool ex1; int64_t s1 = watchGridSince(ex1);
  long long d = (long long)((s1 - s0) / 1000000);
  printf("    відколи до перезапуску: %lld с від запуску теста, після: %lld с; різниця %lld с\n", (long long)(s0 / 1000000), (long long)(s1 / 1000000), d);
  check(grid.st == T_OFF && ex1 && d >= -2 && d <= 2, "після перезапуску: «немає» з того самого моменту, точно");
  check(g_outStarts == outs, "облік не почав нового відключення");
  bool lost = false; for (auto& x : v) if (has(x.text, "Зникло")) lost = true;
  check(!lost, "«Зникло міське світло» вдруге не надіслано");
  bool quiet = false; for (auto& x : v) if (has(x.text, "Сторож світла перезапустився") && x.silent && x.cat == C_ADMIN) quiet = true;
  check(quiet, "«Сторож світла перезапустився» — адміністраторам, без звуку");
  (void)offAt;
  puts("12) За час перезапуску місто повернулось");
  reboot();
  avrUp = true; avrUpAt = T;
  run(T / 1000000 + 150);
  v = since();
  bool back12 = false; std::string bt12;
  for (auto& x : v) if (has(x.text, "Міське світло повернулось")) { back12 = true; bt12 = x.text; }
  check(back12 && has(bt12, "Не було 3") && !has(bt12, "щонайменше"), "«Міське світло повернулось · Не було 3… хв» — справжня тривалість, без «щонайменше»");
  if (back12) { for (auto& ch : bt12) if (ch == '\n') ch = '|'; printf("    %s\n", bt12.c_str()); }
  puts("13) Запис у RTC давній (понад 10 хв без запису) — не віримо");
  avrUp = false; avrDownAt = T;
  run(T / 1000000 + 300);
  T += 20LL * 60 * 1000000;                      // Сторож «лежав» 20 хв
  reboot();
  run(T / 1000000 + 150);
  bool ex2; watchGridSince(ex2);
  check(grid.st == T_OFF && !ex2, "стан визначено наново, «щонайменше» — бо що було за 20 хв, невідомо");

  //  ---------- 1.0.29: без «визначається» й без зайвих переходів ----------
  puts("14) Перезапуск, коли місто є; АВР знову в точці за 20 с");
  avrUp = true; avrUpAt = T;
  run(T / 1000000 + 120);
  since();
  bool exOn; int64_t sOn = watchGridSince(exOn);
  int outs14 = g_outStarts;
  reboot();
  avrUpAt = T + 20LL * 1000000;                  // АВР шукає точку після її перезапуску
  std::vector<int> seen14;
  for (int k = 0; k < 120; k++) { run(T / 1000000 + 1); if (seen14.empty() || seen14.back() != grid.st) seen14.push_back(grid.st); }
  v = since();
  bool ex14; int64_t s14 = watchGridSince(ex14);
  check(seen14.size() == 1 && seen14[0] == T_ON, "увесь час після запуску — «є», жодного «визначається» чи «немає»");
  check(s14 == sOn && ex14 == exOn, "«відколи» — той самий, що до перезапуску");
  check(v.size() == 1 && has(v[0].text, "перезапустився") && v[0].cat == C_ADMIN, "лише «перезапустився» адміністраторам — без «Зникло» й «Повернулось»");
  check(g_outStarts == outs14, "облік не почав відключення");
  puts("15) Перезапуск, коли місто є, а за час перезапуску світло зникло");
  reboot();
  avrUp = false; avrDownAt = T - 60LL * 1000000;
  run(T / 1000000 + 150);
  v = since();
  bool lost15 = false, start15 = false;
  for (auto& x : v) { if (has(x.text, "Зникло міське світло") && !x.silent && x.cat == RF_LIGHT) lost15 = true; if (has(x.text, "перезапустився")) start15 = true; }
  check(grid.st == T_OFF && lost15, "«Зникло міське світло» зі звуком — справжня зміна");
  check(g_outStarts == outs14 + 1, "облік почав одне відключення");
  check(start15, "і «перезапустився» адміністраторам");
  for (auto& x : v) { std::string one = x.text; for (auto& ch : one) if (ch == '\n') ch = '|'; printf("    %s\n", one.c_str()); }

  //  ---------- 1.0.29: обрив зв'язку з АВР — не «міста немає» ----------
  S.glitchSec = 90;
  puts("16) АВР сам вийшов із точки (код 3, живий) і повернувся за 60 с");
  avrUp = true; avrUpAt = T; avrGlitch = false;
  run(T / 1000000 + 120);
  since();
  check(grid.st == T_ON, "до обриву: місто є");
  size_t lg16 = g_log.size();
  avrGlitch = true; glitchAt = T;
  bool off16 = false;
  for (int k = 0; k < 60; k++) { run(T / 1000000 + 1); if (grid.st != T_ON) off16 = true; }
  avrGlitch = false; avrUpAt = T;
  run(T / 1000000 + 60);
  v = since();
  bool said16 = false, back16 = false;
  for (size_t k = lg16; k < g_log.size(); k++) { if (has(g_log[k], "вийшов із мережі сам")) said16 = true; if (has(g_log[k], "обрив зв'язку")) back16 = true; }
  check(!off16 && grid.st == T_ON, "увесь час — «є»: 60 с без АВР після живого виходу — не відключення");
  check(v.empty(), "жодного повідомлення в Telegram");
  check(said16 && back16, "у журналі — «вийшов із мережі сам … обрив зв'язку» і «повернувся»");
  puts("17) АВР сам вийшов живим — і не повернувся (світло зникло відразу після)");
  avrGlitch = true; glitchAt = T;
  int64_t t17 = T;
  int64_t offAt17 = 0;
  for (int k = 0; k < 150 && !offAt17; k++) { run(T / 1000000 + 1); if (grid.st == T_OFF) offAt17 = T; }
  v = since();
  long long after17 = offAt17 ? (long long)((offAt17 - t17) / 1000000) : -1;
  printf("    «міста немає» через %lld с після виходу\n", after17);
  check(offAt17 && after17 >= 95 && after17 <= 105, "«міста немає» — лише через ~90 с (S.glitchSec) + 10 с, а не за 12 с");
  bool lost17 = false; for (auto& x : v) if (has(x.text, "Зникло міське світло")) lost17 = true;
  check(lost17, "і тоді «Зникло міське світло»");
  bool ex17; int64_t s17 = watchGridSince(ex17);
  check(s17 <= t17 + 1000000 && s17 >= t17 - 3000000, "час зникнення — коли АВР вийшов, а не коли минули 3 хв");
  avrGlitch = false;
  puts("18) Справжнє зникнення живлення: АВР замовк, із точки не виходив — «міста немає» за ~12 с");
  avrUp = true; avrUpAt = T;
  size_t lg18 = g_log.size();
  run(T / 1000000 + 120);
  bool wrong18 = false; for (size_t k = lg18; k < g_log.size(); k++) if (has(g_log[k], "не зникало")) wrong18 = true;
  check(!wrong18, "після справжнього відключення (17) — без «світло не зникало»");
  since();
  avrUp = false; avrDownAt = T;
  int64_t t18 = T, offAt18 = 0;
  for (int k = 0; k < 120 && !offAt18; k++) { run(T / 1000000 + 1); if (grid.st == T_OFF) offAt18 = T; }
  long long after18 = offAt18 ? (long long)((offAt18 - t18) / 1000000) : -1;
  printf("    «міста немає» через %lld с\n", after18);
  check(offAt18 && after18 <= 15, "«міста немає» не довше 15 с (власник: «лучше не больше 15»)");
  bool ex18; int64_t s18 = watchGridSince(ex18);
  long long d18 = (long long)((s18 - t18) / 1000000);
  printf("    час зникнення в повідомленні: %lld с від справжнього\n", d18);
  check(ex18 && d18 >= -2 && d18 <= 1, "час зникнення — остання відповідь АВР (±2 с)");

  //  ---------- 1.0.31: АВР шукає мережу — живий ----------
  puts("19) АВР замовк і випав із точки мовчки (не «вийшов сам»), але 5 хв шукає мережу; потім повернувся");
  avrUp = true; avrUpAt = T; avrGlitch = false;
  run(T / 1000000 + 120);
  since();
  size_t lg19 = g_log.size();
  avrGlitch = true; avrLeftAlive = false; avrProbing = true; probeDelay = 4000000; glitchAt = T;
  bool off19 = false;
  for (int k = 0; k < 300; k++) { run(T / 1000000 + 1); if (grid.st != T_ON) off19 = true; }
  avrGlitch = false; avrProbing = false; avrLeftAlive = true; avrUpAt = T;
  run(T / 1000000 + 30);
  v = since();
  bool said19 = false; for (size_t k = lg19; k < g_log.size(); k++) if (has(g_log[k], "шукає мережу Сторожа")) said19 = true;
  check(!off19 && grid.st == T_ON, "5 хв без зв'язку, але шукає мережу — увесь час «є»");
  check(v.empty(), "жодного повідомлення");
  check(said19, "у журналі — «шукає мережу Сторожа … живий»");
  puts("20) Світла не було; повернулось — АВР спершу шукає мережу, під'єднується за 8 с");
  avrUp = false; avrDownAt = T;
  run(T / 1000000 + 60);
  check(grid.st == T_OFF, "спершу — «немає»");
  since();
  avrGlitch = true; avrLeftAlive = false; avrProbing = true; probeDelay = 0; glitchAt = T;   // увімкнувся: одразу шукає
  glitchLastPing = avrDownAt;                    // востаннє відповідав перед відключенням
  int64_t t20 = T, on20 = 0;
  for (int k = 0; k < 8 && !on20; k++) { run(T / 1000000 + 1); if (grid.st == T_ON) on20 = T; }
  avrGlitch = false; avrProbing = false; avrLeftAlive = true; avrUp = true; avrUpAt = T; glitchLastPing = -1;
  for (int k = 0; k < 30 && !on20; k++) { run(T / 1000000 + 1); if (grid.st == T_ON) on20 = T; }
  v = since();
  long long a20 = on20 ? (long long)((on20 - t20) / 1000000) : -1;
  printf("    «місто є» через %lld с після першого запиту АВР\n", a20);
  check(on20 && a20 <= 12, "«місто є» за ~10 с від першого запиту, ще до під'єднання");
  bool back20 = false; for (auto& x : v) if (has(x.text, "Міське світло повернулось")) back20 = true;
  check(back20, "«Міське світло повернулось»");

  printf("\n%s: %d помилок\n", fails ? "НЕ ПРОЙДЕНО" : "УСЕ ПРОЙДЕНО", fails);
  return fails ? 1 : 0;
}
