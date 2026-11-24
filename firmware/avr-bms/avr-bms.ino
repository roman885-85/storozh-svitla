/*  Міст батареї «Сторожа світла» — ESP32-C3 біля JK BMS (плата AirM2M / LuatOS «ESP32-C3 Core», як маячок).
 *
 *  Власник 22.11.2026: «добавляй функцию мониторинга бмс через esp 32 c3», «с3 держит постоянную связь с бмс, а сторож
 *  лишь читает эти данные из с3», «в ha эта работа ранее была организована»; прошивку обрав власну, як у маячка.
 *
 *  Плата під'єднана до JK BMS по Bluetooth і кожні 5 с каже Сторожу, що з батареєю: UDP «BMS1 …» на 10.77.0.1:47801
 *  (той самий порт, що й маячки). Сторож до BMS не під'єднується й нічого їй не пише.
 *
 *  Як у мосту HA (esphome-jk-bms, jk_bms_ble): після під'єднання — запит «хто ти» (0x97), далі запит стану (0x96) кожні
 *  5 с, доки не прийде перший кадр стану; потім BMS шле кадри сама, а плата лише слухає. Інших команд плата не шле.
 *  BMS пищить на кожне під'єднання (журнал HA 08.11: «кожне перепідключення — писк», сотня за добу на слабкому сигналі),
 *  тож після короткого з'єднання пауза перед новим росте: 30 с, 1, 2, 5 хв; після довгого — 10 с.
 *  Живити плату — від ДБЖ чи інвертора, не з міської розетки: батарея важлива саме тоді, коли світла немає.
 *
 *  1.2.0 (23.11.2026). Власник: «для моста бмс сделать поддержку подключения к нескольким бмс (путем подключения к ним
 *  не одновременно, а циклично раз в выбранный период их опрашивать, чтобы они постоянно не пищали при опросах),
 *  добавить световую индикацию светодиодами или через адресную ленту, добавить функцию с пассивным бузером и активным,
 *  для звукового оповещения (аккумулятор зарядился, осталось половина заряда, 30 процентов, 20, 10, 5 и полный разряд,
 *  ошибка аккумулятора, возможно придумай еще что-то)».
 *    • Два режими: «одна BMS — постійне з'єднання» (як було; типово) і «кілька BMS — по черзі»: до 4 BMS, раз на
 *      період (1–30 хв) міст під'єднується до кожної, бере кадр стану й від'єднується — кожна BMS пищить раз на період.
 *    • Сторожу — окремий пакет на кожну батарею (bi — номер, bn — скільки, per — період, lbl — назва); Сторож 1.0.48+.
 *    • Світло й звук — alerts.h (події — batev_core.h, мелодії й кадри — alert_core.h, обидва перевірено на Mac).
 *    • Дисплей: кілька батарей — по черзі, по 5 с.
 *
 *  Налаштування з телефона — як у маячка: без мережі Сторожа (ще не задано, 3 хв без зв'язку або кнопка BOOT 3 с)
 *  плата піднімає відкриту мережу «BMS-XXXX» зі сторінкою http://192.168.4.1: мережа Сторожа і які BMS слухати
 *  (типово — найближчу JK, з найсильнішим сигналом). Сторінка відкривається й зі сторінки Сторожа (/bms/).
 *
 *  По USB (115200, рядок з Enter):
 *    wifi <назва> <пароль>   — мережа Сторожа (у лапках, якщо є пробіли)
 *    bms <MAC> | bms auto    — яку BMS слухати в режимі «одна BMS» (auto — найближчу)
 *    info                    — стан;  scan — які BMS чути;  dump — кадри BMS шістнадцятково (ще раз — вимкнути)
 *    poll — опитати всі BMS зараз («по черзі»);  mute — тиша (як коротке натискання BOOT);  reboot — перезапуск
 *  У флеш пишуть лише команди wifi, bms і кнопки «Зберегти» на сторінці (правило власника).
 *
 *  Світлодіоди плати: D4 (GPIO12) коротко блимає на кожне повідомлення Сторожу; D5 (GPIO13) часто блимає, поки немає
 *  мережі Сторожа, рідко — поки немає BMS. Кнопка BOOT: коротко — тиша (звук і повтори важливого), 3 с — сторінка
 *  налаштування.
 */
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_chip_info.h>
#include <BLEDevice.h>
#include <HTTPUpdate.h>
#include <NetworkClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "jk_core.h"
#include "batev_core.h"
#include "alert_core.h"
#include "version.h"

static const int LED_BEAT = 12, LED_NOLINK = 13, BTN = 9;
static const uint16_t BRIDGE_PORT = 47801;
static const IPAddress AP_IP(192, 168, 4, 1);
static String ssid, pass, macS, cmdLine, apName, lastErr;
static WiFiUDP udp;
static WebServer web(80);
static DNSServer dns;
static bool leds = false, setupOn = false;
static String otaUrl;                    // адреса прошивки, яку треба завантажити (1.1.0); "" — нічого
static char otaNote[120] = "";           // що з останнім оновленням — для сторінки
static bool fromStorozh();
static uint32_t lastBeat = 0, lastLink = 0, lastTry = 0, setupSince = 0, linkSince = 0, bootMs = 0;
static volatile uint16_t discReason = 0;
static volatile bool gotIp = false;

// ---------- Bluetooth: спільне між задачею Bluetooth, повідомленнями BMS і основним циклом ----------
struct Found { char mac[18]; char name[24]; int rssi; bool jk; };
static Found found[8];
static int nFound = 0;
static uint32_t foundAtMs = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ---------- батареї (1.2.0) ----------
static const int MAX_PK = 4;
struct PackCfg { char mac[18]; char lbl[24]; };
static PackCfg pkc[MAX_PK];                   // перелік власника для режиму «по черзі» (під mux)
static int nPkc = 0;
static volatile uint8_t pollMode = 0;         // 0 — одна BMS, постійне з'єднання; 1 — кілька, по черзі
static volatile uint16_t pollSec = 300;       // період опитування «по черзі», с
static volatile uint32_t cfgGen = 0;          // +1 на кожну зміну переліку чи режиму: задача Bluetooth починає наново
static volatile bool scanReq = false, pollNowReq = false;
static const uint16_t PERIODS[] = {60, 120, 300, 600, 900, 1800};
//  Стан батареї: 0 шукаю, 1 під'єднуюсь, 2 на зв'язку (чи свіжі дані «по черзі»), 3 на зв'язку без стану, 4 пауза,
//  5 чекаю черги, 6 BMS не чути, 7 BMS не відповіла. Той самий номер іде Сторожу як «st».
enum { ST_SCAN = 0, ST_CONN, ST_OK, ST_NOSTAT, ST_PAUSE, ST_WAIT, ST_GONE, ST_FAIL };

struct PackRt {
  jk::Cells c;                   // останній кадр стану (під mux)
  jk::Info in;                   // «хто ти» (під mux)
  bool hasInfo = false;
  uint8_t hint = 0;              // розкладка за прошивкою BMS
  volatile uint32_t atMs = 0;    // коли прийшов останній кадр стану (0 — ще не було)
  volatile uint8_t st = ST_SCAN;
  volatile int rssi = 0;
  char mac[18] = "", name[24] = "";   // з якою BMS був зв'язок (під mux)
  volatile uint32_t nextMs = 0;  // «по черзі»: коли опитати знову (0 — зараз)
  volatile uint32_t polls = 0, fails = 0;
  //  далі — лише основний цикл (alerts.h, display.h)
  uint32_t seenAt = 0, avgAt = 0;
  float fastMa = 0, slowMa = 0;  // струм, мА: згладжений ~5 с (показ, напрям) і середній ~5 хв (на скільки вистачить)
  bool avgInit = false;
  char evMac[18] = "";           // для якої BMS ведеться стан подій
  bev::State ev;
  uint8_t crit = 0;              // важлива подія, що повторюється (bev::E_5, E_EMPTY, E_ERROR), 0 — немає
  uint32_t critNext = 0;
};
static PackRt R[MAX_PK];
static volatile int curPk = 0;               // якій батареї належить поточне з'єднання

static volatile uint8_t bst = ST_SCAN;       // загальний стан задачі Bluetooth («по черзі» між опитуваннями — ST_WAIT)
static char curMac[18] = "", curName[24] = "";
static volatile int curRssi = 0;
static volatile bool gotStatus = false;      // у цьому з'єднанні кадр стану вже був
static volatile uint32_t reconnects = 0, connSinceMs = 0, waitUntilMs = 0, bleBeat = 0, badFmt = 0, cmds = 0;

