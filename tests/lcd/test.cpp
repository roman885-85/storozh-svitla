// Картинки екрана Nokia 5110 для перевірки на комп'ютері: tests/lcd/run.sh → tests/lcd/out/*.pgm (і PNG через sips)
#include "../../firmware/avr-mist/lcd_core.h"
#include <cstdio>
#include <vector>
//  Усі кадри ще й одним аркушем 3×2 (out/all.png): сам тест кладе їх у сітку, без сторонніх перетворень, що
//  перевертали картинку (18.11 власник: «картинка отзеркалена и перевернута» — то було склеювання, не екран).
static std::vector<LcdFb> sheet;
static void save(const LcdFb& f, const char* name) {
  sheet.push_back(f);
  char p[256];
  snprintf(p, sizeof p, "out/%s.pgm", name);
  FILE* o = fopen(p, "wb");
  const int S = 6, B = 3;                           // ×6 і рамка — схоже на справжнє скло: темні точки на сіро-зеленому
  int W = (84 + 2 * B) * S, H = (48 + 2 * B) * S;
  fprintf(o, "P5\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    int X = x / S - B, Y = y / S - B;
    bool on = f.get(X, Y);
    bool gap = (x % S == S - 1) || (y % S == S - 1);
    fputc(on ? (gap ? 70 : 40) : (gap ? 165 : 175), o);
  }
  fclose(o);
}
int main() {
  LcdFb f;
  LcdView v;
  //  тривалості (1.0.26): словами, щонайбільше 14 знаків
  struct { long long s; bool ex; const char* want; } D[] = {
    {20, true, "менше хвилини"}, {20, false, "понад хвилину"}, {180, true, "вже 3 хв"}, {2460, false, "понад 41 хв"},
    {3900, true, "вже 1 год 5 хв"}, {3900 + 600, true, "1 год 15 хв"}, {3900, false, "понад 1 год"}, {86399, true, "23 год 59 хв"},
    {2 * 86400 + 3 * 3600 + 5, true, "вже 2 дн 3 год"}, {9 * 86400, false, "понад 9 дн"}};
  int bad = 0;
  for (auto& d : D) {
    char o[48];
    lcdDur(o, sizeof o, d.s, d.ex);
    bool ok = !strcmp(o, d.want) && LcdFb::len(o) <= 14;
    printf("%s lcdDur(%lld, %s) = «%s»\n", ok ? "✅" : "❌", d.s, d.ex ? "точно" : "не точно", o);
    if (!ok) bad++;
  }
  const char* MQ = "Черга 3.1 · сьогодні: 14:00–16:30, 23:00–24:00 · завтра: ще не оприлюднено · наступне відключення о 23:00, через 2 год 15 хв";
  {
    char pg[96];
    int n = lcdPage(MQ, 0, pg, sizeof pg);
    printf("сторінок: %d\n", n);
    for (int k = 0; k < n; k++) {
      lcdPage(MQ, k, pg, sizeof pg);
      int L = LcdFb::len(pg);
      printf("  %s %d: «%s» (%d)\n", L <= 14 && L > 0 ? "✅" : "❌", k, pg, L);
      if (L > 14 || L == 0) bad++;
    }
    n = lcdPage("Надзвичайнодовжелезнеслово і ще", 0, pg, sizeof pg);
    printf("  довге слово: %d сторінок, перша «%s»\n", n, pg);
  }
  v.grid = 1; v.gen = 0; strcpy(v.since, "є з 19:01"); lcdDur(v.dur, sizeof v.dur, 3 * 3600 + 7 * 60, true);
  v.mq = MQ; v.mqOff = 0;
  v.rWifi = 4; v.rAvr = 1; v.rMayak = -1; v.tg = 1;
  lcdRender(f, v); save(f, "1-svitlo-ye");
  v.grid = 0; v.gen = 0; strcpy(v.since, "немає з 13:53"); lcdDur(v.dur, sizeof v.dur, 41 * 60, false); v.mqOff = 150; v.tg = 0;
  lcdRender(f, v); save(f, "2-nemaye-nevidomo-vidkoly");
  v.stFlip = true; lcdRender(f, v); save(f, "2b-zyknulo-myhotinnya"); v.stFlip = false;
  v.invAll = true; lcdRender(f, v); save(f, "2c-zyknulo-spalah"); v.invAll = false;
  v.grid = 0; v.gen = 1; v.rMayak = 0; v.tg = 1; strcpy(v.since, "працює з 16:26"); lcdDur(v.dur, sizeof v.dur, 32 * 60, true); v.mqOff = 400;
  lcdRender(f, v); save(f, "3-generator");
  v.grid = 1; v.gen = 1; v.blink = true; strcpy(v.since, "є з 02:31"); lcdDur(v.dur, sizeof v.dur, 50, true); v.mqOff = 0;
  v.mq = "Черга 3.1 · сьогодні: без відключень · завтра: ще не оприлюднено";
  lcdRender(f, v); save(f, "4-vymknit-gen");
  v.blink = false; v.mqPages = true; v.mqOff = 1; lcdRender(f, v); save(f, "4b-storinkamy");
  v.grid = 1; v.gen = 0; v.mqPages = false; v.mq = "УВАГА: о 14:00 відключення за графіком, через 30 хв"; v.mqInv = true; v.mqOff = 0;
  lcdRender(f, v); save(f, "6-poperedzhennya");
  v.mqInv = false; v.title = "ОГОЛОШЕННЯ"; v.ann = "Сьогодні о 18:00 молитовне зібрання в малому залі. Приходьте всі, хто може"; v.annPage = 0;
  lcdRender(f, v); save(f, "7-ogoloshennya");
  v.annPage = 1; v.stFlip = true; lcdRender(f, v); save(f, "7b-ogoloshennya-str2");
  v = LcdView(); v.grid = -1; v.rWifi = 3; v.rAvr = -1; v.rMayak = -1; v.tg = -1; v.mq = "Графік YASNO: даних поки немає";
  lcdRender(f, v); save(f, "5-vyznachayu");
  {
    const int S = 4, B = 3, G = 4, cols = 3, rows = (int)(sheet.size() + cols - 1) / cols;
    int cw = (84 + 2 * B) * S, ch = (48 + 2 * B) * S, W = cols * cw + (cols + 1) * G * S, H = rows * ch + (rows + 1) * G * S;
    FILE* o = fopen("out/all.pgm", "wb");
    fprintf(o, "P5\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      int gx = x - G * S, gy = y - G * S, cx = gx / (cw + G * S), cy = gy / (ch + G * S);
      int lx = gx - cx * (cw + G * S), ly = gy - cy * (ch + G * S);
      int k = cy * cols + cx;
      if (gx < 0 || gy < 0 || cx >= cols || lx >= cw || ly >= ch || k >= (int)sheet.size()) { fputc(255, o); continue; }
      int X = lx / S - B, Y = ly / S - B;
      bool on = sheet[k].get(X, Y), gap = (lx % S == S - 1) || (ly % S == S - 1);
      fputc(on ? (gap ? 70 : 40) : (gap ? 165 : 175), o);
    }
    fclose(o);
  }
  printf("%s\n", bad ? "❌ є помилки" : "✅ рядки в порядку");
  return bad ? 1 : 0;
}
