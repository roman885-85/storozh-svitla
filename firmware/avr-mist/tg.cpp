#include "tg.h"
#include "settings.h"
#include "bridge.h"
#include "watch.h"
#include "util.h"
#include "stats.h"
#include "certs.h"
#include "yasno.h"
#include "bms.h"
#include "mem.h"
#include "version.h"
#include "lcd.h"
#include <NetworkClientSecure.h>
#include "tlspool.h"
#include <HTTPClient.h>
#include <deque>
#include <initializer_list>
#include <utility>
#include <esp_rom_crc.h>

static const size_t QMAX = 40;
//  kind (1.0.21): 0 — sendMessage, 1 — editMessageText (mid), 2 — answerCallbackQuery (mk — id натискання, text — підказка).
//  mk — розмітка кнопок під повідомленням (inline_keyboard); cat — кому (біти RF_*), щоб перерахувати одержувачів.
struct Item { int64_t evUs; String text; long long chat; uint32_t pending; uint16_t fails; bool silent; uint8_t kb;
              uint16_t cat = 0; uint8_t kind = 0; String mk; int32_t mid = 0;
              uint32_t qEp = 0; int64_t qUs = 0; };   // 1.0.29: коли стало в чергу (с від 1970; мкс цього запуску, 0 — з RTC)
struct Seen { long long id; String name; String text; int64_t us; };

static std::deque<Item> q;
static SemaphoreHandle_t qm = nullptr;
static void stamp(Item& it) { it.qUs = nowUs(); it.qEp = clockValid() ? (uint32_t)time(nullptr) : 0; }   // 1.0.29: коли стало в чергу
static Chat rec[MAXR];
static int nrec = 0;
static const int NSEEN = 10;                          // «Писали боту» — останні 10 чатів, лише в RAM
static Seen seen[NSEEN];
static uint8_t* photoBody = nullptr;                  // картинка бота на відправку (під qm)
static size_t photoLen = 0;
static String photoState;                              // "" / "надсилаю" / "поставлено" / "помилка: …"
static String botName;
static String botTitle, nameWant, nameState;           // назва бота (getMyName) і її зміна зі сторінки (під qm)                                // @ім'я бота з getMe (під qm), для підказок на сторінці
static int nseen = 0;
static volatile int64_t hbUs = 0;
static String lastErr;
static int64_t lastOkUs = 0, lastErrUs = 0;
static uint32_t sentCount = 0, dropped = 0;
static long long updOffset = 0;
static NetworkClientSecure* cli = nullptr;

//  18.11.2026 (1.0.18): о 16:20 «✅ Генератор вимкнено» стало в чергу, а Telegram з того часу на кожен запит мовчав
//  (-11, відповідь не прийшла за 15 с), хоча інтернет на об'єкті був. Черга — лише в RAM, тож перезапуск її губив.
//  Тепер: (1) після двох невдач поспіль — новий об'єкт з'єднання замість старого; (2) черга дзеркалиться в пам'ять RTC,
//  що переживає перезапуск і оновлення (як облік світла); (3) Telegram працював, а тепер 10 хв мовчить — перезапуск.
static volatile bool wakeSend = false;               // 1.0.29: Telegram знову відповів — чергу надсилати одразу, не чекати паузи
static volatile int consecFail = 0;                   // невдач поспіль (будь-який запит: надсилання, опитування, getMe; усе, крім 200)
static volatile int64_t lastAnyOkUs = 0, failSinceUs = 0, lastRespUs = 0;   // lastRespUs — будь-яка відповідь HTTP, навіть 502
//  1.0.29: рукостискання TLS у ядрі типово чекає до 120 с — на поганому інтернеті задача стояла хвилинами. Тепер 15 с.
static NetworkClientSecure* newClient() {
  NetworkClientSecure* c = new NetworkClientSecure();
  c->setCACert(TG_ROOT_CA);
  c->setHandshakeTimeout(15);
  return c;
}
//  1.0.32 (порівняння з Home Assistant). Home Assistant
//  (python-telegram-bot) тримає з'єднання з Telegram відкритим і шле ним багато запитів. Сторож же на кожен запит (~6 на
//  хвилину) робив нове рукопожаття TLS — щоразу шукав у подрібненій пам'яті два шматки по ~16,7 КБ. Тепер — одне з'єднання
//  надовго (keep-alive): HTTPClient не локальний, бо його деструктор закриває з'єднання. Буфери — із запасу (tlspool.h).
static HTTPClient* hc = nullptr;
static void dropConn() { if (hc) { delete hc; hc = nullptr; } }   // деструктор кличе cli->stop() — буфери TLS звільнено
static void resetClient(bool log) {
  dropConn();
  if (cli) { cli->stop(); delete cli; }
  cli = newClient();
  if (log) logEvent("Telegram: з'єднання не відповідає — створюю нове");
}
//  Перед графіком YASNO: його з'єднанню потрібен той самий запас пам'яті. Та сама задача — без замків.
void tgNetPause() {
  dropConn();
  if (cli) cli->stop();
}

struct RtcQ { uint32_t magic, len, crc; uint8_t buf[3072]; };
RTC_NOINIT_ATTR static RtcQ RQ;                       // після stats.cpp у збірці — адреса обліку світла не зсувається
static const uint32_t RQ_MAGIC = 0x32514754;          // «TGQ2» (1.0.29: + коли стало в чергу; старий формат «TGQ1» відкидається)
//  Під qm. Час події (%T) вписується в текст одразу: після перезапуску лічильник мікросекунд починається з нуля.
static void rtcSave() {
  size_t used = 0;
  for (auto& it : q) {
    if (it.kind || it.mk.length()) continue;          // правки й відповіді на кнопки після перезапуску вже не потрібні
    String t = it.text;
    if (clockValid()) t.replace("%T", fmtClock(it.evUs));
    size_t L = t.length(), need = 20 + L;
    if (L > 2000 || used + need > sizeof RQ.buf) break;
    uint8_t* e = RQ.buf + used;
    e[0] = L & 0xFF; e[1] = L >> 8; e[2] = it.silent; e[3] = it.kb;
    memcpy(e + 4, &it.pending, 4);
    memcpy(e + 8, &it.chat, 8);
    memcpy(e + 16, &it.qEp, 4);
    memcpy(e + 20, t.c_str(), L);
    used += need;
  }
  RQ.len = used;
  RQ.crc = esp_rom_crc32_le(0, RQ.buf, used);
  RQ.magic = RQ_MAGIC;
}
static int rtcRestore() {                             // під qm, до запуску задачі
  if (RQ.magic != RQ_MAGIC || RQ.len > sizeof RQ.buf || RQ.crc != esp_rom_crc32_le(0, RQ.buf, RQ.len)) { RQ.magic = 0; return 0; }
  size_t used = 0;
  int n = 0;
  while (used + 20 <= RQ.len) {
    const uint8_t* e = RQ.buf + used;
    size_t L = e[0] | (e[1] << 8);
    if (used + 20 + L > RQ.len) break;
    Item it;
    it.text.concat((const char*)(e + 20), L);
    memcpy(&it.qEp, e + 16, 4);
    it.qUs = 0;
    it.silent = e[2];
    it.kb = e[3];
    memcpy(&it.pending, e + 4, 4);
    memcpy(&it.chat, e + 8, 8);
    it.evUs = nowUs() + n;                            // %T вже вписано; +n — щоб записи черги відрізнялись
    it.fails = 0;
    if (q.size() < QMAX) { q.push_back(it); n++; }
    used += 20 + L;
  }
  return n;
}
static String tok;                                    // копія S.tgToken для задачі (під qm)
struct In { long long chat; String name; String text; long long date; String cb; long long from = 0; int32_t mid = 0; };
static std::deque<In> inbox;                          // вхідні від задачі до loop() (під qm)

int64_t tgHeartbeatUs() { return hbUs; }

//  1.0.18–1.0.28 тут був tgStuck(): «Telegram 10 хв мовчить, черга не порожня — перезапуск». 20.11.2026 о 16:19 він
//  перезапустив Сторож через секунду після «Зникло міське світло»: інтернету на об'єкті не було, а перезапуск його не
//  повертає. Прибрано (1.0.29): черга чекає в RTC, завислий клієнт замінює resetClient().

//  Під qm. Кому з одержувачів іде повідомлення цього виду (1.0.21).
static uint32_t maskFor(uint16_t cat) {
  uint32_t m = 0;
  for (int i = 0; i < nrec; i++) {
    uint16_t f = rec[i].fl;
    bool want = ((f & RF_ON) && (!(cat & RF_CATS) || (f & cat & RF_CATS))) || ((cat & C_ADMIN) && (f & RF_ADMIN));
    if ((cat & RF_CATS) == 0 && (cat & C_ADMIN)) want = f & RF_ADMIN;       // лише адміністраторам
    if (want) m |= 1u << i;
  }
  return m;
}
static bool nightNow() {
  if (!clockValid() || S.tgNightFrom == S.tgNightTo) return false;
  time_t n = time(nullptr);
  struct tm t;
  localtime_r(&n, &t);
  int h = t.tm_hour;
  return S.tgNightFrom < S.tgNightTo ? (h >= S.tgNightFrom && h < S.tgNightTo) : (h >= S.tgNightFrom || h < S.tgNightTo);
}

