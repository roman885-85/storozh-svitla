/*  Дисплей мосту батареї (1.1.0): налаштування, показ, сторінка. Власник 22.11.2026: «добавить поддержку lcd 1.8 open-smart
    st7567 в bms по примеру с мостом (настройки и отображения)» — тобто як дисплей Nokia на Сторожі: картка налаштувань
    зі знімком екрана, виводи, контраст, підсвітка, ніч, перевірка; на екрані — головне великими.

    Це частина avr-bms.ino (вставляється після спільних змінних): малює batlcd_core.h, на скло шле st7567.h.
    У флеш пише лише кнопка «Зберегти дисплей» (правило власника).  */
#include "batlcd_core.h"
#include "st7567.h"
#include <time.h>

struct LcdSet {
  bool on = false;                                 // дисплей під'єднано
  int8_t clk = 4, din = 6, cs = 7, rs = 5, rst = 10, bl = 3;   // rst, bl: −1 — не під'єднано
  //  Типові — як у робочому зразку власника (moto_battery_reader: U8g2 ENH_DG128064, зсув картинки 4)
  uint8_t con = 50, rat = 3, xoff = 4;             // контраст 0–63, грубий рівень 0–7, зсув картинки вправо 0–4
  bool b7 = false, inv = false, flip = false;      // зсув 1/7 (інакше 1/9), інверсія, поворот на 180°
  uint8_t bri = 100;                               // підсвітка, %
  bool blLow = false;                              // підсвітка вмикається низьким рівнем
  uint8_t nf = 22, nt = 7, nb = 10;                // ніч: з години, до години (однакові — ночі немає), підсвітка вночі, %
};
static LcdSet L;
static St7567 lcdDrv;
static BatFb lcdFb;
static int lcdBlPin = -1;                          // на якому виводі зараз ШІМ підсвітки
static uint8_t lcdTestMode = 0;                    // перевірка: 1 — усе чорне, 2 — шахівниця
static int lcdTestCon = -1, lcdTestBri = -1;       // перевірка повзунків: контраст і підсвітка без збереження
static uint32_t lcdTestUntil = 0, lcdStartMs = 0, lcdRefreshMs = 0;
static float curFast = 0, curSlow = 0;             // струм, мА: згладжений для показу (~5 с) і середній (~5 хв) для «на скільки вистачить»
static bool curInit = false;
static char lcdAlarm[200];
//  Виводи ESP32-C3, які можна дати дисплею: без 9 (кнопка BOOT), 18 і 19 (USB), 11–17 (флеш). 2 і 8 — виводи запуску:
//  працюють, але краще не займати (на платі SuperMini на 8 ще й світлодіод).
static const int8_t LCD_PINS[] = {0, 1, 3, 4, 5, 6, 7, 10, 20, 21, 2, 8};

static bool lcdPinAllowed(int p) { for (int8_t a : LCD_PINS) if (a == p) return true; return false; }
static bool lcdPinsOk(const LcdSet& s, String& err) {
  const int8_t p[6] = {s.clk, s.din, s.cs, s.rs, s.rst, s.bl};
  static const char* const N[6] = {"CLK", "DI", "CS", "RS", "RST", "LED"};
  for (int i = 0; i < 6; i++) {
    if (p[i] < 0) { if (i < 4) { err = String("Вивід ") + N[i] + " потрібен обов'язково"; return false; } continue; }
    if (!lcdPinAllowed(p[i])) { err = String("GPIO") + p[i] + " для " + N[i] + " не підходить"; return false; }
    for (int k = 0; k < i; k++) if (p[k] == p[i]) { err = String("GPIO") + p[i] + " вибрано двічі: " + N[k] + " і " + N[i]; return false; }
  }
  return true;
}

