/*  АВР-маячок — ESP32-C3 (плата AirM2M / LuatOS «ESP32-C3 Core»).
 *
 *  Вмикається через звичайний USB-зарядник у розетку на ВИХОДІ ГЕНЕРАТОРА (до АВР) — 18.11.2026, власник:
 *  «маячок подключён к выходу напряжения генератора и активен только когда работает генератор». Поки генератор
 *  працює, маячок живий і кожні 2 с каже Сторожу «я тут» (UDP на 10.77.0.1:47801). Замовк — генератор зупинився.
 *
 *  Налаштування з телефона (1.0.16, власник: «маячок должен иметь свой веб интерфейс для подключения к сторожу»):
 *  маячок без мережі Сторожа (ще не задано, 3 хв без зв'язку або кнопка BOOT 3 с) піднімає свою відкриту мережу
 *  «Mayak-XXXX» зі сторінкою http://192.168.4.1 — телефон відкриває її сам. Там вибирають мережу Сторожа «AVR-mist-…»
 *  і вводять її пароль. Під'єднався — за хвилину-три своя мережа вимикається.
 *
 *  Те саме по USB (115200, рядок з Enter):
 *    wifi <назва точки моста> <пароль>   — запам'ятати й під'єднатись (у лапках, якщо є пробіли)
 *    info                                 — стан
 *    pause <секунд>                       — для перевірки: вимкнути Wi-Fi на N секунд
 *    reboot                               — перезапуск
 *  У флеш пише лише команда wifi і кнопка «Під'єднати» на сторінці (правило власника).
 *
 *  Світлодіоди: D4 (GPIO12) коротко блимає на кожен сигнал мосту; D5 (GPIO13) блимає,
 *  поки зв'язку з точкою моста немає.
 */
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_chip_info.h>
#include "version.h"

static const int LED_BEAT = 12, LED_NOLINK = 13;
static const uint16_t BRIDGE_PORT = 47801;
static String ssid, pass, macS, cmdLine;
static WiFiUDP udp;
static uint32_t lastBeat = 0, lastLink = 0, lastTry = 0, pauseUntil = 0;
static bool paused = false;
static bool leds = false;   // на C3 з вбудованою флеш виводи 12/13 зайняті нею — тоді світлодіодів не чіпаємо

// ---------- сторінка налаштування (1.0.16) ----------
static const int BTN = 9;                        // кнопка BOOT на C3: утримати 3 с — увімкнути сторінку налаштування
static const IPAddress AP_IP(192, 168, 4, 1);
static WebServer web(80);
static DNSServer dns;
static bool setupOn = false;
static uint32_t setupSince = 0, linkSince = 0;
static String apName, lastErr;
static volatile uint16_t discReason = 0;         // причина останнього обриву (подія Wi-Fi), розбирає loop()
static volatile bool gotIp = false;

static void led(int pin, int v) { if (leds) digitalWrite(pin, v); }

static void loadCreds() {
  Preferences p;
  if (p.begin("mayak", true)) { ssid = p.getString("ssid", ""); pass = p.getString("pass", ""); p.end(); }
}
static void saveCreds() {
  Preferences p;
  if (p.begin("mayak", false)) { p.putString("ssid", ssid); p.putString("pass", pass); p.end(); }
}
static void connect() {
  lastTry = millis();
  if (!ssid.length()) return;
  discReason = 0;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
}

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