//  1.1.1: «скільки мс тому» для мітки, яку пише ІНША задача (задача Bluetooth, зворотний виклик кадрів BMS). Мітку
//  читаємо ДО millis(), а різницю беремо зі знаком. До 1.1.1 час брали раніше (now на початку циклу), і якщо мітку
//  записали вже після, now − мітка без знака давала ~4,29·10⁹ мс. 23.11.2026 так міст тричі за 5 хв «бачив», що задача
//  Bluetooth завмерла, і перезапускався через хвилину роботи, а задача Bluetooth — що «BMS замовкла на хвилину», і рвала
//  з'єднання (BMS щоразу пищить). Та сама пастка, що в годиннику-інформері й моніторі сцени.
static inline uint32_t agoMs(volatile uint32_t& stamp) {
  uint32_t at = stamp;
  int32_t d = (int32_t)(millis() - at);
  return d > 0 ? (uint32_t)d : 0;
}
static char noteBuf[192] = "";          // що сталося з останньою спробою (для сторінки й info)
static char pinBuf[18] = "";            // яку BMS слухати в режимі «одна BMS»: MAC малими; "" — найближчу JK
static jk::Assembler as;
static BLERemoteCharacteristic* wch = nullptr;
static volatile bool dumpOn = false, dumpReady = false;
static uint8_t dumpBuf[jk::FRAME];
static char goodMac[18] = "";           // BMS, що вже дала стан у цьому запуску (лише в RAM)

static void led(int pin, int v) { if (leds) digitalWrite(pin, v); }

static String jsonEsc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
//  Між задачею Bluetooth і основним циклом — лише копії буферів під mux (String ділити між задачами не можна).
static void setNote(const String& s) { portENTER_CRITICAL(&mux); strlcpy(noteBuf, s.c_str(), sizeof noteBuf); portEXIT_CRITICAL(&mux); }
static String getNote() { char t[sizeof noteBuf]; portENTER_CRITICAL(&mux); memcpy(t, noteBuf, sizeof t); portEXIT_CRITICAL(&mux); t[sizeof t - 1] = 0; return String(t); }
static String getPin() { char t[18]; portENTER_CRITICAL(&mux); memcpy(t, pinBuf, sizeof t); portEXIT_CRITICAL(&mux); t[17] = 0; return String(t); }
static String getCur() { char t[18]; portENTER_CRITICAL(&mux); memcpy(t, curMac, sizeof t); portEXIT_CRITICAL(&mux); t[17] = 0; return String(t); }
static int packN() { return pollMode ? nPkc : 1; }        // скільки батарей показуємо й шлемо Сторожу

//  Повідомлення від BMS (задача Bluetooth ядра): зібрати кадр і розібрати — у дані тієї батареї, з якою зараз з'єднання.
static void onNotify(BLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (!as.feed(d, (int)len)) return;
  const uint8_t* f = as.buf;
  int k = curPk;
  if (k < 0 || k >= MAX_PK) return;
  if (dumpOn && !dumpReady) { memcpy(dumpBuf, f, jk::FRAME); dumpReady = true; }
  if (f[4] == 0x03) {
    jk::Info in;
    if (jk::parseInfo(f, in)) { uint8_t h = jk::fmtHint(in); portENTER_CRITICAL(&mux); R[k].in = in; R[k].hasInfo = true; R[k].hint = h; portEXIT_CRITICAL(&mux); }
  } else if (f[4] == 0x02) {
    jk::Cells c;
    if (jk::parseCells(f, R[k].hint, c)) {
      portENTER_CRITICAL(&mux);
      R[k].c = c;
      portEXIT_CRITICAL(&mux);
      R[k].atMs = millis() | 1;
      if (!gotStatus) { portENTER_CRITICAL(&mux); memcpy(goodMac, curMac, sizeof goodMac); portEXIT_CRITICAL(&mux); }   // це справді JK — запам'ятати
      gotStatus = true;
    } else badFmt++;
  }
}

static bool macOk(const String& m) {
  if (m.length() != 17) return false;
  for (int i = 0; i < 17; i++) { char c = m[i]; if (i % 3 == 2 ? c != ':' : !isxdigit((unsigned char)c)) return false; }
  return true;
}
static bool periodOk(long s) { for (uint16_t x : PERIODS) if (x == s) return true; return false; }

//  Перелік BMS: рядок «MAC\tназва\nMAC\tназва…» у NVS «pk», режим — «pm», період — «pp»
static void loadPacks(Preferences& p) {
  uint8_t m = p.getUChar("pm", 0);
  uint16_t ps = p.getUShort("pp", 300);
  pollSec = periodOk(ps) ? ps : 300;
  String s = p.getString("pk", "");
  int n = 0, i = 0;
  while (i < (int)s.length() && n < MAX_PK) {
    int e = s.indexOf('\n', i);
    if (e < 0) e = s.length();
    String line = s.substring(i, e);
    i = e + 1;
    int t = line.indexOf('\t');
    String mac = t < 0 ? line : line.substring(0, t), lbl = t < 0 ? String() : line.substring(t + 1);
    if (!macOk(mac)) continue;
    mac.toLowerCase();
    strlcpy(pkc[n].mac, mac.c_str(), sizeof pkc[n].mac);
    strlcpy(pkc[n].lbl, lbl.c_str(), sizeof pkc[n].lbl);
    n++;
  }
  nPkc = n;
  pollMode = (m && n) ? 1 : 0;                     // перелік порожній — як «одна BMS»
}
static void loadPrefs() {
  Preferences p;
  if (p.begin("bms", true)) {
    ssid = p.getString("ssid", ""); pass = p.getString("pass", ""); strlcpy(pinBuf, p.getString("mac", "").c_str(), sizeof pinBuf);
    loadPacks(p);
    p.end();
  }
  //  Плата була маячком (без стирання флешу) — мережа Сторожа вже відома: лише читаємо, нічого не пишемо
  if (!ssid.length() && p.begin("mayak", true)) {
    ssid = p.getString("ssid", ""); pass = p.getString("pass", ""); p.end();
    if (ssid.length()) Serial.printf("Мережа Сторожа «%s» — з налаштувань маячка, що були на цій платі\n", ssid.c_str());
  }
}
static void saveWifi() {
  Preferences p;
  if (p.begin("bms", false)) { p.putString("ssid", ssid); p.putString("pass", pass); p.end(); }
}
static void savePin() {
  Preferences p;
  if (p.begin("bms", false)) { p.putString("mac", getPin()); p.end(); }
}
static bool savePacks() {
  PackCfg l[MAX_PK];
  int n;
  portENTER_CRITICAL(&mux); memcpy(l, pkc, sizeof l); n = nPkc; portEXIT_CRITICAL(&mux);
  String s;
  for (int i = 0; i < n; i++) s += String(i ? "\n" : "") + l[i].mac + "\t" + l[i].lbl;
  Preferences p;
  if (!p.begin("bms", false)) return false;
  p.putUChar("pm", pollMode);
  p.putUShort("pp", pollSec);
  p.putString("pk", s);
  p.end();
  return true;
}
//  Дані й стан подій батареї — з чистого аркуша (інша BMS у цьому місці переліку, інший режим)
static void packReset(int i) {
  portENTER_CRITICAL(&mux);
  R[i].c = jk::Cells();
  R[i].in = jk::Info();
  R[i].hasInfo = false;
  R[i].hint = 0;
  R[i].mac[0] = R[i].name[0] = 0;
  portEXIT_CRITICAL(&mux);
  R[i].atMs = 0; R[i].st = ST_WAIT; R[i].rssi = 0; R[i].nextMs = 0; R[i].polls = 0; R[i].fails = 0;
  R[i].seenAt = 0; R[i].avgAt = 0; R[i].fastMa = R[i].slowMa = 0; R[i].avgInit = false;
  R[i].evMac[0] = 0; R[i].ev = bev::State(); R[i].crit = 0; R[i].critNext = 0;
}

//  Чи схоже на JK BMS — jk::looksLikeJk (jk_core.h). BMS, з якою вже був зв'язок у цьому запуску, пам'ятаємо (у RAM):
//  її беремо знову за адресою, навіть якщо оголошення прийшло неповне.
static bool jkLike(const String& name, const String& mac, bool svc) {
  if (goodMac[0] && mac.equalsIgnoreCase(goodMac)) return true;
  return jk::looksLikeJk(name.c_str(), mac.c_str(), svc);
}