void tgReload() {
  xSemaphoreTake(qm, portMAX_DELAY);
  static bool first = true;
  Chat old[MAXR];
  int nold = nrec;
  for (int i = 0; i < nold; i++) old[i] = rec[i];
  nrec = parseChats(S.tgChats, rec, MAXR);
  if (tok != S.tgToken) botName = "";
  tok = S.tgToken;
  bool same = nold == nrec;
  for (int i = 0; same && i < nrec; i++) same = old[i].id == rec[i].id;
  //  Одержувачі змінились — розіслати чергу всім заново. Перше читання після запуску — не зміна: черга з RTC (1.0.18)
  //  пам'ятає, кому вже доставлено, і повторювати їм не треба (1.0.19).
  if (!same && !first)
    for (auto& it : q) if (!it.chat) it.pending = maskFor(it.cat);
  first = false;
  rtcSave();
  xSemaphoreGive(qm);
}

bool tgBroadcast(int64_t evUs, const String& text, bool silent, uint16_t cat) {
  String t = text;
  t.replace("%T", fmtClock(evUs));
  bool queued = false, nobody = false;
  xSemaphoreTake(qm, portMAX_DELAY);
  if (S.tgToken.length() && nrec > 0) {
    uint32_t m = maskFor(cat);
    if (!m) nobody = true;
    else {
      if (q.size() >= QMAX) { q.pop_front(); dropped++; }
      Item it{evUs, text, 0, m, 0, silent, 0};
      it.cat = cat;
      stamp(it);
      q.push_back(it);
      queued = true;
      rtcSave();
    }
  }
  xSemaphoreGive(qm);
  logEvent((queued ? "До Telegram: " : nobody ? "Нікому не надсилаю (вимкнено в налаштуваннях одержувачів): " : "Telegram не налаштовано, не надіслано: ") + stripTags(t));
  return queued;
}

//  Повідомлення з кнопками під ним, правка свого повідомлення, відповідь на натискання (1.0.21).
static void qPush(Item&& it) {
  stamp(it);
  xSemaphoreTake(qm, portMAX_DELAY);
  if (q.size() >= QMAX) { q.pop_front(); dropped++; }
  q.push_back(std::move(it));
  xSemaphoreGive(qm);
}
static void replyMk(long long chat, const String& text, const String& mk) {
  Item it{nowUs(), text, chat, 1, 0, false, 0};
  it.mk = mk;
  qPush(std::move(it));
}
static void editMk(long long chat, int32_t mid, const String& text, const String& mk) {
  Item it{nowUs(), text, chat, 1, 0, false, 0};
  it.kind = 1; it.mid = mid; it.mk = mk;
  qPush(std::move(it));
}
static void answerCb(const String& cbId, const String& toast) {
  Item it{nowUs(), toast, 1, 1, 0, false, 0};
  it.kind = 2; it.mk = cbId;
  qPush(std::move(it));
}

void tgReply(long long chat, const String& text, uint8_t kb) {
  xSemaphoreTake(qm, portMAX_DELAY);
  if (q.size() >= QMAX) { q.pop_front(); dropped++; }
  Item it{nowUs(), text, chat, 1, 0, false, kb};
  stamp(it);
  q.push_back(it);
  rtcSave();
  xSemaphoreGive(qm);
}

// ---------- малий розбір JSON (відповіді Telegram прості) ----------
static void appendUtf8(String& o, uint32_t cp) {
  char b[5] = {0};
  if (cp < 0x80) b[0] = (char)cp;
  else if (cp < 0x800) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); }
  else if (cp < 0x10000) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); }
  else { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); }
  o += b;
}
static String jStrAt(const String& s, int i) {
  String o;
  int L = s.length();
  while (i < L) {
    char c = s[i++];
    if (c == '"') break;
    if (c != '\\') { o += c; continue; }
    if (i >= L) break;
    char e = s[i++];
    if (e == 'n') o += '\n';
    else if (e == 't') o += '\t';
    else if (e == 'r' || e == 'b' || e == 'f') {}
    else if (e == 'u') {
      if (i + 4 > L) break;
      uint32_t cp = strtoul(s.substring(i, i + 4).c_str(), nullptr, 16);
      i += 4;
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= L && s[i] == '\\' && s[i + 1] == 'u') {
        uint32_t lo = strtoul(s.substring(i + 2, i + 6).c_str(), nullptr, 16);
        if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
      }
      appendUtf8(o, cp);
    } else o += e;
  }
  return o;
}
static String jStr(const String& s, const char* key, int from = 0) {   // key разом із «"…":"»
  int i = s.indexOf(key, from);
  return i < 0 ? String() : jStrAt(s, i + strlen(key));
}
static long long jNum(const String& s, const char* key, int from = 0) {
  int i = s.indexOf(key, from);
  return i < 0 ? 0 : strtoll(s.c_str() + i + strlen(key), nullptr, 10);
}

// ---------- запити ----------
static int call(const String& token, const char* method, const String& body, String& resp, uint32_t readMs = 15000) {
  int code = -100;
  hbUs = nowUs();                                     // 1.0.29: задача жива — кожен запит теж крок
  if (!hc) { hc = new HTTPClient(); hc->setReuse(true); }
  hc->setConnectTimeout(8000);
  hc->setTimeout(readMs);
  if (hc->begin(*cli, String("https://api.telegram.org/bot") + token + "/" + method)) {
    //  1.0.33: нове з'єднання — лише коли пам'яті досить. 22.11.2026 о 10:44: «X509 — Certificate verification failed»,
    //  хоча сертифікат Telegram правильний: перевірці підпису забракло пам'яті, поки сторінка й Wi-Fi тимчасово її забрали.
    //  Чекаємо до 8 с, поки вільно ≥ 16 КБ і найбільший шматок ≥ 4 КБ. 1.0.38: це справжні цифри (mem.h); старі
    //  «36 і 12 КБ» рахувались разом із ~32 КБ пам'яті команд, тобто насправді вимагали лише ~4 КБ.
    if (!hc->connected()) {
      int64_t t0 = nowUs();
      while ((memFree() < 16000 || memBlock() < 4000) && nowUs() - t0 < 8000000LL) {
        hbUs = nowUs();
        vTaskDelay(pdMS_TO_TICKS(250));
      }
      int64_t w = (nowUs() - t0) / 1000000;
      if (w >= 2) logEvent("Telegram: чекав пам'ять для з'єднання " + String((long)w) + " с (вільно " + String(memFree() / 1024) +
                           " КБ, найбільший шматок " + String(memBlock() / 1024) + " КБ)");
    }
    hc->addHeader("Content-Type", "application/json");
    code = hc->POST(body);
    resp = code > 0 ? hc->getString() : String();
    hc->end();                                        // з'єднання лишається відкритим, якщо Telegram не сказав «закрити»
  }
  if (code < 0) {
    //  1.0.32: чому — у журнал (перша невдача поспіль і кожна десята): помилка TLS і стан пам'яті
    if (consecFail == 0 || (consecFail + 1) % 10 == 0) {
      char eb[96] = "";
      int te = (code == HTTPC_ERROR_CONNECTION_REFUSED && cli) ? cli->lastError(eb, sizeof eb) : 0;
      String why = code == HTTPC_ERROR_CONNECTION_REFUSED ? (te ? "з'єднання не відкрилось: " + String(eb) + " (" + String(te) + ")"
                                                                 : String("з'єднання не відкрилось (адреса чи TCP, без помилки TLS)"))
                 : code == HTTPC_ERROR_READ_TIMEOUT ? String("відповіді немає за ") + String(readMs / 1000) + " с"
                 : code == HTTPC_ERROR_TOO_LESS_RAM ? String("бракує пам'яті") : String("код ") + String(code);
      logEvent("Telegram (" + String(method) + "): " + why + " · пам'ять: вільно " + String(memFree() / 1024) +
               " КБ, найбільший шматок " + String(memBlock() / 1024) + " КБ, запас TLS зайнято " + String(tlsPoolBusy()) + "/2");
    }
    dropConn();                                       // зіпсоване з'єднання вдруге не беремо
  }
  if (code > 0) lastRespUs = nowUs();
  //  Успіх — лише справжня відповідь 200: о 16:29 опитування отримували від api.telegram.org «502 Bad Gateway».
  //  1.0.31: 400 — теж відповідь (перевірка «чи дійшло» правкою чекає саме її): зв'язок є.
  if (code == 200 || code == 400) {
    if (consecFail) wakeSend = true;
    if (consecFail >= 2) logEvent("Telegram: зв'язок відновився (після " + String(consecFail) + " невдач поспіль)");
    consecFail = 0;
    failSinceUs = 0;
    lastAnyOkUs = nowUs();
  } else {
    if (!consecFail) failSinceUs = nowUs();
    consecFail++;
    if (consecFail == 2 || consecFail % 8 == 0) resetClient(consecFail == 2);
  }
  return code;
}