static void lcdLoad() {
  Preferences p;
  if (!p.begin("bms", true)) return;
  L.on = p.getBool("lOn", false);
  L.clk = p.getChar("lClk", 4); L.din = p.getChar("lDin", 6); L.cs = p.getChar("lCs", 7);
  L.rs = p.getChar("lRs", 5); L.rst = p.getChar("lRst", 10); L.bl = p.getChar("lBl", 3);
  L.con = p.getUChar("lCon", 50); L.rat = p.getUChar("lRat", 3); L.xoff = p.getUChar("lXoff", 4);
  L.b7 = p.getBool("lB7", false); L.inv = p.getBool("lInv", false); L.flip = p.getBool("lFlip", false);
  L.bri = p.getUChar("lBri", 100); L.blLow = p.getBool("lBlLow", false);
  L.nf = p.getUChar("lNf", 22); L.nt = p.getUChar("lNt", 7); L.nb = p.getUChar("lNb", 10);
  p.end();
  String e;
  if (!lcdPinsOk(L, e)) L.on = false;              // зіпсовані налаштування — дисплей не чіпаємо
}
static bool lcdSave() {
  Preferences p;
  if (!p.begin("bms", false)) return false;
  p.putBool("lOn", L.on);
  p.putChar("lClk", L.clk); p.putChar("lDin", L.din); p.putChar("lCs", L.cs);
  p.putChar("lRs", L.rs); p.putChar("lRst", L.rst); p.putChar("lBl", L.bl);
  p.putUChar("lCon", L.con); p.putUChar("lRat", L.rat); p.putUChar("lXoff", L.xoff);
  p.putBool("lB7", L.b7); p.putBool("lInv", L.inv); p.putBool("lFlip", L.flip);
  p.putUChar("lBri", L.bri); p.putBool("lBlLow", L.blLow);
  p.putUChar("lNf", L.nf); p.putUChar("lNt", L.nt); p.putUChar("lNb", L.nb);
  p.end();
  return true;
}

static bool lcdClockOk() { return time(nullptr) > 1735689600; }
static bool lcdNight() {
  if (L.nf == L.nt || !lcdClockOk()) return false;  // без годинника (Сторож без інтернету) ночі немає — світимо як удень
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  int h = tmv.tm_hour;
  return L.nf < L.nt ? (h >= L.nf && h < L.nt) : (h >= L.nf || h < L.nt);
}
static void lcdBacklight() {
  int want = L.on ? L.bl : -1;
  if (lcdBlPin != want) {
    if (lcdBlPin >= 0) { ledcDetach(lcdBlPin); pinMode(lcdBlPin, INPUT); }
    lcdBlPin = want;
    if (lcdBlPin >= 0 && !ledcAttach(lcdBlPin, 2000, 8)) lcdBlPin = -1;
  }
  if (lcdBlPin < 0) return;
  int pct = lcdTestBri >= 0 ? lcdTestBri : lcdNight() ? L.nb : L.bri;
  int duty = pct * 255 / 100;
  ledcWrite(lcdBlPin, L.blLow ? 255 - duty : duty);
}
static St7567::Cfg lcdCfg() {
  St7567::Cfg c;
  c.clk = L.clk; c.din = L.din; c.cs = L.cs; c.rs = L.rs; c.rst = L.rst;
  c.contrast = lcdTestCon >= 0 ? lcdTestCon : L.con;
  c.ratio = L.rat; c.bias7 = L.b7; c.flip = L.flip; c.inv = L.inv; c.xoff = L.xoff;
  return c;
}
//  Увімкнути чи вимкнути дисплей за налаштуваннями (на запуску й після «Зберегти»)
static void lcdStart() {
  if (L.on) lcdDrv.begin(lcdCfg()); else lcdDrv.end();
  lcdStartMs = millis();
  lcdRefreshMs = lcdStartMs;
  lcdBacklight();
}

