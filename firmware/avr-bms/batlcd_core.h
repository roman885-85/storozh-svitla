#pragma once
/*  Екран мосту батареї: дисплей Open-Smart 1,8″, 128×64, ST7567 (1.1.0). Власник 22.11.2026: «добавить поддержку lcd 1.8
    open-smart st7567 в bms по примеру с мостом (настройки и отображения)». Малювання в буфер — без Arduino: той самий
    код малює картинки для перевірки на комп'ютері (tests/batlcd/run.sh), як lcd_core.h Сторожа.

    Буфер — як пам'ять самого ST7567: 8 смуг по 8 рядків, байт = стовпчик з 8 точок, біт 0 — верхня.
    Розкладка (як на Сторожі: головне — великими, внизу сигнали):
      рядки 0–21   заряд великими цифрами, праворуч — «батарейка» й слово «розряд» / «заряд» / «спокій»;
      рядки 24–37  напруга й струм — високими літерами;
      рядок 40     потужність і на скільки вистачить (чи коли зарядиться);  тривога BMS — тут, навиворіт, сторінками;
      рядок 48     комірки: скільки, найнижча й найвища напруга;
      рядок 56     сигнал BMS (Б) і Сторожа (С), температури батареї й ключів.
    Правило власника про екрани: числа не стрибають щосекунди — струм усереднений, напруга з одним знаком після коми.  */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "lcd_font.h"

struct BatView {
  //  зв'язок з BMS: 2 — на зв'язку й дані свіжі, 0 — шукаю, 1 — під'єднуюсь, 3 — стану немає, 4 — пауза;
  //  1.2.0, «по черзі»: 5 — чекаю черги, 6 — BMS не чути, 7 — BMS не відповіла
  int8_t link = 0;
  bool has = false;          // дані BMS були хоч раз
  int soc = 0;               // заряд, %
  int32_t mv = 0;            // напруга, мВ
  int32_t ma = 0;            // струм, мА (усереднений): + заряд, − розряд
  int leftMin = -1;          // хвилин: розряд — до повного розряду, заряд — до повної; −1 — невідомо
  uint8_t n = 0;             // комірок
  uint16_t lo = 0, hi = 0;   // найнижча й найвища комірка, мВ
  bool hT = false, hTm = false;
  int16_t t = 0, tm = 0;     // температура батареї (вища з двох датчиків) і ключів, 0,1 °C
  const char* alarm = "";    // тривоги BMS простим текстом; "" — немає
  int alarmPage = 0;         // яка сторінка тексту тривоги (по колу)
  int ageMin = 0;            // скільки хвилин немає даних (коли link != 2)
  int8_t rBms = -1, rWifi = -1;   // рівень сигналу 0…4; −1 — немає
  bool setup = false;        // увімкнено сторінку налаштування (своя мережа)
  const char* ap = "";       // її назва, «BMS-XXXX»
  const char* ver = "";      // заставка на запуску: версія ("" — не заставка)
  uint8_t test = 0;          // перевірка: 1 — усе чорне, 2 — шахівниця
  int8_t idx = 0, count = 1; // 1.2.0: яка батарея на екрані (з 0) і скільки їх — кілька показуються по черзі
  const char* label = "";    // назва батареї від власника ("" — немає)
};