static String noteErr(int code, const String& resp) {
  String desc = jStr(resp, "\"description\":\"");
  String e;
  if (code < 0) e = "немає зв'язку з api.telegram.org (" + String(code) + ")";
  else if (code == 401) e = "Telegram не прийняв токен бота (401)";
  else if (code == 409) e = "цей бот вже читає інша програма (409)";
  else e = "HTTP " + String(code) + (desc.length() ? ": " + desc : String());
  xSemaphoreTake(qm, portMAX_DELAY);
  lastErr = e;
  lastErrUs = nowUs();
  xSemaphoreGive(qm);
  return e;
}

//  Кнопка «📊 Стан» під полем вводу (1.0.7, власник: «в меню бота сделать кнопку стан»). 1.0.8: іде з кожним
//  повідомленням особистого чату — з'являється сама, без /start; kb == 2 («вимкнено») її прибирає. Лише в особистих чатах:
//  у групі бот із типовим режимом приватності звичайних повідомлень не бачить — там /status з меню команд.
//  1.0.21: «🔔 Сповіщення» — що надсилати саме вам; адміністраторам ще «👥 Користувачі» і «📋 Журнал».
static const char* KB_USR = ",\"reply_markup\":{\"keyboard\":[[{\"text\":\"📊 Стан\"},{\"text\":\"🗓 Графік\"}],[{\"text\":\"📈 Статистика\"},{\"text\":\"🔔 Сповіщення\"}]],"
                            "\"resize_keyboard\":true,\"is_persistent\":true,\"input_field_placeholder\":\"Натисніть «📊 Стан»\"}";
static const char* KB_ADM = ",\"reply_markup\":{\"keyboard\":[[{\"text\":\"📊 Стан\"},{\"text\":\"🗓 Графік\"}],[{\"text\":\"📈 Статистика\"},{\"text\":\"🔔 Сповіщення\"}],"
                            "[{\"text\":\"👥 Користувачі\"},{\"text\":\"📋 Журнал\"}],[{\"text\":\"🛠 Сторож\"},{\"text\":\"📣 Оголошення\"}]],\"resize_keyboard\":true,\"is_persistent\":true,\"input_field_placeholder\":\"Натисніть «📊 Стан»\"}";
static const char* KB_HIDE = ",\"reply_markup\":{\"remove_keyboard\":true}";

//  ---------- без дублів (1.0.31) ----------
//  20.11.2026 о 16:19–16:38 Telegram приймав запити, але відповідав довше за 15 с (−11): Сторож вважав «не дійшло» й надсилав
//  знову — власникові: «в 16.19 пришло куча сообщений об отсутствии света, вместо одного». Тепер відповіді на надсилання
//  чекаємо 30 с, а якщо запит пішов і відповіді немає (−11, обрив, 5xx), то наступного разу спершу перевіряємо, чи
//  повідомлення вже в чаті: правка повідомлення з наступним номером тим самим текстом. «message is not modified» — воно
//  там, удруге не надсилаємо; «message to edit not found» — не дійшло, надсилаємо. Номери повідомлень у чаті йдуть підряд —
//  останній відомий беремо з наших відповідей і з вхідних. Усе це — лише в задачі Telegram (без замків).
struct MidRec { long long chat; int32_t mid; };
static MidRec mids[24];
static int nmid = 0;
static void midSet(long long chat, long long mid) {
  if (!chat || mid <= 0) return;
  for (int i = 0; i < nmid; i++) if (mids[i].chat == chat) { if (mid > mids[i].mid) mids[i].mid = (int32_t)mid; return; }
  if (nmid < 24) mids[nmid++] = {chat, (int32_t)mid};
  else { memmove(mids, mids + 1, sizeof(MidRec) * 23); mids[23] = {chat, (int32_t)mid}; }
}
static int32_t midGet(long long chat) { for (int i = 0; i < nmid; i++) if (mids[i].chat == chat) return mids[i].mid; return 0; }
struct Probe { long long chat; uint32_t key; int32_t base; String text; String mk; };
static Probe probes[MAXR + 2];
static int nprobe = 0;
static void probeAdd(long long chat, uint32_t key, int32_t base, const String& text, const String& mk) {
  if (nprobe == MAXR + 2) { for (int i = 1; i < nprobe; i++) probes[i - 1] = probes[i]; nprobe--; }
  probes[nprobe++] = {chat, key, base, text, mk};
}
static void probeDrop(int i) { for (int k = i + 1; k < nprobe; k++) probes[k - 1] = probes[k]; nprobe--; }
static uint32_t itemKey(const Item& it) {
  uint32_t h = 2166136261u ^ (uint32_t)it.evUs ^ (uint32_t)(it.evUs >> 32);
  for (size_t i = 0; i < it.text.length(); i++) h = (h ^ (uint8_t)it.text[i]) * 16777619u;
  return h;
}

static bool sendOne(const String& token, long long chat, const String& text, bool silent, uint8_t kb, bool& permanent, bool plain = false,
                    const String& mk = String(), int* outCode = nullptr) {
  permanent = false;
  //  1.0.6: оформлення HTML (жирний заголовок). Не прийняв розмітку («can't parse entities») — той самий текст
  //  без неї, щоб повідомлення не загубилось.
  auto body = [&](bool full) {
    if (!full) return "{\"chat_id\":" + String(chat) + ",\"text\":\"" + jsonEsc(stripTags(text)) + "\"}";
    return "{\"chat_id\":" + String(chat) + ",\"text\":\"" + jsonEsc(text) + "\",\"parse_mode\":\"HTML\"" +
           (silent ? ",\"disable_notification\":true" : "") +
           (mk.length() ? ",\"reply_markup\":" + mk : String(chat > 0 ? (kb == 2 ? KB_HIDE : kb == 3 ? KB_ADM : kb == 1 ? KB_USR : "") : "")) +
           ",\"link_preview_options\":{\"is_disabled\":true}}";
  };
  String resp;
  int code = call(token, "sendMessage", body(!plain), resp, 30000);   // 1.0.18: не йде 6 разів — простим текстом; 1.0.31: чекати 30 с
  //  1.0.8: Telegram відхилив повідомлення (400) не через хибний чат — розмітку, кнопку чи щось інше з оформлення.
  //  Повідомлення про світло важливіше за вигляд: те саме найпростішим запитом, без оформлення й кнопки.
  if (code == 400 && resp.indexOf("chat not found") < 0) {
    logEvent("Telegram не прийняв оформлене повідомлення (" + jStr(resp, "\"description\":\"") + ") — надсилаю простим текстом");
    code = call(token, "sendMessage", body(false), resp, 30000);
  }
  if (outCode) *outCode = code;
  if (code == 200 && resp.indexOf("\"ok\":true") >= 0) {
    midSet(chat, jNum(resp, "\"message_id\":"));
    long long d = jNum(resp, "\"date\":");
    if (d > 0) clockFromEpoch((time_t)d, "відповіддю Telegram");
    xSemaphoreTake(qm, portMAX_DELAY);
    lastOkUs = nowUs();
    sentCount++;
    xSemaphoreGive(qm);
    return true;
  }
  String e = noteErr(code, resp);
  if (code > 0 || consecFail <= 1 || consecFail % 10 == 0) logEvent("Telegram, чат " + String(chat) + ": " + e);   // 1.0.32: без повторів
  if (code == 400 || code == 403) permanent = true;           // хибний chat_id або бота заблоковано
  if (code == 429) {
    long long ra = jNum(resp, "\"retry_after\":");
    if (ra > 0 && ra < 300) vTaskDelay(pdMS_TO_TICKS(ra * 1000));
  }
  return false;
}

//  1 — повідомлення в чаті (надіслано зараз або дійшла попередня спроба), 0 — ні (пізніше ще раз).
static int verifyDelivered(const String& token, const Probe& p) {
  for (int k = 1; k <= 3; k++) {
    String body = "{\"chat_id\":" + String(p.chat) + ",\"message_id\":" + String(p.base + k) + ",\"text\":\"" + jsonEsc(p.text) +
                  "\",\"parse_mode\":\"HTML\",\"link_preview_options\":{\"is_disabled\":true}" +
                  (p.mk.length() ? ",\"reply_markup\":" + p.mk : String()) + "}";
    String resp;
    int code = call(token, "editMessageText", body, resp);
    if (code == 200) { midSet(p.chat, p.base + k); return 1; }
    if (code != 400) return -1;                      // не з'ясували — пізніше
    String d = jStr(resp, "\"description\":\"");
    if (d.indexOf("not modified") >= 0) { midSet(p.chat, p.base + k); return 1; }
    if (d.indexOf("not found") >= 0) return 0;      // номера ще немає — не дійшло
    //  «message can't be edited» — повідомлення людини з цим номером: дивимось наступний
  }
  return 0;
}
static int deliver(const String& token, long long chat, const String& text, bool silent, uint8_t kb, bool& perm, bool plain,
                   const String& mk, uint32_t key) {
  perm = false;
  for (int i = 0; i < nprobe; i++) {
    if (probes[i].chat != chat || probes[i].key != key) continue;
    int v = verifyDelivered(token, probes[i]);
    if (v < 0) return 0;                             // перевірити не вдалося — пізніше, і вдруге не надсилаємо
    probeDrop(i);
    if (v == 1) {
      logEvent("Telegram, чат " + String(chat) + ": попередня спроба таки дійшла — удруге не надсилаю");
      return 1;
    }
    logEvent("Telegram, чат " + String(chat) + ": попередня спроба не дійшла — надсилаю");
    break;
  }
  int32_t before = midGet(chat);
  int code = 0;
  if (sendOne(token, chat, text, silent, kb, perm, plain, mk, &code)) return 1;
  //  Запит пішов, а відповіді немає — могло й дійти
  bool maybe = code == HTTPC_ERROR_READ_TIMEOUT || code == HTTPC_ERROR_CONNECTION_LOST || code >= 500;
  if (maybe && before > 0) probeAdd(chat, key, before, text, mk);
  else if (maybe) logEvent("Telegram, чат " + String(chat) + ": відповіді немає, а номерів повідомлень цього чату ще не знаю — перевірити не зможу");
  return 0;
}

