/*  Світло й звук мосту батареї (1.2.0) — частина avr-bms.ino (вставляється після display.h).

    Власник 23.11.2026: «добавить световую индикацию светодиодами или через адресную ленту, добавить функцию с пассивным
    бузером и активным, для звукового оповещения (аккумулятор зарядился, осталось половина заряда, 30 процентов, 20, 10,
    5 и полный разряд, ошибка аккумулятора, возможно придумай еще что-то)».

    Події — batev_core.h (на кожну батарею свій стан), мелодії, шаблони й кадри світла — alert_core.h; обидва ядра без
    Arduino й перевірені на комп'ютері (tests/batev, tests/alert). Тут — лише виводи, налаштування, черга звуків і сторінка.
      • Світло: два світлодіоди (зелений, червоний; ШІМ 5 кГц) чи стрічка WS2812B (RMT 10 МГц, як на Сторожі).
      • Звук: активний бузер (пищить сам від живлення; гучність — шпаруватість 25 кГц, як на Сторожі) чи пасивний
        (мелодії: частота ШІМ, гучність — шпаруватість до 50 %). Модулі «вмикається низьким рівнем» — галочкою.
      • Важливе (5 %, повний розряд, тривога BMS) повторюється раз на N хв, доки стан не мине; коротке натискання BOOT
        чи «Тиша» на сторінці — тиша до нової події.
      • Уночі (з … до … години; годинник — з інтернету через Сторожа) звук «як удень», «лише важливе» чи «тиша»,
        світло — своя яскравість (0 — вимкнено).
    У флеш пише лише кнопка «Зберегти» на сторінці (правило власника).  */
#include <time.h>

struct AlertCfg {
  uint8_t light = 0;                 // 0 — немає, 1 — два світлодіоди, 2 — стрічка WS2812B
  int8_t ledG = -1, ledR = -1;       // −1 — не під'єднано
  bool ledLow = false;               // світлодіоди вмикаються низьким рівнем
  int8_t stripPin = -1;
  uint8_t stripN = 8, stripOrder = 0;   // скільки світлодіодів; порядок кольорів: 0 GRB, 1 RGB, 2 BGR
  uint8_t bri = 40, nbri = 5;        // яскравість удень і вночі, % (уночі 0 — вимкнено)
  uint8_t snd = 0;                   // 0 — немає, 1 — активний бузер, 2 — пасивний
  int8_t bzPin = -1;
  bool bzLow = false;                // бузер вмикається низьким рівнем
  uint8_t vol = 100;                 // гучність, %
  uint16_t mask = 0;                 // які події звучать: біт = bev::Ev
  uint8_t rep = 10;                  // повтор важливого, хв (0 — не повторювати)
  uint8_t nf = 22, nt = 7, nmode = 1;   // ніч з nf до nt години (однакові — ночі немає); звук уночі: 0 як удень, 1 лише важливе, 2 тиша
};
static AlertCfg A;
//  Типово звучить усе, крім «почався розряд / заряд»: про світло й так скаже Сторож
static const uint16_t EV_DEFAULT = (uint16_t)(((1u << bev::E_COUNT) - 2) & ~((1u << bev::E_DIS) | (1u << bev::E_CHG)));
static const int8_t AL_PINS[] = {0, 1, 3, 4, 5, 6, 7, 10, 20, 21, 2, 8};
static const int STRIP_MAX = 60;