class BatFb {
 public:
  static constexpr int W = 128, H = 64;
  uint8_t fb[W * H / 8];
  void clear() { memset(fb, 0, sizeof fb); }
  void px(int x, int y, bool on) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t& b = fb[(y >> 3) * W + x];
    if (on) b |= 1 << (y & 7); else b &= ~(1 << (y & 7));
  }
  bool get(int x, int y) const { return x >= 0 && x < W && y >= 0 && y < H && (fb[(y >> 3) * W + x] >> (y & 7) & 1); }
  void fill(int x, int y, int w, int h, bool on) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x + i, y + j, on); }
  void frame(int x, int y, int w, int h, bool on) { fill(x, y, w, 1, on); fill(x, y + h - 1, w, 1, on); fill(x, y, 1, h, on); fill(x + w - 1, y, 1, h, on); }
  void invert(int x, int y, int w, int h) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x + i, y + j, !get(x + i, y + j)); }
  static uint32_t nextCp(const char*& s) {
    uint8_t c = (uint8_t)*s;
    if (!c) return 0;
    if (c < 0x80) { s++; return c; }
    int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    uint32_t cp = c & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
    s++;
    for (int i = 0; i < n && (*s & 0xC0) == 0x80; i++) cp = (cp << 6) | (*s++ & 0x3F);
    return cp;
  }
  static int glyph(uint32_t cp) {
    int lo = 0, hi = LCD_NG - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (LCD_CP[m] == cp) return m; if (LCD_CP[m] < cp) lo = m + 1; else hi = m - 1; }
    return cp == '?' ? 0 : glyph('?');
  }
  static bool has(uint32_t cp) {
    int lo = 0, hi = LCD_NG - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (LCD_CP[m] == cp) return true; if (LCD_CP[m] < cp) lo = m + 1; else hi = m - 1; }
    return false;
  }
  static int len(const char* s) { int n = 0; while (nextCp(s)) n++; return n; }
  static int width(const char* s, int sx = 1) { int n = len(s); return n ? n * 6 * sx - sx : 0; }
  //  sx, sy — у скільки разів більші точки: 1×1 — 5×7, 1×2 — високі 5×14, 3×3 — великі 15×21
  void text(int x, int y, const char* s, bool on = true, int sy = 1, int sx = 1) {
    uint32_t cp;
    while ((cp = nextCp(s))) {
      const uint8_t* g = LCD_GL[glyph(cp)];
      for (int c = 0; c < 5; c++)
        for (int r = 0; r < 7; r++)
          if (g[c] >> r & 1) fill(x + c * sx, y + r * sy, sx, sy, on);
      x += 6 * sx;
    }
  }
  void center(int y, const char* s, bool on = true, int sy = 1, int sx = 1) { text((W - width(s, sx)) / 2, y, s, on, sy, sx); }
  void right(int xr, int y, const char* s, bool on = true, int sy = 1, int sx = 1) { text(xr - width(s, sx), y, s, on, sy, sx); }
  //  Рівень сигналу: 4 смужки, як на дисплеї Сторожа; lvl < 0 — хрестик
  void bars(int x, int y, int lvl) {
    static const int HB[4] = {2, 3, 5, 7};
    if (lvl < 0) { for (int i = 0; i < 5; i++) { px(x + 3 + i, y + 1 + i, true); px(x + 7 - i, y + 1 + i, true); } return; }
    for (int i = 0; i < 4; i++) {
      int bx = x + i * 3;
      if (i < lvl) fill(bx, y + 7 - HB[i], 2, HB[i], true);
      else fill(bx, y + 6, 2, 1, true);
    }
  }
  //  «Батарейка» w×h з носиком праворуч; заповнення за відсотком; noData — порожня з рискою
  void gauge(int x, int y, int w, int h, int pct, bool noData) {
    frame(x, y, w - 3, h, true);
    fill(x + w - 3, y + h / 4, 3, h - 2 * (h / 4), true);
    if (noData) { for (int i = 0; i < h - 4; i++) px(x + (w - 3) / 2 - (h - 4) / 2 + i, y + 2 + i, true); return; }
    int inner = w - 3 - 4;
    int f = (pct < 0 ? 0 : pct > 100 ? 100 : pct) * inner / 100;
    if (pct > 0 && f < 1) f = 1;
    fill(x + 2, y + 2, f, h - 4, true);
  }
};

