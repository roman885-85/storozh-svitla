#include "bridge.h"
#include "settings.h"
#include "util.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "ping/ping_sock.h"
#include "esp_netif_net_stack.h"   // esp_netif_get_netif_impl: прапорець NAT у самому інтерфейсі lwIP
#include "lwip/netif.h"
#include "tg.h"

static const IPAddress AP_IP(10, 77, 0, 1), AP_MASK(255, 255, 255, 0), AP_LEASE(10, 77, 0, 10), DNS_FALLBACK(8, 8, 8, 8);

static SemaphoreHandle_t mtx;
static ApDev tbl[MAXC];
static volatile bool gStaUp = false, gStaGotIp = false;
static int curCh = 0;
static int64_t chChangedUs = 0;
//  Під час пошуку мереж радіо перебирає канали, і esp_wifi_get_channel показує той, що слухає зараз.
//  18.11.2026 «Знайти» на сторінці дав у журналі «канал змінився 6→3→10→13→6» за секунди, і кожен такий
//  «перехід» ще й подовжував затримку «міста немає» до 120 с. Тепер у пошуку й 3 с після нього канал не міряємо,
//  а новий приймаємо лише з двох замірів поспіль.
static int64_t quietUntilUs = 0;          // до цього моменту канал не міряємо (пошук роутера при під'єднанні)
static volatile uint32_t scanDoneMs = 0;  // коли закінчився пошук мереж зі сторінки (millis; 0 — ще не було)
static int candCh = 0;                    // новий канал, побачений один раз
static int64_t staNextTry = 0;
static int staTries = 0;
static bool ntpStarted = false;

// ---------- таблиця пристроїв точки (під mtx) ----------
static ApDev* findLocked(const uint8_t m[6]) {
  for (auto& c : tbl) if (c.used && !memcmp(c.mac, m, 6)) return &c;
  return nullptr;
}
static ApDev* getLocked(const uint8_t m[6]) {
  ApDev* c = findLocked(m);
  if (c) return c;
  ApDev* best = nullptr;
  for (auto& x : tbl) if (!x.used) { best = &x; break; }
  if (!best) for (auto& x : tbl) if (!x.assoc && (!best || x.leaveUs < best->leaveUs)) best = &x;
  if (!best) return nullptr;
  *best = ApDev();
  best->used = true;
  memcpy(best->mac, m, 6);
  return best;
}

int clientsSnapshot(ApDev* out, int maxN) {
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (auto& c : tbl) if (c.used && n < maxN) out[n++] = c;
  xSemaphoreGive(mtx);
  return n;
}
bool findClient(const uint8_t mac[6], ApDev& out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ApDev* c = findLocked(mac);
  if (c) out = *c;
  xSemaphoreGive(mtx);
  return c != nullptr;
}

//  MAC АВР для обробника подій (інша задача): копія з налаштувань, оновлюється раз на секунду в bridgeLoop
static uint8_t avrMacC[6];
static bool avrMacOk = false;
static volatile uint32_t probeN = 0, probeAvrN = 0;   // запитів пошуку мережі від запуску: усіх і від АВР (для сторінки)

