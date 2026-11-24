/*  Світло й звук мосту батареї (1.2.0): що грати й як світити. Без Arduino — перевірка на комп'ютері (tests/alert).

    Звук: пасивний бузер грає мелодії («нота:мс», як на Сторожі: C5:120, R:80 — пауза, 880:200 — частота), активний —
    шаблони «звук, пауза, звук…» у мс. Коли BMS кілька — перед сигналом стільки коротких «тік», який номер батареї.
    Світло: два світлодіоди (зелений, червоний) чи адресна стрічка WS2812B — смуга заряду; кілька BMS — стрічка ділиться
    на відрізки, по одному на батарею.  */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "batev_core.h"

namespace alr {

// ---------- звук ----------
//  Мелодії пасивного бузера: зростання — добре (зарядилась, заряд почався), спадання — гірше; частіше — терміновіше.
inline const char* melody(uint8_t e) {
  switch (e) {
    case bev::E_FULL: return "C5:120 E5:120 G5:120 C6:420";
    case bev::E_50: return "A5:180 R:120 A5:180";
    case bev::E_30: return "A5:150 R:100 A5:150 R:100 A5:150";
    case bev::E_20: return "C6:120 R:80 C6:120 R:80 C6:120 R:80 C6:120";
    case bev::E_10: return "E6:400 R:120 E6:120 R:80 E6:120 R:80 E6:400";
    case bev::E_5: return "E6:90 R:60 E6:90 R:60 E6:90 R:60 E6:90 R:60 E6:90 R:60 E6:90";
    case bev::E_EMPTY: return "A5:300 E5:300 A5:300 E5:300 A5:300 E5:300 A4:700";
    case bev::E_ERROR: return "F6:180 B5:180 F6:180 B5:180 F6:180 B5:180";
    case bev::E_ERR_OK: return "E5:100 G5:160";
    case bev::E_DIS: return "G5:200 C5:350";
    case bev::E_CHG: return "C5:200 G5:350";
    case bev::E_LOWTIME: return "E6:150 C6:150 A5:150 E5:400";
    case bev::E_IMBAL: return "B6:60 R:60 B6:60 R:60 B6:60";
    case bev::E_LOST: return "C4:600";
    case bev::E_BACK: return "C5:80 R:60 C6:120";
    default: return "";
  }
}
//  Шаблони активного бузера (мс: звук, пауза, звук…)
inline const char* pattern(uint8_t e) {
  switch (e) {
    case bev::E_FULL: return "100,100,100,100,600";
    case bev::E_50: return "200,200,200";
    case bev::E_30: return "200,150,200,150,200";
    case bev::E_20: return "150,100,150,100,150,100,150";
    case bev::E_10: return "500,150,150,100,150,100,500";
    case bev::E_5: return "90,60,90,60,90,60,90,60,90,60,90";
    case bev::E_EMPTY: return "1000,300,1000,300,1000";
    case bev::E_ERROR: return "300,150,300,150,300,150,300";
    case bev::E_ERR_OK: return "80,80,80";
    case bev::E_DIS: return "500";
    case bev::E_CHG: return "80,80,300";
    case bev::E_LOWTIME: return "150,150,150,150,600";
    case bev::E_IMBAL: return "60,60,60,60,60";
    case bev::E_LOST: return "800";
    case bev::E_BACK: return "80,60,80";
    default: return "";
  }
}
//  Перед сигналом — номер батареї (коли BMS кілька): стільки коротких «тік», далі пауза
inline void prefixMelody(int num, char* out, size_t cap) {
  out[0] = 0;
  for (int i = 0; i < num && i < 8; i++) { size_t L = strlen(out); snprintf(out + L, cap - L, "%sA6:60 R:140", i ? " " : ""); }
  if (num > 0) { size_t L = strlen(out); snprintf(out + L, cap - L, " R:300"); }
}
inline void prefixPattern(int num, char* out, size_t cap) {
  out[0] = 0;
  for (int i = 0; i < num && i < 8; i++) { size_t L = strlen(out); snprintf(out + L, cap - L, "%s60,140", i ? "," : ""); }
  if (num > 0) { size_t L = strlen(out); snprintf(out + L, cap - L, ",0,300"); }    // «0» — порожній звук, тоді пауза 300 мс
}

struct Note { uint16_t hz, ms; };
//  «E5:220 C5:220 A4:700», «R:200» — пауза, «880:300» — частота. Повертає кількість нот; −1 — помилка.
inline int parseMelody(const char* m, Note* out, int maxN) {
  int n = 0;
  const char* p = m;
  while (*p) {
    while (*p == ' ' || *p == ',' || *p == ';') p++;
    if (!*p) break;
    const char* e = p;
    while (*e && *e != ' ' && *e != ',' && *e != ';') e++;
    const char* c = (const char*)memchr(p, ':', e - p);
    if (!c || c == p || n >= maxN) return -1;
    long ms = strtol(c + 1, nullptr, 10);
    if (ms <= 0 || ms > 5000) return -1;
    long hz = 0;
    if ((c - p == 1) && (*p == 'R' || *p == 'r' || *p == '-')) hz = 0;
    else if (*p >= '0' && *p <= '9') { hz = strtol(p, nullptr, 10); if (hz < 100 || hz > 10000) return -1; }
    else {
      static const char names[] = "C D EF G A B";
      char ch = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
      const char* q = strchr(names, ch);
      if (!q || ch == ' ') return -1;
      int semi = (int)(q - names), k = 1;
      if (p + k < c && p[k] == '#') { semi++; k++; }
      else if (p + k < c && p[k] == 'b') { semi--; k++; }
      if (p + k >= c || p[k] < '0' || p[k] > '9') return -1;
      int oct = (int)strtol(p + k, nullptr, 10);
      if (oct < 1 || oct > 8) return -1;
      int midi = (oct + 1) * 12 + semi;
      hz = lroundf(440.0f * powf(2.0f, (midi - 69) / 12.0f));
      if (hz < 30 || hz > 10000) return -1;
    }
    out[n].hz = (uint16_t)hz;
    out[n].ms = (uint16_t)ms;
    n++;
    p = e;
  }
  return n ? n : -1;
}
//  «200,150,200» → мс; −1 — помилка
inline int parsePattern(const char* s, uint16_t* out, int maxN) {
  int n = 0;
  const char* p = s;
  while (*p) {
    while (*p == ' ' || *p == ',' || *p == ';') p++;
    if (!*p) break;
    if (*p < '0' || *p > '9' || n >= maxN) return -1;
    long v = strtol(p, (char**)&p, 10);
    if (v < 0 || v > 10000) return -1;
    out[n++] = (uint16_t)v;
    if (*p && *p != ' ' && *p != ',' && *p != ';') return -1;
  }
  return n ? n : -1;
}

// ---------- світло ----------
struct PackIn {          // що відомо про одну батарею
  bool has = false;      // є свіжі дані
  bool lost = false;     // зв'язку з BMS немає довше межі
  uint8_t soc = 0;
  int8_t dir = 0;        // −1 розряд, +1 заряд, 0 спокій
  bool alarm = false;    // тривога BMS
};

//  Два світлодіоди: що світить зараз (t — мс). Повертає яскравість 0–255 для зеленого й червоного.
inline void twoLeds(const PackIn& b, uint32_t t, uint8_t& g, uint8_t& r) {
  g = r = 0;
  bool fast = (t / 125) % 2 == 0, med = (t / 250) % 2 == 0, slow = (t / 700) % 2 == 0;
  if (!b.has || b.lost) { r = (t % 3000) < 120 ? 255 : 0; return; }      // немає даних — рідкий короткий спалах червоного
  if (b.alarm) { g = med ? 255 : 0; r = med ? 0 : 255; return; }          // тривога BMS — по черзі
  if (b.soc <= 5) { r = fast ? 255 : 0; return; }
  if (b.soc <= 10) r = med ? 255 : 0;
  else if (b.soc <= 20) r = 255;
  else if (b.soc < 50) { r = 255; g = 255; }                              // обидва — «середньо»
  else g = 255;
  if (b.dir > 0) g = slow ? 255 : 40;                                     // заряд — зелений повільно блимає
}

//  Колір рівня заряду
inline void levelColor(uint8_t soc, uint8_t* c) {
  if (soc >= 50) { c[0] = 0; c[1] = 255; c[2] = 0; }
  else if (soc > 20) { c[0] = 255; c[1] = 150; c[2] = 0; }
  else { c[0] = 255; c[1] = 0; c[2] = 0; }
}

//  Стрічка: n світлодіодів, k батарей — кожній свій відрізок (якщо світлодіодів на кожну хоч 2), інакше — найгірша.
//  px[i] — {r,g,b} 0–255 до яскравості. Заряд — смуга знизу (світлодіод 0) вгору; заряджається — по смузі біжить
//  вогник угору; тривога — відрізок блимає червоним; немає даних — перший світлодіод відрізка блимає синім.
inline void stripFrame(const PackIn* packs, int k, uint32_t t, uint8_t (*px)[3], int n) {
  for (int i = 0; i < n; i++) px[i][0] = px[i][1] = px[i][2] = 0;
  if (n <= 0 || k <= 0) return;
  int segs = k, seg = n / k;
  int pick = 0;
  if (seg < 2) {                                      // замало світлодіодів — показуємо найгіршу батарею
    segs = 1; seg = n;
    int worst = 999;
    for (int j = 0; j < k; j++) {
      int score = !packs[j].has || packs[j].lost ? 200 : packs[j].alarm ? -1 : packs[j].soc;
      if (score < worst) { worst = score; pick = j; }
    }
  }
  for (int s = 0; s < segs; s++) {
    const PackIn& b = packs[segs == 1 ? pick : s];
    int a = s * seg, len = (s == segs - 1) ? n - a : seg;
    if (segs > 1 && s < segs - 1) len = seg;           // відрізки однакові, залишок — останньому
    if (!b.has || b.lost) { if ((t % 2000) < 300) { px[a][2] = 200; } continue; }
    if (b.alarm) { bool on = (t / 250) % 2 == 0; for (int i = 0; i < len; i++) if (on) px[a + i][0] = 255; continue; }
    uint8_t c[3];
    levelColor(b.soc, c);
    int lit = (b.soc * len + 50) / 100;
    if (lit < 1) lit = 1;                              // хоч один — що батарея є
    bool crit = b.soc <= 5;
    for (int i = 0; i < lit && i < len; i++) {
      if (crit && (t / 125) % 2) continue;             // 5 % і менше — блимає часто
      memcpy(px[a + i], c, 3);
    }
    if (b.dir > 0 && lit > 0) {                        // заряд: вогник біжить угору по світній смузі
      int pos = (int)((t / 150) % (uint32_t)lit);
      px[a + pos][0] = px[a + pos][1] = px[a + pos][2] = 255;
    }
  }
}

}  // namespace alr
