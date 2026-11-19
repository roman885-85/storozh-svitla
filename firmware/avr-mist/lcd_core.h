#pragma once
/*  Екран дисплея Nokia 5110 (84×48, PCD8544) — малювання в буфер (1.0.22). Без Arduino: той самий код малює
    картинки для перевірки на Mac (tests/lcd/run.sh). Буфер — як у самого PCD8544: 6 смуг по 8 рядків, байт = стовпчик
    з 8 точок, біт 0 — верхня. Розкладка (власник: «уровень сигнала, статус света и ближайший график отключения или
    включения»): зверху великими літерами стан світла (немає світла — інверсія), далі «з якого часу й скільки»,
    найближча подія за графіком і скільки до неї, унизу — рівень сигналу роутера, АВР і маячка та стан Telegram.
    Правило власника про екрани: числа не миготять щосекунди — хвилини й смужки сигналу (усереднені) міняються рідко.  */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "lcd_font.h"

//  1.0.26 (власник 19.11.2026: «на дисплее не отображается график (может бегущей строкой (с регулировкой скорости). не
//  понятное время отображается и к тому же имеются ошибки в отображении этого времени»). Було «з ?14:2 · 0:03»: знака «≥»
//  у шрифті немає (малювався «?»), «≥14:22» не влазив у 8 байт («14:2»), а «0:03» без одиниць незрозуміле. Тепер:
//  рядок 17 — «з 14:22», рядок 25 — скільки, словами («1 год 5 хв», «понад 31 хв»), рядок 33 — біжучий рядок графіка.
struct LcdView {
  int8_t grid = -1;          // 1 — місто є, 0 — немає, -1 — визначається, -2 — АВР не призначено
  int8_t gen = -1;           // 1 — генератор працює, 0 — ні, -1 — маячка немає
  char since[48] = "";       // готовий рядок: «з 14:22», «з 18.11 23:10» (до 14 знаків, кирилиця — по 2 байти)
  char dur[48] = "";         // готовий рядок: «3 хв», «1 год 5 хв», «понад 2 год» («менше хвилини» — 25 байт)
  const char* mq = "";       // графік простим текстом для біжучого рядка (UTF-8); "" — графіка немає
  int mqOff = 0;             // біжучий рядок: зсув у точках; сторінками — номер сторінки
  bool mqPages = false;      // сторінками (по 14 знаків, щоразу нова) замість біжучого рядка
  //  Ефекти сповіщень (1.0.26, власник: «добавить эффекты оповещения на дисплее (мигание текста и т.п.)»)
  const char* title = "";    // замість стану великими літерами (оголошення: «ОГОЛОШЕННЯ»); "" — стан світла
  bool stFlip = false;       // фаза миготіння: смуга стану навиворіт (світле ↔ темне)
  bool invAll = false;       // фаза спалаху: увесь екран навиворіт
  bool mqInv = false;        // рядок 33 навиворіт — попередження (світле на темному)
  const char* ann = "";      // текст оголошення: рядки 17, 25, 33 — сторінками по три рядки; "" — немає
  int annPage = 0;           // яка сторінка оголошення (по колу)
  //  Тексти для дисплея — лише знаки зі шрифту (lcd_font.h): емодзі («⏰», «📣») і «≥» малюються «?»
  int8_t rWifi = -1, rAvr = -1, rMayak = -1;   // рівень 0…4, -1 — немає
  int8_t tg = -1;            // Telegram: 1 — працює, 0 — збій, -1 — не налаштовано
  bool blink = false;        // фаза повільного миготіння (раз на секунду) — лише для «вимкніть генератор»
};