// ---------- події Wi-Fi (потік arduino_events) ----------
static void onWifi(arduino_event_id_t ev, arduino_event_info_t info) {
  switch (ev) {
    //  1.0.31: живий пристрій, що втратив точку, шукає її — шле запити пошуку (probe request). Без живлення — тиша.
    //  Тож запит від АВР, коли той не на зв'язку, = АВР під напругою = місто є. Чужі пристрої (телефони) не заносимо.
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: {
      const uint8_t* m = info.wifi_ap_probereqrecved.mac;
      xSemaphoreTake(mtx, portMAX_DELAY);
      ApDev* c = findLocked(m);
      if (!c && avrMacOk && !memcmp(m, avrMacC, 6)) c = getLocked(m);
      if (c) { c->lastProbeUs = nowUs(); c->probeRssi = (int8_t)info.wifi_ap_probereqrecved.rssi; }
      if (avrMacOk && !memcmp(m, avrMacC, 6)) probeAvrN++;
      probeN++;
      xSemaphoreGive(mtx);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      xSemaphoreTake(mtx, portMAX_DELAY);
      ApDev* c = getLocked(m);
      int64_t away = -1;
      if (c) {
        if (!c->assoc && c->leaveUs) away = (nowUs() - c->leaveUs) / 1000000;
        c->assoc = true; c->assocUs = nowUs(); c->leftAlive = false;
      }
      xSemaphoreGive(mtx);
      logEvent("До точки під'єднався " + macToStr(m) + (away >= 0 ? " (не було " + fmtDur(away) + ")" : String()));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      xSemaphoreTake(mtx, portMAX_DELAY);
      ApDev* c = findLocked(m);
      bool alive = false;
      int64_t n = nowUs();
      if (c) {
        //  1.0.29: за мить до того відповідав на ping — живий, вийшов сам (перепідключає Wi-Fi). Без живлення пристрій
        //  мовчить, і точка викидає його лише за 2 хв неактивності (код 4) — тоді ping давно не відповідає.
        alive = c->lastPingOkUs && n - c->lastPingOkUs < 6000000LL;
        c->assoc = false; c->leaveUs = n; c->leftAlive = alive; c->leaveReason = info.wifi_ap_stadisconnected.reason;
      }
      xSemaphoreGive(mtx);
      logEvent("Від точки від'єднався " + macToStr(m) + " (код " + String(info.wifi_ap_stadisconnected.reason) +
               (alive ? "; за мить до того відповідав на ping — живий, вийшов сам)" : ")"));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
      const uint8_t* m = info.wifi_ap_staipassigned.mac;
      uint32_t ip = info.wifi_ap_staipassigned.ip.addr;
      xSemaphoreTake(mtx, portMAX_DELAY);
      ApDev* c = getLocked(m);
      if (c) c->ip = ip;
      xSemaphoreGive(mtx);
      logEvent("Видано адресу " + IPAddress(ip).toString() + " пристрою " + macToStr(m));
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gStaUp = true;
      gStaGotIp = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (gStaUp) logEvent("Зв'язок з роутером втрачено (код " + String(info.wifi_sta_disconnected.reason) + ")");
      gStaUp = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      if (gStaUp) logEvent("Роутер забрав адресу");
      gStaUp = false;
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      scanDoneMs = millis() | 1;
      break;
    default:
      break;
  }
}

// ---------- ping АВР ----------
static esp_ping_handle_t ph = nullptr;
static uint8_t pingMac[6];
static uint32_t pingIp = 0;

static void onPingOk(esp_ping_handle_t, void*) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ApDev* c = findLocked(pingMac);
  if (c) { c->lastPingOkUs = nowUs(); c->pingWorks = true; }
  xSemaphoreGive(mtx);
}
static void pingStop() {
  if (!ph) return;
  esp_ping_stop(ph);
  esp_ping_delete_session(ph);
  ph = nullptr;
  pingIp = 0;
}
static void pingStart(uint32_t ip) {
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr.type = IPADDR_TYPE_V4;
  cfg.target_addr.u_addr.ip4.addr = ip;
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = 1000;      // 1.0.29: щосекунди (було 2 с) — «міста немає» за ~12 с замість ~38 с
  cfg.timeout_ms = 900;
  cfg.task_stack_size = 3072;
  esp_ping_callbacks_t cb = {};
  cb.on_ping_success = onPingOk;
  if (esp_ping_new_session(&cfg, &cb, &ph) == ESP_OK && esp_ping_start(ph) == ESP_OK) pingIp = ip;
  else { if (ph) esp_ping_delete_session(ph); ph = nullptr; logEvent("Не вдалося запустити ping"); }
}