static const char PAGE[] = R"PAGE(<!doctype html><html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Маячок</title><style>
:root{--bg:#14161c;--card:#1d2028;--fg:#e8e8ec;--mut:#9aa0ab;--acc:#2f7de1;--bd:#2c303b;--ok:#2fb36b;--bad:#e05252}
@media (prefers-color-scheme:light){:root{--bg:#f3f4f7;--card:#fff;--fg:#16181d;--mut:#5d6470;--bd:#d9dce3}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:12px 16px 30px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin:12px 0}
h1{font-size:20px;margin:8px 0 2px}.mut{color:var(--mut);font-size:13px}
label{display:block;margin:12px 0 4px;color:var(--mut);font-size:13px}
input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
button{margin-top:12px;width:100%;padding:11px;border:0;border-radius:8px;background:var(--acc);color:#fff;font:inherit;font-weight:600}
button.s{background:none;border:1px solid var(--bd);color:var(--fg);font-weight:400}button:disabled{opacity:.5}
.ok{color:var(--ok)}.bad{color:var(--bad)}.row{display:flex;gap:8px;align-items:center}.row input{width:auto}
</style></head><body><main>
<h1>Маячок «Сторожа світла»</h1><div class="mut" id="who"></div>
<div class="card"><b>Зараз</b><div id="st" style="margin-top:6px">…</div>
<button class="s" id="rb" type="button">Перезапустити маячок</button><div id="rbmsg" style="margin-top:8px"></div></div>
<div class="card" id="form"><b>Мережа Сторожа</b>
<div class="mut">Виберіть мережу Сторожа («AVR-mist-…») і введіть її пароль — той самий, що на сторінці Сторожа в «Налаштування → Точка доступу для АВР».</div>
<label for="ss">Мережа</label><select id="ss"><option value="">шукаю мережі…</option></select>
<button class="s" id="rescan" type="button">Шукати ще раз</button>
<label for="mn">Або назва вручну</label><input id="mn" maxlength="32" placeholder="AVR-mist-XXXX" autocomplete="off" autocapitalize="off">
<label for="pw">Пароль мережі</label><input id="pw" type="password" maxlength="63" autocomplete="new-password">
<label class="row"><input type="checkbox" id="sh"> показати пароль</label>
<button id="go" type="button">Під'єднати</button><div id="msg" style="margin-top:8px"></div></div>
<div class="card mut" id="done">Коли маячок під'єднається, він сам вимкне свою мережу «<span id="apn"></span>». Далі: увімкнути його через USB-зарядник у розетку <b>від генератора</b> — на виході генератора, до АВР; не в ДБЖ і не в міську розетку: маячок має жити, лише поки працює генератор. На сторінці Сторожа в «Налаштування → АВР і маячок» поставити «Маячок встановлено».</div>
</main><script>
const $=i=>document.getElementById(i),W=ms=>new Promise(r=>setTimeout(r,ms));
function esc(t){return String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function st(){try{const s=await (await fetch('/state',{cache:'no-store'})).json();
 $('who').textContent='Версія '+s.ver+' · MAC '+s.mac;$('apn').textContent=s.ap;$('form').style.display=s.setup?'':'none';
 $('st').innerHTML=s.conn?'<span class="ok">● На зв\'язку зі Сторожем</span>: мережа «'+esc(s.ssid)+'», сигнал '+s.rssi+' дБм'+(s.setup?'':'<div class="mut">Змінити мережу: утримати кнопку BOOT на маячку 3 секунди — з\'явиться мережа «'+esc(s.ap)+'».</div>')
  :s.ssid?'<span class="bad">● Немає зв\'язку</span> з мережею «'+esc(s.ssid)+'»'+(s.err?': '+esc(s.err):''):'<span class="bad">● Мережу Сторожа ще не задано</span>';
 return s}catch(e){return null}}
async function scan(fresh){$('rescan').disabled=true;for(let i=0;i<25;i++){let r=null;try{r=await (await fetch('/scan'+(fresh&&!i?'?new=1':''),{cache:'no-store'})).json()}catch(e){}
 if(r&&!r.busy){const L=r.list||[];$('ss').innerHTML=L.length?L.map(n=>'<option value="'+esc(n.s)+'">'+esc(n.s)+(n.st?' — Сторож':'')+' · '+n.r+' дБм</option>').join(''):'<option value="">мереж не видно — «Шукати ще раз»</option>';break}
 await W(800)}$('rescan').disabled=false}
$('rescan').onclick=()=>scan(true);$('sh').onchange=()=>{$('pw').type=$('sh').checked?'text':'password'};
$('go').onclick=async()=>{const s=$('mn').value.trim()||$('ss').value,p=$('pw').value,m=$('msg');
 if(!s){m.innerHTML='<span class="bad">Виберіть мережу Сторожа</span>';return}if(p.length<8){m.innerHTML='<span class="bad">Пароль — щонайменше 8 символів</span>';return}
 $('go').disabled=true;m.textContent='Під\'єднуюсь до «'+s+'»…';
 try{const r=await fetch('/save',{method:'POST',body:new URLSearchParams({ssid:s,pass:p})});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');$('pw').value='';
  for(let i=0;i<40;i++){await W(1000);const t=await st();if(!t)continue;
   if(t.conn){m.innerHTML='<span class="ok">Готово: маячок на зв\'язку зі Сторожем.</span>';$('go').disabled=false;return}
   if(t.err&&i>5){m.innerHTML='<span class="bad">'+esc(t.err)+'</span>';$('go').disabled=false;return}}
  m.textContent='Ще під\'єднуюсь — оновіть сторінку за хвилину'}
 catch(e){m.textContent=e.message&&e.message!=='Failed to fetch'?e.message:'Зв\'язок обірвався — під\'єднайте телефон до мережі маячка знову й оновіть сторінку'}
 $('go').disabled=false};
$('rb').onclick=async()=>{if(!confirm('Перезапустити маячок? Він почне з чистого аркуша, як після ввімкнення.'))return;const m=$('rbmsg');
 try{const r=await fetch('/reboot',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');
  m.textContent='Перезапускаю… Якщо телефон був у мережі маячка, під\'єднайте його знову, коли мережа з\'явиться.'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
st();setInterval(st,4000);scan(false);
</script></body></html>)PAGE";

static String reasonText(uint16_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_802_1X_AUTH_FAILED: case WIFI_REASON_MIC_FAILURE:
      return "пароль не підходить — перевірте на сторінці Сторожа «Точка доступу для АВР»";
    case WIFI_REASON_NO_AP_FOUND: return "мережі не видно поруч — Сторож увімкнений? Назва правильна?";
    case WIFI_REASON_ASSOC_TOOMANY: return "Сторож вже має забагато пристроїв у своїй мережі";   // 1.0.19: «вже», не «уже»
    case WIFI_REASON_ASSOC_LEAVE: return String();          // це ми самі перепід'єднуємось
    default: return r ? "не вдалося під'єднатись (код " + String(r) + ")" : String();
  }
}

static void startSetup(const char* why) {
  if (setupOn) return;
  setupOn = true;
  setupSince = millis();
  WiFi.setAutoReconnect(false);                  // без безперервних спроб: кожна перебирає канали, і телефон губить мережу маячка
  WiFi.mode(WIFI_AP_STA);
  int ch = 1;
  int n = WiFi.scanNetworks(false, false, false, 120);      // канал — як у мережі Сторожа: тоді під'єднання не перескочить каналом
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
  Serial.println("Сторінку налаштування вимкнено: маячок на зв'язку зі Сторожем");
}

static void hRoot() { web.send(200, "text/html; charset=utf-8", PAGE); }
static void hState() {
  bool c = WiFi.isConnected();
  String o = String("{\"ver\":\"") + FW_VERSION + "\",\"mac\":\"" + macS + "\",\"ssid\":\"" + jsonEsc(ssid) + "\",\"conn\":" + (c ? "true" : "false") +
             ",\"rssi\":" + String(c ? WiFi.RSSI() : 0) + ",\"err\":\"" + jsonEsc(lastErr) + "\",\"ap\":\"" + jsonEsc(apName) +
             "\",\"setup\":" + (setupOn ? "true" : "false") + "}";
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
  //  без повторів (кілька точок однієї мережі — найсильніша); мережі Сторожа — першими
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
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < k; i++) {
      bool sto = names[i].startsWith("AVR-mist-");
      if (sto != (pass == 0)) continue;
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
  saveCreds();
  lastErr = "";
  Serial.printf("Сторінка: збережено мережу «%s». Під'єднуюсь…\n", ssid.c_str());
  connect();
  web.send(200, "application/json", "{\"ok\":true}");
}
//  1.0.18: кнопка «Перезапустити маячок» (власник 23.11.2026: «в устройствах в веб интерфейсах нужны кнопки перезагрузки»).
//  Зі сторінки налаштування (своя мережа маячка) чи від Сторожа; інші пристрої мережі Сторожа — ні.
static uint32_t rebootAt = 0;
static void hReboot() {
  bool trusted = setupOn || (WiFi.isConnected() && web.client().remoteIP() == WiFi.gatewayIP());
  if (!trusted) return web.send(403, "application/json", "{\"err\":\"Перезапустити можна зі сторінки налаштування маячка (кнопка BOOT 3 с)\"}");
  Serial.println("Сторінка: перезапуск маячка");
  rebootAt = (millis() + 600) | 1;
  web.send(200, "application/json", "{\"ok\":true}");
}
static void hAny() {                             // телефон перевіряє «чи є інтернет» — показуємо йому сторінку маячка
  if (!setupOn) return web.send(404, "text/plain; charset=utf-8", "Немає такої сторінки");
  web.sendHeader("Location", "http://192.168.4.1/");
  web.send(302, "text/plain", "");
}

static void bootBtn(uint32_t now) {               // BOOT 3 с — сторінка налаштування
  static uint32_t down = 0;
  if (digitalRead(BTN) == LOW) {
    if (!down) down = now | 1;
    else if (now - down > 3000 && !setupOn) startSetup("кнопка BOOT");
  } else down = 0;
}

static void info() {
  Serial.printf("\nАВР-маячок %s (збірка %s), MAC %s\n", FW_VERSION, FW_BUILD, macS.c_str());
  if (!ssid.length()) Serial.println("Мережу Сторожа не задано: сторінка маячка або команда wifi <назва> <пароль>");
  else if (WiFi.isConnected())
    Serial.printf("Мережа «%s»: під'єднано, адреса %s, Сторож %s, сигнал %d дБм\n", ssid.c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  else Serial.printf("Мережа «%s»: немає зв'язку%s%s\n", ssid.c_str(), lastErr.length() ? " — " : "", lastErr.c_str());
  if (setupOn) Serial.printf("Сторінка налаштування: мережа «%s» без пароля, http://192.168.4.1\n", apName.c_str());
}

//  Розбір «a b» або «"a b" "c d"»: два слова, кожне можна взяти в лапки.
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
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (cmdLine.length() < 200) cmdLine += c; continue; }
    String l = cmdLine;
    cmdLine = "";
    l.trim();
    if (l.startsWith("wifi ")) {
      String a, b;
      if (two(l.substring(5), a, b) && a.length() && a.length() <= 32 && b.length() >= 8 && b.length() <= 63) {
        ssid = a;
        pass = b;
        saveCreds();
        Serial.printf("Збережено: точка «%s». Під'єднуюсь…\n", ssid.c_str());
        connect();
      } else Serial.println("Формат: wifi <назва точки> <пароль від 8 символів>; у лапках, якщо є пробіли");
    } else if (l == "info") info();
    else if (l.startsWith("pause ")) {
      long s = l.substring(6).toInt();
      if (s > 0 && s <= 3600) {
        Serial.printf("Перевірка: Wi-Fi вимкнено на %ld с\n", s);
        paused = true;
        pauseUntil = millis() + s * 1000UL;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
    } else if (l == "reboot") ESP.restart();
    else if (l.length()) Serial.println("Команди: wifi <назва> <пароль>, info, pause <с>, reboot");
  }
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  leds = !(ci.features & CHIP_FEATURE_EMB_FLASH);
  if (leds) { pinMode(LED_BEAT, OUTPUT); pinMode(LED_NOLINK, OUTPUT); }
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  macS = b;
  char an[16];
  snprintf(an, sizeof an, "Mayak-%02X%02X", m[4], m[5]);
  apName = an;
  pinMode(BTN, INPUT_PULLUP);
  loadCreds();
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
  web.on("/reboot", HTTP_POST, hReboot);
  web.onNotFound(hAny);
  web.begin();
  if (ssid.length()) connect();
  else startSetup("мережу Сторожа ще не задано");
  udp.begin(BRIDGE_PORT + 1);
  delay(500);
  info();
}

void loop() {
  serialCmd();
  web.handleClient();
  if (setupOn) dns.processNextRequest();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
  uint32_t now = millis();
  bootBtn(now);
  uint16_t dr = discReason;
  if (dr) {
    discReason = 0;
    String e = reasonText(dr);
    if (e.length() && e != lastErr) { lastErr = e; Serial.printf("Мережа «%s»: %s\n", ssid.c_str(), e.c_str()); }
  } else if (gotIp && lastErr.length()) lastErr = "";
  if (paused) {
    led(LED_BEAT, LOW);
    led(LED_NOLINK, (now / 250) % 2);
    if ((int32_t)(now - pauseUntil) >= 0) {
      paused = false;
      WiFi.mode(WIFI_STA);
      connect();
      lastLink = now;
      Serial.println("Перевірка закінчена, Wi-Fi знову ввімкнено");
    }
    delay(10);
    return;
  }
  bool conn = WiFi.isConnected();
  if (conn) { if (!linkSince) linkSince = now | 1; } else linkSince = 0;
  //  під'єднався — сторінку налаштування вимкнути: за хвилину, якщо телефон вже відключився й минуло 3 хв від її
  //  увімкнення (встигнути під'єднатись після кнопки BOOT), і в будь-якому разі за 5 хв на зв'язку
  if (setupOn && linkSince && ((now - linkSince > 60000 && WiFi.softAPgetStationNum() == 0 && now - setupSince > 180000) || now - linkSince > 300000)) stopSetup();
  if (conn) {
    lastLink = now;
    if (now - lastBeat >= 2000) {
      lastBeat = now;
      char msg[96];
      snprintf(msg, sizeof msg, "MAYAK1 %s up=%lu rssi=%d ver=%s", macS.c_str(), (unsigned long)(now / 1000), WiFi.RSSI(), FW_VERSION);
      udp.beginPacket(WiFi.gatewayIP(), BRIDGE_PORT);
      udp.write((const uint8_t*)msg, strlen(msg));
      udp.endPacket();
      led(LED_BEAT, HIGH);
    }
    if (now - lastBeat > 80) led(LED_BEAT, LOW);
    led(LED_NOLINK, LOW);
  } else {
    led(LED_BEAT, setupOn ? (now / 500) % 2 == 0 : LOW);
    led(LED_NOLINK, (now / 500) % 2);
    bool phone = setupOn && WiFi.softAPgetStationNum() > 0;   // телефон на сторінці — спроби лише за кнопкою «Під'єднати»
    if (ssid.length() && !phone && now - lastTry > (setupOn ? 60000UL : 30000UL)) connect();
    if (!setupOn && now - lastLink > 180000) startSetup("3 хв без зв'язку зі Сторожем");
    if (now - lastLink > 15UL * 60 * 1000 && !phone) ESP.restart();     // чверть години без точки — почати з чистого аркуша
  }
  delay(10);
}