//  Пошук (5 с, активний — щоб назва приходила завжди; у пасивному JK буває без назви). Заповнює found[] для сторінки,
//  повертає знайдені з переліку want (за MAC, будь-які) і, якщо best ≠ nullptr, найближчу схожу на JK.
static void scanBle(const char want[][18], int nw, BLEAdvertisedDevice* got, bool* have, BLEAdvertisedDevice* best, bool* haveBest) {
  BLEScan* sc = BLEDevice::getScan();
  sc->setActiveScan(true);
  sc->setInterval(100);
  sc->setWindow(99);
  BLEScanResults* r = sc->start(5, false);
  Found tmp[8];
  int n = 0, bestR = -999;
  for (int k = 0; k < nw; k++) have[k] = false;
  if (haveBest) *haveBest = false;
  for (int i = 0; r && i < r->getCount(); i++) {
    BLEAdvertisedDevice d = r->getDevice(i);
    String name = d.getName();
    String mac = d.getAddress().toString();
    int wi = -1;
    for (int k = 0; k < nw; k++) if (want[k][0] && mac.equalsIgnoreCase(want[k])) wi = k;
    bool svc = d.haveServiceUUID() && d.isAdvertisingService(BLEUUID((uint16_t)0xFFE0));
    bool jk = jkLike(name, mac, svc);
    if (!jk && !svc && wi < 0) continue;
    if (n < 8) {                                 // у списку — і схожі на JK, і інші з FFE0 (вибрати вручну)
      strlcpy(tmp[n].mac, mac.c_str(), sizeof tmp[n].mac);
      strlcpy(tmp[n].name, name.length() ? name.c_str() : "без назви", sizeof tmp[n].name);
      tmp[n].rssi = d.getRSSI();
      tmp[n].jk = jk;
      n++;
    }
    if (wi >= 0) { got[wi] = d; have[wi] = true; }
    if (best && jk && d.getRSSI() > bestR) { bestR = d.getRSSI(); *best = d; *haveBest = true; }
  }
  sc->clearResults();
  portENTER_CRITICAL(&mux);
  memcpy(found, tmp, sizeof tmp);
  nFound = n;
  foundAtMs = millis() | 1;
  portEXIT_CRITICAL(&mux);
}
//  Режим «одна BMS»: вибрана власником (за MAC, будь-яка) чи найближча схожа на JK
static bool scanJk(BLEAdvertisedDevice& out) {
  String pin = getPin();
  char w[1][18];
  strlcpy(w[0], pin.c_str(), sizeof w[0]);
  BLEAdvertisedDevice g[1], best;
  bool h[1] = {false}, hb = false;
  if (pin.length()) { scanBle(w, 1, g, h, nullptr, nullptr); if (h[0]) out = g[0]; return h[0]; }
  scanBle(w, 0, g, h, &best, &hb);
  if (hb) out = best;
  return hb;
}
static void scanOnly() {                         // «Шукати BMS» зі сторінки: лише оновити список для сторінки
  char w[1][18] = {""};
  BLEAdvertisedDevice g[1];
  bool h[1];
  scanBle(w, 0, g, h, nullptr, nullptr);
}

static bool setupChars(BLEClient* cl) {
  //  1.2.0: служби — заново щоразу. Бібліотека пам'ятає їх від попереднього з'єднання навіть з ІНШОЮ BMS (кеш не
  //  скидається при від'єднанні), а «по черзі» міст під'єднується до різних BMS одним клієнтом.
  cl->getServices();
  BLERemoteService* s = cl->getService(BLEUUID((uint16_t)0xFFE0));
  if (!s) { setNote("немає служби FFE0 — це не JK BMS?"); return false; }
  BLERemoteCharacteristic* nch = nullptr;
  wch = nullptr;
  //  У нового модуля JK дві характеристики FFE1 (запис і повідомлення) — шукаємо за дескрипторами, а не за назвою.
  auto* m = s->getCharacteristicsByHandle();
  for (auto& kv : *m) {
    BLERemoteCharacteristic* c = kv.second;
    if (!c->getUUID().equals(BLEUUID((uint16_t)0xFFE1))) continue;
    if (!nch && c->canNotify()) nch = c;
    if (!wch && (c->canWriteNoResponse() || c->canWrite())) wch = c;
  }
  if (!nch || !wch) { setNote("у службі FFE0 немає потрібних характеристик FFE1"); return false; }
  if (!nch->subscribe(true, onNotify, true)) { setNote("BMS не дала підписатися на повідомлення"); return false; }
  return true;
}
static void sendCmd(uint8_t cmd) {
  if (!wch) return;
  uint8_t q[20];
  jk::request(cmd, q);
  wch->writeValue(q, 20, !wch->canWriteNoResponse());
  cmds++;
}

//  «По черзі»: під'єднатися до BMS i, «хто ти», узяти кадр стану (до 14 с), від'єднатися.
static bool pollOne(BLEClient* cl, int i, BLEAdvertisedDevice& d, const char* mac) {
  curPk = i;
  String nm = d.getName();
  if (!nm.length()) nm = "JK BMS";
  portENTER_CRITICAL(&mux);
  strlcpy(curMac, mac, sizeof curMac);
  strlcpy(curName, nm.c_str(), sizeof curName);
  strlcpy(R[i].mac, mac, sizeof R[i].mac);
  strlcpy(R[i].name, curName, sizeof R[i].name);
  portEXIT_CRITICAL(&mux);
  curRssi = d.getRSSI();
  R[i].rssi = curRssi;
  as.n = 0;
  gotStatus = false;
  R[i].st = ST_CONN;
  bst = ST_CONN;
  bool ok = cl->connect(d.getAddress());
  bleBeat = millis();
  if (!ok) setNote("не вдалося під'єднатися до " + nm);
  else ok = setupChars(cl);
  bool got = false;
  if (ok) {
    reconnects++;
    R[i].st = ST_NOSTAT;
    bst = ST_NOSTAT;
    uint32_t t0 = millis(), lastReq = 0;
    //  1.2.2: «хто ти» — у КОЖНОМУ з'єднанні, далі запит стану через 2 с (як «одна BMS» і міст HA, jk_bms_ble).
    //  23.11.2026 о 22:55 друге опитування без «хто ти» (лише запит стану, тричі за 12 с) BMS JK_B1A8S10P (прошивка
    //  15.24) мовчала, а перше, з ним, — відповіла за 2 с.
    sendCmd(jk::CMD_INFO);
    bool asked = false;
    while (cl->isConnected() && millis() - t0 < 14000) {
      bleBeat = millis();
      if (gotStatus) { got = true; break; }
      uint32_t now = millis();
      if (now - t0 >= 2000 && (!asked || now - lastReq >= 4000)) { sendCmd(jk::CMD_CELLS); lastReq = now; asked = true; }
      delay(100);
    }
    if (cl->isConnected()) { int r = cl->getRssi(); if (r) { curRssi = r; R[i].rssi = r; } }
    setNote(nm + (got ? ": опитано" : ": стану не дала — спробую пізніше"));
  }
  if (cl->isConnected()) cl->disconnect();
  wch = nullptr;
  R[i].polls++;
  if (!got) R[i].fails++;
  R[i].st = got ? ST_OK : ST_FAIL;
  return got;
}
//  Один крок режиму «по черзі»: чиї строки підійшли — один пошук на всіх, далі кожну по черзі
static void cycleStep(BLEClient* cl) {
  uint32_t gen = cfgGen;
  PackCfg l[MAX_PK];
  int n;
  portENTER_CRITICAL(&mux); memcpy(l, pkc, sizeof l); n = nPkc; portEXIT_CRITICAL(&mux);
  if (pollNowReq) { pollNowReq = false; for (int i = 0; i < n; i++) R[i].nextMs = 0; }
  uint32_t now = millis();
  bool due[MAX_PK] = {false, false, false, false}, any = false;
  for (int i = 0; i < n; i++) if (!R[i].nextMs || (int32_t)(now - R[i].nextMs) >= 0) { due[i] = true; any = true; }
  if (!any) { bst = ST_WAIT; delay(500); return; }
  bst = ST_SCAN;
  char w[MAX_PK][18];
  for (int i = 0; i < MAX_PK; i++) strlcpy(w[i], i < n && due[i] ? l[i].mac : "", sizeof w[i]);
  for (int i = 0; i < n; i++) if (due[i]) R[i].st = ST_SCAN;
  BLEAdvertisedDevice got[MAX_PK];
  bool have[MAX_PK];
  scanBle(w, MAX_PK, got, have, nullptr, nullptr);
  bleBeat = millis();
  for (int i = 0; i < n; i++) {
    if (!due[i]) continue;
    if (gen != cfgGen) return;                   // перелік змінили — почати наново
    if (!have[i]) {                              // не чути — пошук BMS не турбує, тож знову за хвилину
      R[i].st = ST_GONE;
      R[i].fails++;
      R[i].nextMs = (millis() + 60000UL) | 1;
      setNote(String("BMS ") + l[i].mac + " не чути (зайнята телефоном? далеко?)");
      continue;
    }
    bool ok = pollOne(cl, i, got[i], l[i].mac);
    //  не відповіла — знову не раніше ніж за 2 хв (кожне під'єднання — писк)
    uint32_t per = (uint32_t)pollSec * 1000UL;
    R[i].nextMs = (millis() + (ok ? per : (per < 120000UL ? 120000UL : per))) | 1;
    bleBeat = millis();
    delay(800);                                  // між батареями — щоб Bluetooth устиг закрити з'єднання
  }
  bst = ST_WAIT;
}