static bool alPinAllowed(int p) {
  if (!ARDUINO_USB_CDC_ON_BOOT && (p == 20 || p == 21)) return false;   // плата «uart»: 20/21 — порт USB-UART
  for (int8_t a : AL_PINS) if (a == p) return true;
  return false;
}
static int alPins(const AlertCfg& a, int8_t* p, const char** nm) {       // які виводи займає налаштування
  int n = 0;
  if (a.light == 1) { if (a.ledG >= 0) { p[n] = a.ledG; nm[n++] = "зелений світлодіод"; } if (a.ledR >= 0) { p[n] = a.ledR; nm[n++] = "червоний світлодіод"; } }
  if (a.light == 2 && a.stripPin >= 0) { p[n] = a.stripPin; nm[n++] = "стрічка"; }
  if (a.snd && a.bzPin >= 0) { p[n] = a.bzPin; nm[n++] = "бузер"; }
  return n;
}
static bool alertUsesPin(int p) {
  int8_t pp[4];
  const char* nm[4];
  int n = alPins(A, pp, nm);
  for (int i = 0; i < n; i++) if (pp[i] == p) return true;
  return false;
}
static bool alertPinsOk(const AlertCfg& a, String& err) {
  if (a.light == 1 && a.ledG < 0 && a.ledR < 0) { err = "Виберіть вивід хоч одного світлодіода"; return false; }
  if (a.light == 2 && a.stripPin < 0) { err = "Виберіть вивід стрічки"; return false; }
  if (a.snd && a.bzPin < 0) { err = "Виберіть вивід бузера"; return false; }
  int8_t p[4];
  const char* nm[4];
  int n = alPins(a, p, nm);
  for (int i = 0; i < n; i++) {
    if (!alPinAllowed(p[i])) { err = String("GPIO") + p[i] + " (" + nm[i] + ") не підходить"; return false; }
    for (int k = 0; k < i; k++) if (p[k] == p[i]) { err = String("GPIO") + p[i] + " вибрано двічі: " + nm[k] + " і " + nm[i]; return false; }
    if (L.on) {
      const int8_t d[6] = {L.clk, L.din, L.cs, L.rs, L.rst, L.bl};
      for (int8_t x : d) if (x >= 0 && x == p[i]) { err = String("GPIO") + p[i] + " (" + nm[i] + ") вже зайнятий дисплеєм"; return false; }
    }
  }
  return true;
}

static void alertLoad() {
  A = AlertCfg();
  A.mask = EV_DEFAULT;
  Preferences p;
  if (!p.begin("bms", true)) return;
  A.light = p.getUChar("aLt", 0); A.ledG = p.getChar("aG", -1); A.ledR = p.getChar("aR", -1); A.ledLow = p.getBool("aLl", false);
  A.stripPin = p.getChar("aSp", -1); A.stripN = p.getUChar("aSn", 8); A.stripOrder = p.getUChar("aSo", 0);
  A.bri = p.getUChar("aBr", 40); A.nbri = p.getUChar("aNb", 5);
  A.snd = p.getUChar("aSt", 0); A.bzPin = p.getChar("aBz", -1); A.bzLow = p.getBool("aBl", false); A.vol = p.getUChar("aVo", 100);
  A.mask = p.getUShort("aEv", EV_DEFAULT); A.rep = p.getUChar("aRe", 10);
  A.nf = p.getUChar("aNf", 22); A.nt = p.getUChar("aNt", 7); A.nmode = p.getUChar("aNm", 1);
  p.end();
  if (A.light > 2) A.light = 0;
  if (A.snd > 2) A.snd = 0;
  if (A.stripN < 1 || A.stripN > STRIP_MAX) A.stripN = 8;
  String e;
  if (!alertPinsOk(A, e)) { A.light = 0; A.snd = 0; }            // зіпсовані налаштування — виводів не чіпаємо
}
static bool alertSave() {
  Preferences p;
  if (!p.begin("bms", false)) return false;
  p.putUChar("aLt", A.light); p.putChar("aG", A.ledG); p.putChar("aR", A.ledR); p.putBool("aLl", A.ledLow);
  p.putChar("aSp", A.stripPin); p.putUChar("aSn", A.stripN); p.putUChar("aSo", A.stripOrder);
  p.putUChar("aBr", A.bri); p.putUChar("aNb", A.nbri);
  p.putUChar("aSt", A.snd); p.putChar("aBz", A.bzPin); p.putBool("aBl", A.bzLow); p.putUChar("aVo", A.vol);
  p.putUShort("aEv", A.mask); p.putUChar("aRe", A.rep);
  p.putUChar("aNf", A.nf); p.putUChar("aNt", A.nt); p.putUChar("aNm", A.nmode);
  p.end();
  return true;
}

