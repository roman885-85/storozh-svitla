#include "util.h"
#include "box.h"
#include <esp_system.h>
#include <sys/time.h>

struct Ev { int64_t us; String text; };
static const int LOGN = 80;
static Ev evs[LOGN];
static int evHead = 0, evCount = 0;
static SemaphoreHandle_t logMtx = nullptr;

void utilBegin() {
  logMtx = xSemaphoreCreateMutex();
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
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "спрацював сторож — програма зависла";
    case ESP_RST_BROWNOUT: return "просідання живлення";
    case ESP_RST_EXT:      return "кнопка скидання";
    default:               return "інша причина";
  }
}

void logEvent(const String& text) {
  boxLine(text);                                 // 1.0.29: і в «чорну скриньку» — переживе перезапуск
  logEventLocal(text);
}
void logEventLocal(const String& text) {
  Serial.printf("[%s] %s\n", fmtClock(nowUs()).c_str(), text.c_str());
  if (!logMtx) return;
  xSemaphoreTake(logMtx, portMAX_DELAY);
  evs[evHead].us = nowUs();
  evs[evHead].text = text;
  evHead = (evHead + 1) % LOGN;
  if (evCount < LOGN) evCount++;
  xSemaphoreGive(logMtx);
}

String logJson(int maxN) {
  String o = "[";
  if (!logMtx) return "[]";
  xSemaphoreTake(logMtx, portMAX_DELAY);
  int n = maxN < evCount ? maxN : evCount;
  int64_t now = nowUs();
  for (int i = 0; i < n; i++) {
    int idx = (evHead - 1 - i + LOGN) % LOGN;
    if (i) o += ',';
    o += "{\"t\":\"" + jsonEsc(fmtClock(evs[idx].us)) + "\",\"ago\":" + String((long)((now - evs[idx].us) / 1000000)) +
         ",\"x\":\"" + jsonEsc(evs[idx].text) + "\"}";
  }
  xSemaphoreGive(logMtx);
  return o + "]";
}

String logText(int maxN, size_t maxLen) {
  String o;
  if (!logMtx) return o;
  xSemaphoreTake(logMtx, portMAX_DELAY);
  int n = maxN < evCount ? maxN : evCount;
  for (int i = 0; i < n; i++) {
    int idx = (evHead - 1 - i + LOGN) % LOGN;
    String t = evs[idx].text;
    int nl = t.indexOf('\n');
    if (nl > 0) t = t.substring(0, nl) + " …";          // повідомлення Telegram у журналі — лише перший рядок
    String line = "\n<code>" + fmtHM(evs[idx].us) + "</code> " + htmlEsc(t.substring(0, 160));
    if (o.length() + line.length() > maxLen) break;
    o += line;
  }
  xSemaphoreGive(logMtx);
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