//  Задача Bluetooth: «одна BMS» — пошук → під'єднання → слухати, доки з'єднання живе → пауза → знову;
//  «по черзі» — cycleStep.
static void bleTask(void*) {
  BLEDevice::init("");
  BLEDevice::setPower(ESP_PWR_LVL_P20);           // +20 дБм — як у панелі HA: на межі сигналу BMS рве з'єднання й пищить
  BLEDevice::setMTU(247);                         // більші шматки кадру — менше повідомлень
  BLEClient* cl = BLEDevice::createClient();
  uint32_t backoff = 0;
  for (;;) {
    bleBeat = millis();
    if (scanReq) { scanReq = false; scanOnly(); }
    if (pollMode) { cycleStep(cl); backoff = 0; waitUntilMs = 0; continue; }
    if (waitUntilMs && (int32_t)(millis() - waitUntilMs) < 0) { bst = ST_PAUSE; R[0].st = ST_PAUSE; delay(500); continue; }
    waitUntilMs = 0;
    uint32_t gen = cfgGen;
    curPk = 0;
    bst = ST_SCAN;
    R[0].st = ST_SCAN;
    BLEAdvertisedDevice t;
    if (!scanJk(t)) {
      String pin = getPin();
      setNote(pin.length() ? "вибраної BMS " + pin + " не чути (зайнята телефоном? далеко?)" : String("JK BMS поруч не чути"));
      delay(3000);
      continue;
    }
    if (gen != cfgGen || pollMode) continue;
    bleBeat = millis();
    bst = ST_CONN;
    R[0].st = ST_CONN;
    String tn = t.getName(), ta = t.getAddress().toString();
    if (!tn.length()) tn = "JK BMS";
    portENTER_CRITICAL(&mux);
    strlcpy(curMac, ta.c_str(), sizeof curMac);
    strlcpy(curName, tn.c_str(), sizeof curName);
    strlcpy(R[0].mac, curMac, sizeof R[0].mac);
    strlcpy(R[0].name, curName, sizeof R[0].name);
    R[0].hint = 0;
    portEXIT_CRITICAL(&mux);
    curRssi = t.getRSSI();
    R[0].rssi = curRssi;
    as.n = 0;
    gotStatus = false;
    uint32_t began = 0;
    bool changed = false;
    bool ok = cl->connect(t.getAddress());
    bleBeat = millis();
    if (!ok) setNote("не вдалося під'єднатися до " + tn);
    else ok = setupChars(cl);
    if (ok) {
      reconnects++;
      began = millis();
      connSinceMs = began | 1;
      setNote("під'єднано");
      sendCmd(jk::CMD_INFO);                      // як jk_bms_ble: одразу — «хто ти»
      uint32_t lastReq = millis() - 3000;         // а запит стану — через 2 с і далі кожні 5 с, доки не піде стан
      int reqs = 0;
      while (cl->isConnected()) {
        bleBeat = millis();
        if (gen != cfgGen) { changed = true; setNote("налаштування змінено — під'єднуюсь наново"); break; }
        if (scanReq) { scanReq = false; scanOnly(); }      // пошук для сторінки — і під час з'єднання
        uint32_t now = millis();
        if (!gotStatus) {
          if (now - lastReq >= 5000) {
            if (reqs >= 6) { setNote("BMS не шле стан на запити — перепід'єднуюсь"); break; }
            sendCmd(jk::CMD_CELLS);
            lastReq = now;
            reqs++;
          }
          bst = ST_NOSTAT;
        } else {
          uint32_t age = agoMs(R[0].atMs);        // 1.1.1: не «now − мітка»: кадр міг прийти вже після now
          bst = age < 15000 ? ST_OK : ST_NOSTAT;
          if (age > 60000) { setNote("BMS замовкла на хвилину — перепід'єднуюсь"); break; }
        }
        R[0].st = bst;
        int r = cl->getRssi();
        if (r) { curRssi = r; R[0].rssi = r; }
        delay(500);
      }
      if (cl->isConnected()) cl->disconnect();
      else if (gotStatus) setNote("BMS розірвала з'єднання (" + String((millis() - began) / 1000) + " с на зв'язку)");
    } else if (cl->isConnected()) cl->disconnect();
    wch = nullptr;
    connSinceMs = 0;
    if (changed) { backoff = 0; waitUntilMs = 0; delay(500); continue; }   // змінили власноруч — одразу
    uint32_t lasted = began ? millis() - began : 0;
    //  кожне під'єднання — писк BMS: після короткого з'єднання чекати довше
    //  1.1.0: після короткого з'єднання — 30 с, 1, 2, 5 хв (було від 10 с). 22.11.2026 о 19:13 міст перезапустився, за
    //  8 хв під'єднався 5 разів — і BMS перестала оголошувати себе (як удома 07.11: «її тримає інше підключення»).
    if (lasted > 10UL * 60 * 1000) backoff = 10000;
    else backoff = backoff < 30000 ? 30000 : backoff < 60000 ? 60000 : backoff < 120000 ? 120000 : 300000;
    waitUntilMs = (millis() + backoff) | 1;
  }
}

//  Дисплей Open-Smart 1,8″ (ST7567) — налаштування, показ, сторінка (1.1.0); світло й звук (1.2.0)
static bool alertUsesPin(int p);
#include "display.h"
#include "alerts.h"

// ---------- повідомлення Сторожу ----------
static void addKV(char* b, size_t cap, const char* fmt, ...) {
  size_t L = strlen(b);
  if (L >= cap - 1) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b + L, cap - L, fmt, ap);
  va_end(ap);
}
static void safeName(char* dst, const char* src, size_t cap) {      // без пробілів і «=» — щоб Сторож розібрав «ключ=значення»
  size_t k = 0;
  for (size_t i = 0; src[i] && k + 1 < cap; i++) { char c = src[i]; dst[k++] = (c == ' ' || c == '=' || (uint8_t)c < 0x21) ? '_' : c; }
  dst[k] = 0;
}
//  Кожній батареї — свій пакет: bi — номер з 0, bn — скільки їх, per — період «по черзі» (0 — постійне з'єднання).
//  Нові ключі — до комірок: Сторож до 1.0.48 читає пакет до 599 байтів, решту відкидає.
static void beat(uint32_t now) {
  static char b[720];
  int n = packN();
  for (int i = 0; i < n; i++) {
    jk::Cells c;
    jk::Info in;
    bool hi;
    char mac[18], name[24], lbl[24] = "";
    portENTER_CRITICAL(&mux);
    c = R[i].c; in = R[i].in; hi = R[i].hasInfo;
    memcpy(mac, R[i].mac, sizeof mac); memcpy(name, R[i].name, sizeof name);
    if (pollMode && i < nPkc) { memcpy(lbl, pkc[i].lbl, sizeof lbl); if (!mac[0]) memcpy(mac, pkc[i].mac, sizeof mac); }
    portEXIT_CRITICAL(&mux);
    uint8_t st = pollMode ? R[i].st : bst;
    b[0] = 0;
    //  «по черзі» rc = 0: кожне опитування — нове з'єднання, і Сторож до 1.0.48 писав би про кожне в журнал
    addKV(b, sizeof b, "BMS1 %s up=%lu rssi=%d ver=%s st=%u rc=%lu bad=%lu heap=%lu lcd=%d", macS.c_str(), (unsigned long)(now / 1000), WiFi.RSSI(),
          FW_VERSION, (unsigned)st, (unsigned long)(pollMode ? 0 : reconnects), (unsigned long)(as.bad + badFmt), (unsigned long)ESP.getFreeHeap(), L.on ? 1 : 0);
    addKV(b, sizeof b, " rst=%d bi=%d bn=%d per=%u", (int)esp_reset_reason(), i, n, (unsigned)(pollMode ? pollSec : 0));
    if (pollMode) addKV(b, sizeof b, " pl=%lu pf=%lu", (unsigned long)R[i].polls, (unsigned long)R[i].fails);
    if (lbl[0]) { char lb[24]; safeName(lb, lbl, sizeof lb); addKV(b, sizeof b, " lbl=%s", lb); }
    if (mac[0]) {
      char nm[24];
      safeName(nm, name[0] ? name : "JK_BMS", sizeof nm);
      addKV(b, sizeof b, " bmac=%s name=%s brssi=%d", mac, nm, (int)R[i].rssi);
    }
    uint32_t at = R[i].atMs;
    if (at && c.fmt) {
      addKV(b, sizeof b, " age=%lu fmt=%u soc=%u v=%lu i=%ld rem=%lu full=%lu cyc=%lu soh=%u n=%u lo=%u lox=%u hi=%u hix=%u",
            (unsigned long)(agoMs(R[i].atMs) / 1000), c.fmt, c.soc, (unsigned long)c.totalMv, (long)c.ma, (unsigned long)c.remMah,
            (unsigned long)c.fullMah, (unsigned long)c.cycles, c.soh, c.n, c.vmin, c.imin, c.vmax, c.imax);
      if (c.hasT1) addKV(b, sizeof b, " t1=%d", c.t1);
      if (c.hasT2) addKV(b, sizeof b, " t2=%d", c.t2);
      if (c.hasTmos) addKV(b, sizeof b, " tm=%d", c.tmos);
      addKV(b, sizeof b, " err=%lx chg=%d dis=%d bal=%d heat=%d c=", (unsigned long)c.err, c.chg, c.dis, c.bal, c.heat);
      bool first = true;
      for (int k = 0; k < 32; k++) if (c.mv[k]) { addKV(b, sizeof b, first ? "%u" : ",%u", c.mv[k]); first = false; }
    }
    if (hi) {
      char m[17], h[9], s[9];
      safeName(m, in.model, sizeof m); safeName(h, in.hw, sizeof h); safeName(s, in.sw, sizeof s);
      addKV(b, sizeof b, " model=%s hw=%s sw=%s", m, h, s);
    }
    udp.beginPacket(WiFi.gatewayIP(), BRIDGE_PORT);
    udp.write((const uint8_t*)b, strlen(b));
    udp.endPacket();
  }
}