// ---------- під'єднання до роутера ----------
static void quietFor(int64_t us) {
  int64_t t = nowUs() + us;
  if (t > quietUntilUs) quietUntilUs = t;
}
// ---------- бажана точка роутера (1.0.3) ----------
//  18.11.2026, власник: «мост должен всегда подключаться к ближайшему роутеру, так как только он имеет
//  бесперебойное питание и лучшую связь». У церкві три точки «MyRouter-2,4»; одна з них живе лише від міста.
//  Бажану точку задає власник (BSSID у налаштуваннях). Міст під'єднується лише до неї; після 3 невдалих спроб —
//  тимчасово до найсильнішої, і поки він не на бажаній, раз на 5 хв шукає її в ефірі й повертається.
static bool prefFallback = false;          // бажана не відповідала — тимчасово будь-яка
static int64_t prefCheckUs = 0;            // коли наступного разу шукати бажану
static bool prefScanOurs = false;          // пошук мереж запустили ми, а не сторінка
static bool onPreferred() {
  if (!S.staBssid.length() || !gStaUp) return true;
  return WiFi.BSSIDstr().equalsIgnoreCase(S.staBssid);
}

static void staBegin() {
  //  Найсильніша точка з потрібною назвою, а не перша знайдена (ядро 3.3.3 за замовчуванням WIFI_FAST_SCAN).
  //  У церкві від міста живе ще один Keenetic: якщо це ретранслятор, під час відключення він зникне.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  quietFor(10000000LL);                   // пошук роутера перебирає всі канали
  uint8_t b[6];
  if (S.staBssid.length() && !prefFallback && strToMac(S.staBssid, b)) {
    WiFi.begin(S.staSsid.c_str(), S.staPass.c_str(), 0, b);
    return;
  }
  WiFi.begin(S.staSsid.c_str(), S.staPass.c_str());
}

static void prefCheck(int64_t now) {
  if (!S.staBssid.length() || !gStaUp) { prefScanOurs = false; return; }
  if (onPreferred()) { prefFallback = false; prefScanOurs = false; return; }
  if (prefScanOurs) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    prefScanOurs = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.BSSIDstr(i).equalsIgnoreCase(S.staBssid) && WiFi.RSSI(i) >= -85) {
        logEvent("Бажана точка роутера " + S.staBssid + " знову в ефірі (" + String(WiFi.RSSI(i)) + " дБм) — переходжу на неї");
        prefFallback = false;
        staTries = 0;
        staNextTry = now;
        WiFi.disconnect(false, false);
        break;
      }
    }
    return;
  }
  if (now >= prefCheckUs && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    prefCheckUs = now + 5LL * 60 * 1000000;
    quietFor(3000000LL);
    WiFi.scanNetworks(true, false, false, 300);
    prefScanOurs = true;
  }
}

// ---------- NAT ----------
//  18.11.2026: телефон у точці моста за пів хвилини з неї виходив, а eWeLink мережу моста «не бачив». У 1.0.0–1.0.1
//  NAT вмикався одразу після softAP() і без перевірки, а приклад ядра WiFiExtender чекає запуску точки й вмикає NAT,
//  коли є роутер. Тепер щосекунди звіряємо прапорець napt у самому інтерфейсі lwIP і, якщо треба, вмикаємо знову.
static bool natBootOk = false;
static int natState = -1;                   // -1 — ще не звіряли, 0 — вимкнений, 1 — увімкнений
static int64_t natFailLogUs = 0;
static bool natFlag() {
  esp_netif_t* ap = WiFi.AP.netif();
  struct netif* n = ap ? (struct netif*)esp_netif_get_netif_impl(ap) : nullptr;
  return n && n->napt;
}
bool natOn() { return natFlag(); }
static void natCheck(int64_t now) {
  if (!gStaUp) return;                      // без роутера NAT нічого не дасть; звіримо, коли буде
  bool was = natFlag(), on = was;
  if (!on) { WiFi.AP.enableNAPT(true); on = natFlag(); }
  if (natState == -1) {
    logEvent(String("NAT після запуску: виклик ") + (natBootOk ? "успішний" : "невдалий") + ", прапорець " +
             (was ? "увімкнений" : on ? "вимкнений — увімкнено зараз" : "вимкнений — увімкнути не вдалося"));
    if (!on) natFailLogUs = now;
  } else if (on && natState == 0) logEvent("NAT увімкнено: пристрої в мережі для АВР мають інтернет");
  else if (on && !was) logEvent("NAT злетів — увімкнено знову");
  else if (!on && (natState == 1 || now - natFailLogUs > 60LL * 1000000)) {
    logEvent("ПОМИЛКА: NAT не вмикається, пристрої в мережі для АВР без інтернету");
    natFailLogUs = now;
  }
  natState = on ? 1 : 0;
}

