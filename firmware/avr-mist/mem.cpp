#include "mem.h"
#include "util.h"
#include "wifituning.h"
#include <esp_heap_caps.h>

static const uint32_t CAPS = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;   // звичайна пам'ять: без «лише 32-бітної» IRAM

uint32_t memFree() { return heap_caps_get_free_size(CAPS); }
uint32_t memBlock() { return heap_caps_get_largest_free_block(CAPS); }
uint32_t memMinIdf() { return heap_caps_get_minimum_free_size(CAPS); }
uint32_t memIram() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - heap_caps_get_free_size(CAPS); }

static uint32_t minAll = 0xFFFFFFFF, minNow = 0xFFFFFFFF, minPrev = 0xFFFFFFFF;
static uint32_t lastMs = 0, winMs = 0;
void memSample() {
  uint32_t ms = millis();
  if (ms - lastMs < 20) return;
  lastMs = ms;
  uint32_t f = memFree();
  if (f < minAll) minAll = f;
  if (f < minNow) minNow = f;
  if (ms - winMs > 300000) { winMs = ms; minPrev = minNow; minNow = f; }   // вікно 5 хв: «за останні 5–10 хв»
}
uint32_t memMin() { return minAll == 0xFFFFFFFF ? memFree() : minAll; }
uint32_t memMinRecent() { uint32_t m = minNow < minPrev ? minNow : minPrev; return m == 0xFFFFFFFF ? memFree() : m; }

// ---------- кроки запуску ----------
struct Stage { const char* name; uint32_t free; };
static Stage st[14];
static int nst = 0;
void memStage(const char* name) {
  if (nst < (int)(sizeof st / sizeof st[0])) st[nst++] = {name, memFree()};
}
String memBootLine() {
  String o = "Пам'ять на запуску, вільно КБ:";
  for (int i = 0; i < nst; i++) o += String(i ? " → " : " ") + st[i].name + " " + String(st[i].free / 1024);
  return o;
}

// ---------- подробиці ----------
//  Найменший запас стека задач від запуску, Б (на ESP-IDF — у байтах). Наші: loopTask 8192, tg 10240 (до 1.0.38 — 12288), snd 6144,
//  selfchk 4096, ping 3072. Задачі може й не бути — тоді її немає в списку.
String memJson() {
  //  1.0.41: найменше — і з теперішнім значенням: «вільно» міряється вже під час відповіді, а найменше — раз на 20 мс,
  //  і сторінка показувала «вільно» менше за «найменше від запуску»
  uint32_t f = memFree();
  if (f < minAll) minAll = f;
  if (f < minNow) minNow = f;
  static const char* const TASKS[] = {"loopTask", "tg", "snd", "selfchk", "ping", "tiT", "wifi", "sys_evt", "arduino_events", "esp_timer", "Tmr Svc"};
  String o = "{\"free\":" + String(f) + ",\"block\":" + String(memBlock()) + ",\"min\":" + String(memMin()) +
             ",\"minIdf\":" + String(memMinIdf()) + ",\"recent\":" + String(memMinRecent()) + ",\"iram\":" + String(memIram()) +
             ",\"wifi\":" + wifiTuneJson() + ",\"log\":" + logMemJson() + ",\"stk\":{";
  bool first = true;
  for (const char* n : TASKS) {
    TaskHandle_t h = xTaskGetHandle(n);
    if (!h) continue;
    o += String(first ? "" : ",") + "\"" + n + "\":" + String((unsigned)uxTaskGetStackHighWaterMark(h));
    first = false;
  }
  o += "},\"boot\":[";
  for (int i = 0; i < nst; i++) o += String(i ? "," : "") + "[\"" + st[i].name + "\"," + String(st[i].free) + "]";
  return o + "]}";
}