static void lcdView(BatView& v, uint32_t now) {
  jk::Cells c;
  portENTER_CRITICAL(&mux);
  c = cells;
  portEXIT_CRITICAL(&mux);
  uint32_t at = cellsAtMs;
  v.has = at && c.fmt;
  v.link = bst;
  if (v.has && bst == 2) {
    if (!curInit) { curFast = curSlow = (float)c.ma; curInit = true; }
    curFast += ((float)c.ma - curFast) * 0.2f;
    //  середній — за ~5 хв; розряд чи заряд щойно почався — з теперішнього струму, а не з нуля
    if ((c.ma <= -300 && curSlow > -300) || (c.ma >= 300 && curSlow < 300)) curSlow = (float)c.ma;
    else curSlow += ((float)c.ma - curSlow) / 300.0f;
  }
  v.soc = c.soc;
  v.mv = (int32_t)c.totalMv;
  v.ma = (int32_t)curFast;
  v.leftMin = -1;
  if (curSlow <= -300) { float m = (float)c.remMah / -curSlow * 60.0f; v.leftMin = m > 99999 ? 99999 : (int)m; }
  else if (curSlow >= 300 && c.fullMah > c.remMah) { float m = (float)(c.fullMah - c.remMah) / curSlow * 60.0f; v.leftMin = m > 99999 ? 99999 : (int)m; }
  v.n = c.n; v.lo = c.vmin; v.hi = c.vmax;
  v.hT = c.hasT1 || c.hasT2;
  v.t = c.hasT1 && c.hasT2 ? (c.t1 > c.t2 ? c.t1 : c.t2) : c.hasT1 ? c.t1 : c.t2;
  v.hTm = c.hasTmos; v.tm = c.tmos;
  //  тривоги BMS — ті самі, що на сторінці й у Сторожа
  lcdAlarm[0] = 0;
  if (v.has) {
    uint32_t a = c.err & ~jk::ERR_INFO;
    for (int b = 0; b < 32; b++) {
      if (!(a >> b & 1)) continue;
      const char* nm = jk::errName(b);
      char one[48];
      if (*nm) snprintf(one, sizeof one, "%s", nm); else snprintf(one, sizeof one, "тривога %d", b);
      if (strlen(lcdAlarm) + strlen(one) + 3 < sizeof lcdAlarm) { if (lcdAlarm[0]) strcat(lcdAlarm, ", "); strcat(lcdAlarm, one); }
    }
    if (!c.dis && strlen(lcdAlarm) + 40 < sizeof lcdAlarm) { if (lcdAlarm[0]) strcat(lcdAlarm, ", "); strcat(lcdAlarm, "BMS вимкнула розряд"); }
  }
  v.alarm = lcdAlarm;
  v.alarmPage = (int)(now / 3000);                 // сторінка тексту тривоги — кожні 3 с
  v.ageMin = at ? (int)((now - at) / 60000) : 0;
  v.rBms = bst == 2 || bst == 3 ? batLevel((int)curRssi, true) : -1;
  v.rWifi = WiFi.isConnected() ? batLevel(WiFi.RSSI(), false) : -1;
  v.setup = setupOn;
  v.ap = apName.c_str();
  if (now - lcdStartMs < 3000 && !v.has) v.ver = FW_VERSION;      // заставка, поки даних ще немає
  if (lcdTestMode && (int32_t)(now - lcdTestUntil) < 0) v.test = lcdTestMode;
}

//  Раз на секунду: намалювати в буфер (знімок для сторінки — завжди), на скло — лише коли дисплей увімкнено
static void lcdTick(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 1000) return;
  last = now;
  if ((lcdTestMode || lcdTestCon >= 0 || lcdTestBri >= 0) && (int32_t)(now - lcdTestUntil) >= 0) {   // перевірка скінчилась — як збережено
    lcdTestMode = 0; lcdTestCon = lcdTestBri = -1;
    lcdDrv.apply(lcdCfg());
  }
  BatView v;
  lcdView(v, now);
  batRender(lcdFb, v);
  lcdBacklight();
  if (!L.on) return;
  if (now - lcdRefreshMs > 60000) { lcdRefreshMs = now; lcdDrv.refresh(); }   // раз на хвилину — регістри й увесь вміст заново
  lcdDrv.flush(lcdFb.fb);
}

