#include "signals.h"
#include "settings.h"
#include "util.h"
#include <LittleFS.h>
#include <math.h>
#include "driver/dac_continuous.h"
#include "esp_random.h"

// ---------- виводи ----------
//  Без 0 (кнопка BOOT — скидання налаштувань), 1/3 (порт), 5/12/15 (вивід стану при запуску), 6–11 (флеш), 34–39 (лише вхід).
static const int8_t OK_PINS[] = {2, 4, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
bool sigPinOk(int p) {
  for (int8_t x : OK_PINS) if (x == p) return true;
  return false;
}
static void pinIdle(int p) {                    // вивід, що звільняється, — у низький рівень, щоб нічого не пищало й не світилось
  if (p > 0 && p == S.pwBtn) return;              // вивід кнопки скидання пароля: низький рівень на ньому = «натиснута» (1.0.15)
  if (p >= 0 && sigPinOk(p)) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
}

static bool quietNow();
static bool nightNow();

// ---------- адресна стрічка WS2812B (1.0.13) ----------
//  Один вивід — будь-які кольори. Рекомендовано відрізок на 8 світлодіодів: видно здалеку, при 30 % ≈ 0,2 А від ДБЖ.
//  Тайминги як у ядра (esp32-hal-rgb-led.c): RMT 10 МГц, «1» — 0,8/0,4 мкс, «0» — 0,4/0,8 мкс. Апаратно — Wi-Fi не заважає.
static const int STRIP_MAX = 60;
static int stripPin = -1, stripN = 0;
static rmt_data_t* stripBuf = nullptr;
static uint8_t gam[256];                          // гама 2,2 — щоб «дихання» йшло рівно, а не стрибками внизу
static volatile int animType = 0;                 // 1 — спалахи «зникло», 2 — хвиля «з'явилось»
static volatile int64_t animStart = 0;
static volatile bool animTest = false;            // 1.0.28: анімацію запустила кнопка перевірки — світить і вночі
static int64_t stripNext = 0;
static void stripFree() {
  if (stripPin >= 0) { rmtDeinit(stripPin); pinIdle(stripPin); }
  stripPin = -1;
  free(stripBuf);
  stripBuf = nullptr;
}
static bool stripInit() {
  stripFree();
  if (!S.ledOn || S.ledType != 1 || !sigPinOk(S.stripPin)) return false;
  stripN = S.stripN < 1 ? 1 : S.stripN > STRIP_MAX ? STRIP_MAX : S.stripN;
  stripBuf = (rmt_data_t*)malloc(sizeof(rmt_data_t) * 24 * stripN);
  if (!stripBuf) return false;
  if (!rmtInit(S.stripPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) { free(stripBuf); stripBuf = nullptr; return false; }
  stripPin = S.stripPin;
  return true;
}
static void stripShow(const uint8_t (*px)[3]) {
  if (stripPin < 0) return;
  int k = 0;
  for (int i = 0; i < stripN; i++) {
    uint8_t r = px[i][0], g = px[i][1], b = px[i][2], c[3];
    switch (S.stripOrder) {
      case 1: c[0] = r; c[1] = g; c[2] = b; break;     // RGB
      case 2: c[0] = b; c[1] = g; c[2] = r; break;     // BGR
      default: c[0] = g; c[1] = r; c[2] = b; break;    // GRB — звичайний WS2812B
    }
    for (int ch = 0; ch < 3; ch++)
      for (int bit = 7; bit >= 0; bit--) {
        bool one = c[ch] & (1 << bit);
        stripBuf[k].level0 = 1;
        stripBuf[k].duration0 = one ? 8 : 4;
        stripBuf[k].level1 = 0;
        stripBuf[k].duration1 = one ? 4 : 8;
        k++;
      }
  }
  rmtWrite(stripPin, stripBuf, k, 100);
}
//  Безперервна хвиля (1.0.22): яскравість біжить уздовж стрічки синусоїдою, один гребінь на всю стрічку.
//  Швидкість 1–10 — від 8 с до 0,8 с на прохід; інтенсивність — наскільки гасне западина (100 % — до нуля).
static void waveLv(float* lv, uint32_t t) {
  const float TWO_PI_F = 6.2831853f;
  uint32_t T = 8000u / (S.stWvSpd < 1 ? 1 : S.stWvSpd > 10 ? 10 : S.stWvSpd);
  float I = (S.stWvInt > 100 ? 100 : S.stWvInt) / 100.0f;
  int L = stripN < 4 ? 4 : stripN;
  float ph0 = (float)(t % T) / T;
  for (int i = 0; i < stripN; i++) lv[i] = 1.0f - I * (0.5f - 0.5f * cosf(TWO_PI_F * ((float)i / L - ph0)));
}
static void stripFrame(int mode, int line, float nightK) {
  static uint8_t px[STRIP_MAX][3];
  float lv[STRIP_MAX];
  uint32_t t = millis(), col = S.colUnk;
  uint16_t per = S.ledBlink < 200 ? 200 : S.ledBlink;
  const float TWO_PI_F = 6.2831853f;
  if (mode == 1 && line == 1) {                      // місто є, а генератор ще працює — зелений перемежовується з жовтим (1.0.17)
    col = (t % per) < per / 2 ? S.colOn : S.colGen;
    for (int i = 0; i < stripN; i++) lv[i] = 1.0f;
  } else if (mode == 1) {                            // світло є — рівний колір або хвиля
    col = S.colOn;
    if (S.stFxOn == 1) waveLv(lv, t);
    else for (int i = 0; i < stripN; i++) lv[i] = 1.0f;
  } else if (mode == 0 && line == 1) {               // міста немає, але працює генератор — бурштин, рівно або хвилею
    col = S.colGen;
    if (S.stFxGen == 1) waveLv(lv, t);
    else for (int i = 0; i < stripN; i++) lv[i] = 1.0f;
  } else if (mode == 0) {                            // світла немає
    col = S.colOff;
    if (S.stripFx == 3) waveLv(lv, t);               // 1.0.22: безперервна хвиля
    else if (S.stripFx == 2 && stripN > 1) {         // біжить вогник із хвостом
      int head = (t / 90) % stripN;
      for (int i = 0; i < stripN; i++) {
        int d = (head - i + stripN) % stripN;
        lv[i] = d == 0 ? 1.0f : d == 1 ? 0.4f : d == 2 ? 0.12f : 0.03f;
      }
    } else {
      float k = S.stripFx == 0 ? ((t % per) < per / 2 ? 1.0f : 0.0f)                          // блимає
                               : 0.12f + 0.88f * (0.5f - 0.5f * cosf(TWO_PI_F * (t % (2 * per)) / (2.0f * per)));  // дихає
      for (int i = 0; i < stripN; i++) lv[i] = k;
    }
  } else {                                           // невідомо: тьмяне повільне дихання
    float k = 0.12f + 0.18f * (0.5f - 0.5f * cosf(TWO_PI_F * (t % 3000) / 3000.0f));
    for (int i = 0; i < stripN; i++) lv[i] = k;
  }
  if (animType) {                                    // подія поверх стану
    uint32_t a = (uint32_t)((nowUs() - animStart) / 1000);
    if (animType == 1) {
      if (a >= 1200) animType = 0;
      else { col = S.colOff; float k = (a % 400) < 150 ? 1.0f : 0.0f; for (int i = 0; i < stripN; i++) lv[i] = k; }
    } else {
      if (a >= 1000) animType = 0;
      else { col = S.colOn; for (int i = 0; i < stripN; i++) lv[i] = a >= (uint32_t)(i * 800 / stripN) ? 1.0f : 0.0f; }
    }
  }
  float master = S.stripBri / 100.0f * nightK;
  uint8_t cr = col >> 16, cg = col >> 8, cb = col;
  uint32_t sum = 0;
  for (int i = 0; i < stripN; i++) {
    float k = lv[i] * master;
    if (k > 1) k = 1;
    px[i][0] = gam[(uint8_t)(cr * k)];
    px[i][1] = gam[(uint8_t)(cg * k)];
    px[i][2] = gam[(uint8_t)(cb * k)];
    sum += px[i][0] + px[i][1] + px[i][2];
  }
  float ma = sum * 20.0f / 255.0f;                   // ≈ 20 мА на канал при повній яскравості
  if (ma > S.stripMa && ma > 0) {
    float s = S.stripMa / ma;
    for (int i = 0; i < stripN; i++)
      for (int ch = 0; ch < 3; ch++) px[i][ch] = (uint8_t)(px[i][ch] * s);
  }
  stripShow(px);
}

// ---------- світлодіоди ----------
static int ledG = -1, ledR = -1;
static bool gOn = false, rOn = false, gWant = false, rWant = false;
static int64_t testUntil = 0;
static int testWhat = 0;                         // 1 — зелений, 2 — червоний
static int ledPct = -1;                          // з якою яскравістю світять зараз, % (1.0.28: уночі — менше чи 0)
static void ledOut(int pin, bool on, int pct) {
  if (pin < 0) return;
  uint32_t d = on ? (uint32_t)pct * 255 / 100 : 0;
  ledcWrite(pin, S.ledLow ? 255 - d : d);
}
static void ledInit() {
  if (ledG >= 0) { ledcDetach(ledG); pinIdle(ledG); }
  if (ledR >= 0) { ledcDetach(ledR); pinIdle(ledR); }
  ledG = ledR = -1;
  gOn = rOn = false;
  ledPct = -1;
  stripFree();
  if (!S.ledOn) return;
  if (S.ledType == 1) {
    if (!stripInit()) logEvent("Стрічка: не вдалося зайняти вивід " + String(S.stripPin));
    return;
  }
  if (sigPinOk(S.ledG) && ledcAttach(S.ledG, 5000, 8)) ledG = S.ledG;
  if (sigPinOk(S.ledR) && S.ledR != S.ledG && ledcAttach(S.ledR, 5000, 8)) ledR = S.ledR;
  ledOut(ledG, false, 0);
  ledOut(ledR, false, 0);
  if (ledG < 0 || ledR < 0) logEvent("Світлодіоди: не вдалося зайняти вивід " + String(ledG < 0 ? S.ledG : S.ledR));
}
static volatile int gridNow = -1;
static volatile int ledMode = -1;               // що зараз показують світлодіоди: 1 — зелений, 0 — червоний блимає, -1 — нічого
static volatile bool ledDark = false;           // 1.0.28: зараз ніч — індикатор погашено
void sigLoop(int st, int line) {
  gridNow = st;
  int mode = st;                                 // 1 — зелений світить, 0 — червоний блимає, -1 — обидва згасли
  if (testUntil) {
    if (nowUs() < testUntil) mode = testWhat == 1 ? 1 : 0;
    else testUntil = 0;
  }
  if (!animType) animTest = false;
  //  Ніч (1.0.28): світлодіоди й стрічка гаснуть (nLedOff) або світять на stripNight % від денної яскравості.
  //  Перевірки зі сторінки («Показати…», «Спалахи», «Хвиля») світять і вночі — інакше вночі нічого не перевірити.
  bool test = testUntil || (animType && animTest);
  float nightK = (!test && nightNow()) ? (S.nLedOff ? 0.0f : S.stripNight / 100.0f) : 1.0f;
  ledDark = S.ledOn && nightK <= 0.0f;
  ledMode = S.ledOn ? mode : -1;
  if (S.ledOn && S.ledType == 1) {
    int64_t n = nowUs();
    if (n >= stripNext) { stripNext = n + 20000; stripFrame(mode, line, nightK); }   // 50 кадрів/с
    return;
  }
  uint16_t per = S.ledBlink < 200 ? 200 : S.ledBlink;
  //  Два світлодіоди (1.0.17): зелений світить — місто є; зелений блимає — місто є, а генератор ще працює;
  //  червоний світить — міста немає, працює генератор; червоний блимає — світла немає зовсім.
  bool ph = (millis() % per) < per / 2;
  int pct = (int)lroundf(S.ledBri * nightK);
  gWant = pct > 0 && mode == 1 && (line == 1 ? ph : true);
  rWant = pct > 0 && mode == 0 && (line == 1 ? true : ph);
  bool re = pct != ledPct;                       // яскравість змінилась (настала ніч чи ранок) — переписати обидва
  ledPct = pct;
  if (gWant != gOn || re) { ledOut(ledG, gWant, pct); gOn = gWant; }
  if (rWant != rOn || re) { ledOut(ledR, rWant, pct); rOn = rWant; }
}

// ---------- звук: загальне ----------
struct SndReq { uint8_t ev; bool test; int8_t vol; };   // ev: 0 — «світло зникло», 1 — «світло є»; vol: -1 — збережена гучність
static volatile int playVol = -1;               // гучність того, що грає зараз: перевірка може йти з повзунка, ще не збереженою (1.0.16)
static int curVol() { return playVol >= 0 ? playVol : S.sndVol; }
static QueueHandle_t sq = nullptr;
static volatile bool sndBusy = false, sndAbort = false;
static char lastPlayed[64] = "";                // пише задача звуку, читає сторінка — без String, щоб без гонок
//  Нічні години qFrom–qTo (1.0.28): діють на світлодіоди, стрічку й підсвітку дисплея завжди, на звук — лише з qOn.
//  Години однакові — ночі немає (раніше «з 22 до 22» означало «ніч цілодобово»). Годинника ще немає — день.
static volatile int64_t nightDemo = 0;          // «Показати ніч» зі сторінки: до цієї миті — як уночі (15 с)
static bool nightNow() {
  int64_t now = nowUs();
  if (nightDemo && now < nightDemo) return true;
  static int64_t at = -1;                        // цикл кличе ~400 разів за секунду — годину перевіряємо раз на секунду
  static bool v = false;
  if (at >= 0 && now - at < 1000000) return v;
  at = now;
  if (S.qFrom == S.qTo || !clockValid()) return v = false;
  time_t n = time(nullptr);
  struct tm a;
  localtime_r(&n, &a);
  int h = a.tm_hour;
  return v = S.qFrom < S.qTo ? (h >= S.qFrom && h < S.qTo) : (h >= S.qFrom || h < S.qTo);
}
static bool quietNow() { return S.qOn && nightNow(); }
bool sigNightNow() { return nightNow(); }
static bool waitMs(uint32_t ms) {               // false — перервано
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { if (sndAbort) return false; vTaskDelay(pdMS_TO_TICKS(ms > 20 ? 10 : 1)); }
  return true;
}
static float volGain() { float v = curVol() / 100.0f; return v * v; }   // гучність на слух

// ---------- активний бузер: шаблон «звук, пауза, звук…» (мс) ----------
static bool parseInts(const String& s, int* out, int maxN, int& n) {
  n = 0;
  int i = 0, L = s.length();
  while (i < L) {
    while (i < L && (s[i] == ' ' || s[i] == ',' || s[i] == ';')) i++;
    if (i >= L) break;
    int v = 0, k = 0;
    while (i < L && isdigit((unsigned char)s[i])) { v = v * 10 + (s[i] - '0'); i++; k++; if (v > 10000) return false; }
    if (!k || n >= maxN) return false;
    if (i < L && s[i] != ' ' && s[i] != ',' && s[i] != ';') return false;
    out[n++] = v;
  }
  return n > 0;
}
bool sigPatternOk(const String& p, String& err) {
  int v[40], n;
  if (!parseInts(p, v, 40, n)) { err = "Шаблон: числа в мілісекундах через кому, до 40 штук, кожне до 10000 (звук, пауза, звук…)"; return false; }
  return true;
}
static void playPattern(const String& pat) {
  int v[40], n;
  if (!parseInts(pat, v, 40, n) || !sigPinOk(S.sndPin)) return;
  int pin = S.sndPin;
  if (!ledcAttach(pin, 25000, 8)) return;        // 25 кГц: гучність — шпаруватість живлення бузера
  uint32_t duty = (uint32_t)(255.0f * (curVol() / 100.0f));
  for (int i = 0; i < n; i++) {
    ledcWrite(pin, (i % 2 == 0) ? duty : 0);
    if (!waitMs(v[i])) break;
  }
  ledcWrite(pin, 0);
  ledcDetach(pin);
  pinIdle(pin);
}

// ---------- пасивний бузер: мелодія «E5:220 C5:220 A4:700», «R:200» — пауза, «880:300» — частота ----------
struct Note { uint16_t hz, ms; };
static int parseMelody(const String& m, Note* out, int maxN) {   // -1 — помилка
  int n = 0, i = 0, L = m.length();
  while (i < L) {
    while (i < L && (m[i] == ' ' || m[i] == ',' || m[i] == ';')) i++;
    if (i >= L) break;
    int j = i;
    while (j < L && m[j] != ' ' && m[j] != ',' && m[j] != ';') j++;
    String t = m.substring(i, j);
    i = j;
    int c = t.indexOf(':');
    if (c <= 0 || n >= maxN) return -1;
    String a = t.substring(0, c), b = t.substring(c + 1);
    long ms = b.toInt();
    if (ms <= 0 || ms > 5000) return -1;
    long hz = 0;
    if (a == "R" || a == "r" || a == "-") hz = 0;
    else if (isdigit((unsigned char)a[0])) { hz = a.toInt(); if (hz < 100 || hz > 10000) return -1; }
    else {
      static const char* names = "C D EF G A B";
      char ch = toupper(a[0]);
      const char* p = strchr(names, ch);
      if (!p || ch == ' ') return -1;
      int semi = p - names, k = 1;
      if (k < (int)a.length() && a[k] == '#') { semi++; k++; }
      else if (k < (int)a.length() && a[k] == 'b') { semi--; k++; }
      if (k >= (int)a.length() || !isdigit((unsigned char)a[k])) return -1;
      int oct = a.substring(k).toInt();
      if (oct < 1 || oct > 8) return -1;
      int midi = (oct + 1) * 12 + semi;
      hz = lroundf(440.0f * powf(2.0f, (midi - 69) / 12.0f));
      if (hz < 30 || hz > 10000) return -1;
    }
    out[n].hz = (uint16_t)hz;
    out[n].ms = (uint16_t)ms;
    n++;
  }
  return n ? n : -1;
}
bool sigMelodyOk(const String& m, String& err) {
  Note x[40];
  if (parseMelody(m, x, 40) < 0) { err = "Мелодія: ноти через пробіл, кожна «нота:мс» (C5:200, F#4:300, R:150 — пауза, 880:200 — частота), до 40 нот"; return false; }
  return true;
}
static void playMelody(const String& mel) {
  Note x[40];
  int n = parseMelody(mel, x, 40);
  if (n < 0 || !sigPinOk(S.sndPin)) return;
  int pin = S.sndPin;
  if (!ledcAttach(pin, 1000, 10)) return;
  uint32_t duty = (uint32_t)(512.0f * (curVol() / 100.0f));    // 50 % — найгучніше
  for (int i = 0; i < n; i++) {
    if (x[i].hz) { ledcChangeFrequency(pin, x[i].hz, 10); ledcWrite(pin, duty); }
    else ledcWrite(pin, 0);
    if (!waitMs(x[i].ms > 20 ? x[i].ms - 15 : x[i].ms)) break;
    ledcWrite(pin, 0);                                         // коротка пауза між нотами — чути кожну
    if (!waitMs(15)) break;
  }
  ledcWrite(pin, 0);
  ledcDetach(pin);
  pinIdle(pin);
}

// ---------- динамік через ЦАП: 16 кГц, 8 біт ----------
//  Вбудовані звуки синтезуються на льоту: дзвін — кілька неспівмірних частот із загасанням, як у справжнього дзвону;
//  м'який сигнал — синус із легкою другою гармонікою. Плавний наступ (без клацання), дизеринг ±1 молодший розряд —
//  щоб тихі хвости не рипіли на 8 бітах. На початку й наприкінці вихід плавно йде від 0 до середини й назад.
static const int FS = 16000;
static float sinT[1024];
static dac_continuous_handle_t dh = nullptr;
static bool dacOpen() {
  dac_continuous_config_t c = {};
  c.chan_mask = S.dacPin == 26 ? DAC_CHANNEL_MASK_CH1 : DAC_CHANNEL_MASK_CH0;
  c.desc_num = 4;
  c.buf_size = 1024;
  c.freq_hz = FS;
  c.offset = 0;
  c.clk_src = DAC_DIGI_CLK_SRC_APLL;             // типове джерело не опускається нижче 19,6 кГц
  c.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  if (dac_continuous_new_channels(&c, &dh) != ESP_OK) { dh = nullptr; return false; }
  if (dac_continuous_enable(dh) != ESP_OK) { dac_continuous_del_channels(dh); dh = nullptr; return false; }
  return true;
}
static void dacPut(uint8_t* b, size_t n) {
  size_t done = 0;
  if (dh) dac_continuous_write(dh, b, n, &done, 1000);
}
static void dacRamp(bool up) {                   // 60 мс від 0 до середини (або назад) — без удару в динаміку
  uint8_t b[960];
  for (int i = 0; i < 960; i++) b[i] = (uint8_t)(up ? (128 * i / 960) : (128 - 128 * i / 960));
  dacPut(b, sizeof b);
}
static void dacClose() {
  if (!dh) return;
  dac_continuous_disable(dh);
  dac_continuous_del_channels(dh);
  dh = nullptr;
  pinIdle(S.dacPin);
}
static inline uint8_t toU8(float y) {            // -1…1 → 0…255 з трикутним дизерингом
  float d = ((esp_random() & 0xFFFF) + (esp_random() & 0xFFFF)) / 65536.0f - 1.0f;
  int v = (int)lroundf(128.0f + y * 126.0f + d);
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
struct Part { uint32_t ph, inc; float amp, env, k; int att, n; };
struct Hit { int at; Part p[5]; int np; };
static void addHit(Hit& h, int at, float hz, bool bell, float decayMul) {
  static const float R[5] = {1.0f, 2.003f, 2.76f, 4.07f, 5.41f};      // частоти дзвону (неспівмірні)
  static const float A[5] = {1.0f, 0.50f, 0.28f, 0.14f, 0.07f};
  static const float T[5] = {1.5f, 0.85f, 0.55f, 0.38f, 0.26f};      // загасання, с
  h.at = at;
  h.np = bell ? 5 : 2;
  for (int i = 0; i < h.np; i++) {
    Part& p = h.p[i];
    float f = bell ? hz * R[i] : hz * (i + 1);
    if (f > FS * 0.45f) { h.np = i; break; }
    p.ph = 0;
    p.inc = (uint32_t)(f / FS * 4294967296.0f);
    p.amp = bell ? A[i] : (i ? 0.15f : 1.0f);
    float tau = (bell ? T[i] : 0.55f) * decayMul;
    p.k = expf(-1.0f / (tau * FS));
    p.att = (int)((bell ? 0.004f : 0.018f) * FS);
    p.env = 0;
    p.n = 0;
  }
}
static void synth(int ev, bool bell) {
  //  «світло зникло» — униз: E5 C5 A4; «світло є» — угору: C5 E5 G5 C6
  static const float LOSTB[3] = {659.26f, 523.25f, 440.00f}, BACKB[4] = {523.25f, 659.26f, 783.99f, 1046.50f};
  static const float LOSTS[3] = {659.26f, 493.88f, 392.00f}, BACKS[3] = {523.25f, 659.26f, 783.99f};
  Hit h[4];
  int nh = 0, total;
  if (ev == 0) {
    const float* f = bell ? LOSTB : LOSTS;
    for (int i = 0; i < 3; i++) addHit(h[nh++], (int)(i * (bell ? 0.42f : 0.36f) * FS), f[i], bell, 1.0f);
    total = (int)((bell ? 2.45f : 2.0f) * FS);
  } else {
    const float* f = bell ? BACKB : BACKS;
    int n = bell ? 4 : 3;
    for (int i = 0; i < n; i++) addHit(h[nh++], (int)(i * (bell ? 0.16f : 0.20f) * FS), f[i], bell, bell ? 0.75f : 0.8f);
    total = (int)((bell ? 1.75f : 1.5f) * FS);
  }
  const float g = 0.30f * volGain();             // три удари дзвону разом не впираються в стелю й на 100 %
  uint8_t buf[512];
  for (int s = 0; s < total && !sndAbort; s += 512) {
    int m = total - s < 512 ? total - s : 512;
    for (int j = 0; j < m; j++) {
      int t = s + j;
      float x = 0;
      for (int a = 0; a < nh; a++) {
        if (t < h[a].at) continue;
        for (int i = 0; i < h[a].np; i++) {
          Part& p = h[a].p[i];
          if (p.n < p.att) p.env = (float)++p.n / p.att;
          else p.env *= p.k;
          x += p.amp * p.env * sinT[p.ph >> 22];
          p.ph += p.inc;
        }
      }
      if (total - t < 1600) x *= (total - t) / 1600.0f;      // останні 0,1 с — плавно в тишу
      float y = x * g;
      buf[j] = toU8(y > 1 ? 1 : y < -1 ? -1 : y);
    }
    dacPut(buf, m);
  }
}
static bool fsOk = false;
static const char* sndFile(int w) { return w ? "/back.u8" : "/lost.u8"; }
static bool playFile(int ev) {
  if (!fsOk) return false;
  File f = LittleFS.open(sndFile(ev), "r");
  if (!f) return false;
  const float g = volGain();
  uint8_t buf[512];
  for (;;) {
    int m = f.read(buf, sizeof buf);
    if (m <= 0 || sndAbort) break;
    for (int j = 0; j < m; j++) {
      float y = (buf[j] - 128) / 127.0f * g;
      buf[j] = (uint8_t)lroundf(128.0f + y * 126.0f);
    }
    dacPut(buf, m);
  }
  f.close();
  return true;
}
static void playDac(int ev) {
  if (!dacOpen()) { logEvent("Звук: ЦАП не запустився"); return; }
  dacRamp(true);
  uint8_t src = ev ? S.srcBack : S.srcLost;
  if (src == 2 && playFile(ev)) {}
  else synth(ev, src != 1);
  dacRamp(false);
  vTaskDelay(pdMS_TO_TICKS(300));                // 4 буфери DMA × 1024 Б ≈ 0,26 с — дати дограти, інакше клацне
  dacClose();
}

static void play(int ev) {
  switch (S.sndType) {
    case 1: playPattern(ev ? S.patBack : S.patLost); break;
    case 2: playMelody(ev ? S.melBack : S.melLost); break;
    case 3: playDac(ev); break;
    default: break;
  }
}

static void sndTask(void*) {
  SndReq r;
  for (;;) {
    if (xQueueReceive(sq, &r, portMAX_DELAY) != pdTRUE) continue;
    sndAbort = false;
    sndBusy = true;
    playVol = r.vol;
    int reps = (r.ev == 0 && !r.test) ? (S.sndRep < 1 ? 1 : S.sndRep > 10 ? 10 : S.sndRep) : 1;
    for (int i = 0; i < reps && !sndAbort; i++) {
      if (i && !waitMs(3000)) break;
      if (r.ev == 0 && !r.test && gridNow == 1) break;    // світло вже повернулось — повтори не потрібні
      play(r.ev);
    }
    if (r.test) snprintf(lastPlayed, sizeof lastPlayed, "%s (перевірка, %d %%)", r.ev ? "«світло є»" : "«світло зникло»", curVol());
    else snprintf(lastPlayed, sizeof lastPlayed, "%s", r.ev ? "«світло є»" : "«світло зникло»");
    playVol = -1;
    sndBusy = false;
  }
}

void sigEvent(int from, int to) {
  if (to != 0 && to != 1) return;
  if (from != -1 && S.ledOn && S.ledType == 1 && S.stripAnim) { animTest = false; animType = to == 0 ? 1 : 2; animStart = nowUs(); }
  if (from == -1 && !S.sndBoot) return;          // перше визначення після запуску — без звуку (якщо не просили)
  if (to == 1) sndAbort = true;                  // світло повернулось — обірвати повтори «зникло»
  if (!S.sndType || (to == 0 && !S.sndLost) || (to == 1 && !S.sndBack)) return;
  if (quietNow()) { logEvent("Звук: тиша вночі — без сигналу"); return; }
  SndReq r = {(uint8_t)(to == 0 ? 0 : 1), false, -1};
  if (sq) xQueueSend(sq, &r, 0);
}

bool sigTest(const String& w, int vol) {
  if (w == "green" || w == "red") {
    testWhat = w == "green" ? 1 : 2;
    testUntil = nowUs() + 8000000LL;
    return S.ledOn;
  }
  if (w == "off") { testUntil = 0; animType = 0; animTest = false; nightDemo = 0; return true; }
  if (w == "night") { testUntil = 0; animType = 0; animTest = false; nightDemo = nowUs() + 15000000LL; return true; }
  if (w == "flash" || w == "wave") {
    if (!S.ledOn || S.ledType != 1) return false;
    animTest = true;
    animType = w == "flash" ? 1 : 2;
    animStart = nowUs();
    return true;
  }
  if (w == "lost" || w == "back") {
    if (!S.sndType || !sq) return false;
    sndAbort = true;                              // перервати поточне
    xQueueReset(sq);
    SndReq r = {(uint8_t)(w == "back" ? 1 : 0), true, (int8_t)(vol >= 0 && vol <= 100 ? vol : -1)};
    vTaskDelay(pdMS_TO_TICKS(30));
    xQueueSend(sq, &r, 0);
    return true;
  }
  return false;
}

// ---------- свій звук: запис файлу (лише за кнопкою власника) ----------
static File up;
static size_t upLen = 0;
static int upWhich = 0;
static String upErr;
bool sigSoundBegin(int which) {
  upErr = "";
  upLen = 0;
  upWhich = which ? 1 : 0;
  if (!fsOk) {
    //  Перший свій звук: розділ для файлів ще не розмічено — розмічаємо (явна дія власника, не сам прилад).
    fsOk = LittleFS.begin(true, "/littlefs", 4, "spiffs");
    logEvent(fsOk ? "Пам'ять для звуків розмічено" : "ПОМИЛКА: пам'ять для звуків не розмічається");
    if (!fsOk) { upErr = "Пам'ять для звуків недоступна"; return false; }
  }
  up = LittleFS.open("/new.u8", "w");
  if (!up) { upErr = "Не вдалося відкрити файл"; return false; }
  return true;
}
bool sigSoundWrite(const uint8_t* d, size_t n) {
  if (!up) return false;
  if (upLen + n > SIG_MAX_PCM) { up.close(); LittleFS.remove("/new.u8"); upErr = "Звук задовгий: до 3 секунд"; return false; }
  if (up.write(d, n) != n) { up.close(); LittleFS.remove("/new.u8"); upErr = "Не вдалося записати — бракує місця"; return false; }
  upLen += n;
  return true;
}
bool sigSoundEnd(String& err) {
  if (!up) { err = upErr.length() ? upErr : String("Файл не отримано"); return false; }
  up.close();
  if (upLen < 1600) { LittleFS.remove("/new.u8"); err = "Звук закороткий: щонайменше 0,1 с"; return false; }
  LittleFS.remove(sndFile(upWhich));
  if (!LittleFS.rename("/new.u8", sndFile(upWhich))) { err = "Не вдалося зберегти файл"; return false; }
  logEvent(String("Свій звук ") + (upWhich ? "«світло є»" : "«світло зникло»") + ": " + String(upLen * 10 / FS / 10.0f, 1) + " с");
  return true;
}
void sigSoundAbort() { if (up) { up.close(); LittleFS.remove("/new.u8"); } }
bool sigSoundDelete(int which) {
  if (!fsOk) return false;
  bool ok = LittleFS.remove(sndFile(which ? 1 : 0));
  if (ok) logEvent(String("Свій звук ") + (which ? "«світло є»" : "«світло зникло»") + " видалено");
  return ok;
}

String sigJson() {
  size_t a = 0, b = 0;
  if (fsOk) {
    File f = LittleFS.open("/lost.u8", "r"); if (f) { a = f.size(); f.close(); }
    f = LittleFS.open("/back.u8", "r"); if (f) { b = f.size(); f.close(); }
  }
  return String("{\"led\":") + String((int)ledMode) +
         ",\"test\":" + (testUntil ? "true" : "false") + ",\"busy\":" + (sndBusy ? "true" : "false") +
         ",\"quiet\":" + (quietNow() ? "true" : "false") + ",\"night\":" + (nightNow() ? "true" : "false") +
         ",\"dark\":" + (ledDark ? "true" : "false") + ",\"demo\":" + (nightDemo && nowUs() < nightDemo ? "true" : "false") + ",\"clock\":" + (clockValid() ? "true" : "false") + ",\"fs\":" + (fsOk ? "true" : "false") +
         ",\"lost\":" + String((unsigned)a) + ",\"back\":" + String((unsigned)b) +
         ",\"last\":\"" + jsonEsc(lastPlayed) + "\"}";
}

static int prevSnd = -1, prevDac = -1;
void sigReload() {
  ledInit();
  if (prevSnd >= 0 && prevSnd != S.sndPin) pinIdle(prevSnd);
  if (prevDac >= 0 && prevDac != S.dacPin) pinIdle(prevDac);
  if (S.sndType == 1 || S.sndType == 2) pinIdle(S.sndPin);
  if (S.sndType == 3) pinIdle(S.dacPin);
  prevSnd = S.sndPin;
  prevDac = S.dacPin;
}

void sigBegin() {
  for (int i = 0; i < 1024; i++) sinT[i] = sinf(i * 6.2831853f / 1024.0f);
  for (int i = 0; i < 256; i++) gam[i] = (uint8_t)lroundf(255.0f * powf(i / 255.0f, 2.2f));
  fsOk = LittleFS.begin(false, "/littlefs", 4, "spiffs");     // без розмітки: розмічаємо лише за першим своїм звуком
  sq = xQueueCreate(4, sizeof(SndReq));
  xTaskCreatePinnedToCore(sndTask, "snd", 6144, nullptr, 2, nullptr, ARDUINO_RUNNING_CORE);
  sigReload();
}