static bool editOne(const String& token, long long chat, int32_t mid, const String& text, const String& mk, bool& perm) {
  perm = false;
  String body = "{\"chat_id\":" + String(chat) + ",\"message_id\":" + String(mid) + ",\"text\":\"" + jsonEsc(text) + "\",\"parse_mode\":\"HTML\"" +
                (mk.length() ? ",\"reply_markup\":" + mk : String()) + ",\"link_preview_options\":{\"is_disabled\":true}}";
  String resp;
  int code = call(token, "editMessageText", body, resp);
  if (code == 200) return true;
  if (code == 400) {                                    // «message is not modified» чи повідомлення вже немає — повторювати марно
    perm = true;
    if (resp.indexOf("not modified") < 0) logEvent("Telegram, правка повідомлення: " + jStr(resp, "\"description\":\""));
  } else noteErr(code, resp);
  return false;
}
static bool answerOne(const String& token, const String& cbId, const String& toast, bool& perm) {
  perm = false;
  String resp;
  int code = call(token, "answerCallbackQuery", "{\"callback_query_id\":\"" + jsonEsc(cbId) + "\",\"text\":\"" + jsonEsc(toast) + "\"}", resp);
  if (code == 200) return true;
  if (code == 400) perm = true;                         // натискання застаріло — нічого не вдієш
  return false;
}

static bool isRecipient(long long id) {
  for (int i = 0; i < nrec; i++) if (rec[i].id == id) return true;
  return false;
}

static void remember(long long id, const String& name, const String& text) {
  xSemaphoreTake(qm, portMAX_DELAY);
  int k = -1;
  for (int i = 0; i < nseen; i++) if (seen[i].id == id) k = i;
  if (k < 0) {
    if (nseen < NSEEN) k = nseen++;
    else { k = 0; for (int i = 1; i < NSEEN; i++) if (seen[i].us < seen[k].us) k = i; }
  }
  seen[k].id = id;
  seen[k].name = name;
  seen[k].text = text.substring(0, 60);
  seen[k].us = nowUs();
  xSemaphoreGive(qm);
}

// ---------- одержувачі з бота (1.0.21) ----------
//  Власник: «добавь функцию настройки пользователей бота — кому отправлять оповещения, а кому не отправлять». Людина сама
//  вмикає й вимикає свої сповіщення кнопкою «🔔 Сповіщення»; адміністратор — кожному через «👥 Користувачі» й підтверджує
//  заявки нових людей. Кожна зміна — явна дія людини, тож пишемо одразу (один ключ tgChats у флеш).
static int recIdx(long long id) { for (int i = 0; i < nrec; i++) if (rec[i].id == id) return i; return -1; }
static bool isAdmin(long long id) { int i = recIdx(id); return i >= 0 && (rec[i].fl & RF_ADMIN); }
static int adminCount() { int n = 0; for (int i = 0; i < nrec; i++) if (rec[i].fl & RF_ADMIN) n++; return n; }
static bool saveRec(const Chat* c, int n) { S.tgChats = chatsToText(c, n); bool ok = settingsSaveChats(); tgReload(); return ok; }
static String who(const Chat& c) { return c.name.length() ? c.name : String(c.id); }
static String btn(const String& text, const String& data) { return "{\"text\":\"" + jsonEsc(text) + "\",\"callback_data\":\"" + jsonEsc(data) + "\"}"; }
static const char* const BIT_NAME[] = {"усі сповіщення", "світло", "генератор", "графік", "підсумок", "службові", "маячки", "уночі без звуку", "адміністратор", "батарея"};

//  adm — вигляд для адміністратора (інший користувач): ще «👑» і «🗑». У даних кнопки: n — свій вигляд, N — адміністратора.
static String notifyText(int i, bool adm) {
  const Chat& c = rec[i];
  String t = "🔔 <b>Сповіщення: " + htmlEsc(who(c)) + "</b>\n" + objIco() + htmlEsc(S.objName);
  if (!(c.fl & RF_ON)) t += "\n🔕 Зараз сповіщення вимкнено зовсім. Кнопки «📊 Стан», «🗓 Графік», «📈 Статистика» працюють.";
  else t += "\nНатисніть, щоб увімкнути ✅ чи вимкнути ⬜:\n· світло — зникло й повернулось міське світло\n· генератор — запустився, зупинився, «вимкніть»\n"
            "· графік — попередження перед відключенням і графік на завтра\n· підсумок — щодня, скільки світло було\n· службові — Сторож перезапустився, пароль\n"
            "· маячки — напруга на лініях і фазах\n· батарея — заряд, на скільки вистачить, тривоги BMS (адміністраторам — завжди)";
  if (c.fl & RF_NIGHT) t += "\n🌙 З " + String(S.tgNightFrom) + ":00 до " + String(S.tgNightTo) + ":00 — без звуку.";
  if (adm) t += "\n\n" + String(c.id < 0 ? "Група" : "Людина") + " · " + String(c.id) + (c.fl & RF_ADMIN ? " · 👑 адміністратор" : "");
  return t;
}
static String notifyMk(int i, bool adm) {
  const Chat& c = rec[i];
  String p = String(adm ? "N:" : "n:") + c.id + ":";
  auto b = [&](int bit, const char* label) { return btn(String((c.fl >> bit) & 1 ? "✅ " : "⬜ ") + label, p + bit); };
  String m = "{\"inline_keyboard\":[";
  m += "[" + b(1, "Світло") + "," + b(2, "Генератор") + "],";
  m += "[" + b(3, "Графік") + "," + b(4, "Підсумок") + "],";
  m += "[" + b(5, "Службові") + "," + b(6, "Маячки") + "],";
  m += "[" + b(9, "🔋 Батарея") + "],";
  m += "[" + btn(c.fl & RF_NIGHT ? "🌙 Уночі без звуку: так" : "🌙 Уночі без звуку: ні", p + "7") + "],";
  m += "[" + btn(c.fl & RF_ON ? "🔕 Вимкнути всі сповіщення" : "🔔 Увімкнути сповіщення", p + "0") + "]";
  if (adm) {
    m += ",[" + btn(c.fl & RF_ADMIN ? "👑 Адміністратор: так" : "👤 Адміністратор: ні", p + "8") + "," + btn("🗑 Прибрати", "d:" + String(c.id)) + "]";
    m += ",[" + btn("⬅️ Усі користувачі", "u:list") + "]";
  }
  return m + "]}";
}
static String usersText() {
  String t = "👥 <b>Користувачі бота</b>\n" + objIco() + htmlEsc(S.objName) + " · " + String(nrec) + " з " + String(MAXR);
  for (int i = 0; i < nrec; i++) {
    const Chat& c = rec[i];
    int cats = 0;
    for (int b = 1; b <= 9; b++) if (b <= 6 || b == 9) cats += c.fl >> b & 1;
    t += "\n" + String(i + 1) + ". " + htmlEsc(who(c)) + (c.id < 0 ? " (група)" : "") + " — " +
         (!(c.fl & RF_ON) ? String("🔕 вимкнено") : cats == 7 ? String("🔔 усе") : String("🔔 " + String(cats) + " з 7")) +
         (c.fl & RF_NIGHT ? " · 🌙" : "") + (c.fl & RF_ADMIN ? " · 👑" : "");
  }
  if (!nrec) t += "\nПоки нікого немає.";
  return t + "\n\nНатисніть ім'я, щоб змінити, що людина отримує. Нові люди: хай напишуть боту /start — вам прийде заявка з кнопкою «Додати».";
}
static String usersMk() {
  String m = "{\"inline_keyboard\":[";
  for (int i = 0; i < nrec; i++) {
    const Chat& c = rec[i];
    if (i) m += ",";
    m += "[" + btn(String(c.fl & RF_ON ? "🔔 " : "🔕 ") + who(c) + (c.fl & RF_ADMIN ? " 👑" : ""), "u:" + String(c.id)) + "]";
  }
  return m + "]}";
}