//  «24,7» з 24712 (ділити на div, dec знаків після коми), з комою; мінус — звичайний «-»
inline void batFix(char* o, size_t n, long v, int div, int dec) {
  bool neg = v < 0;
  unsigned long a = neg ? (unsigned long)-v : (unsigned long)v;
  unsigned long p = dec == 0 ? 1 : dec == 1 ? 10 : dec == 2 ? 100 : 1000;
  unsigned long q = (a * p + (unsigned long)div / 2) / (unsigned long)div;      // округлене число в одиницях 1/p
  if (dec == 0) snprintf(o, n, "%s%lu", neg && q ? "-" : "", q);
  else snprintf(o, n, "%s%lu,%0*lu", neg && q ? "-" : "", q / p, dec, q % p);
}
//  Тривалість для рядка: «45 год», «2 год 10 хв», «35 хв», «3 дн 4 год»
inline void batDur(char* o, size_t n, int minutes) {
  if (minutes < 0) { if (n) o[0] = 0; return; }
  int h = minutes / 60, d = h / 24;
  if (minutes < 60) snprintf(o, n, "%d хв", minutes < 1 ? 1 : minutes);
  else if (h < 10) { if (minutes % 60) snprintf(o, n, "%d год %d хв", h, minutes % 60); else snprintf(o, n, "%d год", h); }
  else if (d < 3) snprintf(o, n, "%d год", h);
  else snprintf(o, n, "%d дн %d год", d, h % 24);
}
//  Слова по рядках до maxCh знаків: повертає кількість рядків, у out — рядок k (по колу). Як lcdPage Сторожа.
inline int batPage(const char* s, int k, char* out, size_t n, int maxCh) {
  const char* st[24];
  const char* en[24];
  int np = 0;
  const char* p = s;
  while (*p && np < 24) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* a = p;
    const char* e = p;
    const char* c = p;
    int cnt = 0;
    while (*c) {
      const char* w = c;
      int wl = 0;
      while (*w && *w != ' ') { BatFb::nextCp(w); wl++; }
      int need = cnt ? cnt + 1 + wl : wl;
      if (need > maxCh) {
        if (!cnt) { const char* t = c; for (int i = 0; i < maxCh && *t; i++) BatFb::nextCp(t); e = t; }
        break;
      }
      cnt = need;
      e = w;
      c = w;
      while (*c == ' ') c++;
    }
    st[np] = a;
    en[np] = e;
    np++;
    p = e;
  }
  if (!np) { if (n) out[0] = 0; return 0; }
  int i = ((k % np) + np) % np;
  size_t L = (size_t)(en[i] - st[i]);
  if (L >= n) L = n - 1;
  memcpy(out, st[i], L);
  out[L] = 0;
  return np;
}
inline int batLevel(int rssi, bool ble) {         // смужки з дБм; Bluetooth слабший за Wi-Fi на ті самі смужки
  if (rssi >= 0) return -1;
  int a = ble ? -62 : -55;
  return rssi >= a ? 4 : rssi >= a - 10 ? 3 : rssi >= a - 20 ? 2 : rssi >= a - 30 ? 1 : 0;
}