// ---------- запуск ----------
void bridgeBegin() {
  mtx = xSemaphoreCreateMutex();
  WiFi.persistent(false);              // налаштування Wi-Fi не пишемо у флеш
  WiFi.onEvent(onWifi);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);        // перепідключаємось самі, з паузами
  int ch = 6;
  if (S.staSsid.length()) {
    //  Спершу роутер: точку відразу піднімаємо на його каналі, щоб АВР потім не перескакував.
    staBegin();
    uint32_t t0 = millis();
    while (!gStaUp && millis() - t0 < 15000) delay(100);
    if (gStaUp) ch = WiFi.channel();
    staTries = 1;
    staNextTry = nowUs() + 15000000LL;
  }
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK, AP_LEASE, DNS_FALLBACK);
  bool apOk = WiFi.softAP(S.apSsid.c_str(), S.apPass.c_str(), ch, 0, 6);
  natBootOk = WiFi.AP.enableNAPT(true);    // як у 1.0.0–1.0.1; що з ним стало, звіряє natCheck() у bridgeLoop
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_inactive_time(WIFI_IF_AP, 120);
  esp_wifi_set_event_mask(WIFI_EVENT_MASK_NONE);   // 1.0.31: і запити пошуку мережі (типово вимкнено) — ознака життя АВР
  curCh = ch;
  MDNS.begin("avr-mist");
  MDNS.addService("http", "tcp", 80);
  logEvent(String(apOk ? "Точка «" : "ПОМИЛКА: не запустилась точка «") + S.apSsid + "» на каналі " + String(ch) +
           (S.staSsid.length() ? (gStaUp ? ", роутер під'єднано" : ", роутер поки не відповідає") : ", роутер не налаштовано"));
}