class LcdFb {
 public:
  uint8_t fb[504];
  void clear() { memset(fb, 0, sizeof fb); }
  void px(int x, int y, bool on) {
    if (x < 0 || x >= 84 || y < 0 || y >= 48) return;
    uint8_t& b = fb[(y >> 3) * 84 + x];
    if (on) b |= 1 << (y & 7); else b &= ~(1 << (y & 7));
  }
  bool get(int x, int y) const { return x >= 0 && x < 84 && y >= 0 && y < 48 && (fb[(y >> 3) * 84 + x] >> (y & 7) & 1); }
  void fill(int x, int y, int w, int h, bool on) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x + i, y + j, on); }
  void frame(int x, int y, int w, int h, bool on) { fill(x, y, w, 1, on); fill(x, y + h - 1, w, 1, on); fill(x, y, 1, h, on); fill(x + w - 1, y, 1, h, on); }
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
  static int len(const char* s) { int n = 0; while (nextCp(s)) n++; return n; }
  static bool has(uint32_t cp) {                    // чи є знак у шрифті (1.0.26: текст оголошень від людей — з емодзі)
    int lo = 0, hi = LCD_NG - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (LCD_CP[m] == cp) return true; if (LCD_CP[m] < cp) lo = m + 1; else hi = m - 1; }
    return false;
  }
  static int width(const char* s, int sx = 1) { int n = len(s); return n ? n * 6 * sx - sx : 0; }
  //  sy = 2 — високі літери (кожен рядок двічі): 5×14 у клітинці 6×16
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
  void center(int y, const char* s, bool on = true, int sy = 1) { text((84 - width(s)) / 2, y, s, on, sy); }
  //  Рівень сигналу: 4 смужки 2 точки завширшки, заввишки 2/3/5/7; погашені — лише риска внизу. lvl < 0 — хрестик.
  void bars(int x, int y, int lvl) {
    static const int H[4] = {2, 3, 5, 7};
    if (lvl < 0) { for (int i = 0; i < 5; i++) { px(x + 3 + i, y + 1 + i, true); px(x + 7 - i, y + 1 + i, true); } return; }
    for (int i = 0; i < 4; i++) {
      int bx = x + i * 3;
      if (i < lvl) fill(bx, y + 7 - H[i], 2, H[i], true);
      else fill(bx, y + 6, 2, 1, true);
    }
  }
  void envelope(int x, int y, int st) {             // Telegram: конверт; збій — «!» поруч, не налаштовано — нічого
    if (st < 0) return;
    frame(x, y + 1, 9, 6, true);
    for (int i = 0; i < 4; i++) { px(x + 1 + i, y + 2 + i, true); px(x + 7 - i, y + 2 + i, true); }
    if (st == 0) { fill(x + 11, y, 1, 5, true); px(x + 11, y + 6, true); }
  }
};