inline void batRender(BatFb& f, const BatView& v) {
  f.clear();
  if (v.test == 1) { f.fill(0, 0, BatFb::W, BatFb::H, true); return; }
  if (v.test == 2) { for (int y = 0; y < BatFb::H; y++) for (int x = 0; x < BatFb::W; x++) f.px(x, y, ((x >> 3) + (y >> 3)) & 1); return; }
  char b[64], c[40];
  if (v.ver && v.ver[0]) {                          // заставка на запуску
    f.center(6, "МІСТ БАТАРЕЇ", true, 2);
    f.center(26, "Сторож світла");
    snprintf(b, sizeof b, "версія %s", v.ver);
    f.center(40, b);
    f.center(54, "шукаю BMS…");
    return;
  }
  const bool live = v.link == 2 && v.has;
  static const char* const WHY[8] = {"шукаю BMS поруч", "під'єднуюсь до BMS", "", "BMS не шле стан", "пауза перед спробою",
                                     "чекаю опитування", "BMS не чути", "BMS не відповіла"};
  //  1.2.0: кілька батарей — котра на екрані: назва від власника (до 7 знаків під «батарейкою») чи «1 з 2»
  char tag[40] = "";
  if (v.count > 1) {
    if (v.label && v.label[0]) {
      const char* p = v.label;
      for (int i = 0; i < 7 && *p; i++) BatFb::nextCp(p);
      size_t L = (size_t)(p - v.label);
      if (L >= sizeof tag) L = sizeof tag - 1;
      memcpy(tag, v.label, L);
      tag[L] = 0;
    } else snprintf(tag, sizeof tag, "%d з %d", v.idx + 1, v.count);
  }

  // 1) Заряд великими цифрами й «батарейка»
  if (live) {
    snprintf(b, sizeof b, "%d%%", v.soc);
    f.text(2, 0, b, true, 3, 3);
    const char* w = tag[0] ? tag : v.ma <= -300 ? "розряд" : v.ma >= 300 ? "заряд" : "спокій";
    f.gauge(82, 1, 44, 11, v.soc, false);
    f.text(82 + (41 - BatFb::width(w)) / 2, 14, w);
  } else {
    f.fill(0, 0, BatFb::W, 22, true);
    f.center(4, "НЕМАЄ BMS", false, 2, 2);
  }

  if (live) {
    // 2) Напруга й струм — високими
    batFix(c, sizeof c, v.mv, 1000, 1);
    snprintf(b, sizeof b, "%s В", c);
    f.text(2, 24, b, true, 2);
    long aa = v.ma < 0 ? -v.ma : v.ma;
    batFix(c, sizeof c, aa, 1000, aa >= 99950 ? 0 : 1);
    snprintf(b, sizeof b, "%s А", c);
    f.right(126, 24, b, true, 2);

    // 3) Потужність і час — або тривога BMS навиворіт
    if (v.alarm && v.alarm[0]) {
      char a[96];
      snprintf(a, sizeof a, "BMS: %s", v.alarm);
      batPage(a, v.alarmPage, b, sizeof b, 21);
      f.fill(0, 39, BatFb::W, 9, true);
      f.center(40, b, false);
    } else {
      long w = (long)(((long long)v.mv * (v.ma < 0 ? -v.ma : v.ma)) / 1000000);
      w = w < 1000 ? (w + 2) / 5 * 5 : (w + 5) / 10 * 10;                       // до 5 Вт (понад кіловат — до 10)
      char d[32];
      batDur(d, sizeof d, v.leftMin);
      if (v.ma <= -300) {
        if (d[0]) snprintf(b, sizeof b, "%ld Вт на %s", w, d); else snprintf(b, sizeof b, "розряд %ld Вт", w);
        if (BatFb::len(b) > 21) snprintf(b, sizeof b, "на %s", d);
      } else if (v.ma >= 300) {
        if (d[0]) snprintf(b, sizeof b, "%ld Вт повна за %s", w, d); else snprintf(b, sizeof b, "заряд %ld Вт", w);
        if (BatFb::len(b) > 21) snprintf(b, sizeof b, "повна за %s", d);
      } else snprintf(b, sizeof b, "без навантаження");
      f.text(2, 40, b);
    }

    // 4) Комірки
    if (v.n) {
      char lo[12], hi[12];
      batFix(lo, sizeof lo, v.lo, 1000, 3);
      batFix(hi, sizeof hi, v.hi, 1000, 3);
      snprintf(b, sizeof b, "%u ком. %s–%s В", (unsigned)v.n, lo, hi);
      if (BatFb::len(b) > 21) snprintf(b, sizeof b, "%s–%s В", lo, hi);
      f.text(2, 48, b);
    }
  } else {
    // немає зв'язку з BMS: чому, і що було
    f.center(26, WHY[v.link >= 0 && v.link <= 7 ? v.link : 0]);
    if (v.has) {
      char d[32];
      batDur(d, sizeof d, v.ageMin);
      snprintf(b, sizeof b, "було %d %%, %s тому", v.soc, d);
      if (BatFb::len(b) > 21) snprintf(b, sizeof b, "було %d %%", v.soc);
      f.center(36, b);
    } else f.center(36, "даних ще не було");
    if (v.count > 1) {                              // котра батарея — рядок комірок вільний
      if (v.label && v.label[0]) snprintf(b, sizeof b, "«%s» %d з %d", v.label, v.idx + 1, v.count);
      if (!(v.label && v.label[0]) || BatFb::len(b) > 21) snprintf(b, sizeof b, "батарея %d з %d", v.idx + 1, v.count);
      f.center(48, b);
    }
  }
  //  Сторінка налаштування — підказка замість комірок (у «немає зв'язку» — нижче причини)
  if (v.setup) {
    f.fill(0, 47, BatFb::W, 9, false);
    snprintf(b, sizeof b, "%s 192.168.4.1", v.ap);       // «BMS-1A2B 192.168.4.1» — мережа без пароля й адреса сторінки
    f.center(48, b);
  }

  // 5) Унизу: сигнал BMS (Б) і Сторожа (С); температури
  f.text(0, 57, "Б");  f.bars(7, 57, v.rBms);
  f.text(24, 57, "С"); f.bars(31, 57, v.rWifi);
  if (live && (v.hT || v.hTm)) {
    char t1[12] = "", t2[12] = "";
    if (v.hT) { batFix(c, sizeof c, v.t, 10, 0); snprintf(t1, sizeof t1, "%s°", c); }
    if (v.hTm) { batFix(c, sizeof c, v.tm, 10, 0); snprintf(t2, sizeof t2, "кл %s°", c); }
    snprintf(b, sizeof b, "%s%s%s", t1, t1[0] && t2[0] ? " " : "", t2);
    f.right(127, 57, b);
  }
}