// ---------- щосекундна робота ----------
void bridgeLoop() {
  int64_t now = nowUs();

  if (gStaGotIp) {
    gStaGotIp = false;
    staTries = 0;
    Network.setDefaultInterface(WiFi.STA);
    esp_netif_dns_info_t d;
    if (esp_netif_get_dns_info(WiFi.STA.netif(), ESP_NETIF_DNS_MAIN, &d) == ESP_OK && d.ip.u_addr.ip4.addr)
      esp_netif_set_dns_info(WiFi.AP.netif(), ESP_NETIF_DNS_MAIN, &d);   // АВР питатиме DNS роутера
    if (!ntpStarted) { configTzTime(TZ_KYIV, "pool.ntp.org", "time.google.com", "ua.pool.ntp.org"); ntpStarted = true; }
    prefCheckUs = now + 2LL * 60 * 1000000;  // не на бажаній точці — перша перевірка за 2 хв
    logEvent("Роутер «" + S.staSsid + "»: адреса " + WiFi.localIP().toString() + ", канал " + String(WiFi.channel()) +
             ", сигнал " + String(WiFi.RSSI()) + " дБм, точка " + WiFi.BSSIDstr() +
             (!S.staBssid.length() ? String() : onPreferred() ? String(" (бажана)") : String(" (НЕ бажана " + S.staBssid + ")")));
  }

  if (S.staSsid.length() && !gStaUp && now >= staNextTry) {
    staTries++;
    if (S.staBssid.length() && !prefFallback && staTries > 3) {
      prefFallback = true;
      logEvent("Бажана точка роутера " + S.staBssid + " не відповідає — тимчасово під'єднуюсь до найсильнішої");
    }
    WiFi.disconnect(false, false);
    staBegin();
    int64_t gap = staTries < 4 ? 15 : staTries < 8 ? 30 : 60;
    staNextTry = now + gap * 1000000LL;
  }

  static int64_t tick = 0;
  if (now - tick < 1000000) return;
  tick = now;
  {
    uint8_t am[6];
    bool ok = strToMac(S.avrMac, am);
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (ok) memcpy(avrMacC, am, 6);
    avrMacOk = ok;
    xSemaphoreGive(mtx);
  }
  natCheck(now);
  prefCheck(now);
  //  1.0.33 (22.11.2026: бажаною стала далека точка-ретранслятор — Сторож слухняно перейшов на неї, а з нею й увесь зв'язок).
  //  Не на бажаній точці понад 10 хв — у журнал і адміністраторам (без звуку); повернувся — у журнал.
  {
    static int64_t notPrefSince = 0;
    static bool told = false;
    if (gStaUp && S.staBssid.length()) {
      if (onPreferred()) {
        if (told) logEvent("Сторож знову на бажаній точці роутера " + S.staBssid);
        notPrefSince = 0;
        told = false;
      } else if (!notPrefSince) notPrefSince = now;
      else if (!told && now - notPrefSince > 10LL * 60 * 1000000) {
        told = true;
        String m = "Сторож 10 хв не на бажаній точці роутера: зараз " + WiFi.BSSIDstr() + " (" + String(WiFi.RSSI()) + " дБм), бажана — " + S.staBssid;
        logEvent(m);
        tgBroadcast(now, "⚠️ <b>Сторож не на бажаній точці роутера</b>\n⛪ " + htmlEsc(S.objName) + " · %T\nЗараз — " + WiFi.BSSIDstr() + " (" +
                         String(WiFi.RSSI()) + " дБм), бажана — " + htmlEsc(S.staBssid) + ".\nБажаної немає в ефірі або її змінили в налаштуваннях: " +
                         "сторінка Сторожа → «Налаштування» → Wi-Fi роутера.", true, C_ADMIN);
      }
    }
  }

  // канал точки (іде за каналом роутера): з роутером — канал роутера, без нього — канал радіо
  int ch = 0;
  uint32_t sd = scanDoneMs;
  bool quiet = now < quietUntilUs || WiFi.scanComplete() == WIFI_SCAN_RUNNING || (sd && millis() - sd < 3000);
  if (!quiet) {
    if (gStaUp) {
      wifi_ap_record_t r;
      if (esp_wifi_sta_get_ap_info(&r) == ESP_OK) ch = r.primary;
    } else {
      uint8_t pr = 0;
      wifi_second_chan_t sc;
      if (esp_wifi_get_channel(&pr, &sc) == ESP_OK) ch = pr;
    }
  }
  if (!ch || ch == curCh) candCh = 0;      // пропущений замір теж обриває «поспіль»
  else if (ch != candCh) candCh = ch;
  else {
    logEvent("Канал точки змінився з " + String(curCh) + " на " + String(ch));
    curCh = ch;
    chChangedUs = now;
    candCh = 0;
  }

  // звірка зі списком точки: рівень сигналу, пропущені події
  wifi_sta_list_t wl;
  if (esp_wifi_ap_get_sta_list(&wl) == ESP_OK) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (auto& c : tbl) {
      if (!c.used) continue;
      bool found = false;
      for (int i = 0; i < wl.num; i++)
        if (!memcmp(wl.sta[i].mac, c.mac, 6)) { found = true; c.rssi = wl.sta[i].rssi; break; }
      if (found && !c.assoc) { c.assoc = true; c.assocUs = now; }
      if (!found && c.assoc) {
        c.assoc = false; c.leaveUs = now; c.leaveReason = 0;
        c.leftAlive = c.lastPingOkUs && now - c.lastPingOkUs < 6000000LL;
      }
    }
    for (int i = 0; i < wl.num; i++)
      if (!findLocked(wl.sta[i].mac)) {
        ApDev* c = getLocked(wl.sta[i].mac);
        if (c) { c->assoc = true; c->assocUs = now; c->rssi = wl.sta[i].rssi; }
      }
    xSemaphoreGive(mtx);
  }

  // адреси, яких не принесла подія
  esp_netif_pair_mac_ip_t pairs[MAXC];
  int np = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (auto& c : tbl) if (c.used && c.assoc && !c.ip && np < MAXC) { memcpy(pairs[np].mac, c.mac, 6); pairs[np].ip.addr = 0; np++; }
  xSemaphoreGive(mtx);
  if (np && esp_netif_dhcps_get_clients_by_mac(WiFi.AP.netif(), np, pairs) == ESP_OK) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < np; i++) if (pairs[i].ip.addr) { ApDev* c = findLocked(pairs[i].mac); if (c) c->ip = pairs[i].ip.addr; }
    xSemaphoreGive(mtx);
  }

  // ping — лише АВР: живлення АВР зникає без «до побачення», точка дізналася б за 2 хв
  uint8_t am[6];
  uint32_t ip = 0;
  bool have = strToMac(S.avrMac, am);
  if (have) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    ApDev* c = findLocked(am);
    if (c && c->assoc) ip = c->ip;
    xSemaphoreGive(mtx);
  }
  if (!ip) pingStop();
  else if (ip != pingIp || memcmp(am, pingMac, 6)) { pingStop(); memcpy(pingMac, am, 6); pingStart(ip); }
}

