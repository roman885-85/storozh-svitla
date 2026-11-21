#include "web.h"
#include "settings.h"
#include "bridge.h"
#include "watch.h"
#include "tg.h"
#include "util.h"
#include "version.h"
#include "stats.h"
#include "signals.h"
#include "yasno.h"
#include "lcd.h"
#include "box.h"
#include "tlspool.h"
#include <WebServer.h>
#include <esp_rom_crc.h>
#include <Update.h>
#include <esp_random.h>

extern const char INDEX_HTML[];
extern const char LOGIN_HTML[];
extern const char SCHEMES_HTML[];

static WebServer srv(80);
static bool restartPending = false;
static uint32_t restartAt = 0;
static bool upAuth = false, upChipOk = true;

static void json(const String& s, int code = 200) {
  srv.sendHeader("Cache-Control", "no-store");
  srv.send(code, "application/json; charset=utf-8", s);
}
static void fail(const String& msg) { json("{\"err\":\"" + jsonEsc(msg) + "\"}", 400); }

// ---------- вхід: форма й cookie, сесії лише в RAM ----------
//  18.11.2026: вбудований браузер застосунку Claude показав на 401 нечитабельний текст і не дав вікна входу
//  (Basic). Браузер усередині Telegram, певно, такий самий. Тепер звичайна форма входу й cookie сесії;
//  заголовок Basic теж приймаємо (curl, скрипти), але вікна входу браузера не викликаємо ніде.
//  Після перезапуску моста сесій немає — вхід заново. Нічого з цього не пишеться у флеш.
static const int MAX_SESS = 6;
static const int64_t SESS_IDLE_US = 30LL * 86400 * 1000000;     // 30 днів без звернень — сесія зникає
struct Sess { bool used; uint8_t tok[16]; int64_t lastUs; };
static Sess sess[MAX_SESS];
static int authSess = -1;              // сесія поточного запиту; -1 — увійшли через Basic або ніяк
static void sessSave();                // 1.0.22: копія сесій у RTC (нижче)