// ---------- сторінка ----------
//  Сторінка — у page.h: препроцесор Arduino шукає функції й у JavaScript усередині .ino і ламає збірку.
//  1.2.1: стиснута копія — page_gz.h (tools/build.sh → tools/gz_page.py), її й віддаємо, якщо браузер (чи Сторож 1.0.51+)
//  приймає gzip: 38 КБ → 12 КБ. Через Сторожа нестиснута сторінка приходила обрізаною: міст з постійним Bluetooth віддає
//  її повільно, а Сторож тримає відповідь мосту не довше ~9 с.
#include "page.h"
#include "page_gz.h"

static String reasonText(uint16_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_802_1X_AUTH_FAILED: case WIFI_REASON_MIC_FAILURE:
      return "пароль не підходить — перевірте на сторінці Сторожа «Точка доступу для АВР»";
    case WIFI_REASON_NO_AP_FOUND: return "мережі не видно поруч — Сторож увімкнений? Назва правильна?";
    case WIFI_REASON_ASSOC_TOOMANY: return "Сторож уже має забагато пристроїв у своїй мережі";
    case WIFI_REASON_ASSOC_LEAVE: return String();
    default: return r ? "не вдалося під'єднатись (код " + String(r) + ")" : String();
  }
}

static void connectWifi() {
  lastTry = millis();
  if (!ssid.length()) return;
  discReason = 0;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
}
static void startSetup(const char* why) {
  if (setupOn) return;
  setupOn = true;
  setupSince = millis();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  int ch = 1;
  int n = WiFi.scanNetworks(false, false, false, 120);
  for (int i = 0; i < n; i++) if ((ssid.length() && WiFi.SSID(i) == ssid) || WiFi.SSID(i).startsWith("AVR-mist-")) { ch = WiFi.channel(i); break; }
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str(), nullptr, ch);
  dns.start(53, "*", AP_IP);
  Serial.printf("Сторінка налаштування: мережа «%s» без пароля, канал %d, http://192.168.4.1 (%s)\n", apName.c_str(), ch, why);
}
static void stopSetup() {
  if (!setupOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  setupOn = false;
  Serial.println("Сторінку налаштування вимкнено: плата на зв'язку зі Сторожем");
}

static void hRoot() {
  if (web.header("Accept-Encoding").indexOf("gzip") >= 0) {
    web.sendHeader("Content-Encoding", "gzip");
    web.send_P(200, "text/html; charset=utf-8", (PGM_P)PAGE_GZ, PAGE_GZ_LEN);
  } else web.send_P(200, "text/html; charset=utf-8", PAGE, strlen(PAGE));   // send_P — без копії 38 КБ у купу
}
//  Дані однієї батареї для сторінки
static String packJson(int i) {
  jk::Cells c;
  jk::Info in;
  bool hi;
  char mac[18], name[24], lbl[24] = "";
  portENTER_CRITICAL(&mux);
  c = R[i].c; in = R[i].in; hi = R[i].hasInfo;
  memcpy(mac, R[i].mac, sizeof mac); memcpy(name, R[i].name, sizeof name);
  if (pollMode && i < nPkc) { memcpy(lbl, pkc[i].lbl, sizeof lbl); if (!mac[0]) memcpy(mac, pkc[i].mac, sizeof mac); }
  portEXIT_CRITICAL(&mux);
  uint32_t nx = R[i].nextMs;
  long next = pollMode && nx ? (long)((int32_t)(nx - millis()) / 1000) : -1;
  String o = "{\"mac\":\"" + String(mac) + "\",\"name\":\"" + jsonEsc(name) + "\",\"lbl\":\"" + jsonEsc(lbl) + "\",\"st\":" + String(pollMode ? R[i].st : bst) +
             ",\"rssi\":" + String((int)R[i].rssi) + ",\"polls\":" + String((unsigned long)R[i].polls) + ",\"fails\":" + String((unsigned long)R[i].fails) +
             ",\"next\":" + String(next < 0 ? -1 : next);
  uint32_t at = R[i].atMs;
  if (at && c.fmt) {
    o += ",\"c\":{\"age\":" + String(agoMs(R[i].atMs) / 1000) + ",\"fmt\":" + c.fmt + ",\"soc\":" + c.soc + ",\"v\":" + String((unsigned long)c.totalMv) +
         ",\"i\":" + String((long)c.ma) + ",\"rem\":" + String((unsigned long)c.remMah) + ",\"full\":" + String((unsigned long)c.fullMah) +
         ",\"cyc\":" + String((unsigned long)c.cycles) + ",\"soh\":" + c.soh + ",\"n\":" + c.n + ",\"lo\":" + c.vmin + ",\"lox\":" + c.imin +
         ",\"hi\":" + c.vmax + ",\"hix\":" + c.imax + ",\"chg\":" + (c.chg ? "true" : "false") + ",\"dis\":" + (c.dis ? "true" : "false") +
         ",\"bal\":" + (c.bal ? "true" : "false") + ",\"heat\":" + (c.heat ? "true" : "false");
    if (c.hasT1) o += ",\"t1\":" + String(c.t1);
    if (c.hasT2) o += ",\"t2\":" + String(c.t2);
    if (c.hasTmos) o += ",\"tm\":" + String(c.tmos);
    o += ",\"info\":\"" + (hi ? jsonEsc(String(in.model) + ", прошивка " + in.sw) : String()) + "\",\"cells\":[";
    bool first = true;
    for (int k = 0; k < 32; k++) if (c.mv[k]) { o += String(first ? "" : ",") + c.mv[k]; first = false; }
    o += "],\"errs\":[";
    String notes;
    first = true;
    for (int b = 0; b < 32; b++) {
      if (!(c.err >> b & 1)) continue;
      const char* nm = jk::errName(b);
      String t = *nm ? String(nm) : "біт " + String(b);
      if (jk::ERR_INFO >> b & 1) { if (*nm) notes += String(notes.length() ? "," : "") + "\"" + jsonEsc(t) + "\""; continue; }
      o += String(first ? "" : ",") + "\"" + jsonEsc(t) + "\"";
      first = false;
    }
    o += "],\"notes\":[" + notes + "]}";
  }
  return o + "}";
}
static void hState() {
  bool cn = WiFi.isConnected();
  char mac[18], name[24];
  Found fl[8];
  PackCfg l[MAX_PK];
  int nf, nl;
  uint32_t fat;
  portENTER_CRITICAL(&mux);
  nf = nFound; fat = foundAtMs; nl = nPkc;
  memcpy(mac, curMac, sizeof mac); memcpy(name, curName, sizeof name); memcpy(fl, found, sizeof fl); memcpy(l, pkc, sizeof l);
  portEXIT_CRITICAL(&mux);
  String o = String("{\"ver\":\"") + FW_VERSION + "\",\"mac\":\"" + macS + "\",\"ssid\":\"" + jsonEsc(ssid) + "\",\"conn\":" + (cn ? "true" : "false") +
             ",\"rssi\":" + String(cn ? WiFi.RSSI() : 0) + ",\"err\":\"" + jsonEsc(lastErr) + "\",\"ap\":\"" + jsonEsc(apName) +
             "\",\"setup\":" + (setupOn ? "true" : "false") + ",\"pin\":\"" + getPin() + "\"" +
             ",\"trust\":" + (fromStorozh() ? "true" : "false") + ",\"ota\":\"" + jsonEsc(otaNote) + "\",\"var\":\"" + (ARDUINO_USB_CDC_ON_BOOT ? "usb" : "uart") +
             "\",\"heap\":" + String((unsigned long)ESP.getFreeHeap()) + ",\"rst\":" + String((int)esp_reset_reason()) + ",\"up\":" + String((unsigned long)(millis() / 1000)) +
             ",\"mode\":" + String((int)pollMode) + ",\"per\":" + String((unsigned)pollSec);
  o += ",\"b\":{\"st\":" + String(bst) + ",\"mac\":\"" + String(mac) + "\",\"name\":\"" + jsonEsc(name) + "\",\"rssi\":" + String((int)curRssi) +
       ",\"rc\":" + String((unsigned long)reconnects) + ",\"cmds\":" + String((unsigned long)cmds) + ",\"note\":\"" + jsonEsc(getNote()) + "\"}";
  o += ",\"fage\":" + String(fat ? (long)(agoMs(foundAtMs) / 1000) : -1L) + ",\"found\":[";
  for (int i = 0; i < nf; i++) o += String(i ? "," : "") + "{\"mac\":\"" + fl[i].mac + "\",\"name\":\"" + jsonEsc(fl[i].name) + "\",\"rssi\":" + fl[i].rssi + ",\"jk\":" + (fl[i].jk ? "true" : "false") + "}";
  o += "],\"list\":[";
  for (int i = 0; i < nl; i++) o += String(i ? "," : "") + "{\"mac\":\"" + l[i].mac + "\",\"lbl\":\"" + jsonEsc(l[i].lbl) + "\"}";
  o += "],\"packs\":[";
  int n = packN();
  for (int i = 0; i < n; i++) o += String(i ? "," : "") + packJson(i);
  o += "]}";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", o);
}
static void hScan() {
  web.sendHeader("Cache-Control", "no-store");
  int n = WiFi.scanComplete();
  if (web.hasArg("new") || n == WIFI_SCAN_FAILED) {
    if (n != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true, false, false, 120); }
    return web.send(200, "application/json", "{\"busy\":true}");
  }
  if (n == WIFI_SCAN_RUNNING) return web.send(200, "application/json", "{\"busy\":true}");
  String names[24]; int rs[24]; int k = 0;
  for (int i = 0; i < n && k < 24; i++) {
    String s = WiFi.SSID(i);
    if (!s.length() || s == apName) continue;
    int j = 0;
    while (j < k && names[j] != s) j++;
    if (j < k) { if (WiFi.RSSI(i) > rs[j]) rs[j] = WiFi.RSSI(i); continue; }
    names[k] = s; rs[k] = WiFi.RSSI(i); k++;
  }
  String o = "{\"list\":[";
  bool first = true;
  for (int pass2 = 0; pass2 < 2; pass2++)
    for (int i = 0; i < k; i++) {
      bool sto = names[i].startsWith("AVR-mist-");
      if (sto != (pass2 == 0)) continue;
      o += String(first ? "" : ",") + "{\"s\":\"" + jsonEsc(names[i]) + "\",\"r\":" + rs[i] + ",\"st\":" + (sto ? "true" : "false") + "}";
      first = false;
    }
  web.send(200, "application/json", o + "]}");
}
static void hSave() {
  if (!setupOn) return web.send(403, "application/json", "{\"err\":\"Змінити мережу можна лише зі сторінки налаштування (кнопка BOOT 3 с)\"}");
  String s = web.arg("ssid"), p = web.arg("pass");
  s.trim();
  if (!s.length() || s.length() > 32) return web.send(400, "application/json", "{\"err\":\"Назва мережі — від 1 до 32 символів\"}");
  if (p.length() < 8 || p.length() > 63) return web.send(400, "application/json", "{\"err\":\"Пароль — від 8 до 63 символів\"}");
  ssid = s;
  pass = p;
  saveWifi();
  lastErr = "";
  Serial.printf("Сторінка: збережено мережу «%s». Під'єднуюсь…\n", ssid.c_str());
  connectWifi();
  web.send(200, "application/json", "{\"ok\":true}");
}
//  Запит прийшов від Сторожа (його сторінка /bms/… — за входом на Сторожа): йому можна те, що з телефона дозволено лише
//  на сторінці налаштування (кнопка BOOT) — вибрати BMS, оновити прошивку, світло й звук. Інші пристрої мережі — ні.
static bool fromStorozh() { return WiFi.isConnected() && web.client().remoteIP() == WiFi.gatewayIP(); }
static bool trusted(const char* what) {
  if (setupOn || fromStorozh()) return true;
  web.send(403, "application/json", String("{\"err\":\"") + what + " можна зі сторінки Сторожа або зі сторінки налаштування (кнопка BOOT 3 с)\"}");
  return false;
}

