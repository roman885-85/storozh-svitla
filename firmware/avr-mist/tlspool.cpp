#include "tlspool.h"
#include "mbedtls/platform.h"
#include "esp_heap_caps.h"

//  Буфер прийому чи передачі TLS: 16384 байти вмісту + заголовок і запас шифру (~16,7 КБ). Усе від 16 до 17 КБ — у запас;
//  більше (не буває) чи менше — у купу, як і без запасу (esp_mbedtls_mem_calloc: внутрішня пам'ять, 8-бітний доступ).
static const size_t SLOT = 17 * 1024, MINSZ = 16 * 1024;
static const int NSLOT = 2;                              // одне з'єднання за раз: Telegram закривається перед YASNO (tg.cpp)
static uint8_t pool[NSLOT][SLOT] __attribute__((aligned(16)));
static volatile bool busy[NSLOT];
static volatile uint32_t hits = 0, miss = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;  // TLS живе в задачі Telegram, але виділяти може й інше (Wi-Fi)

static void* pCalloc(size_t n, size_t sz) {
  size_t t = n * sz;
  if (sz && t / sz != n) return nullptr;
  if (t >= MINSZ && t <= SLOT) {
    int k = -1;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < NSLOT; i++)
      if (!busy[i]) { busy[i] = true; k = i; break; }
    if (k >= 0) hits++; else miss++;
    portEXIT_CRITICAL(&mux);
    if (k >= 0) { memset(pool[k], 0, t); return pool[k]; }
  }
  return heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void pFree(void* p) {
  if (!p) return;
  uint8_t* b = (uint8_t*)p;
  if (b >= &pool[0][0] && b < &pool[0][0] + sizeof pool) {
    int k = (int)((b - &pool[0][0]) / SLOT);
    portENTER_CRITICAL(&mux);
    busy[k] = false;
    portEXIT_CRITICAL(&mux);
    return;
  }
  heap_caps_free(p);                                     // і те, що виділили до tlsPoolBegin(), — так само, як звільнило б ядро
}

void tlsPoolBegin() { mbedtls_platform_set_calloc_free(pCalloc, pFree); }
int tlsPoolBusy() { int n = 0; for (int i = 0; i < NSLOT; i++) if (busy[i]) n++; return n; }
String tlsPoolJson() {
  return "{\"slots\":" + String(NSLOT) + ",\"busy\":" + String(tlsPoolBusy()) + ",\"hits\":" + String((unsigned)hits) +
         ",\"miss\":" + String((unsigned)miss) + "}";
}
