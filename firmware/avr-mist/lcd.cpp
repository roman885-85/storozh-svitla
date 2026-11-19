#include "lcd.h"
#include "lcd_core.h"
#include "settings.h"
#include "signals.h"
#include "watch.h"
#include "bridge.h"
#include "tg.h"
#include "yasno.h"
#include "util.h"
#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <esp_cpu.h>

static LcdFb fb;
static uint8_t sent[504];
static bool sentOk = false;
static int pRst = -1, pCe = -1, pDc = -1, pDin = -1, pClk = -1, pBl = -1;
static int64_t nextUs = 0, refreshUs = 0;
static uint32_t frameUs = 0, frames = 0;           // тривалість останнього кадру й скільки кадрів надіслано (для сторінки)
//  Перевірка зі сторінки (1.0.24): контраст і підсвітка — одразу, без «Зберегти»; «усе чорне» (команда 0x09: усі точки,
//  хоч би що в пам'яті дисплея — видно, чи доходять команди) і «шахівниця». Через 60 с після останньої зміни — збережене.
static int tVop = -1, tBri = -1, tBlLow = -1, tBias = -1, tMode = 0;   // tMode: 0 — звичайний, 1 — усе чорне, 2 — шахівниця,
static int64_t testUntil = 0, sweepNext = 0;                         //        3 — пошук контрасту (усе чорне, контраст росте сам)
static bool testing() { return testUntil != 0; }
static int vopNow() { return testing() && tVop >= 0 ? tVop : S.lcdVop; }
static int biasNow() { return (testing() && tBias >= 0 ? tBias : S.lcdBias) & 7; }
//  Пошук контрасту (1.0.25, власник: «проверь работу контрастности, может в ней проблема»): «усе чорне», а контраст сам
//  іде від 20 до 127 (крок 1 за 0,15 с — ~16 с), потім те саме з іншим зсувом (4, 3, 5, 2). Людина дивиться на дисплей і
//  тисне «Темніє зараз!» — сторінка бере поточні контраст і зсув. Немає змін за 2 хвилини — кінець перевірки.
static const uint8_t SWEEP_BIAS[] = {4, 3, 5, 2};
static int sweepIdx = 0;