//  Заявки: хтось, кого немає серед одержувачів, написав боту — адміністраторам повідомлення з кнопками (раз на 6 год від людини).
struct Req { long long id = 0; String name; int64_t us = 0; };
static Req reqs[6];
static void askAdmins(long long id, const String& name, const String& text) {
  int64_t now = nowUs();
  int k = -1, old = 0;
  for (int i = 0; i < 6; i++) { if (reqs[i].id == id) k = i; if (reqs[i].us < reqs[old].us) old = i; }
  if (k >= 0 && now - reqs[k].us < 6LL * 3600 * 1000000) return;
  if (k < 0) k = old;
  reqs[k].id = id;
  reqs[k].name = name;
  reqs[k].us = now;
  if (!adminCount()) return;
  String t = "👤 <b>Хоче отримувати сповіщення</b>\n" + objIco() + htmlEsc(S.objName) + "\n" + htmlEsc(name.length() ? name : String("без імені")) + " · " +
             (id < 0 ? "група" : "людина") + " · " + String(id) + (text.length() ? "\nНаписав(ла): «" + htmlEsc(text.substring(0, 60)) + "»" : String());
  String mk = "{\"inline_keyboard\":[[" + btn("✅ Додати", "a:" + String(id)) + "," + btn("✖️ Ні", "x:" + String(id)) + "]]}";
  for (int i = 0; i < nrec; i++) if (rec[i].fl & RF_ADMIN) replyMk(rec[i].id, t, mk);
  logEvent("Заявка в боті: " + (name.length() ? name : String("без імені")) + " (" + String(id) + ") — надіслано адміністраторам");
}
static String reqName(long long id) {
  for (auto& r : reqs) if (r.id == id) return r.name;
  for (int i = 0; i < nseen; i++) if (seen[i].id == id) return seen[i].name;
  return String();
}

static String helpText(bool adm) {
  return String("ℹ️ <b>Як користуватись</b>\n") + objIco() + htmlEsc(S.objName) +
         "\n«📊 Стан» — є світло чи ні, генератор, графік\n«🗓 Графік» — графік відключень на сьогодні й завтра\n«📈 Статистика» — скільки світло було й не було"
         "\n«🔔 Сповіщення» — що надсилати саме вам (і вночі без звуку)" +
         (adm ? "\n«👥 Користувачі» — хто отримує і що; заявки нових людей\n«📋 Журнал» — останні події Сторожа"
                "\n«🛠 Сторож» — прошивка, пам'ять, сигнали Wi-Fi, АВР і маячків, графік YASNO"
                "\n«📣 Оголошення» — розіслати своє повідомлення всім, хто отримує сповіщення" : "");
}

//  Оголошення (1.0.26, власник: «добавить рассылку пользовательских оповещений»): адміністратор пише текст — у боті
//  («📣 Оголошення») чи на сторінці — і його отримують усі, у кого ввімкнено сповіщення (зі сторінки — ще й вибрані).
//  Одна чернетка на Сторожа: хто натиснув «📣 Оголошення», той наступним повідомленням дає текст, далі — кнопки.
struct AnnDraft { long long chat = 0; int64_t waitUs = 0; String text, by; };
static AnnDraft ann;
static String annMsg(const String& text, const String& by) {
  return "📣 <b>Оголошення</b>\n" + objIco() + htmlEsc(S.objName) + " · %T\n\n" + htmlEsc(text) + (by.length() ? "\n\n— " + htmlEsc(by) : String());
}
static int annCount() { int n = 0; for (int i = 0; i < nrec; i++) if (rec[i].fl & RF_ON) n++; return n; }
int tgAnnounce(const String& text, const String& by, const long long* ids, int nIds, bool silent) {
  String msg = annMsg(text, by);
  int64_t now = nowUs();
  int n = 0;
  xSemaphoreTake(qm, portMAX_DELAY);
  uint32_t m = 0;
  for (int i = 0; i < nrec; i++) {
    if (!(rec[i].fl & RF_ON)) continue;            // «Сповіщення» вимкнено — людина не отримує нічого, і оголошень теж
    bool sel = nIds < 0;
    for (int k = 0; k < nIds && !sel; k++) if (ids[k] == rec[i].id) sel = true;
    if (sel) { m |= 1u << i; n++; }
  }
  if (m && S.tgToken.length()) {
    if (q.size() >= QMAX) { q.pop_front(); dropped++; }
    Item it{now, msg, 0, m, 0, silent, 0};
    it.cat = 0;
    stamp(it);
    q.push_back(it);
    rtcSave();
  } else n = 0;
  xSemaphoreGive(qm);
  logEvent("Оголошення" + (by.length() ? " від " + by : String(" зі сторінки")) + (n ? " — до " + String(n) + " одержувачів" + (silent ? ", без звуку" : "") : String(" — нікому не надіслано")) +
           ": " + text.substring(0, 160));
  return n;
}

//  «🛠 Сторож» (1.0.23): усе про сам прилад — щоб адміністратор бачив його стан без сторінки
static String diagText() {
  int64_t now = nowUs();
  String r = "🛠 <b>Сторож світла</b>\n" + objIco() + htmlEsc(S.objName) + " · " + fmtHM(now) +
             "\nПрошивка " FW_VERSION ", працює " + fmtDur(now / 1000000) + "\nОстанній запуск: " + htmlEsc(resetReasonText()) +
             "\nПам'ять: вільно " + String(memFree() / 1024) + " КБ, найменше було " + String(memMin() / 1024) + " КБ" +
             "\n📶 Wi-Fi: " + (staUp() ? htmlEsc(S.staSsid) + ", " + String(staRssi()) + " дБм" : String("роутер не відповідає"));
  int ar;
  if (!watchAvrSet()) r += watchCitySet() ? "\n🔌 АВР: немає — місто за маячком «Міське світло»" : "\n🔌 АВР: не призначено";
  else if (watchAvrRssi(ar)) r += "\n🔌 АВР: у мережі, " + String(ar) + " дБм";
  else r += "\n🔌 АВР: немає в мережі";
  if (S.swMode == 1) r += "\n🔁 Перемикач: ручний — Сторож підказує, коли й на що переключити";   // 1.0.57
  r += "\n📡 Маячки:" + watchBeaconsText();
  r += bmsDiagLine();                            // 1.0.36
  r += schDiagLine();
  xSemaphoreTake(qm, portMAX_DELAY);
  r += "\n✉️ Telegram: надіслано " + String(sentCount) + (dropped ? ", втрачено " + String(dropped) : String()) + (q.size() ? ", у черзі " + String((int)q.size()) : String());
  xSemaphoreGive(qm);
  return r;
}

static void command(const In& m) {
  long long chat = m.chat;
  const String& name = m.name;
  const String& text = m.text;
  remember(chat, name, text);
  if (clockValid() && m.date > 0 && m.date < (long long)time(nullptr) - 600) return;   // старе — не відповідаємо
  String cmd = text;
  cmd.trim();
  int at = cmd.indexOf('@');
  if (cmd.startsWith("/") && at > 0) cmd = cmd.substring(0, at);   // /status@ІмяБота
  bool rcp = isRecipient(chat);
  bool adm = isAdmin(m.from ? m.from : chat);
  String o = htmlEsc(S.objName);
  String w = cmd;
  for (const char* e : {"📊", "📈", "🗓", "🔔", "👥", "📋", "ℹ️", "🛠", "📣"}) w.replace(e, "");
  w.trim();
  //  toLowerCase() кирилицю в UTF-8 не чіпає — тож усі написання явно
  auto is = [&](std::initializer_list<const char*> l) { for (const char* x : l) if (w == x) return true; return false; };
  if (!rcp) {
    //  1.0.21: заявка адміністраторам замість «попросіть адміністратора» вручну
    tgReply(chat, "👋 <b>Вітаю!</b>\nТут сповіщення про міське світло: " + objIco() + o + ".\n" +
                  (adminCount() ? String("Я передав ваш запит адміністраторові — щойно він підтвердить, сповіщення почнуть приходити.")
                                : String("Щоб отримувати їх, попросіть адміністратора додати вас: сторінка «Сторожа світла», вкладка «Сповіщення».")));
    askAdmins(chat, name, cmd.startsWith("/start") ? String() : text);
    return;
  }
  int i = recIdx(chat);
  if (is({"/schedule", "/grafik", "/графік", "Графік", "графік", "ГРАФІК"})) { tgReply(chat, schReport(), 1); return; }
  if (is({"/stats", "/Stats", "Статистика", "статистика", "СТАТИСТИКА"})) { tgReply(chat, statsReport(), 1); return; }
  if (is({"/status", "/Status", "/стан", "Стан", "стан", "СТАН"})) { tgReply(chat, statusReport(), 1); return; }
  if (is({"/notify", "Сповіщення", "сповіщення", "СПОВІЩЕННЯ"})) { replyMk(chat, notifyText(i, false), notifyMk(i, false)); return; }
  //  Список людей і журнал — лише адміністраторові й лише в особистому чаті (не показувати групі)
  if (is({"/users", "Користувачі", "користувачі"})) {
    if (adm && chat > 0) replyMk(chat, usersText(), usersMk());
    else tgReply(chat, chat > 0 ? "👥 Список користувачів — лише для адміністратора." : "👥 Список користувачів — в особистому чаті з ботом.", 1);
    return;
  }
  if (is({"/log", "Журнал", "журнал"})) {
    if (adm && chat > 0) tgReply(chat, "📋 <b>Журнал Сторожа</b>\n" + objIco() + o + " · останні події, нові згори" + logText(20, 3300), 1);
    else tgReply(chat, chat > 0 ? "📋 Журнал — лише для адміністратора." : "📋 Журнал — в особистому чаті з ботом.", 1);
    return;
  }
  //  Оголошення (1.0.26): кнопка → «напишіть текст» → наступне повідомлення — чернетка з кнопками «надіслати»
  if (is({"/announce", "Оголошення", "оголошення"})) {
    if (!(adm && chat > 0)) { tgReply(chat, chat > 0 ? "📣 Оголошення — лише для адміністратора." : "📣 Оголошення — в особистому чаті з ботом.", 1); return; }
    ann = AnnDraft();
    ann.chat = chat;
    ann.waitUs = nowUs();
    ann.by = i >= 0 ? who(rec[i]) : name;
    tgReply(chat, "📣 <b>Нове оголошення</b>\nНапишіть текст одним повідомленням (до 1000 знаків) — я покажу, як його побачать, і спитаю, чи надсилати. "
                  "Отримають усі, у кого ввімкнено сповіщення: " + String(annCount()) + ".\n/cancel — скасувати.", 1);
    return;
  }
  if (ann.chat == chat && ann.waitUs) {
    bool menu = false;
    for (const char* b : {"Стан", "Графік", "Статистика", "Сповіщення", "Користувачі", "Журнал", "Сторож", "Довідка"}) if (w == b) menu = true;
    if (cmd == "/cancel") { ann = AnnDraft(); tgReply(chat, "📣 Оголошення скасовано.", 1); return; }
    if (!menu && !cmd.startsWith("/") && nowUs() - ann.waitUs < 10LL * 60 * 1000000) {
      String t = text;
      t.trim();
      if (t.length() > 3000) { tgReply(chat, "📣 Задовго: до 1000 знаків. Напишіть коротше або /cancel.", 1); return; }
      ann.text = t;
      ann.waitUs = 0;
      String pv = annMsg(t, ann.by);
      pv.replace("%T", fmtClock(nowUs()));
      int n = annCount();
      replyMk(chat, "Так його побачать " + String(n) + " одержувачів:\n\n" + pv,
              "{\"inline_keyboard\":[[" + btn("✅ Надіслати", "A:1") + "," + btn("🔕 Без звуку", "A:2") + "],[" + btn("✖️ Скасувати", "A:0") + "]]}");
      return;
    }
  }
  if (is({"/diag", "Сторож", "сторож"})) {
    if (adm && chat > 0) tgReply(chat, diagText(), 1);
    else tgReply(chat, chat > 0 ? "🛠 Стан Сторожа — лише для адміністратора." : "🛠 Стан Сторожа — в особистому чаті з ботом.", 1);
    return;
  }
  if (cmd == "/start" || cmd == "/help" || is({"Довідка", "довідка"})) {
    tgReply(chat, cmd == "/start" ? "👋 <b>Вітаю!</b>\nВи отримуєте сповіщення про міське світло: " + objIco() + o + ".\n\n" + helpText(adm) : helpText(adm), 1);
    return;
  }
}

