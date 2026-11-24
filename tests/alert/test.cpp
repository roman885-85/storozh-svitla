// Перевірка звуку й світла мосту (firmware/avr-bms/alert_core.h): tests/alert/run.sh
#include "alert_core.h"
#include <cstdio>
#include <string>

static int fails = 0, checks = 0;
static void ok(bool c, const std::string& what) { checks++; if (!c) { fails++; printf("  ПОМИЛКА: %s\n", what.c_str()); } }
static bool px(uint8_t (*p)[3], int i, int r, int g, int b) { return p[i][0] == r && p[i][1] == g && p[i][2] == b; }
static int lit(uint8_t (*p)[3], int n) { int c = 0; for (int i = 0; i < n; i++) c += (p[i][0] | p[i][1] | p[i][2]) != 0; return c; }

int main() {
  printf("1) Мелодії й шаблони кожної події розбираються; тривалість 0,2–6 с\n");
  for (int e = 1; e < bev::E_COUNT; e++) {
    alr::Note nt[64];
    uint16_t pt[64];
    int a = alr::parseMelody(alr::melody(e), nt, 64), b = alr::parsePattern(alr::pattern(e), pt, 64);
    std::string nm = bev::evName(e);
    ok(a > 0, "мелодія «" + nm + "»");
    ok(b > 0 && (b % 2) == 1, "шаблон «" + nm + "» (непарна кількість — закінчується звуком)");
    uint32_t ta = 0, tb = 0;
    for (int i = 0; i < a; i++) ta += nt[i].ms;
    for (int i = 0; i < b; i++) tb += pt[i];
    ok(ta >= 200 && ta <= 6000 && tb >= 200 && tb <= 6000, "тривалість «" + nm + "»: " + std::to_string(ta) + " / " + std::to_string(tb) + " мс");
  }
  printf("2) Ноти й частоти\n");
  {
    alr::Note n[8];
    ok(alr::parseMelody("A4:100 A5:100 C4:100 C#5:100 Bb4:100 880:200 R:50", n, 8) == 7, "розбір");
    ok(n[0].hz == 440 && n[1].hz == 880 && n[2].hz == 262 && n[3].hz == 554 && n[4].hz == 466 && n[5].hz == 880 && n[6].hz == 0 && n[6].ms == 50,
       "частоти: " + std::to_string(n[0].hz) + " " + std::to_string(n[2].hz) + " " + std::to_string(n[3].hz) + " " + std::to_string(n[4].hz));
    const char* bad[] = {"X5:100", "C9:100", "C5:0", "C5", ":100", "50:100", "C5:6000", "H5:100", " ", ""};
    for (auto s : bad) ok(alr::parseMelody(s, n, 8) == -1, std::string("помилка: «") + s + "»");
    ok(alr::parseMelody("C5:10 C5:10 C5:10", n, 2) == -1, "забагато нот — помилка, а не обрізка");
  }
  printf("3) Шаблони активного бузера\n");
  {
    uint16_t p[8];
    ok(alr::parsePattern("200,150,200", p, 8) == 3 && p[1] == 150, "200,150,200");
    ok(alr::parsePattern("0, 300 ,60", p, 8) == 3 && p[0] == 0 && p[1] == 300, "пробіли й нуль");
    const char* bad[] = {"", "a,1", "10001", "1x", "-5"};
    for (auto s : bad) ok(alr::parsePattern(s, p, 8) == -1, std::string("помилка: «") + s + "»");
  }
  printf("4) Номер батареї перед сигналом\n");
  {
    char m[200], q[200];
    alr::prefixMelody(3, m, sizeof m);
    alr::prefixPattern(2, q, sizeof q);
    alr::Note n[16];
    uint16_t p[16];
    ok(alr::parseMelody(m, n, 16) == 7 && n[0].hz == 1760 && n[6].hz == 0 && n[6].ms == 300, std::string("мелодія: ") + m);
    ok(alr::parsePattern(q, p, 16) == 6 && p[4] == 0 && p[5] == 300, std::string("шаблон: ") + q);
    alr::prefixMelody(0, m, sizeof m);
    alr::prefixPattern(0, q, sizeof q);
    ok(!m[0] && !q[0], "одна батарея — без префікса");
    //  префікс + шаблон: чергування «звук, пауза» не збивається — подія починається звуком
    alr::prefixPattern(2, q, sizeof q);
    std::string full = std::string(q) + "," + alr::pattern(bev::E_20);
    int k = alr::parsePattern(full.c_str(), p, 16);
    ok(k == 6 + 7 && p[6] == 150, "префікс + шаблон «20 %»");
  }
  printf("5) Два світлодіоди\n");
  {
    alr::PackIn b;
    uint8_t g, r;
    alr::twoLeds(b, 0, g, r); ok(r == 255 && g == 0, "немає даних — спалах червоного");
    alr::twoLeds(b, 500, g, r); ok(r == 0 && g == 0, "немає даних — між спалахами темно");
    b.has = true; b.soc = 80; b.dir = -1;
    alr::twoLeds(b, 0, g, r); ok(g == 255 && r == 0, "80 % — зелений");
    b.dir = 1;
    alr::twoLeds(b, 0, g, r); ok(g == 255 && r == 0, "80 %, заряд — зелений яскраво…");
    alr::twoLeds(b, 800, g, r); ok(g == 40 && r == 0, "…і тьмяно (повільно блимає)");
    b.dir = 0; b.soc = 35;
    alr::twoLeds(b, 0, g, r); ok(g == 255 && r == 255, "35 % — обидва");
    b.soc = 15;
    alr::twoLeds(b, 0, g, r); ok(g == 0 && r == 255, "15 % — червоний");
    b.soc = 8;
    alr::twoLeds(b, 0, g, r); ok(r == 255, "8 % — червоний блимає…");
    alr::twoLeds(b, 300, g, r); ok(r == 0, "…згас");
    b.soc = 3;
    alr::twoLeds(b, 130, g, r); ok(r == 0 && g == 0, "3 % — часто блимає");
    b.alarm = true;
    alr::twoLeds(b, 0, g, r); ok(g == 255 && r == 0, "тривога — по черзі…");
    alr::twoLeds(b, 300, g, r); ok(g == 0 && r == 255, "…зелений, червоний");
    b.alarm = false; b.lost = true;
    alr::twoLeds(b, 1000, g, r); ok(r == 0 && g == 0, "зв'язку немає — як без даних");
  }
  printf("6) Стрічка\n");
  {
    uint8_t p[16][3];
    alr::PackIn a[3];
    a[0].has = true; a[0].soc = 50;
    alr::stripFrame(a, 1, 0, p, 8);
    ok(lit(p, 8) == 4 && px(p, 0, 0, 255, 0) && px(p, 3, 0, 255, 0) && px(p, 4, 0, 0, 0), "8 шт., 50 % — 4 зелених знизу");
    a[0].soc = 100;
    alr::stripFrame(a, 1, 0, p, 8); ok(lit(p, 8) == 8, "100 % — уся");
    a[0].soc = 1;
    alr::stripFrame(a, 1, 0, p, 8); ok(lit(p, 8) == 1 && px(p, 0, 255, 0, 0), "1 % — один червоний…");
    alr::stripFrame(a, 1, 130, p, 8); ok(lit(p, 8) == 0, "…блимає");
    a[0].soc = 30;
    alr::stripFrame(a, 1, 0, p, 8); ok(lit(p, 8) == 2 && px(p, 1, 255, 150, 0), "30 % — 2 помаранчеві");
    a[0].soc = 60; a[0].dir = 1;
    alr::stripFrame(a, 1, 0, p, 8); ok(px(p, 0, 255, 255, 255) && px(p, 1, 0, 255, 0), "заряд — білий вогник на 0…");
    alr::stripFrame(a, 1, 300, p, 8); ok(px(p, 2, 255, 255, 255) && px(p, 0, 0, 255, 0), "…за 300 мс — на 2");
    a[0].dir = 0; a[0].soc = 100; a[1].has = true; a[1].soc = 25;
    alr::stripFrame(a, 2, 0, p, 8);
    ok(lit(p, 8) == 5 && px(p, 3, 0, 255, 0) && px(p, 4, 255, 150, 0) && px(p, 5, 0, 0, 0), "2 батареї на 8 — відрізки 4 + 4");
    a[0].soc = 80; a[1].soc = 30;
    alr::stripFrame(a, 2, 0, p, 3);
    ok(lit(p, 3) == 1 && px(p, 0, 255, 150, 0), "2 батареї на 3 — показано найгіршу (30 %)");
    a[0].alarm = true;
    alr::stripFrame(a, 2, 0, p, 3); ok(lit(p, 3) == 3 && px(p, 2, 255, 0, 0), "тривога — найгірша, уся червона…");
    alr::stripFrame(a, 2, 300, p, 3); ok(lit(p, 3) == 0, "…блимає");
    a[0].alarm = false; a[1].lost = true;
    alr::stripFrame(a, 2, 0, p, 8); ok(px(p, 4, 0, 0, 200) && lit(p, 8) == 3 + 1, "без зв'язку — перший свого відрізка синій (80 % на 4 = 3)");
    alr::stripFrame(a, 2, 500, p, 8); ok(px(p, 4, 0, 0, 0), "…блимає");
    a[1].lost = false; a[1].soc = 100; a[2].has = true; a[2].soc = 100;
    alr::stripFrame(a, 3, 0, p, 10);
    ok(lit(p, 10) == 2 + 3 + 4 && px(p, 9, 0, 255, 0), "3 батареї на 10 — 3 + 3 + 4 (80 % на 3 = 2)");
    alr::stripFrame(a, 0, 0, p, 8); ok(lit(p, 8) == 0, "без батарей — темно");
    alr::stripFrame(a, 1, 0, p, 0); ok(true, "0 світлодіодів — без збою");
  }
  printf("%s: перевірок %d, помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", checks, fails);
  return fails ? 1 : 0;
}