static String hex16(const uint8_t* b16) {
  static const char hx[] = "0123456789abcdef";
  String s;
  s.reserve(32);
  for (int i = 0; i < 16; i++) { s += hx[b16[i] >> 4]; s += hx[b16[i] & 15]; }
  return s;
}
static bool unhex16(const String& s, uint8_t* b16) {
  if (s.length() != 32) return false;
  for (int i = 0; i < 32; i++) {
    char c = s[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    if (i & 1) b16[i / 2] |= v; else b16[i / 2] = v << 4;
  }
  return true;
}
static String cookieToken() {
  String c = srv.header("Cookie");
  int i = 0;
  while (i < (int)c.length()) {
    int e = c.indexOf(';', i);
    if (e < 0) e = c.length();
    String part = c.substring(i, e);
    part.trim();
    if (part.startsWith("s=")) return part.substring(2);
    i = e + 1;
  }
  return String();
}
static int sessFind() {
  uint8_t t16[16];
  if (!unhex16(cookieToken(), t16)) return -1;
  int64_t now = nowUs();
  for (int i = 0; i < MAX_SESS; i++) {
    if (!sess[i].used) continue;
    if (now - sess[i].lastUs > SESS_IDLE_US) { sess[i].used = false; continue; }
    uint8_t d = 0;
    for (int k = 0; k < 16; k++) d |= sess[i].tok[k] ^ t16[k];
    if (!d) { sess[i].lastUs = now; return i; }
  }
  return -1;
}
static int sessNew() {
  int k = -1;
  for (int i = 0; i < MAX_SESS; i++) if (!sess[i].used) { k = i; break; }
  if (k < 0) {                          // усі зайняті — витісняємо ту, якою найдовше не користувались
    k = 0;
    for (int i = 1; i < MAX_SESS; i++) if (sess[i].lastUs < sess[k].lastUs) k = i;
  }
  for (int i = 0; i < 16; i += 4) { uint32_t r = esp_random(); memcpy(sess[k].tok + i, &r, 4); }
  sess[k].used = true;
  sess[k].lastUs = nowUs();
  sessSave();
  return k;
}
//  1.0.22: сесії переживають оновлення й програмний перезапуск — у пам'яті RTC з CRC (як облік світла й черга
//  Telegram). 18.11 після кожного оновлення через віддалену адресу доводилось входити знову. Без живлення — зникають.
//  Час останнього звернення після перезапуску рахуємо від запуску (лічильник мікросекунд починається з нуля).
struct RtcSess { uint32_t magic, crc; Sess s[MAX_SESS]; };
RTC_NOINIT_ATTR static RtcSess RS;                    // після RQ (tg.cpp) у збірці — адреси обліку й черги не зсуваються
static const uint32_t RS_MAGIC = 0x53534531;          // «SSE1»
static void sessSave() {
  memcpy(RS.s, sess, sizeof sess);
  RS.crc = esp_rom_crc32_le(0, (const uint8_t*)RS.s, sizeof RS.s);
  RS.magic = RS_MAGIC;
}
static int sessRestore() {
  if (RS.magic != RS_MAGIC || RS.crc != esp_rom_crc32_le(0, (const uint8_t*)RS.s, sizeof RS.s)) { RS.magic = 0; return 0; }
  int n = 0;
  for (int i = 0; i < MAX_SESS; i++) { sess[i] = RS.s[i]; if (sess[i].used) { sess[i].lastUs = nowUs(); n++; } }
  return n;
}
static void sessCookie(int k) {
  srv.sendHeader("Set-Cookie", "s=" + hex16(sess[k].tok) + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000");
}
static void sessClearAll() { for (auto& x : sess) x.used = false; sessSave(); }
void webLogoutAll() { sessClearAll(); }

//  Чужий сайт не має змусити браузер щось змінити на мості (CSRF). Cookie з SameSite=Lax на чужі POST браузер не
//  шле, але вхід Basic, запам'ятований ще з 1.0.0, шле завжди. Тому POST із заголовком Origin іншого хоста
//  відхиляємо. Сторінка моста шле свій Origin, curl і скрипти — жодного. Lax, а не Strict: посилання на сторінку
//  з Telegram чи закладки має відкриватись без повторного входу, а все, що змінює міст, — лише POST.
static bool foreignPost() {
  if (srv.method() != HTTP_POST) return false;
  String o = srv.header("Origin");
  if (!o.length()) return false;
  int p = o.indexOf("://");
  return !(p > 0 && o.substring(p + 3).equalsIgnoreCase(srv.hostHeader()));
}
static bool authOk() {                 // лише перевірка, нічого не надсилає
  authSess = -1;
  if (foreignPost()) return false;
  authSess = sessFind();
  if (authSess >= 0) return true;
  return srv.authenticate("admin", S.adminPass.c_str());
}
static bool auth() {                   // для /api/*: без входу — 401 JSON, сторінка сама перезавантажиться на форму
  if (authOk()) return true;
  json("{\"err\":\"Потрібен вхід\",\"login\":true}", 401);
  return false;
}
static void loginPage(int code, const String& err) {
  String pg = FPSTR(LOGIN_HTML);
  pg.replace("%ERR%", err.length() ? "<div class=\"err\">" + err + "</div>" : String());
  srv.sendHeader("Cache-Control", "no-store");
  srv.send(code, "text/html; charset=utf-8", pg);
}

//  Невдалі спроби: кожна — щонайменше 1 с відповіді; 5 за хвилину — вхід не приймаємо 60 с, кожна наступна пауза вдвічі
//  довша (до 15 хв). 1.0.21: сторінка тепер відкрита й з інтернету (адреса KeenDNS роутера), тож 10 невдач за годину —
//  попередження адміністраторам у Telegram. Усі запити йдуть через роутер — одна адреса, тож рахуємо всі разом, а
//  справжню адресу беремо із заголовка X-Forwarded-For, якщо роутер його дав.
static int64_t failUs[5] = {0};
static uint32_t failN = 0;
static int64_t lockUntilUs = 0;
static int lockLevel = 0;
static int64_t hourStartUs = 0, alertUs = 0;
static int hourFails = 0;
static String clientIp() {
  String f = srv.header("X-Forwarded-For");
  if (!f.length()) f = srv.header("X-Real-IP");
  int c = f.indexOf(',');
  if (c > 0) f = f.substring(0, c);
  f.trim();
  String r = srv.client().remoteIP().toString();
  return f.length() && f.length() < 46 ? f + " (через " + r + ")" : r;
}

static void hLogin() {
  int64_t now = nowUs();
  if (now < lockUntilUs) return loginPage(429, "Забагато невдалих спроб. Зачекайте " + String((int)((lockUntilUs - now) / 60000000) + 1) + " хв.");
  if (foreignPost()) return loginPage(403, "Вхід лише з цієї сторінки");
  String pw = srv.arg("p");
  if (pw.length() && pw == S.adminPass) {
    sessCookie(sessNew());
    lockLevel = 0;
    logEvent("Вхід на сторінку з " + clientIp());
    srv.sendHeader("Location", "/");
    srv.sendHeader("Cache-Control", "no-store");
    srv.send(303, "text/plain; charset=utf-8", "");
    return;
  }
  delay(1000);
  now = nowUs();
  failUs[failN++ % 5] = now;
  if (!hourStartUs || now - hourStartUs > 3600LL * 1000000) { hourStartUs = now; hourFails = 0; }
  hourFails++;
  if (hourFails >= 10 && (!alertUs || now - alertUs > 3600LL * 1000000)) {
    alertUs = now;
    tgBroadcast(now, "🔐 <b>Хтось підбирає пароль сторінки Сторожа</b>\n⛪ " + htmlEsc(S.objName) + " · " + String(hourFails) +
                     " невдалих спроб за годину, остання з " + htmlEsc(clientIp()) +
                     "\nЯкщо це не ви — змініть пароль сторінки: «Налаштування → Пароль сторінки».", false, C_ADMIN);
  }
  int recent = 0;
  for (int64_t f : failUs) if (f && now - f < 60LL * 1000000) recent++;
  if (recent >= 5) {
    int sec = 60 << (lockLevel < 4 ? lockLevel : 4);   // 60, 120, 240, 480, 960 с
    if (sec > 900) sec = 900;
    lockLevel++;
    lockUntilUs = now + (int64_t)sec * 1000000;
    for (auto& f : failUs) f = 0;
    logEvent("Вхід на сторінку: 5 невдалих спроб за хвилину (" + clientIp() + ") — пауза " + String(sec / 60) + " хв");
    return loginPage(429, "Забагато невдалих спроб. Зачекайте " + String(sec / 60) + " хв.");
  }
  loginPage(403, "Хибний пароль");
}
static void hLoginGet() {
  if (authOk()) { srv.sendHeader("Location", "/"); srv.send(303, "text/plain; charset=utf-8", ""); return; }
  loginPage(200, String());
}
static void hLogout() {
  int k = sessFind();
  if (k >= 0) { sess[k].used = false; sessSave(); }
  srv.sendHeader("Set-Cookie", "s=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
  json("{\"ok\":true}");
}

//  Пристрій, з якого відкрито сторінку, — у точці моста? (18.11 власник призначив АВР-ом свій телефон:
//  АВР ще не був підключений до точки, і телефон був єдиним пристроєм у списку.)
static bool isRequesterDevice(const uint8_t mac[6]) {
  ApDev c;
  if (!findClient(mac, c) || !c.ip) return false;
  return IPAddress(c.ip).toString() == srv.client().remoteIP().toString();
}
static const char* OWN_DEVICE_ERR =
  "Це пристрій, з якого відкрито цю сторінку (ваш телефон чи комп'ютер), а не АВР. "
  "АВР з'явиться в списку окремим рядком, коли його підключать у застосунку eWeLink до мережі для АВР.";
//  18.11.2026 о 10:42 АВР-ом призначили другий телефон, 1a:00:00:00:00:01. Біт 0x02 першого байта — «адреса задана
//  локально»: так телефони й комп'ютери ховають справжню адресу. У АВР адреса заводська.
static const char* RANDOM_MAC_ERR =
  "Це випадкова адреса: такі вмикають телефони й комп'ютери для приватності. У АВР адреса заводська, "
  "він з'явиться окремим рядком, коли його підключать у застосунку eWeLink до мережі для АВР.";
// ---------- картинка бота (1.0.9) ----------
//  Сторінка надсилає JPG, міст одразу складає з нього тіло multipart для setMyProfilePhoto (без копіювання)
//  і віддає задачі Telegram. Токен лишається на мості.
static const size_t PH_MAX = 72 * 1024;
static uint8_t* phBuf = nullptr;
static size_t phLen = 0, phCap = 0, phJpg = 0;
static String phErr;
static String phHead() {
  return String("--" TG_PHOTO_BOUNDARY "\r\nContent-Disposition: form-data; name=\"photo\"\r\n\r\n"
                "{\"type\":\"static\",\"photo\":\"attach://pic\"}\r\n--" TG_PHOTO_BOUNDARY "\r\n"
                "Content-Disposition: form-data; name=\"pic\"; filename=\"bot.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n");
}
static const char* PH_TAIL = "\r\n--" TG_PHOTO_BOUNDARY "--\r\n";
static void phDrop(const String& e) { free(phBuf); phBuf = nullptr; phLen = 0; phErr = e; }
static void hPhotoUpload() {
  HTTPUpload& u = srv.upload();
  if (u.status == UPLOAD_FILE_START) {
    phDrop("");
    if (!authOk()) return phDrop("Потрібен вхід на сторінку");
    if (!S.tgToken.length()) return phDrop("Спершу збережіть токен бота");
    String h = phHead();
    phJpg = h.length();
    //  Буфер — за довжиною самого запиту (файл + рамки multipart), а не «на найбільший файл»: 18.11 спроба взяти
    //  74 КБ + запас не вдалась — вільно було 145 КБ, але шматками. І лишаємо ≥ 50 КБ на шифроване з'єднання з Telegram.
    int total = srv.clientContentLength();
    if (total <= 0 || total > (int)PH_MAX + 1024) return phDrop("Картинка завелика: до 72 КБ");
    phCap = phJpg + (size_t)total + strlen(PH_TAIL);
    if (ESP.getMaxAllocHeap() < phCap || ESP.getFreeHeap() < phCap + 50000)
      return phDrop("На платі мало пам'яті для картинки (вільно " + String(ESP.getFreeHeap() / 1024) + " КБ, шматок " +
                    String(ESP.getMaxAllocHeap() / 1024) + " КБ) — спробуйте ще раз за хвилину");
    phBuf = (uint8_t*)malloc(phCap);
    if (!phBuf) return phDrop("На платі мало пам'яті для картинки");
    memcpy(phBuf, h.c_str(), phJpg);
    phLen = phJpg;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!phBuf) return;
    if (phLen + u.currentSize + strlen(PH_TAIL) > phCap) return phDrop("Картинка завелика: до 72 КБ");
    memcpy(phBuf + phLen, u.buf, u.currentSize);
    phLen += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (!phBuf) return;
    if (phLen - phJpg < 1000 || phBuf[phJpg] != 0xFF || phBuf[phJpg + 1] != 0xD8) return phDrop("Потрібен файл JPG");
    memcpy(phBuf + phLen, PH_TAIL, strlen(PH_TAIL));
    phLen += strlen(PH_TAIL);
  } else if (u.status == UPLOAD_FILE_ABORTED) phDrop("Завантаження перервано");
}
static void hPhotoDone() {
  if (!authOk()) return json("{\"err\":\"Потрібен вхід\",\"login\":true}", 401);
  if (!phBuf || phErr.length()) { String e = phErr.length() ? phErr : String("Файл не отримано"); phDrop(""); return fail(e); }
  uint8_t* b = phBuf;
  size_t n = phLen;
  phBuf = nullptr;
  phLen = 0;
  if (!tgSetPhoto(b, n)) return fail("Попередня картинка ще надсилається");
  logEvent("Картинка бота: отримано " + String((unsigned)((n - phJpg) / 1024)) + " КБ, надсилаю в Telegram");
  json("{\"ok\":true}");
}

static void hBotName() {               // 1.0.11: назва бота зі сторінки
  if (!auth()) return;
  if (!S.tgToken.length()) return fail("Спершу збережіть токен бота");
  String n = srv.arg("name");
  n.replace("\n", " ");
  n.trim();
  if (!n.length() || n.length() > 64) return fail("Назва бота — від 1 до 64 символів");
  if (!tgSetName(n)) return fail("Попередня зміна назви ще триває");
  json("{\"ok\":true}");
}

// ---------- сигнали: світлодіоди й звук (1.0.13) ----------
static const char* b(bool v);
//  Чим зайнятий вивід у налаштуваннях n (лише те, що ввімкнено); nullptr — вільний. Для кнопки скидання пароля (1.0.15).
static const char* sigUses(const Settings& n, int pin) {
  if (pin <= 0) return nullptr;
  if (n.ledOn && n.ledType == 0 && (n.ledG == pin || n.ledR == pin)) return "світлодіодом";
  if (n.ledOn && n.ledType == 1 && n.stripPin == pin) return "стрічкою";
  if ((n.sndType == 1 || n.sndType == 2) && n.sndPin == pin) return "бузером";
  if (n.sndType == 3 && n.dacPin == pin) return "ЦАП";
  if (n.lcdOn && (n.lcdRst == pin || n.lcdCe == pin || n.lcdDc == pin || n.lcdDin == pin || n.lcdClk == pin || (n.lcdBl && n.lcdBl == pin))) return "дисплеєм";
  return nullptr;
}
static String hexCol(uint32_t c) { char s[8]; snprintf(s, sizeof s, "#%06lX", (unsigned long)(c & 0xFFFFFF)); return s; }
static bool parseCol(const String& s, uint32_t& out) {
  if (s.length() != 7 || s[0] != '#') return false;
  char* e;
  unsigned long v = strtoul(s.c_str() + 1, &e, 16);
  if (*e) return false;
  out = v & 0xFFFFFF;
  return true;
}
static void hSigGet() {
  if (!auth()) return;
  json(String("{\"ledOn\":") + b(S.ledOn) + ",\"ledLow\":" + b(S.ledLow) + ",\"ledG\":" + String(S.ledG) + ",\"ledR\":" + String(S.ledR) +
       ",\"ledBri\":" + String(S.ledBri) + ",\"ledBlink\":" + String(S.ledBlink) + ",\"sndType\":" + String(S.sndType) +
       ",\"sndPin\":" + String(S.sndPin) + ",\"dacPin\":" + String(S.dacPin) + ",\"sndVol\":" + String(S.sndVol) +
       ",\"sndLost\":" + b(S.sndLost) + ",\"sndBack\":" + b(S.sndBack) + ",\"sndBoot\":" + b(S.sndBoot) + ",\"sndRep\":" + String(S.sndRep) +
       ",\"qOn\":" + b(S.qOn) + ",\"qFrom\":" + String(S.qFrom) + ",\"qTo\":" + String(S.qTo) +
       ",\"melLost\":\"" + jsonEsc(S.melLost) + "\",\"melBack\":\"" + jsonEsc(S.melBack) + "\",\"patLost\":\"" + jsonEsc(S.patLost) +
       "\",\"patBack\":\"" + jsonEsc(S.patBack) + "\",\"srcLost\":" + String(S.srcLost) + ",\"srcBack\":" + String(S.srcBack) +
       ",\"ledType\":" + String(S.ledType) + ",\"stripPin\":" + String(S.stripPin) + ",\"stripN\":" + String(S.stripN) +
       ",\"stripBri\":" + String(S.stripBri) + ",\"stripNight\":" + String(S.stripNight) + ",\"stripOrder\":" + String(S.stripOrder) +
       ",\"stripFx\":" + String(S.stripFx) + ",\"stripAnim\":" + b(S.stripAnim) + ",\"stripMa\":" + String(S.stripMa) +
       ",\"colOn\":\"" + hexCol(S.colOn) + "\",\"colOff\":\"" + hexCol(S.colOff) + "\",\"colGen\":\"" + hexCol(S.colGen) +
       "\",\"colUnk\":\"" + hexCol(S.colUnk) + "\",\"stFxOn\":" + String(S.stFxOn) + ",\"stFxGen\":" + String(S.stFxGen) +
       ",\"stWvSpd\":" + String(S.stWvSpd) + ",\"stWvInt\":" + String(S.stWvInt) + ",\"lcdOn\":" + b(S.lcdOn) + ",\"lcdBlLow\":" + b(S.lcdBlLow) +
       ",\"lcdInv\":" + b(S.lcdInv) + ",\"lcdFlip\":" + b(S.lcdFlip) + ",\"lcdRst\":" + String(S.lcdRst) + ",\"lcdCe\":" + String(S.lcdCe) +
       ",\"lcdDc\":" + String(S.lcdDc) + ",\"lcdDin\":" + String(S.lcdDin) + ",\"lcdClk\":" + String(S.lcdClk) + ",\"lcdBl\":" + String(S.lcdBl) +
       ",\"lcdVop\":" + String(S.lcdVop) + ",\"lcdBri\":" + String(S.lcdBri) + ",\"lcdNight\":" + String(S.lcdNight) +
       ",\"lcdBias\":" + String(S.lcdBias) + ",\"lcdMq\":" + String(S.lcdMq) +
       ",\"lcdFx\":" + (S.lcdFx ? "true" : "false") + ",\"lcdFxSec\":" + String(S.lcdFxSec) + ",\"lcdAnnMin\":" + String(S.lcdAnnMin) +
       ",\"nLedOff\":" + b(S.nLedOff) + ",\"nLcdOff\":" + b(S.nLcdOff) +
       ",\"st\":" + sigJson() + "}");
}
static void hSigPost() {
  if (!auth()) return;
  Settings n = S;
  String err;
  auto num = [&](const char* k, long lo, long hi, const char* what, long& out) -> bool {
    if (!srv.hasArg(k)) return true;
    String v = srv.arg(k);
    v.trim();
    long x = v.toInt();
    if (!v.length() || x < lo || x > hi) { err = String(what) + ": від " + lo + " до " + hi; return false; }
    out = x;
    return true;
  };
  auto flag = [&](const char* k, bool& dst) { if (srv.hasArg(k)) dst = srv.arg(k) == "1"; };
  long v;
  flag("ledOn", n.ledOn); flag("ledLow", n.ledLow); flag("sndLost", n.sndLost); flag("sndBack", n.sndBack);
  flag("sndBoot", n.sndBoot); flag("qOn", n.qOn); flag("nLedOff", n.nLedOff); flag("nLcdOff", n.nLcdOff);
  v = n.ledG;     if (!num("ledG", 0, 39, "Вивід зеленого", v)) return fail(err);      n.ledG = v;
  v = n.ledR;     if (!num("ledR", 0, 39, "Вивід червоного", v)) return fail(err);     n.ledR = v;
  v = n.ledBri;   if (!num("ledBri", 1, 100, "Яскравість, %", v)) return fail(err);    n.ledBri = v;
  v = n.ledBlink; if (!num("ledBlink", 200, 5000, "Період блимання, мс", v)) return fail(err); n.ledBlink = v;
  v = n.sndType;  if (!num("sndType", 0, 3, "Тип звуку", v)) return fail(err);         n.sndType = v;
  v = n.sndPin;   if (!num("sndPin", 0, 39, "Вивід бузера", v)) return fail(err);      n.sndPin = v;
  v = n.dacPin;   if (!num("dacPin", 25, 26, "Вивід ЦАП", v)) return fail(err);        n.dacPin = v;
  v = n.sndVol;   if (!num("sndVol", 0, 100, "Гучність, %", v)) return fail(err);      n.sndVol = v;
  v = n.sndRep;   if (!num("sndRep", 1, 10, "Повторів", v)) return fail(err);          n.sndRep = v;
  v = n.qFrom;    if (!num("qFrom", 0, 23, "Ніч з (година)", v)) return fail(err);    n.qFrom = v;
  v = n.qTo;      if (!num("qTo", 0, 23, "Ніч до (година)", v)) return fail(err);     n.qTo = v;
  v = n.srcLost;  if (!num("srcLost", 0, 2, "Звук «світло зникло»", v)) return fail(err); n.srcLost = v;
  v = n.srcBack;  if (!num("srcBack", 0, 2, "Звук «світло є»", v)) return fail(err);   n.srcBack = v;
  flag("stripAnim", n.stripAnim);
  v = n.ledType;    if (!num("ledType", 0, 1, "Вид індикатора", v)) return fail(err);        n.ledType = v;
  v = n.stripPin;   if (!num("stripPin", 0, 39, "Вивід стрічки", v)) return fail(err);        n.stripPin = v;
  v = n.stripN;     if (!num("stripN", 1, 60, "Світлодіодів у стрічці", v)) return fail(err); n.stripN = v;
  v = n.stripBri;   if (!num("stripBri", 1, 100, "Яскравість стрічки, %", v)) return fail(err); n.stripBri = v;
  v = n.stripNight; if (!num("stripNight", 0, 100, "Яскравість уночі, %", v)) return fail(err); n.stripNight = v;
  v = n.stripOrder; if (!num("stripOrder", 0, 2, "Порядок кольорів", v)) return fail(err);   n.stripOrder = v;
  v = n.stripFx;    if (!num("stripFx", 0, 3, "Ефект «світла немає»", v)) return fail(err);  n.stripFx = v;
  //  1.0.22: хвиля стрічки й дисплей
  v = n.stFxOn;     if (!num("stFxOn", 0, 1, "Ефект «світло є»", v)) return fail(err);      n.stFxOn = v;
  v = n.stFxGen;    if (!num("stFxGen", 0, 1, "Ефект «генератор»", v)) return fail(err);    n.stFxGen = v;
  v = n.stWvSpd;    if (!num("stWvSpd", 1, 10, "Швидкість хвилі", v)) return fail(err);     n.stWvSpd = v;
  v = n.stWvInt;    if (!num("stWvInt", 10, 100, "Інтенсивність хвилі, %", v)) return fail(err); n.stWvInt = v;
  flag("lcdOn", n.lcdOn); flag("lcdBlLow", n.lcdBlLow); flag("lcdInv", n.lcdInv); flag("lcdFlip", n.lcdFlip); flag("lcdFx", n.lcdFx);
  v = n.lcdRst;     if (!num("lcdRst", 0, 39, "Вивід RST", v)) return fail(err);   n.lcdRst = v;
  v = n.lcdCe;      if (!num("lcdCe", 0, 39, "Вивід CE", v)) return fail(err);     n.lcdCe = v;
  v = n.lcdDc;      if (!num("lcdDc", 0, 39, "Вивід DC", v)) return fail(err);     n.lcdDc = v;
  v = n.lcdDin;     if (!num("lcdDin", 0, 39, "Вивід DIN", v)) return fail(err);   n.lcdDin = v;
  v = n.lcdClk;     if (!num("lcdClk", 0, 39, "Вивід CLK", v)) return fail(err);   n.lcdClk = v;
  v = n.lcdBl;      if (!num("lcdBl", 0, 39, "Вивід підсвітки", v)) return fail(err); n.lcdBl = v;
  v = n.lcdVop;     if (!num("lcdVop", 0, 127, "Контраст", v)) return fail(err);   n.lcdVop = v;
  v = n.lcdBri;     if (!num("lcdBri", 0, 100, "Підсвітка вдень, %", v)) return fail(err); n.lcdBri = v;
  v = n.lcdNight;   if (!num("lcdNight", 0, 100, "Підсвітка вночі, %", v)) return fail(err); n.lcdNight = v;
  v = n.lcdBias;    if (!num("lcdBias", 0, 7, "Зсув дисплея", v)) return fail(err);      n.lcdBias = v;
  v = n.lcdMq;      if (!num("lcdMq", 0, 10, "Швидкість біжучого рядка", v)) return fail(err); n.lcdMq = v;
  v = n.lcdFxSec;   if (!num("lcdFxSec", 3, 600, "Скільки блимати, с", v)) return fail(err); n.lcdFxSec = v;
  v = n.lcdAnnMin;  if (!num("lcdAnnMin", 0, 240, "Оголошення на дисплеї, хв", v)) return fail(err); n.lcdAnnMin = v;
  v = n.stripMa;    if (!num("stripMa", 50, 2000, "Межа струму, мА", v)) return fail(err);   n.stripMa = v;
  {
    const char* ck[4] = {"colOn", "colOff", "colGen", "colUnk"};
    uint32_t* cd[4] = {&n.colOn, &n.colOff, &n.colGen, &n.colUnk};
    for (int i = 0; i < 4; i++)
      if (srv.hasArg(ck[i]) && !parseCol(srv.arg(ck[i]), *cd[i])) return fail("Колір має вигляд #RRGGBB");
  }
  auto text = [&](const char* k, String& dst, bool mel) -> bool {
    if (!srv.hasArg(k)) return true;
    String s = srv.arg(k);
    s.trim();
    if (s.length() > 300) { err = "Задовго: до 300 символів"; return false; }
    if (!(mel ? sigMelodyOk(s, err) : sigPatternOk(s, err))) return false;
    dst = s;
    return true;
  };
  if (!text("melLost", n.melLost, true) || !text("melBack", n.melBack, true) ||
      !text("patLost", n.patLost, false) || !text("patBack", n.patBack, false)) return fail(err);
  //  зайняті виводи індикатора: два світлодіоди або стрічка
  int ia = -1, ib = -1;
  if (n.ledOn && n.ledType == 0) {
    if (!sigPinOk(n.ledG) || !sigPinOk(n.ledR)) return fail("Цей вивід для світлодіода не підходить — виберіть зі списку");
    if (n.ledG == n.ledR) return fail("Зелений і червоний — на різних виводах");
    ia = n.ledG; ib = n.ledR;
  }
  if (n.ledOn && n.ledType == 1) {
    if (!sigPinOk(n.stripPin)) return fail("Цей вивід для стрічки не підходить — виберіть зі списку");
    ia = n.stripPin;
  }
  if (n.sndType == 1 || n.sndType == 2) {
    if (!sigPinOk(n.sndPin)) return fail("Цей вивід для бузера не підходить — виберіть зі списку");
    if (n.sndPin == ia || n.sndPin == ib) return fail("Бузер та індикатор — на різних виводах");
  }
  if (n.sndType == 3 && (n.dacPin == ia || n.dacPin == ib)) return fail("Вивід ЦАП зайнятий індикатором — виберіть інший");
  if (n.lcdOn) {                                         // дисплей: 5 різних виводів (+ підсвітка), не зайняті іншим
    int lp[6] = {n.lcdRst, n.lcdCe, n.lcdDc, n.lcdDin, n.lcdClk, n.lcdBl};
    const char* nm[6] = {"RST", "CE", "DC", "DIN", "CLK", "підсвітки"};
    for (int i = 0; i < 6; i++) {
      if (i == 5 && !lp[i]) continue;
      if (!sigPinOk(lp[i])) return fail(String("Вивід ") + nm[i] + " дисплея не підходить — виберіть зі списку");
      for (int k = 0; k < i; k++) if (lp[k] == lp[i]) return fail(String("Дисплей: ") + nm[k] + " і " + nm[i] + " — на різних виводах");
      if (lp[i] == ia || lp[i] == ib) return fail(String("Вивід ") + nm[i] + " дисплея зайнятий індикатором");
      if ((n.sndType == 1 || n.sndType == 2) && lp[i] == n.sndPin) return fail(String("Вивід ") + nm[i] + " дисплея зайнятий бузером");
      if (n.sndType == 3 && lp[i] == n.dacPin) return fail(String("Вивід ") + nm[i] + " дисплея зайнятий ЦАП");
    }
  }
  if (sigUses(n, S.pwBtn))
    return fail(String("GPIO") + S.pwBtn + " — кнопка скидання пароля. Виберіть для сигналу інший вивід або перенесіть кнопку: «Налаштування → Пароль сторінки»");
  S = n;
  if (!settingsSave()) return fail("Не вдалося записати налаштування");
  sigReload();
  lcdReload();
  static int64_t lastLog = 0;                      // 1.0.26: сторінка зберігає на льоту — у журнал не частіше разу на хвилину
  if (!lastLog || nowUs() - lastLog > 60LL * 1000000) { lastLog = nowUs(); logEvent("Сигнали: налаштування збережено"); }
  json("{\"ok\":true}");
}
static void hSigTest() {
  if (!auth()) return;
  String w = srv.arg("what");
  int vol = srv.hasArg("vol") ? srv.arg("vol").toInt() : -1;   // перевірка з гучністю повзунка, без запису (1.0.16)
  if (!sigTest(w, vol)) return fail(w == "green" || w == "red" ? "Індикатор вимкнено в налаштуваннях" : w == "flash" || w == "wave" ? "Анімації — лише для адресної стрічки" : "Звук не вибрано — виберіть, що підключено, і збережіть");
  json("{\"ok\":true}");
}
static bool sndAuth = false;
static void hSoundUpload() {
  HTTPUpload& u = srv.upload();
  if (u.status == UPLOAD_FILE_START) {
    sndAuth = authOk();
    if (sndAuth) sigSoundBegin(srv.arg("w").toInt());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (sndAuth) sigSoundWrite(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    sigSoundAbort();
  }
}
static void hSoundDone() {
  if (!sndAuth || !authOk()) return json("{\"err\":\"Потрібен вхід\",\"login\":true}", 401);
  String err;
  if (!sigSoundEnd(err)) return fail(err);
  json("{\"ok\":true}");
}
static void hSoundDel() {
  if (!auth()) return;
  if (!sigSoundDelete(srv.arg("w").toInt())) return fail("Свого звуку тут немає");
  json("{\"ok\":true}");
}

static String restartWhy;
static void scheduleRestart(uint32_t ms, const char* why) { restartPending = true; restartAt = millis() + ms; restartWhy = why; }
static const char* b(bool v) { return v ? "true" : "false"; }

static bool tokenOk(const String& t) {
  int c = t.indexOf(':');
  if (c < 3) return false;
  for (int i = 0; i < c; i++) if (!isdigit((unsigned char)t[i])) return false;
  if ((int)t.length() - c - 1 < 30) return false;
  for (int i = c + 1; i < (int)t.length(); i++) {
    char ch = t[i];
    if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-')) return false;
  }
  return true;
}

static void hIndex() {
  if (!authOk()) return loginPage(200, String());
  srv.sendHeader("Cache-Control", "no-store");
  srv.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

//  1.0.20: стан частинами (chunked), а не одним рядком на 8–16 КБ. 18.11 о 22:09 через віддалену адресу один запит
//  /api/state повернув 200 з порожнім тілом: великий String не склеївся в пам'яті, поділеній шматками (шифроване
//  з'єднання Telegram саме тримало ~47 КБ). Тепер найбільший шматок — журнал, ~6 КБ, і кожен звільняється одразу.
static void hState() {
  if (!auth()) return;
  srv.sendHeader("Cache-Control", "no-store");
  srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv.send(200, "application/json; charset=utf-8", "");
  srv.sendContent("{\"ver\":\"" FW_VERSION "\",\"build\":\"" FW_BUILD "\",\"up\":" + String((long)(nowUs() / 1000000)) +
                  ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"minHeap\":" + String(ESP.getMinFreeHeap()) + ",\"maxBlock\":" + String(ESP.getMaxAllocHeap()) +
                  ",\"reset\":\"" + jsonEsc(resetReasonText()) + "\",\"clock\":\"" + jsonEsc(fmtNowFull()) + "\"" +
                  ",\"you\":\"" + srv.client().remoteIP().toString() + "\"");
  srv.sendContent(",\"watch\":" + watchJson());
  srv.sendContent(",\"net\":" + bridgeJson());
  srv.sendContent(",\"tg\":" + tgJson());
  srv.sendContent(",\"stats\":" + statsJson());
  srv.sendContent(",\"sig\":" + sigJson());
  srv.sendContent(",\"sch\":" + schJson());
  srv.sendContent(",\"tls\":" + tlsPoolJson());          // 1.0.32: запас пам'яті для TLS
  srv.sendContent(",\"log\":[");                         // 1.0.32: по одному запису — без рядка на 10–20 КБ
  for (int i = 0; i < 60; i++) {
    String e = logEntryJson(i);
    if (!e.length()) break;
    srv.sendContent(i ? "," + e : e);
  }
  srv.sendContent("]}");
  srv.sendContent("");
}

static void hSettingsGet() {
  if (!auth()) return;
  String hint = S.tgToken.length() ? S.tgToken.substring(0, S.tgToken.indexOf(':') > 0 ? S.tgToken.indexOf(':') : 0) + ":…" : String();
  String o = "{\"obj\":\"" + jsonEsc(S.objName) + "\",\"staSsid\":\"" + jsonEsc(S.staSsid) + "\",\"staPassSet\":" + b(S.staPass.length()) + ",\"staBssid\":\"" + jsonEsc(S.staBssid) + "\"" +
             ",\"apSsid\":\"" + jsonEsc(S.apSsid) + "\",\"tgTokenSet\":" + b(S.tgToken.length()) + ",\"tgTokenHint\":\"" + jsonEsc(hint) +
             "\",\"tgChats\":\"" + jsonEsc(S.tgChats) + "\",\"avrMac\":\"" + jsonEsc(S.avrMac) + "\",\"mayakOn\":" + b(S.mayakOn) +
             ",\"notifyStart\":" + b(S.notifyStart) + ",\"offSec\":" + String(S.offSec) + ",\"onSec\":" + String(S.onSec) + ",\"glitchSec\":" + String(S.glitchSec) +
             ",\"lineOffSec\":" + String(S.lineOffSec) + ",\"lineOnSec\":" + String(S.lineOnSec) + ",\"lineWarnSec\":" + String(S.lineWarnSec) + ",\"genRemMin\":" + String(S.genRemMin) +
             ",\"sumOn\":" + b(S.sumOn) + ",\"sumHour\":" + String(S.sumHour) +
             ",\"adminDefault\":" + b(S.adminPass == defaultPass()) + ",\"pwBtn\":" + String(S.pwBtn) +
             ",\"defPass\":\"" + jsonEsc(defaultPass()) + "\",\"defAp\":\"" + jsonEsc(defaultApSsid()) + "\"" +
             ",\"schOn\":" + b(S.schOn) + ",\"schDso\":\"" + String(S.schRegion) + "/" + String(S.schDso) + "\",\"schQueue\":\"" + jsonEsc(S.schQueue) +
             "\",\"schWarn\":" + String(S.schWarn) + ",\"schTom\":" + b(S.schTom) +
             ",\"tgNF\":" + String(S.tgNightFrom) + ",\"tgNT\":" + String(S.tgNightTo) + ",\"chats\":[";
  {
    Chat c[MAXR];
    int k = parseChats(S.tgChats, c, MAXR);
    for (int i = 0; i < k; i++) o += String(i ? "," : "") + "{\"id\":\"" + String(c[i].id) + "\",\"name\":\"" + jsonEsc(c[i].name) + "\",\"fl\":" + String(c[i].fl) + "}";
  }
  o += "]}";
  json(o);
}

static bool num(const char* k, uint16_t& dst, int lo, int hi, const char* what) {
  if (!srv.hasArg(k)) return true;
  String v = srv.arg(k);
  v.trim();
  long x = v.toInt();
  if (!v.length() || x < lo || x > hi) { fail(String(what) + ": від " + lo + " до " + hi + " с"); return false; }
  dst = (uint16_t)x;
  return true;
}

static void hSettingsPost() {
  if (!auth()) return;
  Settings n = S;
  if (srv.hasArg("obj")) { n.objName = srv.arg("obj"); n.objName.trim(); if (!n.objName.length()) n.objName = "Церква"; if (n.objName.length() > 80) return fail("Назва задовга"); }
  if (srv.hasArg("staSsid")) { n.staSsid = srv.arg("staSsid"); n.staSsid.replace("\r", ""); n.staSsid.replace("\n", ""); if (n.staSsid.length() > 32) return fail("Назва мережі роутера — до 32 байтів"); }
  if (srv.hasArg("staBssid")) {
    String m = srv.arg("staBssid");
    m.trim();
    uint8_t x[6];
    if (m.length() && !strToMac(m, x)) return fail("Точка роутера (BSSID) має вигляд aa:bb:cc:dd:ee:ff або порожньо");
    n.staBssid = m.length() ? macToStr(x) : String();
  }
  if (srv.arg("staPass").length()) { n.staPass = srv.arg("staPass"); if (n.staPass.length() < 8 || n.staPass.length() > 63) return fail("Пароль роутера — від 8 до 63 символів"); }
  if (srv.hasArg("apSsid")) { n.apSsid = srv.arg("apSsid"); n.apSsid.trim(); if (!n.apSsid.length() || n.apSsid.length() > 32) return fail("Назва точки — від 1 до 32 байтів"); }
  if (srv.arg("apPass").length()) { n.apPass = srv.arg("apPass"); if (n.apPass.length() < 8 || n.apPass.length() > 63) return fail("Пароль точки — від 8 до 63 символів"); }
  if (srv.arg("adminPass").length()) { n.adminPass = srv.arg("adminPass"); if (n.adminPass.length() < 6) return fail("Пароль сторінки — щонайменше 6 символів"); }
  if (srv.arg("tgToken").length()) { n.tgToken = srv.arg("tgToken"); n.tgToken.trim(); if (!tokenOk(n.tgToken)) return fail("Токен бота має вигляд 123456789:AA… (з повідомлення @BotFather)"); }
  if (srv.hasArg("tgChats")) { Chat c[MAXR]; int k = parseChats(srv.arg("tgChats"), c, MAXR); n.tgChats = chatsToText(c, k); }
  if (srv.hasArg("avrMac")) {
    String m = srv.arg("avrMac");
    m.trim();
    uint8_t x[6];
    if (m.length() && !strToMac(m, x)) return fail("MAC АВР має вигляд aa:bb:cc:dd:ee:ff");
    n.avrMac = m.length() ? macToStr(x) : String();
    if (n.avrMac.length() && n.avrMac != S.avrMac && isRequesterDevice(x)) return fail(OWN_DEVICE_ERR);
  }
  if (srv.hasArg("mayakOn")) n.mayakOn = srv.arg("mayakOn") == "1";
  if (srv.hasArg("notifyStart")) n.notifyStart = srv.arg("notifyStart") == "1";
  if (srv.hasArg("sumOn")) n.sumOn = srv.arg("sumOn") == "1";
  if (srv.hasArg("sumHour")) { long h = srv.arg("sumHour").toInt(); if (h < 0 || h > 23) return fail("Година підсумку — від 0 до 23"); n.sumHour = (uint8_t)h; }
  if (!num("offSec", n.offSec, 5, 600, "Затримка «міста немає»")) return;
  if (!num("onSec", n.onSec, 3, 300, "Затримка «місто є»")) return;
  if (!num("glitchSec", n.glitchSec, 20, 900, "Обрив зв'язку з АВР")) return;
  if (!num("lineOffSec", n.lineOffSec, 5, 600, "Затримка «лінія без напруги»")) return;
  if (!num("lineOnSec", n.lineOnSec, 3, 300, "Затримка «лінія під напругою»")) return;
  if (!num("lineWarnSec", n.lineWarnSec, 0, 3600, "Попередження про лінію")) return;
  if (srv.hasArg("tgNF") || srv.hasArg("tgNT")) {        // нічні години без звуку (1.0.21)
    long a = srv.hasArg("tgNF") ? srv.arg("tgNF").toInt() : n.tgNightFrom, z = srv.hasArg("tgNT") ? srv.arg("tgNT").toInt() : n.tgNightTo;
    if (a < 0 || a > 23 || z < 0 || z > 23) return fail("Нічні години — від 0 до 23");
    n.tgNightFrom = (uint8_t)a;
    n.tgNightTo = (uint8_t)z;
  }
  if (srv.hasArg("genRemMin")) { long v = srv.arg("genRemMin").toInt(); if (v < 0 || v > 240) return fail("Нагадування про генератор — від 0 до 240 хв"); n.genRemMin = (uint16_t)v; }
  if (srv.hasArg("pwBtn")) {
    long v = srv.arg("pwBtn").toInt();
    if (v != 0 && (v == 2 || !sigPinOk(v))) return fail("Вивід кнопки скидання пароля — виберіть зі списку");
    if (const char* u = sigUses(n, v)) return fail(String("GPIO") + v + " уже зайнятий " + u + " (вкладка «Сигнали») — виберіть для кнопки інший вивід");
    n.pwBtn = (uint8_t)v;
  }

  //  Графік відключень YASNO (1.0.20)
  if (srv.hasArg("schOn")) n.schOn = srv.arg("schOn") == "1";
  if (srv.hasArg("schTom")) n.schTom = srv.arg("schTom") == "1";
  if (srv.hasArg("schDso")) {
    String v = srv.arg("schDso");
    if (v == "3/301") { n.schRegion = 3; n.schDso = 301; }
    else if (v == "3/303") { n.schRegion = 3; n.schDso = 303; }
    else if (v == "25/902") { n.schRegion = 25; n.schDso = 902; }
    else return fail("Оператор графіка — виберіть зі списку");
  }
  if (srv.hasArg("schQueue")) {
    String q = srv.arg("schQueue");
    q.trim();
    bool okq = !q.length() || (q.length() == 3 && q[0] >= '1' && q[0] <= '6' && q[1] == '.' && (q[2] == '1' || q[2] == '2'));
    if (!okq) return fail("Черга — від 1.1 до 6.2");
    n.schQueue = q;
  }
  if (n.schOn && !n.schQueue.length()) return fail("Щоб брати графік, виберіть свою чергу");
  if (srv.hasArg("schWarn")) { long v = srv.arg("schWarn").toInt(); if (v < 0 || v > 180) return fail("Попередження — від 0 до 180 хв"); n.schWarn = (uint8_t)v; }

  bool wifi = n.staSsid != S.staSsid || n.staPass != S.staPass || n.staBssid != S.staBssid || n.apSsid != S.apSsid || n.apPass != S.apPass;
  bool newPass = n.adminPass != S.adminPass;
  S = n;
  if (!settingsSave()) return fail("Не вдалося записати налаштування");
  if (newPass) {                        // новий пароль сторінки — усі входи недійсні; хто зберігав, лишається
    bool mine = authSess >= 0;
    sessClearAll();
    if (mine) sessCookie(sessNew());
  }
  tgReload();
  schReload();
  logEvent(wifi ? "Налаштування збережено, перезапуск для Wi-Fi" : "Налаштування збережено");
  if (wifi) scheduleRestart(1500, "зміна Wi-Fi у налаштуваннях");
  json(String("{\"ok\":true,\"restart\":") + b(wifi) + "}");
}

static void hAssign() {
  if (!auth()) return;
  String m = srv.arg("mac");
  m.trim();
  uint8_t x[6];
  if (m.length() && !strToMac(m, x)) return fail("Хибний MAC");
  if (m.length() && isRequesterDevice(x)) return fail(OWN_DEVICE_ERR);
  if (m.length() && (x[0] & 0x02)) return fail(RANDOM_MAC_ERR);
  S.avrMac = m.length() ? macToStr(x) : String();
  if (!settingsSave()) return fail("Не вдалося записати налаштування");
  logEvent(S.avrMac.length() ? "АВР призначено: " + S.avrMac : String("АВР знято"));
  json("{\"ok\":true}");
}

static void hAddChat() {
  if (!auth()) return;
  String id = srv.arg("id"), name = srv.arg("name");
  id.trim();
  name.replace("\n", " ");
  name.trim();
  Chat c[MAXR];
  int k = parseChats(S.tgChats, c, MAXR);
  long long v = strtoll(id.c_str(), nullptr, 10);
  if (!v) return fail("Хибний chat_id");
  for (int i = 0; i < k; i++) if (c[i].id == v) return json("{\"ok\":true}");
  if (k >= MAXR) return fail("Не більше " + String(MAXR) + " одержувачів");
  c[k].id = v;
  c[k].name = name;
  c[k].fl = RF_DEFAULT | (k == 0 ? RF_ADMIN : 0);     // перший одержувач — адміністратор (1.0.21)
  S.tgChats = chatsToText(c, k + 1);
  if (!settingsSave()) return fail("Не вдалося записати налаштування");
  tgReload();
  logEvent("Додано одержувача " + id + " " + name);
  if (S.tgToken.length()) tgWelcome(v);
  json("{\"ok\":true}");
}

static void hDelChat() {               // 1.0.4: прибрати одержувача кнопкою на вкладці «Сповіщення»
  if (!auth()) return;
  long long v = strtoll(srv.arg("id").c_str(), nullptr, 10);
  Chat c[MAXR];
  int k = parseChats(S.tgChats, c, MAXR), m = 0;
  String name;
  for (int i = 0; i < k; i++) {
    if (c[i].id == v) { name = c[i].name; continue; }
    c[m++] = c[i];
  }
  if (!v || m == k) return fail("Такого одержувача немає");
  S.tgChats = chatsToText(c, m);
  if (!settingsSave()) return fail("Не вдалося записати налаштування");
  tgReload();
  logEvent("Прибрано одержувача " + String(v) + " " + name);
  if (S.tgToken.length()) tgFarewell(v);
  json("{\"ok\":true}");
}

//  Що отримує одержувач (1.0.21): позначки RF_* з сторінки, «Сповіщення» — кому що надсилати.
static void hChatFlags() {
  if (!auth()) return;
  long long v = strtoll(srv.arg("id").c_str(), nullptr, 10);
  long f = srv.arg("fl").toInt();
  if (!v || f < 0 || f > 0x1FF) return fail("Хибні дані");
  Chat c[MAXR];
  int k = parseChats(S.tgChats, c, MAXR), i = -1;
  for (int j = 0; j < k; j++) if (c[j].id == v) i = j;
  if (i < 0) return fail("Такого одержувача немає");
  uint16_t was = c[i].fl;
  c[i].fl = (uint16_t)f;
  S.tgChats = chatsToText(c, k);
  if (!settingsSaveChats()) return fail("Не вдалося записати налаштування");
  tgReload();
  logEvent("Сповіщення «" + (c[i].name.length() ? c[i].name : String(v)) + "»: позначки " + String(was, HEX) + " → " + String(c[i].fl, HEX) + " (зі сторінки)");
  json("{\"ok\":true}");
}

//  Маячки (1.0.23): додати, змінити роль чи назву, прибрати. У флеш — лише список маячків і mayakOn (дія власника).
static void hBeacon() {
  if (!auth()) return;
  uint8_t m[6];
  String mac = srv.arg("mac");
  mac.trim();
  if (!strToMac(mac, m)) return fail("MAC-адреса маячка — у вигляді aa:bb:cc:dd:ee:ff");
  String act = srv.arg("act"), role = srv.arg("role"), name = srv.arg("name");
  name.replace("\r", " ");
  name.replace("\n", " ");
  name.trim();
  if (name.length() > 64) return fail("Назва маячка задовга: до 24 літер");
  String err;
  bool del = act == "del";
  if (!watchBeaconEdit(m, del ? 'd' : 'a', role.length() ? role[0] : 0, name, err)) return fail(err);
  if (!settingsSaveBeacons()) return fail("Не вдалося записати налаштування");
  logEvent("Маячок " + macToStr(m) + (del ? String(" прибрано зі списку") : " — " + String(role == "g" ? "генератор" : role == "l" ? "лінія" : "лише показувати") +
                                       (name.length() ? ", «" + name + "»" : String())) + " (зі сторінки)");
  json("{\"ok\":true}");
}

//  Оголошення зі сторінки (1.0.26): усім, у кого ввімкнено сповіщення, або вибраним; за бажанням — і на дисплей
static void hAnnounce() {
  if (!auth()) return;
  String t = srv.arg("text");
  t.trim();
  if (!t.length()) return fail("Напишіть текст оголошення");
  if (t.length() > 3000) return fail("Оголошення задовге: до 1000 знаків");
  bool silent = srv.arg("silent") == "1", lcd = srv.arg("lcd") == "1";
  String to = srv.arg("to");
  long long ids[MAXR];
  int n = -1;
  if (to != "all") {
    n = 0;
    int from = 0;
    while (from < (int)to.length() && n < MAXR) {
      int c = to.indexOf(',', from);
      if (c < 0) c = to.length();
      long long id = strtoll(to.substring(from, c).c_str(), nullptr, 10);
      if (id) ids[n++] = id;
      from = c + 1;
    }
    if (!n) return fail("Виберіть, кому надіслати");
  }
  if (!S.tgToken.length() && !lcd) return fail("Бот не підключено — оголошення нікуди надіслати");
  int sent = S.tgToken.length() ? tgAnnounce(t, String(), ids, n, silent) : 0;
  if (lcd) lcdAlert(LCD_A_ANN, lcdSafe(t));
  json("{\"ok\":true,\"n\":" + String(sent) + ",\"lcd\":" + (lcd && S.lcdAnnMin ? "true" : "false") + "}");
}

static void hTest() {
  if (!auth()) return;
  if (!S.tgToken.length()) return fail("Спершу збережіть токен бота на вкладці «Сповіщення»");
  Chat c[MAXR];
  int k = parseChats(S.tgChats, c, MAXR);
  long long one = srv.hasArg("id") ? strtoll(srv.arg("id").c_str(), nullptr, 10) : 0;
  if (one) {                            // перевірка одному одержувачеві (кнопка «Перевірити»)
    bool known = false;
    for (int i = 0; i < k; i++) if (c[i].id == one) known = true;
    if (!known) return fail("Такого одержувача немає");
    tgReply(one, testMessage(), 1);
    return json("{\"ok\":true}");
  }
  if (!k) return fail("Спершу додайте одержувачів на вкладці «Сповіщення»");
  if (!tgBroadcast(nowUs(), testMessage(), false, 0))
    return fail("Немає одержувачів або токена");
  json("{\"ok\":true}");
}

static void hScan() {
  if (!auth()) return;
  if (srv.hasArg("start")) scanStart();
  json(scanJson());
}

//  Скидання зі сторінки (1.0.15, власник: «функция сброса настроек в веб»).
//   prefs — типові назва, затримки, маячок, підсумок, сигнали, кнопка пароля; Wi-Fi, точка для АВР, АВР, бот з
//           одержувачами й пароль сторінки лишаються, тож Сторож працює далі без перезапуску;
//   all   — усе, як BOOT 10 с, лише зі словом «СКИНУТИ»; перед тим одержувачам — «⚠️ Сторожа скинуто…»,
//           свої звуки видаляються, перезапуск за 10 с (час надіслати повідомлення).
static void hReset() {
  if (!auth()) return;
  if (restartPending) return fail("Сторож уже перезапускається — зачекайте");
  String w = srv.arg("what");
  if (w == "prefs") {
    if (!settingsResetPrefs()) return fail("Не вдалося скинути налаштування");
    sigReload();
    lcdReload();
    tgReload();
    schReload();
    logEvent("Налаштування скинуто до типових зі сторінки (Wi-Fi, АВР, бот і пароль — без змін)");
    return json("{\"ok\":true}");
  }
  if (w == "all") {
    if (srv.arg("confirm") != "СКИНУТИ") return fail("Щоб скинути все, введіть слово СКИНУТИ");
    String who = srv.client().remoteIP().toString();
    tgBroadcast(nowUs(), factoryMessage(who), false, 0);
    sigSoundDelete(0);
    sigSoundDelete(1);
    settingsFactoryReset();
    sessClearAll();
    logEvent("Скидання до заводських зі сторінки (" + who + "): усі налаштування стерто, перезапуск за 10 с");
    scheduleRestart(10000, "скидання до заводських зі сторінки");
    return json("{\"ok\":true,\"restart\":true}");
  }
  fail("Невідома дія");
}

static void hReboot() {
  if (!auth()) return;
  logEvent("Перезапуск зі сторінки");
  scheduleRestart(800, "перезапуск зі сторінки");
  json("{\"ok\":true}");
}

static void hUpdateDone() {
  if (!upAuth) { srv.send(401, "text/plain; charset=utf-8", "Потрібен вхід: оновіть сторінку й увійдіть"); return; }
  if (!upChipOk) { srv.send(400, "text/plain; charset=utf-8", "Це прошивка не для Сторожа світла (інший чип або не прошивка)"); return; }
  bool ok = !Update.hasError();
  srv.send(ok ? 200 : 500, "text/plain; charset=utf-8", ok ? "Готово. Сторож перезапускається, сторінка повернеться за 20–30 секунд." : String("Помилка оновлення: ") + Update.errorString());
  if (ok) scheduleRestart(1500, "оновлення прошивки");
}

static void hUpload() {
  HTTPUpload& u = srv.upload();
  if (u.status == UPLOAD_FILE_START) {
    upAuth = authOk();
    upChipOk = true;
    if (!upAuth) return;
    logEvent("Оновлення прошивки: " + u.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!upAuth || !upChipOk) return;
    if (u.totalSize == 0) {                       // перший шматок: заголовок образу ESP
      uint16_t chip = u.currentSize > 13 ? (uint16_t)(u.buf[12] | (u.buf[13] << 8)) : 0xFFFF;
      if (u.buf[0] != 0xE9 || chip != 0x0000) { upChipOk = false; Update.abort(); logEvent("Оновлення відхилено: не прошивка ESP32"); return; }
    }
    if (!Update.hasError() && Update.write(u.buf, u.currentSize) != u.currentSize) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_END) {
    if (!upAuth || !upChipOk) return;
    if (Update.end(true)) logEvent("Прошивку записано: " + String(u.totalSize) + " байтів");
    else Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void webBegin() {
  if (int n = sessRestore()) logEvent("Входи на сторінку пережили перезапуск: " + String(n));
  static const char* hdr[] = {"Cookie", "Origin", "X-Forwarded-For", "X-Real-IP"};
  srv.collectHeaders(hdr, 4);            // Authorization і Host сервер збирає й так
  srv.on("/", HTTP_GET, hIndex);
  srv.on("/login", HTTP_GET, hLoginGet);
  srv.on("/login", HTTP_POST, hLogin);
  srv.on("/logout", HTTP_POST, hLogout);
  srv.on("/api/state", HTTP_GET, hState);
  //  Без пароля: лише «живий» і версія — для перевірки з мережі й стороннього нагляду.
  srv.on("/api/ping", HTTP_GET, []() { json("{\"ok\":true,\"ver\":\"" FW_VERSION "\",\"up\":" + String((long)(nowUs() / 1000000)) + "}"); });
  srv.on("/api/settings", HTTP_GET, hSettingsGet);
  srv.on("/api/settings", HTTP_POST, hSettingsPost);
  srv.on("/api/assign", HTTP_POST, hAssign);
  srv.on("/api/addchat", HTTP_POST, hAddChat);
  srv.on("/api/delchat", HTTP_POST, hDelChat);
  srv.on("/api/chatflags", HTTP_POST, hChatFlags);
  srv.on("/api/beacon", HTTP_POST, hBeacon);
  srv.on("/api/announce", HTTP_POST, hAnnounce);
  srv.on("/api/lcdfx", HTTP_POST, []() {      // пробний ефект на дисплеї (1.0.26): 1 — зникло, 2 — повернулось, 4 — попередження, 5 — оголошення
    if (!auth()) return;
    int k = srv.arg("kind").toInt();
    if (k == 4) lcdAlert(LCD_A_WARN, "УВАГА: перевірка — так виглядає попередження про відключення за графіком", 60);
    else if (k == 5) lcdAlert(LCD_A_ANN, "Пробне оголошення: так на дисплеї виглядатиме текст, який ви розішлете", 60);
    else if (k >= 1 && k <= 3) lcdAlert(k);
    else return fail("Невідомий ефект");
    json("{\"ok\":true}");
  });
  srv.on("/schemes", HTTP_GET, []() {     // схеми підключення (1.0.23): вкладка «Інструкція» і окремо для друку
    if (!authOk()) return loginPage(200, String());
    srv.sendHeader("Cache-Control", "no-store");
    srv.send_P(200, "text/html; charset=utf-8", SCHEMES_HTML);
  });
  srv.on("/api/botphoto", HTTP_POST, hPhotoDone, hPhotoUpload);
  srv.on("/api/botname", HTTP_POST, hBotName);
  srv.on("/api/signals", HTTP_GET, hSigGet);
  srv.on("/api/signals", HTTP_POST, hSigPost);
  srv.on("/api/sigtest", HTTP_POST, hSigTest);
  srv.on("/api/sound", HTTP_POST, hSoundDone, hSoundUpload);
  srv.on("/api/sounddel", HTTP_POST, hSoundDel);
  srv.on("/api/test", HTTP_POST, hTest);
  srv.on("/api/scan", HTTP_GET, hScan);
  srv.on("/api/reboot", HTTP_POST, hReboot);
  srv.on("/api/reset", HTTP_POST, hReset);
  srv.on("/api/lcd", HTTP_GET, []() { if (!auth()) return; json(lcdJson()); });   // що зараз на дисплеї (1.0.22)
  srv.on("/api/lcdwire", HTTP_POST, []() { if (!auth()) return; json(lcdWireCheck()); });   // перевірка дротів дисплея (1.0.25)
  srv.on("/api/lcdtest", HTTP_POST, []() {   // перевірка дисплея (1.0.24): лише в пам'яті, без запису у флеш
    if (!auth()) return;
    String m = srv.arg("mode");
    if (m == "end") { lcdTestEnd(); return json("{\"ok\":true}"); }
    int mode = m == "all" ? 1 : m == "check" ? 2 : m == "normal" ? 0 : m == "sweep" ? 3 : -1;
    auto iv = [](const char* k) { return srv.hasArg(k) && srv.arg(k).length() ? (int)srv.arg(k).toInt() : -1; };
    if (!lcdTest(iv("vop"), iv("bri"), iv("bllow"), mode, iv("bias"))) return fail("Дисплей не ввімкнено: поставте «Дисплей під'єднано» і натисніть «Зберегти»");
    json("{\"ok\":true}");
  });
  srv.on("/api/schfetch", HTTP_POST, []() { if (!auth()) return; schKick(); json("{\"ok\":true}"); });   // «Оновити графік зараз»
  srv.on("/update", HTTP_POST, hUpdateDone, hUpload);
  srv.onNotFound([]() { srv.send(404, "text/plain; charset=utf-8", "Немає такої сторінки"); });
  srv.begin();
}

void webLoop() {
  srv.handleClient();
  if (restartPending && (int32_t)(millis() - restartAt) >= 0) restartWith(restartWhy, true);   // дія людини
}