//  Натискання кнопки під повідомленням (1.0.21). m.from — хто натиснув, m.chat/m.mid — повідомлення з кнопками.
static void onButton(const In& m) {
  const String& d = m.text;
  long long viewer = m.from;
  bool adm = isAdmin(viewer);
  int vi = recIdx(viewer);
  String vname = vi >= 0 ? who(rec[vi]) : (m.name.length() ? m.name : String(viewer));
  if (d.length() < 3 || d[1] != ':') return answerCb(m.cb, "");
  char k = d[0];
  if (d == "u:list") {
    if (!adm) return answerCb(m.cb, "Лише для адміністратора");
    editMk(m.chat, m.mid, usersText(), usersMk());
    return answerCb(m.cb, "");
  }
  if (k == 'A') {                                  // оголошення (1.0.26): надіслати / без звуку / скасувати
    if (!adm || viewer != ann.chat || !ann.text.length()) return answerCb(m.cb, "Цієї чернетки вже немає");
    int what = d.substring(2).toInt();
    if (what == 0) { editMk(m.chat, m.mid, "📣 Оголошення скасовано.", "{\"inline_keyboard\":[]}"); ann = AnnDraft(); return answerCb(m.cb, "Скасовано"); }
    int n = tgAnnounce(ann.text, ann.by, nullptr, -1, what == 2);
    lcdAlert(LCD_A_ANN, lcdSafe(ann.text));
    editMk(m.chat, m.mid, "📣 Оголошення надіслано: " + String(n) + " одержувачам" + (what == 2 ? ", без звуку" : "") + ".", "{\"inline_keyboard\":[]}");
    ann = AnnDraft();
    return answerCb(m.cb, "Надіслано");
  }
  long long id = strtoll(d.c_str() + 2, nullptr, 10);
  int i = recIdx(id);
  if (k == 'u') {
    if (!adm) return answerCb(m.cb, "Лише для адміністратора");
    if (i < 0) { editMk(m.chat, m.mid, usersText(), usersMk()); return answerCb(m.cb, "Його вже немає серед одержувачів"); }
    editMk(m.chat, m.mid, notifyText(i, true), notifyMk(i, true));
    return answerCb(m.cb, "");
  }
  if (k == 'n' || k == 'N') {
    int c2 = d.indexOf(':', 2);
    int bit = c2 > 0 ? d.substring(c2 + 1).toInt() : -1;
    if (i < 0) return answerCb(m.cb, "Цього чату вже немає серед одержувачів");
    bool self = viewer == id && id > 0;                   // у групі змінює лише адміністратор
    if (bit < 0 || bit > 9 || (!self && !adm) || (bit == 8 && !adm)) return answerCb(m.cb, "Змінити може лише адміністратор");
    if (bit == 8 && (rec[i].fl & RF_ADMIN) && adminCount() <= 1) return answerCb(m.cb, "Це єдиний адміністратор — спершу призначте іншого");
    Chat c[MAXR];
    int n = nrec;
    for (int j = 0; j < n; j++) c[j] = rec[j];
    c[i].fl ^= (uint16_t)(1u << bit);
    bool on = c[i].fl >> bit & 1;
    if (!saveRec(c, n)) return answerCb(m.cb, "Не вдалося записати");
    logEvent("Сповіщення «" + who(c[i]) + "»: " + BIT_NAME[bit] + (on ? " — увімкнено" : " — вимкнено") + (self ? String() : " (змінив(ла) " + vname + ")"));
    i = recIdx(id);
    bool av = k == 'N' && adm;
    editMk(m.chat, m.mid, notifyText(i, av), notifyMk(i, av));
    return answerCb(m.cb, String(on ? "✅ " : "⬜ ") + BIT_NAME[bit]);
  }
  if (!adm) return answerCb(m.cb, "Лише для адміністратора");
  if (k == 'd') {
    if (i < 0) return answerCb(m.cb, "Його вже немає");
    editMk(m.chat, m.mid, "🗑 <b>Прибрати «" + htmlEsc(who(rec[i])) + "» зі сповіщень?</b>\nЙому прийде «Сповіщення вимкнено». Повернути можна, коли він знову напише боту.",
           "{\"inline_keyboard\":[[" + btn("🗑 Так, прибрати", "D:" + String(id)) + "," + btn("Ні", "u:" + String(id)) + "]]}");
    return answerCb(m.cb, "");
  }
  if (k == 'D') {
    if (i < 0) { editMk(m.chat, m.mid, usersText(), usersMk()); return answerCb(m.cb, "Його вже немає"); }
    if ((rec[i].fl & RF_ADMIN) && adminCount() <= 1) return answerCb(m.cb, "Це єдиний адміністратор — спершу призначте іншого");
    Chat c[MAXR];
    int n = 0;
    String nm = who(rec[i]);
    for (int j = 0; j < nrec; j++) if (rec[j].id != id) c[n++] = rec[j];
    if (!saveRec(c, n)) return answerCb(m.cb, "Не вдалося записати");
    logEvent("Прибрано одержувача " + String(id) + " " + nm + " (з бота, " + vname + ")");
    tgFarewell(id);
    editMk(m.chat, m.mid, usersText(), usersMk());
    return answerCb(m.cb, "Прибрано: " + nm);
  }
  if (k == 'a' || k == 'x') {
    String nm = reqName(id);
    String shown = htmlEsc(nm.length() ? nm : String(id));
    if (k == 'x') {
      editMk(m.chat, m.mid, "✖️ Не додано: " + shown + " (вирішив(ла) " + htmlEsc(vname) + ")", "");
      logEvent("Заявку " + String(id) + " " + nm + " відхилено (" + vname + ")");
      return answerCb(m.cb, "Не додано");
    }
    if (i >= 0) { editMk(m.chat, m.mid, "✅ " + shown + " вже отримує сповіщення", ""); return answerCb(m.cb, "Вже додано"); }
    if (nrec >= MAXR) return answerCb(m.cb, "Не більше " + String(MAXR) + " одержувачів");
    Chat c[MAXR];
    int n = nrec;
    for (int j = 0; j < n; j++) c[j] = rec[j];
    c[n].id = id;
    c[n].name = nm;
    c[n].fl = RF_DEFAULT | (n == 0 ? RF_ADMIN : 0);
    if (!saveRec(c, n + 1)) return answerCb(m.cb, "Не вдалося записати");
    logEvent("Додано одержувача " + String(id) + " " + nm + " (заявка в боті, підтвердив(ла) " + vname + ")");
    tgWelcome(id);
    editMk(m.chat, m.mid, "✅ <b>Додано:</b> " + shown + " (підтвердив(ла) " + htmlEsc(vname) + ")", "");
    return answerCb(m.cb, "Додано");
  }
  answerCb(m.cb, "");
}