// ---------- виводи ----------
static int hwG = -1, hwR = -1, hwStrip = -1, hwBz = -1;
static bool hwBzLedc = false;
static rmt_data_t* stripBuf = nullptr;
static int stripN = 0;
static uint8_t stripLast[STRIP_MAX][3];
static bool stripDirty = true;

static void bzIdle() { if (hwBz >= 0) { pinMode(hwBz, OUTPUT); digitalWrite(hwBz, A.bzLow ? HIGH : LOW); } }
static void alertHwFree() {
  if (hwG >= 0) { ledcDetach(hwG); pinMode(hwG, INPUT); }
  if (hwR >= 0) { ledcDetach(hwR); pinMode(hwR, INPUT); }
  if (hwStrip >= 0) { rmtDeinit(hwStrip); pinMode(hwStrip, INPUT); }
  free(stripBuf);
  stripBuf = nullptr;
  if (hwBz >= 0) { if (hwBzLedc) ledcDetach(hwBz); pinMode(hwBz, INPUT); }
  hwG = hwR = hwStrip = hwBz = -1;
  hwBzLedc = false;
}
static void ledOut(int pin, uint8_t v) { if (pin >= 0) ledcWrite(pin, A.ledLow ? 255 - v : v); }
static void alertHwApply() {
  alertHwFree();
  if (A.light == 1) {
    if (A.ledG >= 0 && ledcAttach(A.ledG, 5000, 8)) hwG = A.ledG;
    if (A.ledR >= 0 && ledcAttach(A.ledR, 5000, 8)) hwR = A.ledR;
    ledOut(hwG, 0);
    ledOut(hwR, 0);
  } else if (A.light == 2 && A.stripPin >= 0) {
    stripN = A.stripN < 1 ? 1 : A.stripN > STRIP_MAX ? STRIP_MAX : A.stripN;
    stripBuf = (rmt_data_t*)malloc(sizeof(rmt_data_t) * 24 * stripN);
    if (stripBuf && rmtInit(A.stripPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) hwStrip = A.stripPin;
    else { free(stripBuf); stripBuf = nullptr; }
    stripDirty = true;
  }
  if (A.snd && A.bzPin >= 0) { hwBz = A.bzPin; bzIdle(); }
}
//  WS2812B: тайминги як у ядра (esp32-hal-rgb-led.c) і Сторожа — RMT 10 МГц, «1» — 0,8/0,4 мкс, «0» — 0,4/0,8 мкс
static void stripShow(uint8_t (*px)[3]) {
  if (hwStrip < 0 || !stripBuf) return;
  if (!stripDirty && !memcmp(stripLast, px, (size_t)stripN * 3)) return;   // нічого не змінилось — не слати
  memcpy(stripLast, px, (size_t)stripN * 3);
  stripDirty = false;
  int k = 0;
  for (int i = 0; i < stripN; i++) {
    uint8_t r = px[i][0], g = px[i][1], b = px[i][2], c[3];
    switch (A.stripOrder) {
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
  rmtWrite(hwStrip, stripBuf, k, 100);
}

// ---------- звук: черга й програвання без зупинки циклу ----------
static void bzOn(uint16_t hz) {                     // пасивний — нота hz; активний — увімкнути
  if (hwBz < 0) return;
  if (A.snd == 2) {
    if (!hz) return;
    if (!hwBzLedc) { if (!ledcAttach(hwBz, hz, 10)) return; hwBzLedc = true; }
    else ledcChangeFrequency(hwBz, hz, 10);
    uint32_t d = 512UL * A.vol / 100;               // 50 % — найгучніше
    ledcWrite(hwBz, A.bzLow ? 1023 - d : d);
  } else if (A.vol >= 100) {
    if (hwBzLedc) { ledcDetach(hwBz); hwBzLedc = false; pinMode(hwBz, OUTPUT); }
    digitalWrite(hwBz, A.bzLow ? LOW : HIGH);
  } else {
    if (!hwBzLedc) { if (!ledcAttach(hwBz, 25000, 8)) return; hwBzLedc = true; }   // 25 кГц: гучність — шпаруватість живлення
    uint32_t d = 255UL * A.vol / 100;
    ledcWrite(hwBz, A.bzLow ? 255 - d : d);
  }
}
static void bzOff() {
  if (hwBz < 0) return;
  if (hwBzLedc) { ledcDetach(hwBz); hwBzLedc = false; }
  bzIdle();
}
struct SndItem { char s[240]; bool mel; };
static SndItem sq[4];
static int sqN = 0;
static alr::Note sNote[72];
static uint16_t sPat[72];
static int sLen = 0, sI = -1;
static bool sMel = false, sGap = false, muted = false;
static uint32_t sAt = 0, sDur = 0;

static void sndStop() { sI = -1; sqN = 0; bzOff(); }
static void sndPush(const char* s, bool mel) {
  if (sqN >= 4) { memmove(sq, sq + 1, sizeof(SndItem) * 3); sqN = 3; }   // черга повна — найдавніше геть
  strlcpy(sq[sqN].s, s, sizeof sq[sqN].s);
  sq[sqN].mel = mel;
  sqN++;
}
static void sndBegin(uint32_t now) {
  sAt = now;
  if (sMel) {
    const alr::Note& n = sNote[sI];
    sDur = n.ms > 20 ? n.ms - 15 : n.ms;
    if (n.hz) bzOn(n.hz); else bzOff();
  } else {
    sDur = sPat[sI];
    if (sI % 2 == 0 && sDur) bzOn(0); else bzOff();
  }
}
static void sndStep(uint32_t now) {
  if (sI < 0) {
    if (!sqN || hwBz < 0) return;
    SndItem it = sq[0];
    memmove(sq, sq + 1, sizeof(SndItem) * (sqN - 1));
    sqN--;
    sMel = it.mel;
    sLen = sMel ? alr::parseMelody(it.s, sNote, 72) : alr::parsePattern(it.s, sPat, 72);
    if (sLen <= 0) return;
    sI = 0;
    sGap = false;
    sndBegin(now);
    return;
  }
  if (now - sAt < sDur) return;
  if (sMel && !sGap && sNote[sI].ms > 20) { sGap = true; bzOff(); sAt = now; sDur = 15; return; }   // коротка пауза — чути кожну ноту
  sGap = false;
  if (++sI >= sLen) { sI = -1; bzOff(); return; }
  sndBegin(now);
}

// ---------- події ----------
struct EvLog { uint32_t ms; time_t t; uint8_t pk, ev; };
static EvLog evl[12];
static int evlN = 0, evlHead = 0;
static uint32_t lightTestUntil = 0, lightTestAt = 0;

static bool alNight() {
  if (A.nf == A.nt || !lcdClockOk()) return false;      // без годинника ночі немає
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  int h = tmv.tm_hour;
  return A.nf < A.nt ? (h >= A.nf && h < A.nt) : (h >= A.nf || h < A.nt);
}
static uint32_t packAlarms(const jk::Cells& c) { return (c.err & ~jk::ERR_INFO) | (c.dis ? 0u : (1u << 31)); }
//  Дані свіжі: «одна BMS» — хвилина (BMS шле кадр щосекунди); «по черзі» — період і ще 2 хв
static uint32_t packFreshMs() { return pollMode ? (uint32_t)pollSec * 1000UL + 120000UL : 60000UL; }
//  «BMS не на зв'язку»: 15 хв без даних (як у Сторожа й Home Assistant); «по черзі» — щонайменше два періоди й 5 хв
static uint32_t packLostMs() {
  uint32_t a = 15UL * 60 * 1000, b = pollMode ? 2UL * pollSec * 1000UL + 5UL * 60 * 1000 : 0;
  return a > b ? a : b;
}
static void alertSound(int i, uint8_t e) {
  if (!A.snd || hwBz < 0 || !(A.mask >> e & 1)) return;
  if (alNight() && (A.nmode == 2 || (A.nmode == 1 && !bev::evCritical(e)))) return;
  char s[240];
  int n = packN();
  bool mel = A.snd == 2;
  if (mel) alr::prefixMelody(n > 1 ? i + 1 : 0, s, sizeof s);
  else alr::prefixPattern(n > 1 ? i + 1 : 0, s, sizeof s);
  size_t L = strlen(s);
  snprintf(s + L, sizeof s - L, "%s%s", L ? (mel ? " " : ",") : "", mel ? alr::melody(e) : alr::pattern(e));
  sndPush(s, mel);
}
static void alertEvent(int i, uint8_t e) {
  EvLog& x = evl[evlHead];
  x.ms = millis();
  x.t = lcdClockOk() ? time(nullptr) : 0;
  x.pk = (uint8_t)i;
  x.ev = e;
  evlHead = (evlHead + 1) % 12;
  if (evlN < 12) evlN++;
  Serial.printf("Подія%s: %s\n", packN() > 1 ? (" батареї " + String(i + 1)).c_str() : "", bev::evName(e));
  if (bev::evCritical(e)) { R[i].crit = e; R[i].critNext = millis() + (uint32_t)A.rep * 60000UL; }
  if (e == bev::E_ERR_OK && R[i].crit == bev::E_ERROR) R[i].crit = 0;
  muted = false;                                    // нова подія — тиша скінчилась
  alertSound(i, e);
}
static void alertMute(const char* by) {
  bool any = sI >= 0 || sqN;
  for (int i = 0; i < MAX_PK; i++) if (R[i].crit) any = true;
  sndStop();
  muted = true;
  if (any) Serial.printf("Тиша (%s): звук зупинено, важливе не повторюється до нової події\n", by);
}
static void alertQuiet() {                          // на час оновлення: усе вимкнути
  sndStop();
  if (hwG >= 0) ledOut(hwG, 0);
  if (hwR >= 0) ledOut(hwR, 0);
  if (hwStrip >= 0) { static uint8_t z[STRIP_MAX][3]; memset(z, 0, sizeof z); stripDirty = true; stripShow(z); }
}

//  Кожен новий кадр стану кожної батареї — у стан подій; раз на секунду — зв'язок і повтор важливого
static void alertPacks(uint32_t now) {
  int n = packN();
  for (int i = 0; i < n; i++) {
    uint32_t at = R[i].atMs;
    if (!at || at == R[i].seenAt) continue;
    R[i].seenAt = at;
    jk::Cells c;
    char mac[18];
    portENTER_CRITICAL(&mux); c = R[i].c; memcpy(mac, R[i].mac, sizeof mac); portEXIT_CRITICAL(&mux);
    if (!c.fmt) continue;
    if (strcmp(mac, R[i].evMac)) {                  // інша BMS на цьому місці — події з чистого аркуша
      strlcpy(R[i].evMac, mac, sizeof R[i].evMac);
      R[i].ev = bev::State();
      R[i].avgInit = false;
      R[i].crit = 0;
    }
    if (!R[i].avgInit) { R[i].fastMa = R[i].slowMa = (float)c.ma; R[i].avgInit = true; }
    else {
      float dt = (float)(uint32_t)(at - R[i].avgAt) / 1000.0f;
      float a = dt / 5.0f, b = dt / 300.0f;
      if (a > 1) a = 1;
      if (b > 1) b = 1;
      R[i].fastMa += ((float)c.ma - R[i].fastMa) * a;
      //  середній — за ~5 хв; розряд чи заряд щойно почався — з теперішнього струму, а не з нуля
      if ((c.ma <= -300 && R[i].slowMa > -300) || (c.ma >= 300 && R[i].slowMa < 300)) R[i].slowMa = (float)c.ma;
      else R[i].slowMa += ((float)c.ma - R[i].slowMa) * b;
    }
    R[i].avgAt = at;
    bev::Sample x;
    x.ms = at;
    x.soc = c.soc;
    x.ma = c.ma;
    x.alarms = packAlarms(c);
    x.dmv = c.n && c.vmax > c.vmin ? (uint16_t)(c.vmax - c.vmin) : 0;
    x.leftMin = R[i].slowMa <= -300 ? (int32_t)((float)c.remMah / -R[i].slowMa * 60.0f) : -1;
    bev::Cfg cf;
    if (pollMode) { cf.errHoldMs = 0; cf.dirHoldMs = 0; }   // кадри рідко — кожен вже «тримається» період
    uint8_t ev[8];
    int k = bev::feed(R[i].ev, x, cf, ev, 8);
    for (int j = 0; j < k; j++) alertEvent(i, ev[j]);
  }
  static uint32_t last = 0;
  if (now - last < 1000) return;
  last = now;
  for (int i = 0; i < n; i++) {
    uint32_t at = R[i].atMs;
    bool lost = at ? agoMs(R[i].atMs) > packLostMs() : now - bootMs > packLostMs();
    uint8_t e = bev::link(R[i].ev, lost);
    if (e) alertEvent(i, e);
    if (!R[i].crit) continue;
    jk::Cells c;
    portENTER_CRITICAL(&mux); c = R[i].c; portEXIT_CRITICAL(&mux);
    bool still = R[i].crit == bev::E_ERROR ? packAlarms(c) != 0 : c.soc <= 5;
    if (lost || !still) R[i].crit = 0;
    else if (A.rep && !muted && (int32_t)(now - R[i].critNext) >= 0) { R[i].critNext = now + (uint32_t)A.rep * 60000UL; alertSound(i, R[i].crit); }
  }
}

static void packIn(int i, alr::PackIn& p) {
  jk::Cells c;
  portENTER_CRITICAL(&mux); c = R[i].c; portEXIT_CRITICAL(&mux);
  uint32_t at = R[i].atMs;
  p.has = at && c.fmt && agoMs(R[i].atMs) < packFreshMs();
  p.lost = R[i].ev.lost;
  p.soc = c.soc;
  float f = R[i].avgInit ? R[i].fastMa : (float)c.ma;
  p.dir = f >= 300 ? 1 : f <= -300 ? -1 : 0;
  p.alarm = p.has && packAlarms(c) != 0;
}
static void lightTick(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 40) return;
  last = now;
  if (hwG < 0 && hwR < 0 && hwStrip < 0) return;
  bool test = lightTestUntil && (int32_t)(now - lightTestUntil) < 0;
  int bri = test ? 100 : alNight() ? A.nbri : A.bri;
  alr::PackIn in[MAX_PK];
  int n = packN();
  for (int i = 0; i < n; i++) packIn(i, in[i]);
  if (A.light == 1) {
    uint8_t g = 0, r = 0;
    if (test) { int ph = (int)(((now - lightTestAt) / 1500) % 3); g = ph != 1 ? 255 : 0; r = ph != 0 ? 255 : 0; }   // зелений, червоний, обидва
    else {
      int w = 0, ws = 999;                          // найгірша батарея: тривога, далі найменший заряд
      for (int i = 0; i < n; i++) { int sc = !in[i].has || in[i].lost ? 200 : in[i].alarm ? -1 : in[i].soc; if (sc < ws) { ws = sc; w = i; } }
      alr::twoLeds(in[w], now, g, r);
    }
    ledOut(hwG, (uint8_t)(g * bri / 100));
    ledOut(hwR, (uint8_t)(r * bri / 100));
  } else if (hwStrip >= 0) {
    static uint8_t px[STRIP_MAX][3];
    if (test) {                                     // червоний, зелений, синій, білий — по 1,5 с
      static const uint8_t C[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
      int ph = (int)(((now - lightTestAt) / 1500) % 4);
      for (int i = 0; i < stripN; i++) memcpy(px[i], C[ph], 3);
    } else alr::stripFrame(in, n, now, px, stripN);
    for (int i = 0; i < stripN; i++) for (int c = 0; c < 3; c++) px[i][c] = (uint8_t)(px[i][c] * bri / 100);
    stripShow(px);
  }
}
static void alertTick(uint32_t now) {
  alertPacks(now);
  sndStep(now);
  lightTick(now);
}
static void alertStart() { alertHwApply(); }
static void alertInfo() {
  static const char* const LT[3] = {"немає", "два світлодіоди", "стрічка WS2812B"};
  static const char* const ST[3] = {"немає", "активний бузер", "пасивний бузер"};
  Serial.printf("Світло: %s", LT[A.light % 3]);
  if (A.light == 1) Serial.printf(" (зелений GPIO%d, червоний GPIO%d)", A.ledG, A.ledR);
  if (A.light == 2) Serial.printf(" (GPIO%d, %u шт.)", A.stripPin, A.stripN);
  Serial.printf("; звук: %s", ST[A.snd % 3]);
  if (A.snd) Serial.printf(" (GPIO%d, гучність %u %%)", A.bzPin, A.vol);
  Serial.printf("; ніч %u–%u%s\n", A.nf, A.nt, alNight() ? " (зараз ніч)" : "");
}

// ---------- сторінка ----------
static void hAlertGet() {
  auto b = [](bool x) { return x ? "true" : "false"; };
  String o;
  o.reserve(1600);
  o = String("{\"light\":") + A.light + ",\"g\":" + A.ledG + ",\"r\":" + A.ledR + ",\"low\":" + b(A.ledLow) + ",\"sp\":" + A.stripPin +
      ",\"sn\":" + A.stripN + ",\"so\":" + A.stripOrder + ",\"bri\":" + A.bri + ",\"nbri\":" + A.nbri + ",\"snd\":" + A.snd + ",\"bz\":" + A.bzPin +
      ",\"bzLow\":" + b(A.bzLow) + ",\"vol\":" + A.vol + ",\"mask\":" + A.mask + ",\"rep\":" + A.rep + ",\"nf\":" + A.nf + ",\"nt\":" + A.nt +
      ",\"nm\":" + A.nmode + ",\"night\":" + b(alNight()) + ",\"clock\":" + b(lcdClockOk()) + ",\"muted\":" + b(muted) +
      ",\"playing\":" + b(sI >= 0 || sqN > 0) + ",\"pins\":[";
  bool first = true;
  for (int8_t p : AL_PINS) if (alPinAllowed(p)) { o += String(first ? "" : ",") + p; first = false; }
  o += "],\"lcdPins\":[";
  if (L.on) { const int8_t d[6] = {L.clk, L.din, L.cs, L.rs, L.rst, L.bl}; first = true; for (int8_t x : d) if (x >= 0) { o += String(first ? "" : ",") + x; first = false; } }
  o += "],\"names\":[";
  for (int e = 0; e < bev::E_COUNT; e++) o += String(e ? "," : "") + "\"" + bev::evName(e) + "\"";
  o += "],\"crit\":[";
  for (int e = 0; e < bev::E_COUNT; e++) o += String(e ? "," : "") + (bev::evCritical(e) ? "1" : "0");
  o += "],\"crits\":[";
  first = true;
  for (int i = 0; i < packN(); i++) if (R[i].crit) { o += String(first ? "" : ",") + "{\"pk\":" + (i + 1) + ",\"e\":" + R[i].crit + "}"; first = false; }
  o += "],\"log\":[";
  for (int k = 0; k < evlN; k++) {
    const EvLog& x = evl[(evlHead - 1 - k + 12) % 12];
    char tm[8] = "";
    if (x.t) { struct tm tmv; localtime_r(&x.t, &tmv); snprintf(tm, sizeof tm, "%02d:%02d", tmv.tm_hour, tmv.tm_min); }
    o += String(k ? "," : "") + "{\"t\":\"" + tm + "\",\"ago\":" + String((millis() - x.ms) / 1000) + ",\"pk\":" + (x.pk + 1) + ",\"e\":" + x.ev + "}";
  }
  o += "]}";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", o);
}
static bool alArg(const char* name, int lo, int hi, int& out) {
  if (!web.hasArg(name)) return false;
  String s = web.arg(name);
  s.trim();
  if (!s.length()) return false;
  long v = s.toInt();
  if (v < lo || v > hi) return false;
  out = (int)v;
  return true;
}
static void hAlertSave() {
  if (!trusted("Змінити світло й звук")) return;
  AlertCfg n = A;
  int v;
  auto bad = [](const String& m) { web.send(400, "application/json", "{\"err\":\"" + jsonEsc(m) + "\"}"); };
  if (!alArg("light", 0, 2, v)) return bad("Світло: виберіть тип"); n.light = v;
  if (!alArg("g", -1, 21, v)) return bad("Вивід зеленого світлодіода"); n.ledG = v;
  if (!alArg("r", -1, 21, v)) return bad("Вивід червоного світлодіода"); n.ledR = v;
  if (!alArg("sp", -1, 21, v)) return bad("Вивід стрічки"); n.stripPin = v;
  if (!alArg("sn", 1, STRIP_MAX, v)) return bad("Світлодіодів у стрічці — від 1 до 60"); n.stripN = v;
  if (!alArg("so", 0, 2, v)) return bad("Порядок кольорів"); n.stripOrder = v;
  if (!alArg("bri", 1, 100, v)) return bad("Яскравість удень — від 1 до 100 %"); n.bri = v;
  if (!alArg("nbri", 0, 100, v)) return bad("Яскравість уночі — від 0 до 100 %"); n.nbri = v;
  if (!alArg("snd", 0, 2, v)) return bad("Звук: виберіть тип"); n.snd = v;
  if (!alArg("bz", -1, 21, v)) return bad("Вивід бузера"); n.bzPin = v;
  if (!alArg("vol", 5, 100, v)) return bad("Гучність — від 5 до 100 %"); n.vol = v;
  if (!alArg("mask", 0, 65535, v)) return bad("Які події звучать"); n.mask = (uint16_t)v & (uint16_t)((1u << bev::E_COUNT) - 2);
  if (!alArg("rep", 0, 120, v)) return bad("Повтор — від 0 до 120 хв"); n.rep = v;
  if (!alArg("nf", 0, 23, v)) return bad("Ніч: години від 0 до 23"); n.nf = v;
  if (!alArg("nt", 0, 23, v)) return bad("Ніч: години від 0 до 23"); n.nt = v;
  if (!alArg("nm", 0, 2, v)) return bad("Звук уночі"); n.nmode = v;
  n.ledLow = web.arg("low") == "1";
  n.bzLow = web.arg("bzLow") == "1";
  if (n.light != 1) n.ledG = n.ledR = -1;
  if (n.light != 2) n.stripPin = -1;
  if (!n.snd) n.bzPin = -1;
  String err;
  if (!alertPinsOk(n, err)) return bad(err);
  sndStop();
  A = n;
  alertHwApply();
  if (!alertSave()) return bad("Не вдалося записати налаштування");
  alertInfo();
  web.send(200, "application/json", "{\"ok\":true}");
}
//  Перевірка (нічого не зберігає): what=light — 6 с кольорів; what=sound&ev=N — сигнал події; what=mute — тиша
static void hAlertTest() {
  if (!trusted("Перевірити світло й звук")) return;
  String w = web.arg("what");
  if (w == "mute") { alertMute("сторінка"); return web.send(200, "application/json", "{\"ok\":true}"); }
  if (w == "light") {
    if (hwG < 0 && hwR < 0 && hwStrip < 0) return web.send(400, "application/json", "{\"err\":\"Світло не налаштовано: виберіть тип і виводи, «Зберегти»\"}");
    lightTestAt = millis();
    lightTestUntil = (lightTestAt + 6000) | 1;
    return web.send(200, "application/json", "{\"ok\":true}");
  }
  if (w == "sound") {
    if (hwBz < 0) return web.send(400, "application/json", "{\"err\":\"Звук не налаштовано: виберіть тип і вивід, «Зберегти»\"}");
    int e;
    if (!alArg("ev", 1, bev::E_COUNT - 1, e)) return web.send(400, "application/json", "{\"err\":\"Невідома подія\"}");
    sndStop();
    sndPush(A.snd == 2 ? alr::melody(e) : alr::pattern(e), A.snd == 2);
    return web.send(200, "application/json", "{\"ok\":true}");
  }
  web.send(400, "application/json", "{\"err\":\"Невідома перевірка\"}");
}