// ---------- SPI програмно ----------
//  1.0.24: щонайменше 1 мкс на кожен півперіод CLK (≈ 0,3 МГц). PCD8544 приймає до 4 МГц, але без затримок digitalWrite
//  на ESP32 дає імпульси ~0,1–0,2 мкс — на межі паспортних 0,1 мкс, а з дротами на макетці — гірше. 19.11.2026 власник
//  під'єднав дисплей за схемою — зображення не було. Кадр 504 байти тепер ≈ 12 мс; шлемо лише змінений і раз на хвилину.
static inline void hold() { delayMicroseconds(1); }
static inline void sb(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(pDin, (b >> i) & 1);
    hold();
    digitalWrite(pClk, HIGH);
    hold();
    digitalWrite(pClk, LOW);
  }
}
static void cmd(uint8_t c) { digitalWrite(pDc, LOW); digitalWrite(pCe, LOW); hold(); sb(c); hold(); digitalWrite(pCe, HIGH); hold(); }
static void release() {
  for (int* p : {&pRst, &pCe, &pDc, &pDin, &pClk}) if (*p >= 0) { pinMode(*p, INPUT); *p = -1; }
  if (pBl >= 0) { ledcDetach(pBl); pinMode(pBl, INPUT); pBl = -1; }
  sentOk = false;
}
static void blSet() {
  if (pBl < 0) return;
  uint8_t pct = testing() && tBri >= 0 ? tBri : sigQuietNow() ? S.lcdNight : S.lcdBri;
  bool low = testing() && tBlLow >= 0 ? tBlLow : S.lcdBlLow;
  uint32_t d = (uint32_t)pct * 255 / 100;
  ledcWrite(pBl, low ? 255 - d : d);
}
static void setVop() {                             // контраст і зсув без скидання дисплея — повзунок діє одразу
  cmd(0x21);
  cmd(0x10 | biasNow());
  cmd(0x80 | (vopNow() & 0x7F));
  cmd(0x20);
}
static void setMode() { cmd(testing() && (tMode == 1 || tMode == 3) ? 0x09 : S.lcdInv ? 0x0D : 0x0C); }
static void chipInit() {
  digitalWrite(pRst, LOW);
  delay(20);                                       // PCD8544: RES — низький після ввімкнення живлення
  digitalWrite(pRst, HIGH);
  delay(5);
  cmd(0x21);                                       // розширені команди
  cmd(0x80 | (vopNow() & 0x7F));                   // контраст (Vop)
  cmd(0x04);                                       // температурний коефіцієнт 0
  cmd(0x10 | biasNow());                           // зсув (bias): типово 4 — 1:48 (1.0.25 — на вибір)
  cmd(0x20);                                       // звичайні команди, горизонтальна адресація
  setMode();                                       // звичайний / інверсія / «усе чорне» під час перевірки
  sentOk = false;
}
void lcdReload() {
  //  1.0.26 («работать должно на лету»): дисплей уже працює на тих самих виводах — без скидання й мигання, лише контраст,
  //  зсув, вигляд (інверсія, поворот) і підсвітка. Повне скидання — коли змінилися виводи чи дисплей вмикають.
  bool blSame = pBl >= 0 ? pBl == S.lcdBl : !(S.lcdBl && sigPinOk(S.lcdBl));
  if (S.lcdOn && pClk >= 0 && pRst == S.lcdRst && pCe == S.lcdCe && pDc == S.lcdDc && pDin == S.lcdDin && pClk == S.lcdClk && blSame) {
    testUntil = 0;
    tVop = tBri = tBlLow = tBias = -1;
    tMode = 0;
    setVop();
    setMode();
    blSet();
    sentOk = false;                                // поворот чи інверсія — кадр заново
    nextUs = 0;
    return;
  }
  release();
  if (!S.lcdOn) return;
  int ps[5] = {S.lcdRst, S.lcdCe, S.lcdDc, S.lcdDin, S.lcdClk};
  for (int p : ps) if (!sigPinOk(p)) { logEvent("Дисплей: вивід GPIO" + String(p) + " не підходить — дисплей вимкнено"); return; }
  pRst = S.lcdRst; pCe = S.lcdCe; pDc = S.lcdDc; pDin = S.lcdDin; pClk = S.lcdClk;
  for (int p : ps) pinMode(p, OUTPUT);
  digitalWrite(pCe, HIGH);
  digitalWrite(pClk, LOW);
  if (S.lcdBl && sigPinOk(S.lcdBl) && ledcAttach(S.lcdBl, 5000, 8)) pBl = S.lcdBl;
  testUntil = 0;                                   // «Зберегти» закінчує перевірку: діє збережене
  tVop = tBri = tBlLow = tBias = -1;
  tMode = 0;
  chipInit();
  blSet();
  nextUs = 0;
  logEvent("Дисплей: увімкнено — RST GPIO" + String(pRst) + ", CE GPIO" + String(pCe) + ", DC GPIO" + String(pDc) + ", DIN GPIO" + String(pDin) +
           ", CLK GPIO" + String(pClk) + (pBl >= 0 ? ", підсвітка GPIO" + String(pBl) + (S.lcdBlLow ? " (низьким рівнем)" : " (високим рівнем)") : String(", без підсвітки")) +
           ", контраст " + String(S.lcdVop));
}

bool lcdTest(int vop, int bri, int blLow, int mode, int bias) {
  if (pClk < 0) return false;
  bool modeChanged = mode >= 0 && mode != tMode;
  if (vop >= 0) tVop = vop > 127 ? 127 : vop;
  if (bri >= 0) tBri = bri > 100 ? 100 : bri;
  if (blLow >= 0) tBlLow = blLow ? 1 : 0;
  if (bias >= 0) tBias = bias & 7;
  if (mode >= 0) tMode = mode;
  if (mode == 3) { tVop = 20; sweepIdx = 0; tBias = SWEEP_BIAS[0]; sweepNext = nowUs(); }
  bool was = testing();
  testUntil = nowUs() + (tMode == 3 ? 120LL : 60LL) * 1000000;
  if (!was || modeChanged) { setMode(); sentOk = false; nextUs = 0; }
  setVop();
  blSet();
  return true;
}
void lcdTestEnd() {
  if (!testing()) return;
  testUntil = 0;
  tVop = tBri = tBlLow = tBias = -1;
  tMode = 0;
  if (pClk < 0) return;
  chipInit();                                      // збережений контраст і вигляд
  blSet();
  nextUs = 0;
}
void lcdBegin() { lcdReload(); }

