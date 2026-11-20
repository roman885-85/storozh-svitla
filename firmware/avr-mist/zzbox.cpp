#include "box.h"
#include "util.h"
#include <esp_system.h>
#include <time.h>

//  Ім'я на «zz»: збірка кладе файли за абеткою, і RTC-змінна тут лягає після всіх інших (облік R, черга RQ, входи RS,
//  графік RY, стан RW) — їхні адреси не зсуваються, записане до оновлення лишається на місці.
static const uint32_t BOX_MAGIC = 0x31584F42;     // «BOX1»
static const int BOXN = 14;
struct BoxLine { uint32_t ep; char t[92]; };      // секунди від 1970 (0 — годинника ще не було) і перший рядок тексту
struct RtcBox {
  uint32_t magic, sum;
  uint32_t at;                                    // коли записано причину
  uint8_t planned, hasWhy, head, n;
  char why[64];
  BoxLine l[BOXN];
};
RTC_NOINIT_ATTR RtcBox BX;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;   // рядки пишуть і основний цикл, і задачі Telegram та звуку

static uint32_t boxSum(const RtcBox& b) {
  const uint8_t* p = (const uint8_t*)&b + 8;      // усе після magic і sum
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof b - 8; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void seal() { BX.sum = boxSum(BX); BX.magic = BOX_MAGIC; }

//  Обрізати до n байтів, не розрізаючи літеру (UTF-8: байти 10xxxxxx — продовження)
static void cut(char* dst, size_t cap, const char* src) {
  size_t L = strlen(src);
  if (L >= cap) {
    L = cap - 1;
    while (L > 0 && ((uint8_t)src[L] & 0xC0) == 0x80) L--;
  }
  memcpy(dst, src, L);
  dst[L] = 0;
}

static String prevWhy;
static bool prevValid = false, prevPlanned = false;
static uint32_t prevAt = 0;
static int prevN = 0;
static BoxLine prev[BOXN];

void boxBegin() {
  prevValid = BX.magic == BOX_MAGIC && BX.sum == boxSum(BX) && BX.n <= BOXN && BX.head < BOXN;
  if (prevValid) {
    BX.why[sizeof BX.why - 1] = 0;
    if (BX.hasWhy) prevWhy = BX.why;
    prevPlanned = BX.planned;
    prevAt = BX.at;
    prevN = BX.n;
    for (int i = 0; i < prevN; i++) {             // від найдавнішого
      prev[i] = BX.l[(BX.head + BOXN - prevN + i) % BOXN];
      prev[i].t[sizeof prev[i].t - 1] = 0;
    }
  }
  memset(&BX, 0, sizeof BX);
  seal();
}

void boxLine(const String& text) {
  String t = text;
  int nl = t.indexOf('\n');
  if (nl >= 0) t = t.substring(0, nl) + " …";      // повідомлення Telegram — лише перший рядок
  uint32_t ep = clockValid() ? (uint32_t)time(nullptr) : 0;
  portENTER_CRITICAL(&mux);
  BoxLine& l = BX.l[BX.head];
  l.ep = ep;
  cut(l.t, sizeof l.t, t.c_str());
  BX.head = (BX.head + 1) % BOXN;
  if (BX.n < BOXN) BX.n++;
  seal();
  portEXIT_CRITICAL(&mux);
}

void boxMark(const String& why, bool planned) {
  logEvent("Перезапуск: " + why);
  uint32_t at = clockValid() ? (uint32_t)time(nullptr) : 0;
  portENTER_CRITICAL(&mux);
  cut(BX.why, sizeof BX.why, why.c_str());
  BX.hasWhy = 1;
  BX.planned = planned ? 1 : 0;
  BX.at = at;
  seal();
  portEXIT_CRITICAL(&mux);
}

void restartWith(const String& why, bool planned) {
  boxMark(why, planned);
  Serial.flush();
  delay(300);
  ESP.restart();
  for (;;) delay(1000);
}

static String hms(uint32_t ep) {
  time_t x = ep;
  struct tm a;
  localtime_r(&x, &a);
  char b[24];
  strftime(b, sizeof b, "%d.%m %H:%M:%S", &a);
  return b;
}

String boxReason() { return prevValid && esp_reset_reason() == ESP_RST_SW ? prevWhy : String(); }
bool boxPlanned() { return prevValid && prevPlanned && esp_reset_reason() == ESP_RST_SW; }

void boxReport() {
  if (!prevValid) return;                         // пам'ять порожня: Сторож був без живлення — нічого не лишилось
  if (boxPlanned() || bootAfterUpdate()) return;  // людина чи оновлення — зрозуміло й так
  String why = boxReason();
  logEvent("Попередній запуск закінчився: " + (why.length() ? why : resetReasonText()) + (prevAt && why.length() ? " (" + hms(prevAt) + ")" : String()));
  if (!prevN) return;
  logEventLocal("Рядки «↳» — журнал перед перезапуском (" + String(prevN) + ", новіші вище)");
  for (int i = 0; i < prevN; i++) logEventLocal("↳ " + (prev[i].ep ? hms(prev[i].ep) + " " : String()) + prev[i].t);
}