//  Оновлення прошивки по мережі (1.1.0): міст сам завантажує файл …-app.bin за адресою (http чи https) через Сторожа.
//  Нова прошивка — «на перевірці»: за хвилину роботи визнаємо її справною; упала раніше — завантажувач поверне попередню.
//  https — без перевірки сертифіката (на платі немає сховища кореневих сертифікатів): адресу задає власник.
//  Сторож 1.0.48+ сам роздає мосту файл, завантажений на його сторінку (http://10.77.0.1:8081/…/avr-bms.bin, з x-MD5).
extern "C" bool verifyRollbackLater() { return true; }
static void hOta() {
  if (!trusted("Оновити")) return;
  String u = web.arg("url");
  u.trim();
  if (!(u.startsWith("http://") || u.startsWith("https://")) || u.length() > 300 || !u.endsWith(".bin"))
    return web.send(400, "application/json", "{\"err\":\"Потрібна адреса файлу …-app.bin, що починається з http:// або https://\"}");
  otaUrl = u;
  strlcpy(otaNote, "завантажую прошивку…", sizeof otaNote);
  web.send(200, "application/json", "{\"ok\":true}");
}
static void otaRun() {
  String u = otaUrl;
  otaUrl = "";
  Serial.printf("Оновлення прошивки: %s\n", u.c_str());
  alertQuiet();                                    // звук і світло — вимкнути на час завантаження
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  t_httpUpdate_return r;
  if (u.startsWith("https://")) { NetworkClientSecure c; c.setInsecure(); r = httpUpdate.update(c, u); }
  else { NetworkClient c; r = httpUpdate.update(c, u); }
  if (r == HTTP_UPDATE_OK) {
    Serial.println("Оновлення записано — перезапуск");
    delay(300);
    ESP.restart();
  }
  snprintf(otaNote, sizeof otaNote, "не вдалося: %s", httpUpdate.getLastErrorString().c_str());
  Serial.printf("Оновлення %s\n", otaNote);
}