// ---------- що показати ----------
static int lvl(int rssi) {                         // дБм → 0…4 смужки
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -88) return 1;
  return 0;
}
//  Смужки сигналу не мають стрибати щосекунди (правило власника для екранів): середнє з плавним забуванням (≈ 10 с)
//  і поріг ±2 дБ, щоб на межі рівень не перемикався туди-сюди.
struct Smooth {
  float v = 0;
  bool have = false;
  int shown = -1;
  int step(bool present, int rssi) {
    if (!present) { have = false; shown = -1; return -1; }
    v = have ? v * 0.9f + rssi * 0.1f : rssi;
    have = true;
    int l = lvl((int)lroundf(v));
    if (shown < 0 || (l > shown && lvl((int)lroundf(v) - 2) > shown) || (l < shown && lvl((int)lroundf(v) + 2) < shown)) shown = l;
    return shown;
  }
};
static Smooth sW, sA, sM;

// ---------- біжучий рядок і ефекти сповіщень (1.0.26) ----------
static String mqText;                              // графік простим текстом (оновлюється раз на секунду)
static int mqOff = 0;                              // зсув біжучого рядка, точок; сторінками — номер сторінки
static int64_t mqNext = 0;
static int64_t flashUntil = 0, blinkUntil = 0, warnUntil = 0, warnBlinkUntil = 0, annUntil = 0, annSince = 0;
static String warnText, annText;
static LcdView cur;                                // останній зібраний вигляд (для кроків біжучого рядка між секундами)
static bool fxActive(int64_t now) { return now < flashUntil || now < blinkUntil || now < warnBlinkUntil || now < annUntil; }

