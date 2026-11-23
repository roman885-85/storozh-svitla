/*  Міст батареї «Сторожа світла» — ESP32-C3 біля JK BMS (плата AirM2M / LuatOS «ESP32-C3 Core», як маячок).
 *
 *  Власник 22.11.2026: «добавляй функцию мониторинга бмс через esp 32 c3», «с3 держит постоянную связь с бмс, а сторож
 *  лишь читает эти данные из с3», «в ha эта работа ранее была организована»; прошивку обрав власну, як у маячка.
 *
 *  Плата постійно під'єднана до JK BMS по Bluetooth і кожні 5 с каже Сторожу, що з батареєю: UDP «BMS1 …» на
 *  10.77.0.1:47801 (той самий порт, що й маячки). Сторож до BMS не під'єднується й нічого їй не пише.
 *
 *  Як у мосту HA (esphome-jk-bms, jk_bms_ble): після під'єднання — запит «хто ти» (0x97), далі запит стану (0x96) кожні
 *  5 с, доки не прийде перший кадр стану; потім BMS шле кадри сама, а плата лише слухає. Інших команд плата не шле.
 *  BMS пищить на кожну команду (журнал HA 08.11: «кожне перепідключення — писк», сотня за добу на слабкому сигналі),
 *  тож після короткого з'єднання пауза перед новим росте: 10 с, 30 с, 1, 2, 5 хв; після довгого — 5 с.
 *  Живити плату — від ДБЖ чи інвертора, не з міської розетки: батарея важлива саме тоді, коли світла немає.
 *
 *  Налаштування з телефона — як у маячка: без мережі Сторожа (ще не задано, 3 хв без зв'язку або кнопка BOOT 3 с)
 *  плата піднімає відкриту мережу «BMS-XXXX» зі сторінкою http://192.168.4.1: мережа Сторожа і яку BMS слухати
 *  (типово — найближчу JK, з найсильнішим сигналом).
 *
 *  По USB (115200, рядок з Enter):
 *    wifi <назва> <пароль>   — мережа Сторожа (у лапках, якщо є пробіли)
 *    bms <MAC> | bms auto    — яку BMS слухати (auto — найближчу)
 *    info                    — стан;  scan — які BMS чути;  dump — кадри BMS шістнадцятково (ще раз — вимкнути)
 *    reboot                  — перезапуск
 *  У флеш пишуть лише команди wifi, bms і кнопки на сторінці (правило власника).
 *
 *  Світлодіоди: D4 (GPIO12) коротко блимає на кожне повідомлення Сторожу; D5 (GPIO13) часто блимає, поки немає мережі
 *  Сторожа, рідко — поки немає BMS.
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
static jk::Cells cells;                  // останній кадр стану
static jk::Info binfo;                   // «хто ти»
static bool hasInfo = false;
static volatile uint32_t cellsAtMs = 0;  // коли прийшов останній кадр стану (0 — ще не було)
static volatile uint8_t hint = 0;        // розкладка за прошивкою BMS
static volatile bool gotStatus = false;  // у цьому з'єднанні кадр стану вже був
static volatile uint8_t bst = 0;         // 0 шукаю BMS, 1 під'єднуюсь, 2 на зв'язку, 3 на зв'язку без кадрів, 4 пауза
static char curMac[18] = "", curName[24] = "";
static volatile int curRssi = 0;
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
static char pinBuf[18] = "";            // яку BMS слухати: MAC малими; "" — найближчу JK
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

//  Повідомлення від BMS (задача Bluetooth ядра): зібрати кадр і розібрати.
static void onNotify(BLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (!as.feed(d, (int)len)) return;
  const uint8_t* f = as.buf;
  if (dumpOn && !dumpReady) { memcpy(dumpBuf, f, jk::FRAME); dumpReady = true; }
  if (f[4] == 0x03) {
    jk::Info in;
    if (jk::parseInfo(f, in)) { portENTER_CRITICAL(&mux); binfo = in; hasInfo = true; portEXIT_CRITICAL(&mux); hint = jk::fmtHint(in); }
  } else if (f[4] == 0x02) {
    jk::Cells c;
    if (jk::parseCells(f, hint, c)) {
      portENTER_CRITICAL(&mux);
      cells = c;
      portEXIT_CRITICAL(&mux);
      cellsAtMs = millis() | 1;
      if (!gotStatus) { portENTER_CRITICAL(&mux); memcpy(goodMac, curMac, sizeof goodMac); portEXIT_CRITICAL(&mux); }   // це справді JK — запам'ятати
      gotStatus = true;
    } else badFmt++;
  }
}

static void loadPrefs() {
  Preferences p;
  if (p.begin("bms", true)) { ssid = p.getString("ssid", ""); pass = p.getString("pass", ""); strlcpy(pinBuf, p.getString("mac", "").c_str(), sizeof pinBuf); p.end(); }
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
static bool macOk(const String& m) {
  if (m.length() != 17) return false;
  for (int i = 0; i < 17; i++) { char c = m[i]; if (i % 3 == 2 ? c != ':' : !isxdigit((unsigned char)c)) return false; }
  return true;
}

//  Чи схоже на JK BMS — jk::looksLikeJk (jk_core.h). BMS, з якою вже був зв'язок у цьому запуску, пам'ятаємо (у RAM):
//  її беремо знову за адресою, навіть якщо оголошення прийшло неповне.
static bool jkLike(const String& name, const String& mac, bool svc) {
  if (goodMac[0] && mac.equalsIgnoreCase(goodMac)) return true;
  return jk::looksLikeJk(name.c_str(), mac.c_str(), svc);
}

//  Пошук: сама вибирається лише схожа на JK (jkLike); вибрана власником — за MAC, будь-яка. Пошук активний — щоб назва
//  приходила завжди (у пасивному JK буває без назви).
static bool scanJk(BLEAdvertisedDevice& out) {
  BLEScan* sc = BLEDevice::getScan();
  sc->setActiveScan(true);
  sc->setInterval(100);
  sc->setWindow(99);
  BLEScanResults* r = sc->start(5, false);
  Found tmp[8];
  int n = 0, bestR = -999;
  bool have = false;
  String pin = getPin();
  for (int i = 0; r && i < r->getCount(); i++) {
    BLEAdvertisedDevice d = r->getDevice(i);
    String name = d.getName();
    String mac = d.getAddress().toString();
    bool pinned = pin.length() && mac.equalsIgnoreCase(pin);
    bool svc = d.haveServiceUUID() && d.isAdvertisingService(BLEUUID((uint16_t)0xFFE0));
    bool jk = jkLike(name, mac, svc);
    if (!jk && !svc && !pinned) continue;
    if (n < 8) {                                 // у списку — і схожі на JK, і інші з FFE0 (вибрати вручну)
      strlcpy(tmp[n].mac, mac.c_str(), sizeof tmp[n].mac);
      strlcpy(tmp[n].name, name.length() ? name.c_str() : "без назви", sizeof tmp[n].name);
      tmp[n].rssi = d.getRSSI();
      tmp[n].jk = jk;
      n++;
    }
    if (pinned) { out = d; have = true; }
    else if (!pin.length() && jk && d.getRSSI() > bestR) { bestR = d.getRSSI(); out = d; have = true; }
  }
  sc->clearResults();
  portENTER_CRITICAL(&mux);
  memcpy(found, tmp, sizeof tmp);
  nFound = n;
  foundAtMs = millis() | 1;
  portEXIT_CRITICAL(&mux);
  return have;
}

static bool setupChars(BLEClient* cl) {
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

//  Задача Bluetooth: пошук → під'єднання → слухати, доки з'єднання живе → пауза → знову.
static void bleTask(void*) {
  BLEDevice::init("");
  BLEDevice::setPower(ESP_PWR_LVL_P20);           // +20 дБм — як у панелі HA: на межі сигналу BMS рве з'єднання й пищить
  BLEDevice::setMTU(247);                         // більші шматки кадру — менше повідомлень
  BLEClient* cl = BLEDevice::createClient();
  uint32_t backoff = 0;
  for (;;) {
    bleBeat = millis();
    if (waitUntilMs && (int32_t)(millis() - waitUntilMs) < 0) { bst = 4; delay(500); continue; }
    waitUntilMs = 0;
    bst = 0;
    BLEAdvertisedDevice t;
    if (!scanJk(t)) {
      String pin = getPin();
      setNote(pin.length() ? "вибраної BMS " + pin + " не чути (зайнята телефоном? далеко?)" : String("JK BMS поруч не чути"));
      delay(3000);
      continue;
    }
    bleBeat = millis();
    bst = 1;
    portENTER_CRITICAL(&mux);
    strlcpy(curMac, t.getAddress().toString().c_str(), sizeof curMac);
    strlcpy(curName, t.getName().length() ? t.getName().c_str() : "JK BMS", sizeof curName);
    portEXIT_CRITICAL(&mux);
    curRssi = t.getRSSI();
    as.n = 0;
    gotStatus = false;
    hint = 0;
    uint32_t began = 0;
    bool ok = cl->connect(t.getAddress());
    bleBeat = millis();
    if (!ok) setNote("не вдалося під'єднатися до " + String(curName));
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
        uint32_t now = millis();
        if (!gotStatus) {
          if (now - lastReq >= 5000) {
            if (reqs >= 6) { setNote("BMS не шле стан на запити — перепід'єднуюсь"); break; }
            sendCmd(jk::CMD_CELLS);
            lastReq = now;
            reqs++;
          }
          bst = 3;
        } else {
          uint32_t age = agoMs(cellsAtMs);       // 1.1.1: не «now − мітка»: кадр міг прийти вже після now
          bst = age < 15000 ? 2 : 3;
          if (age > 60000) { setNote("BMS замовкла на хвилину — перепід'єднуюсь"); break; }
        }
        int r = cl->getRssi();
        if (r) curRssi = r;
        delay(500);
      }
      if (cl->isConnected()) cl->disconnect();
      else if (gotStatus) setNote("BMS розірвала з'єднання (" + String((millis() - began) / 1000) + " с на зв'язку)");
    } else if (cl->isConnected()) cl->disconnect();
    wch = nullptr;
    uint32_t lasted = began ? millis() - began : 0;
    //  кожне під'єднання — писк BMS: після короткого з'єднання чекати довше
    //  1.1.0: після короткого з'єднання — 30 с, 1, 2, 5 хв (було від 10 с). 22.11.2026 о 19:13 міст перезапустився, за
    //  8 хв під'єднався 5 разів — і BMS перестала оголошувати себе (як удома 07.11: «її тримає інше підключення»).
    if (lasted > 10UL * 60 * 1000) backoff = 10000;
    else backoff = backoff < 30000 ? 30000 : backoff < 60000 ? 60000 : backoff < 120000 ? 120000 : 300000;
    connSinceMs = 0;
    waitUntilMs = (millis() + backoff) | 1;
  }
}

//  Дисплей Open-Smart 1,8″ (ST7567) — налаштування, показ, сторінка (1.1.0)
#include "display.h"

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
static void beat(uint32_t now) {
  static char b[640];
  jk::Cells c;
  jk::Info in;
  bool hi;
  char mac[18], name[24];
  portENTER_CRITICAL(&mux);
  c = cells; in = binfo; hi = hasInfo;
  memcpy(mac, curMac, sizeof mac); memcpy(name, curName, sizeof name);
  portEXIT_CRITICAL(&mux);
  b[0] = 0;
  addKV(b, sizeof b, "BMS1 %s up=%lu rssi=%d ver=%s st=%u rc=%lu bad=%lu heap=%lu lcd=%d", macS.c_str(), (unsigned long)(now / 1000), WiFi.RSSI(), FW_VERSION,
        (unsigned)bst, (unsigned long)reconnects, (unsigned long)(as.bad + badFmt), (unsigned long)ESP.getFreeHeap(), L.on ? 1 : 0);
  addKV(b, sizeof b, " rst=%d", (int)esp_reset_reason());
  if (mac[0]) {
    char nm[24];
    safeName(nm, name, sizeof nm);
    addKV(b, sizeof b, " bmac=%s name=%s brssi=%d", mac, nm, (int)curRssi);
  }
  uint32_t at = cellsAtMs;
  if (at && c.fmt) {
    addKV(b, sizeof b, " age=%lu fmt=%u soc=%u v=%lu i=%ld rem=%lu full=%lu cyc=%lu soh=%u n=%u lo=%u lox=%u hi=%u hix=%u",
          (unsigned long)(agoMs(cellsAtMs) / 1000), c.fmt, c.soc, (unsigned long)c.totalMv, (long)c.ma, (unsigned long)c.remMah,
          (unsigned long)c.fullMah, (unsigned long)c.cycles, c.soh, c.n, c.vmin, c.imin, c.vmax, c.imax);
    if (c.hasT1) addKV(b, sizeof b, " t1=%d", c.t1);
    if (c.hasT2) addKV(b, sizeof b, " t2=%d", c.t2);
    if (c.hasTmos) addKV(b, sizeof b, " tm=%d", c.tmos);
    addKV(b, sizeof b, " err=%lx chg=%d dis=%d bal=%d heat=%d c=", (unsigned long)c.err, c.chg, c.dis, c.bal, c.heat);
    bool first = true;
    for (int i = 0; i < 32; i++) if (c.mv[i]) { addKV(b, sizeof b, first ? "%u" : ",%u", c.mv[i]); first = false; }
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

// ---------- сторінка ----------
//  Сторінка — у page.h: препроцесор Arduino шукає функції й у JavaScript усередині .ino і ламає збірку.
#include "page.h"

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

static void hRoot() { web.send(200, "text/html; charset=utf-8", PAGE); }
static void hState() {
  bool cn = WiFi.isConnected();
  jk::Cells c;
  jk::Info in;
  bool hi;
  char mac[18], name[24];
  Found fl[8];
  int nf;
  portENTER_CRITICAL(&mux);
  c = cells; in = binfo; hi = hasInfo; nf = nFound;
  memcpy(mac, curMac, sizeof mac); memcpy(name, curName, sizeof name); memcpy(fl, found, sizeof fl);
  portEXIT_CRITICAL(&mux);
  String o = String("{\"ver\":\"") + FW_VERSION + "\",\"mac\":\"" + macS + "\",\"ssid\":\"" + jsonEsc(ssid) + "\",\"conn\":" + (cn ? "true" : "false") +
             ",\"rssi\":" + String(cn ? WiFi.RSSI() : 0) + ",\"err\":\"" + jsonEsc(lastErr) + "\",\"ap\":\"" + jsonEsc(apName) +
             "\",\"setup\":" + (setupOn ? "true" : "false") + ",\"pin\":\"" + getPin() + "\"" +
             ",\"trust\":" + (fromStorozh() ? "true" : "false") + ",\"ota\":\"" + jsonEsc(otaNote) + "\",\"var\":\"" + (ARDUINO_USB_CDC_ON_BOOT ? "usb" : "uart") +
             "\",\"heap\":" + String((unsigned long)ESP.getFreeHeap()) + ",\"rst\":" + String((int)esp_reset_reason()) + ",\"up\":" + String((unsigned long)(millis() / 1000));
  o += ",\"b\":{\"st\":" + String(bst) + ",\"mac\":\"" + String(mac) + "\",\"name\":\"" + jsonEsc(name) + "\",\"rssi\":" + String((int)curRssi) +
       ",\"rc\":" + String((unsigned long)reconnects) + ",\"cmds\":" + String((unsigned long)cmds) + ",\"note\":\"" + jsonEsc(getNote()) + "\"}";
  o += ",\"found\":[";
  for (int i = 0; i < nf; i++) o += String(i ? "," : "") + "{\"mac\":\"" + fl[i].mac + "\",\"name\":\"" + jsonEsc(fl[i].name) + "\",\"rssi\":" + fl[i].rssi + ",\"jk\":" + (fl[i].jk ? "true" : "false") + "}";
  o += "]";
  uint32_t at = cellsAtMs;
  if (at && c.fmt) {
    o += ",\"c\":{\"age\":" + String((millis() - at) / 1000) + ",\"fmt\":" + c.fmt + ",\"soc\":" + c.soc + ",\"v\":" + String((unsigned long)c.totalMv) +
         ",\"i\":" + String((long)c.ma) + ",\"rem\":" + String((unsigned long)c.remMah) + ",\"full\":" + String((unsigned long)c.fullMah) +
         ",\"cyc\":" + String((unsigned long)c.cycles) + ",\"soh\":" + c.soh + ",\"n\":" + c.n + ",\"lo\":" + c.vmin + ",\"lox\":" + c.imin +
         ",\"hi\":" + c.vmax + ",\"hix\":" + c.imax + ",\"chg\":" + (c.chg ? "true" : "false") + ",\"dis\":" + (c.dis ? "true" : "false") +
         ",\"bal\":" + (c.bal ? "true" : "false") + ",\"heat\":" + (c.heat ? "true" : "false");
    if (c.hasT1) o += ",\"t1\":" + String(c.t1);
    if (c.hasT2) o += ",\"t2\":" + String(c.t2);
    if (c.hasTmos) o += ",\"tm\":" + String(c.tmos);
    o += ",\"info\":\"" + (hi ? jsonEsc(String(in.model) + ", прошивка " + in.sw) : String()) + "\",\"cells\":[";
    bool first = true;
    for (int i = 0; i < 32; i++) if (c.mv[i]) { o += String(first ? "" : ",") + c.mv[i]; first = false; }
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
  o += "}";
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
//  на сторінці налаштування (кнопка BOOT) — вибрати BMS, оновити прошивку. Інші пристрої мережі Сторожа — ні.
static bool fromStorozh() { return WiFi.isConnected() && web.client().remoteIP() == WiFi.gatewayIP(); }

//  Оновлення прошивки по мережі (1.1.0): міст сам завантажує файл …-app.bin за адресою (http чи https) через Сторожа.
//  Нова прошивка — «на перевірці»: за хвилину роботи визнаємо її справною; упала раніше — завантажувач поверне попередню.
//  https — без перевірки сертифіката (на платі немає сховища кореневих сертифікатів): адресу задає власник.
extern "C" bool verifyRollbackLater() { return true; }
static void hOta() {
  if (!setupOn && !fromStorozh()) return web.send(403, "application/json", "{\"err\":\"Оновити можна зі сторінки Сторожа або зі сторінки налаштування (кнопка BOOT 3 с)\"}");
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
  portENTER_CRITICAL(&mux);
  strlcpy(pinBuf, v.c_str(), sizeof pinBuf);
  portEXIT_CRITICAL(&mux);
  savePin();
  Serial.printf("%s: %s\n", by, v.length() ? ("слухаю BMS " + v).c_str() : "слухаю найближчу JK BMS");
}
static void hBms() {
  if (!setupOn && !fromStorozh()) return web.send(403, "application/json", "{\"err\":\"Змінити BMS можна зі сторінки Сторожа або зі сторінки налаштування (кнопка BOOT 3 с)\"}");
  String m = web.arg("mac");
  m.trim();
  if (m.length() && !macOk(m)) return web.send(400, "application/json", "{\"err\":\"Хибна адреса BMS\"}");
  pinSet(m, "Сторінка");
  waitUntilMs = 0;
  web.send(200, "application/json", "{\"ok\":true}");
}
static void hAny() {
  if (!setupOn) return web.send(404, "text/plain; charset=utf-8", "Немає такої сторінки");
  web.sendHeader("Location", "http://192.168.4.1/");
  web.send(302, "text/plain", "");
}

static void bootBtn(uint32_t now) {
  static uint32_t down = 0;
  if (digitalRead(BTN) == LOW) {
    if (!down) down = now | 1;
    else if (now - down > 3000 && !setupOn) startSetup("кнопка BOOT");
  } else down = 0;
}

static void info() {
  Serial.printf("\nМіст батареї %s (збірка %s), MAC %s\n", FW_VERSION, FW_BUILD, macS.c_str());
  if (!ssid.length()) Serial.println("Мережу Сторожа не задано: сторінка плати або команда wifi <назва> <пароль>");
  else if (WiFi.isConnected())
    Serial.printf("Мережа «%s»: під'єднано, адреса %s, Сторож %s, сигнал %d дБм\n", ssid.c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  else Serial.printf("Мережа «%s»: немає зв'язку%s%s\n", ssid.c_str(), lastErr.length() ? " — " : "", lastErr.c_str());
  static const char* const B[] = {"шукаю BMS", "під'єднуюсь", "на зв'язку", "на зв'язку, стану немає", "пауза перед новою спробою"};
  Serial.printf("BMS: %s; %s %s (%d дБм); з'єднань %lu, команд %lu, хибних кадрів %lu; %s\n", B[bst % 5],
                getPin().length() ? "вибрана" : "найближча", getCur().c_str(), (int)curRssi, (unsigned long)reconnects, (unsigned long)cmds,
                (unsigned long)(as.bad + badFmt), getNote().c_str());
  uint32_t at = cellsAtMs;
  if (at) {
    jk::Cells c;
    portENTER_CRITICAL(&mux); c = cells; portEXIT_CRITICAL(&mux);
    Serial.printf("Батарея: %u %%, %.2f В, %.2f А, комірок %u (%u…%u мВ), тривоги 0x%lx, кадр %lu с тому, розкладка %uS\n", c.soc,
                  c.totalMv / 1000.0, c.ma / 1000.0, c.n, c.vmin, c.vmax, (unsigned long)c.err, (unsigned long)((millis() - at) / 1000), c.fmt);
  }
  if (setupOn) Serial.printf("Сторінка налаштування: мережа «%s» без пароля, http://192.168.4.1\n", apName.c_str());
  Serial.printf("Дисплей: %s; виводи CLK %d, DI %d, CS %d, RS %d, RST %d, LED %d; контраст %u, підсвітка %u %%\n", L.on ? "увімкнено" : "вимкнено",
                L.clk, L.din, L.cs, L.rs, L.rst, L.bl, L.con, L.bri);
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
    else if (l == "reboot") ESP.restart();
    else if (l.length()) Serial.println("Команди: wifi <назва> <пароль>, bms <MAC>|auto, info, scan, dump, reboot");
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
  lcdLoad();
  lcdStart();                                      // дисплей — одразу: заставка, поки шукаємо BMS
  WiFi.persistent(false);
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t i) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { discReason = i.wifi_sta_disconnected.reason; gotIp = false; }
    else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) { discReason = 0; gotIp = true; }
  });
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  web.on("/", HTTP_GET, hRoot);
  web.on("/state", HTTP_GET, hState);
  web.on("/scan", HTTP_GET, hScan);
  web.on("/save", HTTP_POST, hSave);
  web.on("/bms", HTTP_POST, hBms);
  web.on("/lcd", HTTP_GET, hLcdGet);
  web.on("/lcd", HTTP_POST, hLcdSave);
  web.on("/lcdtest", HTTP_POST, hLcdTest);
  web.on("/ota", HTTP_POST, hOta);
  web.onNotFound(hAny);
  web.begin();
  if (ssid.length()) connectWifi();
  else startSetup("мережу Сторожа ще не задано");
  udp.begin(BRIDGE_PORT + 2);
  xTaskCreate(bleTask, "ble", 8192, nullptr, 1, nullptr);
  delay(300);
  info();
}

void loop() {
  serialCmd();
  web.handleClient();
  if (setupOn) dns.processNextRequest();
  uint32_t now = millis();
  bootBtn(now);
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
  //  Нова прошивка (після оновлення по мережі) — справна, якщо хвилину працює й задача Bluetooth жива
  static bool valid = false;
  if (!valid && now - bootMs > 60000 && agoMs(bleBeat) < 30000) { valid = true; esp_ota_mark_app_valid_cancel_rollback(); }
  bool conn = WiFi.isConnected();
  if (conn) { if (!linkSince) linkSince = now | 1; } else linkSince = 0;
  //  Годинник — для нічної підсвітки дисплея: з інтернету через Сторожа; немає інтернету — ночі немає
  static bool ntp = false;
  if (conn && !ntp) { ntp = true; configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com"); }
  if (setupOn && linkSince && ((now - linkSince > 60000 && WiFi.softAPgetStationNum() == 0 && now - setupSince > 180000) || now - linkSince > 300000)) stopSetup();
  if (conn) {
    lastLink = now;
    if (now - lastBeat >= 5000) { lastBeat = now; beat(now); led(LED_BEAT, HIGH); }
    if (now - lastBeat > 80) led(LED_BEAT, LOW);
    led(LED_NOLINK, bst == 2 ? LOW : (now / 1000) % 4 == 0);     // без BMS — рідкий спалах
  } else {
    led(LED_BEAT, setupOn ? (now / 500) % 2 == 0 : LOW);
    led(LED_NOLINK, (now / 500) % 2);
    bool phone = setupOn && WiFi.softAPgetStationNum() > 0;
    if (ssid.length() && !phone && now - lastTry > (setupOn ? 60000UL : 30000UL)) connectWifi();
    if (!setupOn && now - lastLink > 180000) startSetup("3 хв без зв'язку зі Сторожем");
    //  Перезапуск лише без мережі Сторожа: 15 хв — якщо й BMS немає, 60 хв — у будь-якому разі (кожне перепід'єднання
    //  до BMS — писк, тож поки BMS на зв'язку, не рвати її через Wi-Fi, який, найпевніше, лагодиться сам)
    if (!phone && ((now - lastLink > 15UL * 60 * 1000 && bst != 2) || now - lastLink > 60UL * 60 * 1000)) ESP.restart();
  }
  //  Задача Bluetooth завмерла (жодного кроку 3 хв: пошук — 5 с, під'єднання — до 30 с) — почати з чистого аркуша
  //  1.1.1: мітка — до millis(), різниця зі знаком, і перезапуск лише коли так двічі поспіль (як у годиннику)
  static uint8_t stuck = 0;
  if (now - bootMs > 60000 && agoMs(bleBeat) > 180000) {
    if (++stuck >= 2) { Serial.println("Задача Bluetooth завмерла — перезапуск"); delay(100); ESP.restart(); }
  } else stuck = 0;
  delay(10);
}