static void pinSet(const String& m, const char* by) {
  String v = m;
  v.toLowerCase();
  bool other = !pollMode && v.length() && !v.equalsIgnoreCase(getCur());   // з'єднання з іншою BMS — розірвати
  portENTER_CRITICAL(&mux);
  strlcpy(pinBuf, v.c_str(), sizeof pinBuf);
  portEXIT_CRITICAL(&mux);
  savePin();
  if (other) cfgGen++;
  Serial.printf("%s: %s\n", by, v.length() ? ("слухаю BMS " + v).c_str() : "слухаю найближчу JK BMS");
}
static void hBms() {
  if (!trusted("Змінити BMS")) return;
  String m = web.arg("mac");
  m.trim();
  if (m.length() && !macOk(m)) return web.send(400, "application/json", "{\"err\":\"Хибна адреса BMS\"}");
  pinSet(m, "Сторінка");
  waitUntilMs = 0;
  web.send(200, "application/json", "{\"ok\":true}");
}
//  1.2.0: режим і перелік BMS. mode: 0 — одна BMS постійно, 1 — по черзі; per — період, с; m0…m3, l0…l3 — MAC і назви.
static void hPacks() {
  if (!trusted("Змінити перелік BMS")) return;
  auto bad = [](const String& m) { web.send(400, "application/json", "{\"err\":\"" + jsonEsc(m) + "\"}"); };
  int mode = web.arg("mode").toInt() == 1 ? 1 : 0;
  long per = web.hasArg("per") ? web.arg("per").toInt() : (long)pollSec;
  if (!periodOk(per)) return bad("Період — 1, 2, 5, 10, 15 або 30 хв");
  PackCfg l[MAX_PK];
  memset(l, 0, sizeof l);
  int n = 0;
  for (int i = 0; i < MAX_PK; i++) {
    String m = web.arg("m" + String(i));
    m.trim();
    m.toLowerCase();
    if (!m.length()) continue;
    if (!macOk(m)) return bad("Хибна адреса BMS: " + m);
    for (int k = 0; k < n; k++) if (!strcmp(l[k].mac, m.c_str())) return bad("BMS " + m + " — у переліку двічі");
    String lb = web.arg("l" + String(i));
    lb.replace("\t", " ");
    lb.replace("\n", " ");
    lb.replace("\r", " ");
    lb.trim();
    if (lb.length() > 23) return bad("Назва батареї — до 23 байтів (≈11 літер кирилицею): " + lb);
    strlcpy(l[n].mac, m.c_str(), sizeof l[n].mac);
    strlcpy(l[n].lbl, lb.c_str(), sizeof l[n].lbl);
    n++;
  }
  if (mode == 1 && !n) return bad("Додайте до переліку хоч одну BMS");
  bool modeCh = (pollMode == 1) != (mode == 1);
  bool listCh = n != nPkc;
  for (int i = 0; i < n && !listCh; i++) listCh = strcmp(l[i].mac, pkc[i].mac) != 0;
  portENTER_CRITICAL(&mux);
  memcpy(pkc, l, sizeof pkc);
  nPkc = n;
  portEXIT_CRITICAL(&mux);
  uint16_t oldPer = pollSec;
  pollSec = (uint16_t)per;
  if (modeCh) for (int i = 0; i < MAX_PK; i++) packReset(i);
  else if (listCh && mode == 1) for (int i = 0; i < MAX_PK; i++) {      // на цьому місці переліку інша BMS — з чистого аркуша
    char cur[18];
    portENTER_CRITICAL(&mux); memcpy(cur, R[i].mac, sizeof cur); portEXIT_CRITICAL(&mux);
    if (i >= n || strcmp(cur, l[i].mac)) packReset(i);
  }
  if (!modeCh && mode == 1 && per < oldPer) {     // період коротшав — наступне опитування не пізніше нового строку
    uint32_t lim = millis() + (uint32_t)per * 1000UL;
    for (int i = 0; i < n; i++) if (R[i].nextMs && (int32_t)(R[i].nextMs - lim) > 0) R[i].nextMs = lim | 1;
  }
  pollMode = (uint8_t)mode;
  if (modeCh || listCh) cfgGen++;
  if (!savePacks()) return bad("Не вдалося записати налаштування");
  Serial.printf("Сторінка: %s, BMS у переліку: %d, період %u с\n", mode ? "кілька BMS по черзі" : "одна BMS, постійне з'єднання", n, (unsigned)pollSec);
  web.send(200, "application/json", "{\"ok\":true}");
}
static void hPollNow() {
  if (!trusted("Опитати BMS")) return;
  if (!pollMode) return web.send(400, "application/json", "{\"err\":\"Лише в режимі «кілька BMS по черзі»\"}");
  pollNowReq = true;
  web.send(200, "application/json", "{\"ok\":true}");
}
static void hBleScan() {
  if (!trusted("Шукати BMS")) return;
  scanReq = true;
  web.send(200, "application/json", "{\"ok\":true}");
}
//  1.2.3: кнопка «Перезапустити міст» (власник 23.11.2026: «в устройствах в веб интерфейсах нужны кнопки перезагрузки»).
//  Відповідь — одразу, перезапуск — за 0,6 с (щоб відповідь дійшла через Сторожа)
static uint32_t rebootAt = 0;
static void hReboot() {
  if (!trusted("Перезапустити міст")) return;
  Serial.println("Сторінка: перезапуск мосту");
  rebootAt = (millis() + 600) | 1;
  web.send(200, "application/json", "{\"ok\":true}");
}
static void hAny() {
  if (!setupOn) return web.send(404, "text/plain; charset=utf-8", "Немає такої сторінки");
  web.sendHeader("Location", "http://192.168.4.1/");
  web.send(302, "text/plain", "");
}

//  Кнопка BOOT: коротко (до 1 с) — тиша; 3 с — сторінка налаштування
static void bootBtn(uint32_t now) {
  static uint32_t down = 0;
  if (digitalRead(BTN) == LOW) {
    if (!down) down = now | 1;
    else if (now - down > 3000 && !setupOn) startSetup("кнопка BOOT");
  } else {
    if (down && now - down >= 50 && now - down < 1000) alertMute("кнопка BOOT");
    down = 0;
  }
}

static void info() {
  Serial.printf("\nМіст батареї %s (збірка %s), MAC %s\n", FW_VERSION, FW_BUILD, macS.c_str());
  if (!ssid.length()) Serial.println("Мережу Сторожа не задано: сторінка плати або команда wifi <назва> <пароль>");
  else if (WiFi.isConnected())
    Serial.printf("Мережа «%s»: під'єднано, адреса %s, Сторож %s, сигнал %d дБм\n", ssid.c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  else Serial.printf("Мережа «%s»: немає зв'язку%s%s\n", ssid.c_str(), lastErr.length() ? " — " : "", lastErr.c_str());
  static const char* const B[] = {"шукаю BMS", "під'єднуюсь", "на зв'язку", "на зв'язку, стану немає", "пауза перед новою спробою",
                                  "чекаю черги", "BMS не чути", "BMS не відповіла"};
  if (pollMode) Serial.printf("Режим: кілька BMS по черзі, раз на %u хв; з'єднань від запуску %lu; %s\n", (unsigned)(pollSec / 60), (unsigned long)reconnects, getNote().c_str());
  else
    Serial.printf("BMS: %s; %s %s (%d дБм); з'єднань %lu, команд %lu, хибних кадрів %lu; %s\n", B[bst % 8],
                  getPin().length() ? "вибрана" : "найближча", getCur().c_str(), (int)curRssi, (unsigned long)reconnects, (unsigned long)cmds,
                  (unsigned long)(as.bad + badFmt), getNote().c_str());
  for (int i = 0; i < packN(); i++) {
    jk::Cells c;
    char mac[18], lbl[24] = "";
    portENTER_CRITICAL(&mux); c = R[i].c; memcpy(mac, R[i].mac, sizeof mac); if (pollMode && i < nPkc) { memcpy(lbl, pkc[i].lbl, sizeof lbl); if (!mac[0]) memcpy(mac, pkc[i].mac, sizeof mac); } portEXIT_CRITICAL(&mux);
    uint32_t at = R[i].atMs;
    Serial.printf("Батарея %d%s%s%s: %s %s", i + 1, lbl[0] ? " «" : "", lbl, lbl[0] ? "»" : "", mac[0] ? mac : "—", B[(pollMode ? R[i].st : bst) % 8]);
    if (pollMode) Serial.printf(", опитувань %lu, невдалих %lu", (unsigned long)R[i].polls, (unsigned long)R[i].fails);
    if (at && c.fmt)
      Serial.printf("; %u %%, %.2f В, %.2f А, комірок %u (%u…%u мВ), тривоги 0x%lx, кадр %lu с тому, розкладка %uS", c.soc, c.totalMv / 1000.0,
                    c.ma / 1000.0, c.n, c.vmin, c.vmax, (unsigned long)c.err, (unsigned long)(agoMs(R[i].atMs) / 1000), c.fmt);
    Serial.println();
  }
  if (setupOn) Serial.printf("Сторінка налаштування: мережа «%s» без пароля, http://192.168.4.1\n", apName.c_str());
  Serial.printf("Дисплей: %s; виводи CLK %d, DI %d, CS %d, RS %d, RST %d, LED %d; контраст %u, підсвітка %u %%\n", L.on ? "увімкнено" : "вимкнено",
                L.clk, L.din, L.cs, L.rs, L.rst, L.bl, L.con, L.bri);
  alertInfo();
  Serial.printf("Пам'ять: вільно %lu Б, найменше було %lu Б\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
}

static bool two(const String& s, String& a, String& b) {
  String t[2];
  int k = 0, i = 0, L = s.length();
  while (k < 2 && i < L) {
    while (i < L && s[i] == ' ') i++;
    if (i >= L) break;
    if (s[i] == '"') { int e = s.indexOf('"', i + 1); if (e < 0) return false; t[k++] = s.substring(i + 1, e); i = e + 1; }
    else { int e = s.indexOf(' ', i); if (e < 0) e = L; t[k++] = s.substring(i, e); i = e; }
  }
  while (i < L && s[i] == ' ') i++;
  if (k != 2 || i < L) return false;
  a = t[0];
  b = t[1];
  return true;
}

static void serialCmd() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') { if (cmdLine.length() < 200) cmdLine += ch; continue; }
    String l = cmdLine;
    cmdLine = "";
    l.trim();
    if (l.startsWith("wifi ")) {
      String a, b;
      if (two(l.substring(5), a, b) && a.length() && a.length() <= 32 && b.length() >= 8 && b.length() <= 63) {
        ssid = a;
        pass = b;
        saveWifi();
        Serial.printf("Збережено: точка «%s». Під'єднуюсь…\n", ssid.c_str());
        connectWifi();
      } else Serial.println("Формат: wifi <назва точки> <пароль від 8 символів>; у лапках, якщо є пробіли");
    } else if (l.startsWith("bms ")) {
      String m = l.substring(4);
      m.trim();
      if (m == "auto") { pinSet("", "Команда"); waitUntilMs = 0; }
      else if (macOk(m)) { pinSet(m, "Команда"); waitUntilMs = 0; }
      else Serial.println("Формат: bms aa:bb:cc:dd:ee:ff або bms auto");
    } else if (l == "info") info();
    else if (l == "scan") {
      Found fl[8];
      int nf;
      uint32_t at;
      portENTER_CRITICAL(&mux); memcpy(fl, found, sizeof fl); nf = nFound; at = foundAtMs; portEXIT_CRITICAL(&mux);
      if (!at) Serial.println("Ще не шукав (плата на зв'язку з BMS — пошук лише між з'єднаннями)");
      else {
        Serial.printf("JK BMS поруч (пошук %lu с тому):\n", (unsigned long)((millis() - at) / 1000));
        for (int i = 0; i < nf; i++) Serial.printf("  %s  %-20s %d дБм%s\n", fl[i].mac, fl[i].name, fl[i].rssi, fl[i].jk ? "" : "  (не схоже на JK — лише вручну)");
        if (!nf) Serial.println("  нікого");
      }
    } else if (l == "dump") { dumpOn = !dumpOn; dumpReady = false; Serial.println(dumpOn ? "Кадри BMS — показую" : "Кадри BMS — не показую"); }
    else if (l == "poll") { if (pollMode) { pollNowReq = true; Serial.println("Опитаю всі BMS за кілька секунд"); } else Serial.println("Лише в режимі «кілька BMS по черзі»"); }
    else if (l == "mute") alertMute("команда");
    else if (l == "reboot") ESP.restart();
    else if (l.length()) Serial.println("Команди: wifi <назва> <пароль>, bms <MAC>|auto, info, scan, dump, poll, mute, reboot");
  }
}