//  Скільки триває (1.0.26, власник: «фраза понад 6 хв — не понятно о чем это»): «уже 1 год 5 хв», «уже 2 дн 3 год» — з «уже»,
//  коли влазить у 14 знаків (інакше «3 год 25 хв»); над ним рядок «немає з 13:53» / «є з 14:22» / «працює з 16:26» каже, про
//  що мова. Невідомо, відколи (Сторож увімкнувся без світла після зникнення живлення): «понад 41 хв». «менше хвилини» — без «уже».
inline void lcdDur(char* o, size_t n, long long sec, bool exact) {
  if (sec < 0) sec = 0;
  long long m = sec / 60, h = m / 60, d = h / 24;
  char c[40];
  if (m < 1) { snprintf(o, n, "%s", exact ? "менше хвилини" : "понад хвилину"); return; }
  if (h < 1) snprintf(c, sizeof c, "%lld хв", m);
  else if (d < 1) { if (exact) snprintf(c, sizeof c, "%lld год %lld хв", h, m % 60); else snprintf(c, sizeof c, "%lld год", h); }
  else { if (exact) snprintf(c, sizeof c, "%lld дн %lld год", d, h % 24); else snprintf(c, sizeof c, "%lld дн", d); }
  const char* p = exact ? "уже " : "понад ";
  char w[64];
  snprintf(w, sizeof w, "%s%s", p, c);
  if (LcdFb::len(w) <= 14) snprintf(o, n, "%s", w);
  else snprintf(o, n, "%s", c);
}
//  Сторінки біжучого рядка (швидкість 0): слова по рядках до 14 знаків; повертає кількість, у out — сторінка k (по колу).
inline int lcdPage(const char* s, int k, char* out, size_t n) {
  const char* st[64];
  const char* en[64];
  int np = 0;
  const char* p = s;
  while (*p && np < 64) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* a = p;
    const char* e = p;                             // кінець прийнятого на цю сторінку
    const char* c = p;                             // початок наступного слова
    int cnt = 0;
    while (*c) {
      const char* w = c;
      int wl = 0;
      while (*w && *w != ' ') { LcdFb::nextCp(w); wl++; }
      int need = cnt ? cnt + 1 + wl : wl;
      if (need > 14) {
        if (!cnt) { const char* t = c; for (int i = 0; i < 14 && *t; i++) LcdFb::nextCp(t); e = t; }   // слово довше за рядок
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
inline int lcdMqPeriod(const char* s) { return LcdFb::width(s) + 30; }   // біжучий рядок: довжина + проміжок до повтору
//  Рядок 33 (смуга 4 дисплея, рядки 32–39) — окремо: біжучий рядок оновлюється частіше за решту екрана.
//  Навиворіт (попередження) — світлі літери на темній смузі. Спалах (invAll) — окремо, після всього (lcdRender).
inline void lcdRenderMq(LcdFb& f, const LcdView& v) {
  bool on = !v.mqInv;
  f.fill(0, 32, 84, 8, v.mqInv);
  if (!v.mq || !v.mq[0]) return;
  if (v.mqPages) {
    char b[96];
    lcdPage(v.mq, v.mqOff, b, sizeof b);
    f.center(33, b, on);
    return;
  }
  int w = LcdFb::width(v.mq);
  if (w <= 84) { f.center(33, v.mq, on); return; }
  int per = w + 30, x = -(((v.mqOff % per) + per) % per);
  f.text(x, 33, v.mq, on);
  if (x + per < 84) f.text(x + per, 33, v.mq, on);
}
inline void lcdInvert(LcdFb& f) { for (auto& b : f.fb) b = ~b; }

inline void lcdRender(LcdFb& f, const LcdView& v) {
  f.clear();
  //  1) Стан світла — великими літерами; світла немає — біле на чорному
  const char* st = v.title && v.title[0] ? v.title : v.grid == -2 ? "АВР НЕ ЗАДАНО" : v.grid == -1 ? "ВИЗНАЧАЮ…" : v.grid == 1 ? (v.gen == 1 ? "ВИМКНІТЬ ГЕН" : "СВІТЛО Є") : (v.gen == 1 ? "ГЕНЕРАТОР" : "НЕМАЄ СВІТЛА");
  bool inv = v.grid == 0 || (v.grid == 1 && v.gen == 1 && v.blink);
  if (v.title && v.title[0]) inv = true;
  if (v.stFlip) inv = !inv;
  if (inv) f.fill(0, 0, 84, 16, true);
  f.center(1, st, !inv, 2);
  if (v.ann && v.ann[0]) {                         // оголошення: три рядки тексту замість часу й графіка
    char b[96];
    int n = lcdPage(v.ann, 0, b, sizeof b), pages = (n + 2) / 3;
    int p0 = pages ? ((v.annPage % pages) + pages) % pages * 3 : 0;
    for (int i = 0; i < 3 && p0 + i < n; i++) { lcdPage(v.ann, p0 + i, b, sizeof b); f.center(17 + 8 * i, b); }
  } else {
    //  2) З якого часу (рядок 17) і скільки триває (рядок 25)
    if (v.since[0]) f.center(17, v.since);
    if (v.dur[0]) f.center(25, v.dur);
    //  3) Графік — біжучий рядок (рядок 33)
    lcdRenderMq(f, v);
  }
  //  5) Унизу: сигнал роутера (W), АВР (А), маячка (М) і Telegram
  f.text(0, 41, "W");  f.bars(7, 41, v.rWifi);
  f.text(21, 41, "А"); f.bars(28, 41, v.rAvr);
  f.text(42, 41, "М"); f.bars(49, 41, v.rMayak);
  f.envelope(68, 41, v.tg);
  if (v.invAll) lcdInvert(f);                      // спалах: увесь екран навиворіт
}