//  Текст від людей — на дисплей лише знаками шрифту: емодзі й інше прибираємо, переноси рядків — пробілами (1.0.26)
String lcdSafe(const String& s) {
  String o;
  const char* p = s.c_str();
  bool sp = true;
  uint32_t cp;
  while ((cp = LcdFb::nextCp(p))) {
    if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
    if (cp == ' ') { if (!sp) { o += ' '; sp = true; } continue; }
    if (!LcdFb::has(cp)) continue;
    char b[5];
    int n = 0;
    if (cp < 0x80) b[n++] = (char)cp;
    else if (cp < 0x800) { b[n++] = (char)(0xC0 | (cp >> 6)); b[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { b[n++] = (char)(0xE0 | (cp >> 12)); b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[n++] = (char)(0x80 | (cp & 0x3F)); }
    else continue;
    b[n] = 0;
    o += b;
    sp = false;
    if (o.length() > 900) break;
  }
  o.trim();
  return o;
}

void lcdAlert(int kind, const String& text, int sec) {
  int64_t now = nowUs();
  int64_t fx = (int64_t)S.lcdFxSec * 1000000;
  if (kind == LCD_A_ANN) {                         // оголошення: на S.lcdAnnMin хв (пробне — на sec), заголовок блимає
    if ((!S.lcdAnnMin && sec <= 0) || !text.length()) return;
    annText = text;
    annSince = now;
    annUntil = now + (sec > 0 ? (int64_t)sec : (int64_t)S.lcdAnnMin * 60) * 1000000;
    if (S.lcdFx) blinkUntil = now + fx;
    nextUs = 0;
    return;
  }
  if (kind == LCD_A_WARN) {                        // попередження за графіком: рядок 33 навиворіт до відключення
    warnText = text;
    warnUntil = now + (int64_t)(sec > 0 ? sec : 1800) * 1000000;
    if (S.lcdFx) warnBlinkUntil = now + fx;
    nextUs = 0;
    return;
  }
  if (!S.lcdFx) return;
  if (kind == LCD_A_LOST || kind == LCD_A_BACK) {  // світло зникло чи повернулось: спалахи 4 с, далі миготить стан
    flashUntil = now + 4000000;
    blinkUntil = now + (fx > 4000000 ? fx : 4000000);
    if (kind == LCD_A_BACK) warnUntil = 0;         // світло повернулось — попередження більше ні до чого
  } else if (kind == LCD_A_GEN) {
    blinkUntil = now + fx;
  }
  nextUs = 0;
}

static void build(LcdView& v) {
  int64_t now = nowUs();
  v = LcdView();
  bool avr = watchAvrSet();
  int g = watchGrid();
  v.grid = !avr ? -2 : g;
  v.gen = watchLine();
  if (avr && g >= 0) {
    //  Про що час — у самому рядку (власник: «фраза понад 6 хв — не понятно о чем это»): «є з 14:22», «немає з 13:53»,
    //  а коли працює генератор — його час: «працює з 16:26». Не сьогодні — лише дата («немає з 18.11»), щоб влізло.
    bool genNow = g == 0 && v.gen == 1, exact;
    int64_t since = genNow ? watchLineSince(exact) : watchGridSince(exact);
    String hm = fmtHM(since);
    int sp = hm.indexOf(' ');
    if (sp > 0 && hm.indexOf(':') > sp) hm = hm.substring(0, sp);
    snprintf(v.since, sizeof v.since, "%s%s", genNow ? "працює з " : g == 1 ? "є з " : "немає з ", hm.c_str());
    lcdDur(v.dur, sizeof v.dur, (now - since) / 1000000, exact);     // «уже 1 год 5 хв»; невідомо відколи — «понад …»
  }
  mqText = schLcdText();
  bool ph = (now / 500000) & 1;                    // фаза миготіння, 2 рази на секунду
  if (now < warnUntil && warnText.length()) {
    v.mq = warnText.c_str();
    v.mqInv = !(now < warnBlinkUntil && ph);
  } else v.mq = mqText.c_str();
  v.mqPages = S.lcdMq == 0;
  v.mqOff = mqOff;
  if (now < flashUntil) v.invAll = ph;
  else if (now < blinkUntil) v.stFlip = ph;
  if (now < annUntil && annText.length()) {        // оголошення: 10 с з кожних 14 — воно, 4 с — звичайний екран (стан світла)
    int64_t t = (now - annSince) / 1000000;
    int c = (int)(t % 14);
    if (c < 10) {
      v.title = "ОГОЛОШЕННЯ";
      v.ann = annText.c_str();
      v.annPage = (int)(t / 14) * 2 + (c >= 5 ? 1 : 0);
    }
  }
  v.rWifi = sW.step(staUp(), staRssi());
  int ra;
  v.rAvr = sA.step(watchAvrRssi(ra), ra);
  int rm;
  v.rMayak = sM.step(watchMayakRssi(rm), rm);
  v.tg = tgHealth();
  v.blink = (millis() / 1000) & 1;
}

static void flip(const uint8_t* in, uint8_t* out) {   // поворот на 180°: дисплей стоїть догори дриґом
  for (int i = 0; i < 504; i++) {
    uint8_t b = in[503 - i], r = 0;
    for (int k = 0; k < 8; k++) if (b >> k & 1) r |= 0x80 >> k;
    out[i] = r;
  }
}

//  Надіслати на дисплей змінені смуги (по 84 байти) — увесь кадр лише коли змінилося все. Біжучий рядок — смуга 4.
static void sendDiff(const uint8_t* out) {
  for (int b = 0; b < 6; b++) {
    const uint8_t* row = out + b * 84;
    if (sentOk && !memcmp(row, sent + b * 84, 84)) continue;
    cmd(0x80);
    cmd(0x40 | b);
    digitalWrite(pDc, HIGH);
    digitalWrite(pCe, LOW);
    hold();
    for (int i = 0; i < 84; i++) sb(row[i]);
    hold();
    digitalWrite(pCe, HIGH);
    memcpy(sent + b * 84, row, 84);
  }
  sentOk = true;
}
static void outOf(uint8_t* out) {
  if (testing() && tMode == 2) {                    // шахівниця 4×4: кожна точка дисплея по черзі світла й темна
    for (int i = 0; i < 504; i++) out[i] = ((i % 84) / 4 + (i / 84)) & 1 ? 0xF0 : 0x0F;
  } else if (S.lcdFlip) flip(fb.fb, out);
  else memcpy(out, fb.fb, 504);
}

void lcdLoop() {
  int64_t now = nowUs();
  if (testing() && now >= testUntil) lcdTestEnd();
  if (testing() && tMode == 3 && pClk >= 0 && now >= sweepNext) {   // пошук контрасту: наступний крок
    sweepNext = now + 150000;
    if (++tVop > 127) {
      tVop = 20;
      sweepIdx = (sweepIdx + 1) % (int)sizeof SWEEP_BIAS;
      tBias = SWEEP_BIAS[sweepIdx];
    }
    setVop();
  }
  //  Біжучий рядок між секундами: крок, перемалювати лише рядок 33 і надіслати одну смугу. Під час спалахів, миготіння й
  //  оголошення — ні: там увесь екран оновлюється двічі на секунду.
  if (now >= mqNext && cur.mq && cur.mq[0] && !cur.ann[0] && !fxActive(now)) {
    bool pages = S.lcdMq == 0;
    int w = LcdFb::width(cur.mq);
    if (pages || w > 84) {
      if (pages) { mqNext = now + 3000000; mqOff++; }
      else {
        int pxps = 4 * (S.lcdMq > 10 ? 10 : S.lcdMq), step = pxps > 20 ? 2 : 1;
        mqNext = now + (int64_t)step * 1000000 / pxps;
        mqOff = (mqOff + step) % lcdMqPeriod(cur.mq);
      }
      cur.mqOff = mqOff;
      lcdRenderMq(fb, cur);
      if (pClk >= 0 && !testing()) { uint8_t out[504]; outOf(out); sendDiff(out); }
    } else mqNext = now + 1000000;
  }
  if (now < nextUs) return;
  nextUs = now + (fxActive(now) ? 500000 : 1000000);
  build(cur);
  lcdRender(fb, cur);                               // малюємо завжди — сторінка показує, що було б на екрані
  if (pClk < 0) return;
  blSet();
  uint8_t out[504];
  outOf(out);
  bool same = sentOk && !memcmp(out, sent, 504);
  if (same && now < refreshUs) return;
  if (now >= refreshUs && !testing()) chipInit();     // раз на хвилину заново: дисплей на довгих дротах міг збитися
  refreshUs = now + 60LL * 1000000;
  uint32_t t0 = micros();
  sendDiff(out);
  frameUs = micros() - t0;
  frames++;
}

// ---------- перевірка дротів з боку плати (1.0.25) ----------
//  Власник 19.11.2026: «выполни отладку по порту через вайфай и определи работает ли дисплей и отвечает ли вообще».
//  Nokia 5110 (PCD8544) нічого не відповідає: у нього немає вихідного дроту, лише прийом. Тож перевіряємо кожну лінію
//  електрично з боку Сторожа, внутрішніми підтяжками ESP32 (~45 кОм):
//   • підтяжка вгору читається 0 — лінія замкнена на GND; вниз читається 1 — її тягне вгору ззовні: замкнена на 3,3 В,
//     підтяжка на платі дисплея (буває на RST), підсвітка (LIGHT червоної плати — через світлодіоди до VCC) або
//     **немає GND модуля**: тоді мікросхема живиться від VCC, а «землю» шукає через захисні діоди входів — і лінії
//     з підтяжкою вниз стають 1;
//   • лінія проти лінії: одна виходом, друга з протилежною підтяжкою — замикання між дротами;
//   • час заряду лінії через підтяжку вгору — ємність: із дротом і входом дисплея вона в кілька разів більша, ніж у
//     вільного виводу (порівнюємо з GPIO13 і GPIO14). Схоже на вільний — дроту на цьому виводі, мабуть, немає.
static portMUX_TYPE dmux = portMUX_INITIALIZER_UNLOCKED;
static int rdPull(int p, bool up) {
  pinMode(p, up ? INPUT_PULLUP : INPUT_PULLDOWN);
  delayMicroseconds(400);
  return digitalRead(p);
}
static int riseNs(int p) {                         // час від відпускання (розряджено до 0) до читання 1 з підтяжкою вгору
  pinMode(p, INPUT_PULLUP);
  gpio_set_level((gpio_num_t)p, 0);
  gpio_set_direction((gpio_num_t)p, GPIO_MODE_INPUT_OUTPUT);   // вихід 0 сильніший за підтяжку — розряд
  delayMicroseconds(60);
  uint32_t mhz = getCpuFrequencyMhz(), lim = mhz * 300, c0, c;
  portENTER_CRITICAL(&dmux);
  c0 = esp_cpu_get_cycle_count();
  gpio_ll_output_disable(&GPIO, (uint32_t)p);
  do { c = esp_cpu_get_cycle_count(); } while (!gpio_ll_get_level(&GPIO, (uint32_t)p) && c - c0 < lim);
  portEXIT_CRITICAL(&dmux);
  pinMode(p, INPUT);
  return (int)((uint64_t)(c - c0) * 1000 / mhz);
}
static int riseMed(int p) {                        // медіана з 5 замірів
  int v[5];
  for (int& x : v) x = riseNs(p);
  for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[2];
}
static bool shorted(int a, int b) {                // b іде за a в обидва боки — дроти замкнені
  pinMode(b, INPUT_PULLDOWN);
  pinMode(a, OUTPUT);
  digitalWrite(a, HIGH);
  delayMicroseconds(300);
  bool h = digitalRead(b);
  pinMode(b, INPUT_PULLUP);
  digitalWrite(a, LOW);
  delayMicroseconds(300);
  bool l = !digitalRead(b);
  pinMode(a, INPUT);
  pinMode(b, INPUT);
  return h && l;
}

//  1.0.25 (друга частина): обхід УСІХ виводів плати — тоді видно, як виглядає вивід, до якого точно під'єднано дріт
//  (стрічка, кнопка пароля), і чи не стоять дроти дисплея на інших виводах. Вивід звуку пропускаємо (клацне динамік).
static const int SCAN[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 27, 32, 33};
static const char* pinRole(int p) {
  if (p == S.lcdRst) return "дисплей RST";
  if (p == S.lcdCe) return "дисплей CE";
  if (p == S.lcdDc) return "дисплей DC";
  if (p == S.lcdDin) return "дисплей DIN";
  if (p == S.lcdClk) return "дисплей CLK";
  if (p == S.lcdBl) return "дисплей BL";
  if (S.ledOn && S.ledType == 1 && p == S.stripPin) return "стрічка DIN";
  if (S.ledOn && S.ledType == 0 && p == S.ledG) return "зелений світлодіод";
  if (S.ledOn && S.ledType == 0 && p == S.ledR) return "червоний світлодіод";
  if (p == S.pwBtn) return "кнопка пароля";
  if (p == 2) return "на багатьох платах — синій світлодіод";
  return "";
}

String lcdWireCheck() {
  struct L { const char* n; int p; int pu, pd, ns; };
  L ln[6] = {{"RST", S.lcdRst}, {"CE", S.lcdCe}, {"DC", S.lcdDc}, {"DIN", S.lcdDin}, {"CLK", S.lcdClk}, {"BL", S.lcdBl}};
  release();                                       // дисплей відпускає виводи (і підсвітку); наприкінці — знову як був
  int ref[2] = {-1, -1}, rp[2] = {13, 14};
  for (int k = 0; k < 2; k++) if (sigPinOk(rp[k])) ref[k] = riseMed(rp[k]);
  //  усі виводи
  const int NS = sizeof SCAN / sizeof SCAN[0];
  int spu[NS], spd[NS], sns[NS];
  for (int i = 0; i < NS; i++) {
    int q = SCAN[i];
    bool snd = (S.sndType == 1 || S.sndType == 2) ? q == S.sndPin : S.sndType == 3 ? q == S.dacPin : false;
    if (snd) { spu[i] = spd[i] = sns[i] = -1; continue; }
    spu[i] = rdPull(q, true);
    spd[i] = rdPull(q, false);
    pinMode(q, INPUT);
    sns[i] = riseMed(q);
  }
  //  еталон «вільного виводу» — медіана трьох найменших серед звичайних (вгору 1, вниз 0)
  int lo[3] = {-1, -1, -1};
  for (int i = 0; i < NS; i++) {
    if (sns[i] < 0 || spu[i] != 1 || spd[i] != 0) continue;
    int v = sns[i];
    for (int k = 0; k < 3; k++) if (lo[k] < 0 || v < lo[k]) { for (int m = 2; m > k; m--) lo[m] = lo[m - 1]; lo[k] = v; break; }
  }
  int rmin = lo[1] >= 0 ? lo[1] : lo[0];
  //  найбільший час серед справді вільних виводів (звичайні й без жодної ролі): лінія вище за нього — на ній щось є.
  //  19.11.2026 на Сторожі: вільні 0,30–0,42 мкс, лінії дисплея 0,53–0,75 мкс — дроти є, хоч ємність мала (дріт і
  //  крихітний вхід мікросхеми на склі); поріг «удвічі більше» помилково казав «дроту немає».
  int fmax = -1;
  for (int i = 0; i < NS; i++)
    if (sns[i] >= 0 && spu[i] == 1 && spd[i] == 0 && !pinRole(SCAN[i])[0] && sns[i] > fmax) fmax = sns[i];
  for (auto& x : ln) {
    if (x.p <= 0 || !sigPinOk(x.p)) { x.p = -1; continue; }
    x.pu = rdPull(x.p, true);
    x.pd = rdPull(x.p, false);
    pinMode(x.p, INPUT);
    x.ns = riseMed(x.p);
  }
  String shorts;
  for (int i = 0; i < 6; i++)
    for (int j = i + 1; j < 6; j++)
      if (ln[i].p > 0 && ln[j].p > 0 && shorted(ln[i].p, ln[j].p)) shorts += String(shorts.length() ? ", " : "") + ln[i].n + "–" + ln[j].n;
  int sigUp = 0, attached = 0, freeLike = 0;
  String o = "{\"ref\":[" + String(ref[0]) + "," + String(ref[1]) + "],\"lines\":[";
  String logT;
  for (int i = 0; i < 6; i++) {
    L& x = ln[i];
    if (x.p < 0) continue;
    bool bl = i == 5;
    String v;
    float pf = rmin > 0 ? (x.ns - rmin) / 62.0f : -1;     // t ≈ 1,39·R·C, R ≈ 45 кОм: 62 нс на пФ (грубо)
    if (!x.pu) v = "замкнено на GND (або тягне вниз ззовні)";
    else if (x.pd) {
      if (bl) v = "тягне вгору через підсвітку — світлодіоди до VCC під'єднано";
      else { v = "тягне вгору ззовні: 3,3 В, підтяжка на платі дисплея або немає GND модуля"; sigUp++; }
    } else if (rmin > 0 && x.ns > rmin * 2 + 150) { v = "дріт під'єднано (≈ " + String((int)lroundf(pf)) + " пФ більше, ніж у вільного виводу)"; attached++; }
    else if (fmax > 0 && x.ns > fmax + 40) { v = "дріт є (≈ " + String((int)lroundf(pf)) + " пФ — більше, ніж у будь-якого вільного виводу); чи доходить до мікросхеми, так не видно"; attached++; }
    else if (rmin > 0) { v = "як вільний вивід — дроту, мабуть, немає або він ні до чого не дотягнутий"; if (!bl) freeLike++; }
    else v = "без порівняння (GPIO13 і GPIO14 зайняті)";
    o += String(o.endsWith("[") ? "" : ",") + "{\"n\":\"" + x.n + "\",\"p\":" + String(x.p) + ",\"pu\":" + String(x.pu) + ",\"pd\":" + String(x.pd) +
         ",\"ns\":" + String(x.ns) + ",\"v\":\"" + jsonEsc(v) + "\"}";
    logT += String(logT.length() ? "; " : "") + x.n + " GPIO" + String(x.p) + ": " + v;
  }
  String verdict;
  if (shorts.length()) verdict = "Замкнено між собою: " + shorts + " — два дроти на одному виводі чи перемичка припою.";
  else if (sigUp >= 3) verdict = "Кілька сигнальних ліній тягне вгору — схоже, GND дисплея не з'єднано з GND плати (мікросхема живиться від VCC через свої входи).";
  else if (freeLike >= 3) verdict = "Більшість ліній — як вільні виводи: дроти не доходять до дисплея (не ті виводи на платі чи непропаяні штирі дисплея).";
  else if (freeLike) verdict = "Окремі лінії — як вільні виводи: перевірте саме ці дроти й пайку штирів дисплея.";
  else verdict = "З боку Сторожа все гаразд: на всіх лініях є дроти, замикань немає, жодна лінія не тягнеться ні до GND, ні до 3,3 В (і GND модуля, схоже, на місці). "
                 "Самі команди дисплей підтвердити не може — у Nokia 5110 немає зворотного дроту. Не темніє на «Усе чорне» за жодного контрасту — сигнали "
                 "не доходять до мікросхеми на склі (пайка штирів модуля, контакт гумової смужки під рамкою, дроти на модулі не в тому порядку) або модуль несправний.";
  o += "],\"base\":" + String(rmin) + ",\"scan\":[";
  String scanLog;
  for (int i = 0; i < NS; i++) {
    if (sns[i] < 0) continue;
    String sv;
    if (!spu[i]) sv = "тягне вниз (GND чи навантаження)";
    else if (spd[i]) sv = "тягне вгору";
    else if (rmin > 0 && sns[i] > rmin * 2 + 150) sv = "щось під'єднано (≈ " + String((int)lroundf((sns[i] - rmin) / 62.0f)) + " пФ)";
    else sv = "вільний";
    o += String(o.endsWith("[") ? "" : ",") + "{\"p\":" + String(SCAN[i]) + ",\"pu\":" + String(spu[i]) + ",\"pd\":" + String(spd[i]) + ",\"ns\":" + String(sns[i]) +
         ",\"role\":\"" + jsonEsc(pinRole(SCAN[i])) + "\",\"v\":\"" + jsonEsc(sv) + "\"}";
    if (sv != "вільний") scanLog += String(scanLog.length() ? ", " : "") + "GPIO" + String(SCAN[i]) + (pinRole(SCAN[i])[0] ? String(" (") + pinRole(SCAN[i]) + ")" : String()) + " — " + sv;
  }
  o += "],\"shorts\":\"" + jsonEsc(shorts) + "\",\"verdict\":\"" + jsonEsc(verdict) + "\"}";
  logEvent("Дисплей, перевірка дротів: " + logT + (shorts.length() ? "; замкнено: " + shorts : String()) + ". Еталон — GPIO13 " + String(ref[0]) + " нс, GPIO14 " + String(ref[1]) + " нс");
  logEvent("Дисплей, висновок: " + verdict);
  logEvent("Виводи плати, де щось є: " + (scanLog.length() ? scanLog : String("ніде, крім звуку")) + ". Вільний вивід — " + String(rmin) + " нс");
  //  назад, як було: дисплей, сигнали (стрічка, світлодіоди, звук) і кнопка пароля з підтяжкою — висячий вивід кнопки
  //  міг би прочитатися «натиснутою» й через 5 с скинути пароль
  for (int q : SCAN) pinMode(q, INPUT);
  if (S.pwBtn > 0) pinMode(S.pwBtn, INPUT_PULLUP);
  sigReload();
  lcdReload();
  return o;
}

String lcdJson() {
  static const char hx[] = "0123456789abcdef";
  String o;
  o.reserve(1100);
  o = String("{\"on\":") + (S.lcdOn ? "true" : "false") + ",\"live\":" + (pClk >= 0 ? "true" : "false") + ",\"us\":" + String(frameUs) +
      ",\"frames\":" + String(frames) + ",\"test\":" + String(testing() ? tMode : -1) +
      ",\"vop\":" + String(vopNow()) + ",\"bias\":" + String(biasNow()) + ",\"fb\":\"";
  for (int i = 0; i < 504; i++) { o += hx[fb.fb[i] >> 4]; o += hx[fb.fb[i] & 15]; }
  return o + "\"}";
}