// ---------- сторінка ----------
static void hLcdGet() {
  String o;
  o.reserve(2600);
  auto b = [](bool x) { return x ? "true" : "false"; };
  o = String("{\"on\":") + b(L.on) + ",\"ok\":" + b(lcdDrv.ok()) + ",\"clk\":" + L.clk + ",\"din\":" + L.din + ",\"cs\":" + L.cs + ",\"rs\":" + L.rs +
      ",\"rst\":" + L.rst + ",\"bl\":" + L.bl + ",\"con\":" + L.con + ",\"rat\":" + L.rat + ",\"xoff\":" + L.xoff + ",\"b7\":" + b(L.b7) + ",\"inv\":" + b(L.inv) +
      ",\"flip\":" + b(L.flip) + ",\"bri\":" + L.bri + ",\"blLow\":" + b(L.blLow) + ",\"nf\":" + L.nf + ",\"nt\":" + L.nt + ",\"nb\":" + L.nb +
      ",\"night\":" + b(lcdNight()) + ",\"test\":" + (lcdTestMode && (int32_t)(millis() - lcdTestUntil) < 0 ? lcdTestMode : 0) + ",\"clock\":\"";
  if (lcdClockOk()) {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char c[8];
    snprintf(c, sizeof c, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    o += c;
  }
  o += "\",\"pins\":[";
  for (size_t i = 0; i < sizeof LCD_PINS; i++) o += String(i ? "," : "") + LCD_PINS[i];
  o += "],\"fb\":\"";
  static const char HX[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof lcdFb.fb; i++) { o += HX[lcdFb.fb[i] >> 4]; o += HX[lcdFb.fb[i] & 15]; }
  o += "\"}";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", o);
}
static bool lcdArgInt(const char* name, int lo, int hi, int& out) {
  if (!web.hasArg(name)) return false;
  long v = web.arg(name).toInt();
  if (v < lo || v > hi) return false;
  out = (int)v;
  return true;
}
static void hLcdSave() {
  LcdSet n = L;
  int v;
  auto bad = [](const char* m) { web.send(400, "application/json", String("{\"err\":\"") + m + "\"}"); };
  n.on = web.arg("on") == "1";
  if (!lcdArgInt("clk", 0, 21, v)) return bad("Вивід CLK не задано"); n.clk = v;
  if (!lcdArgInt("din", 0, 21, v)) return bad("Вивід DI не задано"); n.din = v;
  if (!lcdArgInt("cs", 0, 21, v)) return bad("Вивід CS не задано"); n.cs = v;
  if (!lcdArgInt("rs", 0, 21, v)) return bad("Вивід RS не задано"); n.rs = v;
  if (!lcdArgInt("rst", -1, 21, v)) return bad("Вивід RST не задано"); n.rst = v;
  if (!lcdArgInt("bl", -1, 21, v)) return bad("Вивід LED не задано"); n.bl = v;
  if (!lcdArgInt("con", 0, 63, v)) return bad("Контраст — від 0 до 63"); n.con = v;
  if (!lcdArgInt("rat", 0, 7, v)) return bad("Грубий рівень — від 0 до 7"); n.rat = v;
  if (!lcdArgInt("xoff", 0, 4, v)) return bad("Зсув картинки — від 0 до 4"); n.xoff = v;
  if (!lcdArgInt("bri", 0, 100, v)) return bad("Підсвітка — від 0 до 100 %"); n.bri = v;
  if (!lcdArgInt("nf", 0, 23, v)) return bad("Ніч: години від 0 до 23"); n.nf = v;
  if (!lcdArgInt("nt", 0, 23, v)) return bad("Ніч: години від 0 до 23"); n.nt = v;
  if (!lcdArgInt("nb", 0, 100, v)) return bad("Підсвітка вночі — від 0 до 100 %"); n.nb = v;
  n.b7 = web.arg("b7") == "1"; n.inv = web.arg("inv") == "1"; n.flip = web.arg("flip") == "1"; n.blLow = web.arg("blLow") == "1";
  String err;
  if (!lcdPinsOk(n, err)) return web.send(400, "application/json", "{\"err\":\"" + jsonEsc(err) + "\"}");
  bool rewire = n.on != L.on || n.clk != L.clk || n.din != L.din || n.cs != L.cs || n.rs != L.rs || n.rst != L.rst;
  if (rewire) lcdDrv.end();                        // старі виводи відпускаємо до того, як змінити налаштування
  L = n;
  lcdTestMode = 0; lcdTestCon = lcdTestBri = -1;
  if (!lcdSave()) return bad("Не вдалося записати налаштування");
  if (rewire) lcdStart(); else { lcdDrv.apply(lcdCfg()); lcdBacklight(); }
  Serial.printf("Дисплей: %s, виводи CLK %d, DI %d, CS %d, RS %d, RST %d, LED %d; контраст %u, підсвітка %u %%\n",
                L.on ? "увімкнено" : "вимкнено", L.clk, L.din, L.cs, L.rs, L.rst, L.bl, L.con, L.bri);
  web.send(200, "application/json", "{\"ok\":true}");
}
//  Перевірка на 60 с, нічого не зберігає: «усе чорне», «шахівниця», звичайний вигляд; повзунки контрасту й підсвітки
static void hLcdTest() {
  String m = web.arg("mode");
  int v;
  if (m == "all") lcdTestMode = 1; else if (m == "check") lcdTestMode = 2; else if (m == "normal") { lcdTestMode = 0; lcdTestCon = lcdTestBri = -1; }
  if (lcdArgInt("con", 0, 63, v)) lcdTestCon = v;
  if (lcdArgInt("bri", 0, 100, v)) lcdTestBri = v;
  lcdTestUntil = millis() + 60000;
  lcdDrv.apply(lcdCfg());
  lcdBacklight();
  web.send(200, "application/json", String("{\"ok\":true,\"on\":") + (L.on ? "true" : "false") + "}");
}