void setup() {
  Serial.begin(115200);
  bootMs = millis();
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  leds = !(ci.features & CHIP_FEATURE_EMB_FLASH);   // на C3 з вбудованою флеш виводи 12/13 зайняті нею
  if (leds) { pinMode(LED_BEAT, OUTPUT); pinMode(LED_NOLINK, OUTPUT); }
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  macS = b;
  char an[16];
  snprintf(an, sizeof an, "BMS-%02X%02X", m[4], m[5]);
  apName = an;
  pinMode(BTN, INPUT_PULLUP);
  loadPrefs();
  for (int i = 0; i < MAX_PK; i++) R[i].st = pollMode ? ST_WAIT : ST_SCAN;
  lcdLoad();
  alertLoad();
  lcdStart();                                      // дисплей — одразу: заставка, поки шукаємо BMS
  alertStart();
  WiFi.persistent(false);
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t i) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { discReason = i.wifi_sta_disconnected.reason; gotIp = false; }
    else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) { discReason = 0; gotIp = true; }
  });
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  static const char* hdrs[] = {"Accept-Encoding"};
  web.collectHeaders(hdrs, 1);
  web.on("/", HTTP_GET, hRoot);
  web.on("/state", HTTP_GET, hState);
  web.on("/scan", HTTP_GET, hScan);
  web.on("/save", HTTP_POST, hSave);
  web.on("/bms", HTTP_POST, hBms);
  web.on("/packs", HTTP_POST, hPacks);
  web.on("/pollnow", HTTP_POST, hPollNow);
  web.on("/blescan", HTTP_POST, hBleScan);
  web.on("/lcd", HTTP_GET, hLcdGet);
  web.on("/lcd", HTTP_POST, hLcdSave);
  web.on("/lcdtest", HTTP_POST, hLcdTest);
  web.on("/alert", HTTP_GET, hAlertGet);
  web.on("/alert", HTTP_POST, hAlertSave);
  web.on("/alerttest", HTTP_POST, hAlertTest);
  web.on("/ota", HTTP_POST, hOta);
  web.on("/reboot", HTTP_POST, hReboot);
  web.onNotFound(hAny);
  web.begin();
  if (ssid.length()) connectWifi();
  else startSetup("мережу Сторожа ще не задано");
  udp.begin(BRIDGE_PORT + 2);
  xTaskCreate(bleTask, "ble", 8192, nullptr, 1, nullptr);
  delay(300);
  info();
}

//  Чи все гаразд з BMS — для світлодіода плати й рішення про перезапуск: «одна» — на зв'язку; «по черзі» — у всіх свіжі дані
static bool bleOk() {
  if (!pollMode) return bst == ST_OK;
  for (int i = 0; i < nPkc; i++) if (R[i].st != ST_OK) return false;
  return nPkc > 0;
}

void loop() {
  serialCmd();
  web.handleClient();
  if (setupOn) dns.processNextRequest();
  uint32_t now = millis();
  bootBtn(now);
  alertTick(now);
  uint16_t dr = discReason;
  if (dr) {
    discReason = 0;
    String e = reasonText(dr);
    if (e.length() && e != lastErr) { lastErr = e; Serial.printf("Мережа «%s»: %s\n", ssid.c_str(), e.c_str()); }
  } else if (gotIp && lastErr.length()) lastErr = "";
  if (dumpReady) {
    Serial.printf("Кадр %02X: ", dumpBuf[4]);
    for (int i = 0; i < jk::FRAME; i++) Serial.printf("%02x", dumpBuf[i]);
    Serial.println();
    dumpReady = false;
  }
  lcdTick(now);
  if (otaUrl.length()) otaRun();
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) { alertQuiet(); delay(50); ESP.restart(); }
  //  Нова прошивка (після оновлення по мережі) — справна, якщо хвилину працює й задача Bluetooth жива
  static bool valid = false;
  if (!valid && now - bootMs > 60000 && agoMs(bleBeat) < 30000) { valid = true; esp_ota_mark_app_valid_cancel_rollback(); }
  bool conn = WiFi.isConnected();
  if (conn) { if (!linkSince) linkSince = now | 1; } else linkSince = 0;
  //  Годинник — для нічної підсвітки дисплея й нічного режиму звуку: з інтернету через Сторожа; немає інтернету — ночі немає
  static bool ntp = false;
  if (conn && !ntp) { ntp = true; configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com"); }
  if (setupOn && linkSince && ((now - linkSince > 60000 && WiFi.softAPgetStationNum() == 0 && now - setupSince > 180000) || now - linkSince > 300000)) stopSetup();
  bool bok = bleOk();
  if (conn) {
    lastLink = now;
    if (now - lastBeat >= 5000) { lastBeat = now; beat(now); led(LED_BEAT, HIGH); }
    if (now - lastBeat > 80) led(LED_BEAT, LOW);
    led(LED_NOLINK, bok ? LOW : (now / 1000) % 4 == 0);     // без BMS — рідкий спалах
  } else {
    led(LED_BEAT, setupOn ? (now / 500) % 2 == 0 : LOW);
    led(LED_NOLINK, (now / 500) % 2);
    bool phone = setupOn && WiFi.softAPgetStationNum() > 0;
    if (ssid.length() && !phone && now - lastTry > (setupOn ? 60000UL : 30000UL)) connectWifi();
    if (!setupOn && now - lastLink > 180000) startSetup("3 хв без зв'язку зі Сторожем");
    //  Перезапуск лише без мережі Сторожа: 15 хв — якщо й BMS немає, 60 хв — у будь-якому разі (кожне перепід'єднання
    //  до BMS — писк, тож поки BMS на зв'язку, не рвати її через Wi-Fi, який, найпевніше, лагодиться сам)
    if (!phone && ((now - lastLink > 15UL * 60 * 1000 && !bok) || now - lastLink > 60UL * 60 * 1000)) ESP.restart();
  }
  //  Задача Bluetooth завмерла (жодного кроку 3 хв: пошук — 5 с, під'єднання — до 30 с) — почати з чистого аркуша
  //  1.1.1: мітка — до millis(), різниця зі знаком, і перезапуск лише коли так двічі поспіль (як у годиннику)
  static uint8_t stuck = 0;
  if (now - bootMs > 60000 && agoMs(bleBeat) > 180000) {
    if (++stuck >= 2) { Serial.println("Задача Bluetooth завмерла — перезапуск"); delay(100); ESP.restart(); }
  } else stuck = 0;
  alertTick(millis());
  delay(10);
}
