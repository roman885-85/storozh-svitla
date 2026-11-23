#include "util.h"
#include "box.h"
#include "logring.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <sys/time.h>

//  Журнал (1.0.38) — кільце в пам'яті команд (IRAM), а не 80 рядків String у звичайній: див. logring.h.
//  Беремо шматок лише якщо він справді в IRAM (0x40080000–0x400A0000); дали звичайну пам'ять — повертаємо й пробуємо
//  менший, а зовсім без IRAM — малий журнал у звичайній пам'яті.
static LogRing ring;
static bool ringIram = false;
static const int LOGSHOW = 250;                  // більше рядків за раз не віддаємо
static SemaphoreHandle_t logMtx = nullptr;
static void ringBegin() {
  //  1.0.39: з кроком 512 Б — скільки справді є (у 1.0.38 30 720 не вмістилось, і 6 КБ пам'яті команд лишались порожні)
  for (uint32_t sz = 31232; sz >= 8192; sz -= 512) {
    void* p = heap_caps_malloc(sz, MALLOC_CAP_32BIT);
    if (!p) continue;
    uintptr_t a = (uintptr_t)p;
    if (a >= 0x40080000UL && a + sz <= 0x400A0000UL) { ring.begin((uint32_t*)p, sz); ringIram = true; return; }
    heap_caps_free(p);
  }
  void* p = heap_caps_malloc(6144, MALLOC_CAP_32BIT);
  if (p) ring.begin((uint32_t*)p, 6144);
}
String logMemJson() {
  return String("{\"iram\":") + (ringIram ? "true" : "false") + ",\"cap\":" + String(ring.capacity()) + ",\"used\":" + String(ring.usedBytes()) +
         ",\"n\":" + String(ring.size()) + "}";
}
//  Один запис (i-й від найновішого) у звичайну пам'ять читача: текст — не довше 300 Б (так і записано) + запас
static bool logGet(int i, int64_t& us, String& text) {
  char buf[340];
  if (i < 0 || i >= LOGSHOW || ring.get((uint32_t)i, us, buf, sizeof buf) < 0) return false;
  text = buf;
  return true;
}

void utilBegin() {
  logMtx = xSemaphoreCreateMutex();
  ringBegin();
  setenv("TZ", TZ_KYIV, 1);
  tzset();
  boxBegin();                                    // 1.0.29: що лишив попередній запуск у RTC-пам'яті
}

bool clockValid() { return time(nullptr) > 1735689600; }   // пізніше 01.01.2025

void clockFromEpoch(time_t t, const char* src) {
  if (clockValid() || t < 1735689600) return;
  timeval tv = { t, 0 };
  settimeofday(&tv, nullptr);
  logEvent(String("Годинник встановлено за ") + src);
}

String fmtDur(int64_t s) {
  if (s < 0) s = 0;
  long long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
  char b[48];
  if (d > 0)       { if (h) snprintf(b, sizeof b, "%lld д %lld год", d, h); else snprintf(b, sizeof b, "%lld д", d); }
  else if (h > 0)  { if (m) snprintf(b, sizeof b, "%lld год %lld хв", h, m); else snprintf(b, sizeof b, "%lld год", h); }
  else if (m > 0)  snprintf(b, sizeof b, "%lld хв", m);
  else             snprintf(b, sizeof b, "%lld с", sec);
  return b;
}

String fmtClock(int64_t us) {
  int64_t ago = (nowUs() - us) / 1000000;
  if (ago < 0) ago = 0;
  if (!clockValid()) return ago < 60 ? String("щойно") : fmtDur(ago) + " тому";
  time_t n = time(nullptr), t = n - (time_t)ago;
  struct tm a, b;
  localtime_r(&t, &a);
  localtime_r(&n, &b);
  char buf[40];
  if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) strftime(buf, sizeof buf, "о %H:%M", &a);
  else strftime(buf, sizeof buf, "%d.%m о %H:%M", &a);
  return buf;
}

String fmtNowFull() {
  if (!clockValid()) return "невідомий";
  time_t n = time(nullptr);
  struct tm a;
  localtime_r(&n, &a);
  char b[32];
  strftime(b, sizeof b, "%d.%m.%Y %H:%M:%S", &a);
  return b;
}

String jsonEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", (unsigned)(uint8_t)c); o += b; }
        else o += c;
    }
  }
  return o;
}