static void poll(const String& token) {
  String body = "{\"offset\":" + String(updOffset) + ",\"timeout\":0,\"limit\":10,\"allowed_updates\":[\"message\",\"callback_query\"]}";
  String resp;
  int code = call(token, "getUpdates", body, resp);
  if (code != 200) { noteErr(code, resp); return; }
  int pos = 0;
  for (;;) {
    int u = resp.indexOf("\"update_id\":", pos);
    if (u < 0) break;
    int nx = resp.indexOf("\"update_id\":", u + 12);
    int end = nx < 0 ? (int)resp.length() : nx;
    long long uid = jNum(resp, "\"update_id\":", u);
    if (uid >= updOffset) updOffset = uid + 1;
    String seg = resp.substring(u, end);
    //  1.0.21: натискання кнопки під повідомленням. Усередині є й саме повідомлення (зі своїм «chat» і «text») —
    //  тож розбираємо окремо, а не як нове повідомлення.
    int cq = seg.indexOf("\"callback_query\":{");
    if (cq >= 0) {
      In m;
      m.cb = jStr(seg, "\"id\":\"", cq);
      int fr = seg.indexOf("\"from\":{", cq);
      if (fr >= 0) { m.from = jNum(seg, "\"id\":", fr); m.name = jStr(seg, "\"first_name\":\"", fr); }
      int ms = seg.indexOf("\"message\":{", cq);
      if (ms >= 0) {
        m.mid = (int32_t)jNum(seg, "\"message_id\":", ms);
        int ch = seg.indexOf("\"chat\":{", ms);
        if (ch >= 0) m.chat = jNum(seg, "\"id\":", ch);
        midSet(m.chat, m.mid);                       // 1.0.31: номер у чаті щонайменше такий
      }
      m.text = jStr(seg, "\"data\":\"", cq);
      m.date = 0;
      if (m.cb.length() && m.from) {
        xSemaphoreTake(qm, portMAX_DELAY);
        if (inbox.size() < 20) inbox.push_back(m);
        xSemaphoreGive(qm);
      }
      pos = end;
      continue;
    }
    int c = seg.indexOf("\"chat\":{");
    if (c >= 0) {
      int ce = seg.indexOf('}', c);
      if (ce < 0) ce = seg.length();
      String cs = seg.substring(c, ce);
      long long chat = jNum(cs, "\"id\":");
      String name = jStr(cs, "\"title\":\"");
      if (!name.length()) {
        name = jStr(cs, "\"first_name\":\"");
        String ln = jStr(cs, "\"last_name\":\"");
        if (ln.length()) name += " " + ln;
      }
      String text = jStr(seg, "\"text\":\"", ce);
      long long date = jNum(seg, "\"date\":", ce);
      if (chat) {
        midSet(chat, jNum(seg, "\"message_id\":"));   // 1.0.31: номери в чаті — для перевірки «чи дійшло»
        xSemaphoreTake(qm, portMAX_DELAY);
        if (inbox.size() < 20) {
          In m{chat, name, text, date};
          int fr = seg.indexOf("\"from\":{");          // хто написав (у групі — не сама група)
          m.from = fr >= 0 && fr < c ? jNum(seg, "\"id\":", fr) : chat;
          inbox.push_back(m);
        }
        xSemaphoreGive(qm);
      }
    }
    pos = end;
  }
}

//  Меню команд і опис бота (1.0.6, «оформи красиво»): ставимо лише те, що в боті ще порожнє, — свої зміни
//  власник може робити в @BotFather, міст їх не затирає. Раз на запуск, після getMe.
static void botProfile(const String& token) {
  String r, o = S.objName.length() ? S.objName : String("Об'єкт");
  //  Меню команд: порожнє або без /notify (до 1.0.21) — ставимо повне.
  if (call(token, "getMyCommands", "{}", r) == 200 && r.indexOf("\"notify\"") < 0)
    call(token, "setMyCommands", "{\"commands\":[{\"command\":\"status\",\"description\":\"📊 Стан світла зараз\"},"
                                 "{\"command\":\"schedule\",\"description\":\"🗓 Графік відключень\"},"
                                 "{\"command\":\"stats\",\"description\":\"📈 Скільки світло було й не було\"},"
                                 "{\"command\":\"notify\",\"description\":\"🔔 Що надсилати мені\"},"
                                 "{\"command\":\"help\",\"description\":\"ℹ️ Як користуватись\"},"
                                 "{\"command\":\"start\",\"description\":\"Про бота\"}]}", r);
  if (call(token, "getMyShortDescription", "{}", r) == 200 && !jStr(r, "\"short_description\":\"").length())
    call(token, "setMyShortDescription", "{\"short_description\":\"" + jsonEsc(objIco() + o + ": сповіщення про міське світло") + "\"}", r);
  //  Опис: порожній — ставимо; старий наш (до 1.0.11 — «адміністратора моста») — замінюємо; інший — це вже власника.
  if (call(token, "getMyDescription", "{}", r) == 200) {
    String d = jStr(r, "\"description\":\"");
    if (!d.length() || d.indexOf("моста") >= 0)
      call(token, "setMyDescription", "{\"description\":\"" + jsonEsc(objIco() + o + "\n\nСторож світла: повідомляю, коли зникає й повертається "
                                      "міське світло і чи працює генератор.\n\nНатисніть «Почати» й попросіть адміністратора додати вас "
                                      "до сповіщень.\nКнопка «📊 Стан» — стан зараз.") + "\"}", r);
  }
  if (call(token, "getMyName", "{}", r) == 200) {
    String nm = jStr(r, "\"name\":\"");
    xSemaphoreTake(qm, portMAX_DELAY);
    botTitle = nm;
    xSemaphoreGive(qm);
  }
  logEvent("Telegram: меню команд і опис бота звірено");
}

//  Назва бота (1.0.11): змінює власник на сторінці (там же видно поточну) — без @BotFather.
bool tgSetName(const String& n) {
  xSemaphoreTake(qm, portMAX_DELAY);
  bool ok = !nameWant.length();
  if (ok) { nameWant = n; nameState = "змінюю"; }
  xSemaphoreGive(qm);
  return ok;
}
static void sendName(const String& token) {
  String n;
  xSemaphoreTake(qm, portMAX_DELAY);
  n = nameWant;
  xSemaphoreGive(qm);
  if (!n.length()) return;
  String r, st;
  int code = call(token, "setMyName", "{\"name\":\"" + jsonEsc(n) + "\"}", r);
  if (code == 200 && r.indexOf("\"ok\":true") >= 0) st = "змінено";
  else st = "помилка: " + (code > 0 ? jStr(r, "\"description\":\"") : "код " + String(code));
  logEvent("Назва бота «" + n + "»: " + st);
  xSemaphoreTake(qm, portMAX_DELAY);
  if (st == "змінено") botTitle = n;
  nameWant = "";
  nameState = st;
  xSemaphoreGive(qm);
}

bool tgSetPhoto(uint8_t* body, size_t len) {
  xSemaphoreTake(qm, portMAX_DELAY);
  bool ok = !photoBody;
  if (ok) { photoBody = body; photoLen = len; photoState = "надсилаю"; }
  xSemaphoreGive(qm);
  if (!ok) free(body);
  return ok;
}

static void sendPhoto(const String& token) {
  uint8_t* b;
  size_t n;
  xSemaphoreTake(qm, portMAX_DELAY);
  b = photoBody;
  n = photoLen;
  xSemaphoreGive(qm);
  if (!b) return;
  String st;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(30000);
  if (!http.begin(*cli, String("https://api.telegram.org/bot") + token + "/setMyProfilePhoto")) st = "помилка: немає з'єднання";
  else {
    http.addHeader("Content-Type", "multipart/form-data; boundary=" TG_PHOTO_BOUNDARY);
    int code = http.POST(b, n);
    String resp = code > 0 ? http.getString() : String();
    http.end();
    if (code == 200 && resp.indexOf("\"ok\":true") >= 0) st = "поставлено";
    else st = "помилка: " + (code > 0 ? jStr(resp, "\"description\":\"") : "код " + String(code));
  }
  logEvent("Картинка бота: " + st);
  xSemaphoreTake(qm, portMAX_DELAY);
  free(photoBody);
  photoBody = nullptr;
  photoLen = 0;
  photoState = st;
  xSemaphoreGive(qm);
}