bool staUp() { return gStaUp; }
int apChannel() { return curCh; }
int64_t apChannelChangedUs() { return chChangedUs; }
String staIpStr() { return gStaUp ? WiFi.localIP().toString() : String(); }
int staRssi() { return gStaUp ? WiFi.RSSI() : 0; }

void scanStart() {
  quietFor(3000000LL);                    // поки пошук не почав звітувати про себе
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true, false, false, 300);
}
String scanJson() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return "{\"running\":true,\"list\":[]}";
  String o = "{\"running\":false,\"list\":[";
  for (int i = 0; i < n; i++) {
    if (i) o += ',';
    o += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"ch\":" + String(WiFi.channel(i)) +
         ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"}";
  }
  return o + "]}";
}

String bridgeJson() {
  String o = "{\"sta\":{\"ssid\":\"" + jsonEsc(S.staSsid) + "\",\"up\":" + (gStaUp ? "true" : "false");
  if (gStaUp) o += ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) + ",\"gw\":\"" + WiFi.gatewayIP().toString() + "\"" +
                    ",\"bssid\":\"" + WiFi.BSSIDstr() + "\",\"pref\":" + (onPreferred() ? "true" : "false");
  o += "},\"ap\":{\"ssid\":\"" + jsonEsc(S.apSsid) + "\",\"ch\":" + String(curCh) + ",\"ip\":\"10.77.0.1\",\"nat\":" + String(natFlag() ? "true" : "false") +
       ",\"probes\":" + String((unsigned)probeN) + ",\"probesAvr\":" + String((unsigned)probeAvrN) + "},\"clients\":[";
  ApDev cs[MAXC];
  int n = clientsSnapshot(cs, MAXC);
  int64_t now = nowUs();
  for (int i = 0; i < n; i++) {
    if (i) o += ',';
    o += "{\"mac\":\"" + macToStr(cs[i].mac) + "\",\"ip\":\"" + (cs[i].ip ? IPAddress(cs[i].ip).toString() : String()) +
         "\",\"assoc\":" + (cs[i].assoc ? "true" : "false") + ",\"rssi\":" + String(cs[i].rssi) +
         ",\"ping\":" + (cs[i].lastPingOkUs ? String((long)((now - cs[i].lastPingOkUs) / 1000000)) : String("null")) +
         ",\"seen\":" + String((long)((now - (cs[i].assoc ? now : cs[i].leaveUs)) / 1000000)) + "}";
  }
  return o + "]}";
}