String macToStr(const uint8_t m[6]) {
  char b[18];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

bool strToMac(const String& s, uint8_t m[6]) {
  unsigned v[6];
  if (s.length() < 11) return false;
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6 &&
      sscanf(s.c_str(), "%x-%x-%x-%x-%x-%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) { if (v[i] > 255) return false; m[i] = (uint8_t)v[i]; }
  return true;
}

static bool afterUpdate = false;
void markBootAfterUpdate() { afterUpdate = true; }
bool bootAfterUpdate() { return afterUpdate; }

String resetReasonText() {
  if (afterUpdate) return "оновлення прошивки";
  String why = boxReason();                      // 1.0.29: Сторож сам записав, чому перезапускається
  if (why.length()) return why;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "увімкнення живлення (Сторож був без живлення)";
    case ESP_RST_SW:       return "програмний перезапуск без записаної причини";
    case ESP_RST_PANIC:    return "збій програми";
    //  1.0.41: який саме сторож — щоб із журналу було видно, що шукати (до того — один текст на всі три)
    case ESP_RST_TASK_WDT: return "спрацював сторож задач — програма зависла";
    case ESP_RST_INT_WDT:  return "спрацював сторож переривань — програма зависла";
    case ESP_RST_WDT:      return "спрацював апаратний сторож — програма зависла";
    case ESP_RST_BROWNOUT: return "просідання живлення";
    case ESP_RST_EXT:      return "кнопка скидання";
    default:               return "інша причина";
  }
}

void logEvent(const String& text) {
  boxLine(text);                                 // 1.0.29: і в «чорну скриньку» — переживе перезапуск
  logEventLocal(text);
}
//  1.0.32: у журналі — не довше 300 байтів (повідомлення Telegram цілком тут не потрібні): менше подрібнення пам'яті
static String capped(const String& t) {
  if (t.length() <= 300) return t;
  size_t L = 297;
  while (L > 0 && ((uint8_t)t[L] & 0xC0) == 0x80) L--;
  return t.substring(0, L) + "…";
}
void logEventLocal(const String& text) {
  Serial.printf("[%s] %s\n", fmtClock(nowUs()).c_str(), text.c_str());
  if (!logMtx) return;
  String c = capped(text);
  xSemaphoreTake(logMtx, portMAX_DELAY);
  ring.put(nowUs(), c.c_str(), c.length());
  xSemaphoreGive(logMtx);
}

//  1.0.32: один запис журналу (i-й від найновішого) — сторінка шле їх по одному, а не одним рядком на 10–20 КБ
String logEntryJson(int i) {
  if (!logMtx) return String();
  int64_t now = nowUs();
  int64_t us = 0;
  String x;
  xSemaphoreTake(logMtx, portMAX_DELAY);
  bool ok = logGet(i, us, x);
  xSemaphoreGive(logMtx);
  if (!ok) return String();
  return "{\"t\":\"" + jsonEsc(fmtClock(us)) + "\",\"ago\":" + String((long)((now - us) / 1000000)) + ",\"x\":\"" + jsonEsc(x) + "\"}";
}

String logJson(int maxN) {
  String o = "[";
  if (!logMtx) return "[]";
  for (int i = 0; i < maxN; i++) {
    String e = logEntryJson(i);
    if (!e.length()) break;
    if (i) o += ',';
    o += e;
  }
  return o + "]";
}

String logText(int maxN, size_t maxLen) {
  String o;
  if (!logMtx) return o;
  for (int i = 0; i < maxN; i++) {
    int64_t us = 0;
    String t;
    xSemaphoreTake(logMtx, portMAX_DELAY);
    bool ok = logGet(i, us, t);
    xSemaphoreGive(logMtx);
    if (!ok) break;
    int nl = t.indexOf('\n');
    if (nl > 0) t = t.substring(0, nl) + " …";          // повідомлення Telegram у журналі — лише перший рядок
    String line = "\n<code>" + fmtHM(us) + "</code> " + htmlEsc(t.substring(0, 160));
    if (o.length() + line.length() > maxLen) break;
    o += line;
  }
  return o;
}

String fmtHM(int64_t us) {
  int64_t ago = (nowUs() - us) / 1000000;
  if (ago < 0) ago = 0;
  if (!clockValid()) return ago < 60 ? String("щойно") : fmtDur(ago) + " тому";
  time_t n = time(nullptr), t = n - (time_t)ago;
  struct tm a, b;
  localtime_r(&t, &a);
  localtime_r(&n, &b);
  char buf[32];
  strftime(buf, sizeof buf, (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) ? "%H:%M" : "%d.%m %H:%M", &a);
  return buf;
}

String htmlEsc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else o += c;
  }
  return o;
}

String stripTags(const String& s) {
  String o;
  o.reserve(s.length());
  bool tag = false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') tag = true;
    else if (c == '>' && tag) tag = false;
    else if (!tag) o += c;
  }
  o.replace("&lt;", "<");
  o.replace("&gt;", ">");
  o.replace("&amp;", "&");
  return o;
}

String fmtHours(int64_t s) {
  if (s < 0) s = 0;
  long long h = s / 3600, m = (s % 3600) / 60;
  char b[32];
  if (h && m) snprintf(b, sizeof b, "%lld год %lld хв", h, m);
  else if (h) snprintf(b, sizeof b, "%lld год", h);
  else if (m) snprintf(b, sizeof b, "%lld хв", m);
  else snprintf(b, sizeof b, s ? "менше хвилини" : "0 хв");
  return b;
}