static void task(void*) {
  cli = newClient();
  int64_t nextPoll = 0, nextSend = 0;
  int backoff = 5;
  for (;;) {
    hbUs = nowUs();
    String token;
    xSemaphoreTake(qm, portMAX_DELAY);
    token = tok;
    xSemaphoreGive(qm);
    if (!staUp()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (!token.length()) { schNetStep(); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }   // графік потрібен і без бота (сторінка)
    int64_t now = nowUs();
    if (wakeSend) { wakeSend = false; nextSend = 0; backoff = 5; }
    if (now >= nextSend) {
      Item it;
      bool have = false;
      Chat r[MAXR];
      int nr;
      xSemaphoreTake(qm, portMAX_DELAY);
      if (!q.empty()) { it = q.front(); have = true; }
      nr = nrec;
      for (int i = 0; i < nr; i++) r[i] = rec[i];
      xSemaphoreGive(qm);
      static bool clockWaited = false;
      if (have && !clockWaited) {
        clockWaited = true;
        for (int i = 0; i < 20 && !clockValid(); i++) vTaskDelay(pdMS_TO_TICKS(500));
      }
      if (have) {
        String text = it.text;
        text.replace("%T", fmtClock(it.evUs));
        //  1.0.29 (власник: «ответа нет — жди пока появится и тогда отправка сообщения с поправкой на время и задержку из-за
        //  недоступности»): час події вже в тексті; що надсилаємо пізно — кажемо, на скільки й чому.
        if (it.kind == 0) {
          int64_t late = it.qEp && clockValid() ? (int64_t)time(nullptr) - it.qEp : it.qUs ? (nowUs() - it.qUs) / 1000000 : 0;
          if (late >= 120) text += "\n<i>⏳ Надіслано із запізненням на " + fmtDur(late) + ": Telegram був недоступний</i>";
        }
        uint32_t pend = it.pending;
        bool ok = true, perm = false;
        bool night = nightNow();
        auto flagsOf = [&](long long id) -> int { for (int i = 0; i < nr; i++) if (r[i].id == id) return r[i].fl; return -1; };
        if (it.kind == 1) {                             // правка свого повідомлення (кнопки під ним)
          if (editOne(token, it.chat, it.mid, text, it.mk, perm) || perm || it.fails >= 3) pend = 0; else ok = false;
        } else if (it.kind == 2) {                      // відповідь на натискання кнопки
          if (answerOne(token, it.mk, text, perm) || perm || it.fails >= 3) pend = 0; else ok = false;
        } else if (it.chat) {
          int f = flagsOf(it.chat);
          uint8_t kb = it.kb == 1 ? (f >= 0 && (f & RF_ADMIN) ? 3 : f >= 0 ? 1 : 0) : it.kb;   // кнопки — лише одержувачам
          bool sil = it.silent || (f >= 0 && (f & RF_NIGHT) && night);
          if (deliver(token, it.chat, text, sil, kb, perm, it.fails >= 6, it.mk, itemKey(it)) == 1 || perm) pend = 0; else ok = false;
        } else {
          for (int i = 0; i < nr && ok; i++) {
            if (!(pend & (1u << i))) continue;
            bool sil = it.silent || ((r[i].fl & RF_NIGHT) && night);   // 1.0.21: уночі без звуку — кому так зручніше
            if (deliver(token, r[i].id, text, sil, (r[i].fl & RF_ADMIN) ? 3 : 1, perm, it.fails >= 6, String(), itemKey(it)) == 1 || perm) pend &= ~(1u << i); else ok = false;
          }
        }
        xSemaphoreTake(qm, portMAX_DELAY);
        if (!q.empty() && q.front().evUs == it.evUs && q.front().text == it.text) {
          if (!pend) { q.pop_front(); nprobe = 0; }        // надіслано всім — перевірки для нього не потрібні
          else {
            q.front().pending = pend;
            q.front().fails++;                          // повідомлення не викидаємо: надійде, щойно Telegram відповість
          }
        }
        rtcSave();
        xSemaphoreGive(qm);

        if (ok) { backoff = 5; continue; }
        nextSend = nowUs() + (int64_t)backoff * 1000000;
        backoff = backoff < 120 ? backoff * 2 : 120;
      }
    }
    //  Ім'я бота (@…) — щоб сторінка підказувала, кому писати /start. Раз на годину, при помилці — раз на хвилину.
    static String meTok;
    static int64_t meDue = 0;
    if (token != meTok) { meTok = token; meDue = 0; }
    if (now >= meDue) {
      String resp;
      int code = call(token, "getMe", "{}", resp);
      String u = code == 200 ? jStr(resp, "\"username\":\"") : String();
      xSemaphoreTake(qm, portMAX_DELAY);
      if (tok == token) botName = u;
      xSemaphoreGive(qm);
      meDue = nowUs() + (u.length() ? 3600LL : 60LL) * 1000000;
      static String profTok;
      if (u.length() && profTok != token) { botProfile(token); profTok = token; }
    }
    //  1.0.20: графік YASNO — у цій самій задачі й лише з порожньою чергою: сповіщення важливіші, і два шифровані
    //  з'єднання разом пам'яті не мають.
    {
      xSemaphoreTake(qm, portMAX_DELAY);
      bool idle = q.empty();
      xSemaphoreGive(qm);
      if (idle) schNetStep();
    }
    if (photoBody) sendPhoto(token);
    if (nameWant.length()) sendName(token);
    if (now >= nextPoll) { poll(token); nextPoll = nowUs() + 10000000LL; }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void tgLoop() {
  for (int k = 0; k < 4; k++) {
    In m;
    bool have = false;
    xSemaphoreTake(qm, portMAX_DELAY);
    if (!inbox.empty()) { m = inbox.front(); inbox.pop_front(); have = true; }
    xSemaphoreGive(qm);
    if (!have) break;
    if (m.cb.length()) onButton(m);
    else command(m);
  }
}

void tgBegin() {
  qm = xSemaphoreCreateMutex();
  xSemaphoreTake(qm, portMAX_DELAY);
  int n = rtcRestore();
  xSemaphoreGive(qm);
  tgReload();
  //  1.0.20: відмінок («1 повідомлень» у 1.0.19)
  if (n) logEvent("Telegram: черга пережила перезапуск — " + String(n) + (n % 10 == 1 && n % 100 != 11 ? " повідомлення чекає" :
                  n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? " повідомлення чекають" : " повідомлень чекають") + " на надсилання");
  //  1.0.39: стек 10 КБ (було 12): за 4,5 год роботи 1.0.38 з десятками рукостискань TLS зайнято щонайбільше 6,1 КБ
  xTaskCreatePinnedToCore(task, "tg", 10240, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
}

int tgHealth() {
  if (!S.tgToken.length() || !nrec) return -1;
  xSemaphoreTake(qm, portMAX_DELAY);
  bool bad = lastErrUs && (!lastOkUs || lastErrUs > lastOkUs) && consecFail > 0;
  xSemaphoreGive(qm);
  return bad ? 0 : 1;
}

String tgJson() {
  int64_t now = nowUs();
  xSemaphoreTake(qm, portMAX_DELAY);
  String o = "{\"token\":" + String(S.tgToken.length() ? "true" : "false") + ",\"bot\":\"" + jsonEsc(botName) + "\",\"max\":" + String(MAXR) +
             ",\"photo\":\"" + jsonEsc(photoState) + "\",\"title\":\"" + jsonEsc(botTitle) + "\",\"nameState\":\"" + jsonEsc(nameState) +
             "\",\"recipients\":" + String(nrec) +
             ",\"queue\":" + String((int)q.size()) + ",\"sent\":" + String(sentCount) + ",\"dropped\":" + String(dropped) +
             ",\"lastOkAgo\":" + (lastOkUs ? String((long)((now - lastOkUs) / 1000000)) : String("null")) +
             ",\"lastErr\":\"" + jsonEsc(lastErr) + "\",\"lastErrAgo\":" + (lastErrUs ? String((long)((now - lastErrUs) / 1000000)) : String("null")) +
             ",\"anyOkAgo\":" + (lastAnyOkUs ? String((long)((now - lastAnyOkUs) / 1000000)) : String("null")) + ",\"fails\":" + String((int)consecFail) +
             ",\"seen\":[";
  for (int i = 0; i < nseen; i++) {
    if (i) o += ',';
    o += "{\"id\":\"" + String(seen[i].id) + "\",\"name\":\"" + jsonEsc(seen[i].name) + "\",\"text\":\"" + jsonEsc(seen[i].text) +
         "\",\"ago\":" + String((long)((now - seen[i].us) / 1000000)) + ",\"rcp\":" + (isRecipient(seen[i].id) ? "true" : "false") + "}";
  }
  xSemaphoreGive(qm);
  return o + "]}";
}

//  Людині, яку додали чи прибрали на сторінці, — коротке підтвердження (1.0.4, власник: додавати й видаляти
//  одержувачів має бути просто й зрозуміло).
void tgWelcome(long long chat) {
  tgReply(chat, "✅ <b>Вас додано до сповіщень</b>\n" + objIco() + htmlEsc(S.objName) +
                "\nСюди приходитимуть повідомлення, коли зникає й повертається міське світло.\nКнопки внизу: «📊 Стан» — що зараз, «🗓 Графік» — графік відключень, "
                "«📈 Статистика» — скільки світло було й не було, «🔔 Сповіщення» — що надсилати саме вам.", 1);
}
void tgFarewell(long long chat) {
  tgReply(chat, "🔕 <b>Сповіщення вимкнено</b>\n" + objIco() + htmlEsc(S.objName) + "\nЩоб отримувати їх знову, попросіть адміністратора.", 2);
}
