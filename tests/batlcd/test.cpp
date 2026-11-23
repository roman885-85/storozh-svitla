// Картинки екрана мосту батареї (Open-Smart 1,8″, 128×64, ST7567) для перевірки на Mac:
// tests/batlcd/run.sh → tests/batlcd/out/*.png — тим самим кодом, що в прошивці (firmware/avr-bms/batlcd_core.h).
#include "../../firmware/avr-bms/batlcd_core.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static std::vector<BatFb> sheet;
static std::vector<std::string> names;
static void save(const BatFb& f, const char* name) {
  sheet.push_back(f);
  names.push_back(name);
  char p[256];
  snprintf(p, sizeof p, "out/%s.pgm", name);
  FILE* o = fopen(p, "wb");
  const int S = 5, B = 3;                           // ×5 і рамка: темні точки на світлому склі з білим підсвітом
  int W = (BatFb::W + 2 * B) * S, H = (BatFb::H + 2 * B) * S;
  fprintf(o, "P5\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    int X = x / S - B, Y = y / S - B;
    bool on = f.get(X, Y);
    bool gap = (x % S == S - 1) || (y % S == S - 1);
    fputc(on ? (gap ? 60 : 30) : (gap ? 205 : 218), o);
  }
  fclose(o);
}
static int bad = 0;
static void expect(bool ok, const char* what) { if (!ok) { bad++; printf("  ❌ %s\n", what); } }

//  Жодна точка не має вийти за свою смугу: рядки тексту не налазять один на одного (перевірка порожніх проміжків)
static bool rowEmpty(const BatFb& f, int y) { for (int x = 0; x < BatFb::W; x++) if (f.get(x, y)) return false; return true; }

int main() {
  BatFb f;
  char b[64];

  // числа з комою
  struct { long v; int div, dec; const char* want; } N[] = {
    {24712, 1000, 1, "24,7"}, {24950, 1000, 1, "25,0"}, {4116, 1000, 3, "4,116"}, {-6208, 1000, 1, "-6,2"}, {-20, 1000, 1, "0,0"},
    {215, 10, 0, "22"}, {-45, 10, 0, "-5"}, {99960, 1000, 1, "100,0"}, {53251, 1000, 2, "53,25"}};
  for (auto& n : N) { batFix(b, sizeof b, n.v, n.div, n.dec); expect(!strcmp(b, n.want), (std::string("число ") + n.want + " ≠ " + b).c_str()); }
  // тривалості
  struct { int m; const char* want; } D[] = {{0, "1 хв"}, {35, "35 хв"}, {60, "1 год"}, {130, "2 год 10 хв"}, {599, "9 год 59 хв"}, {2742, "45 год"}, {4400, "3 дн 1 год"}, {-1, ""}};
  for (auto& d : D) { batDur(b, sizeof b, d.m); expect(!strcmp(b, d.want), (std::string("тривалість ") + d.want + " ≠ " + b).c_str()); }
  // рівні сигналу
  expect(batLevel(-44, true) == 4 && batLevel(-71, true) == 3 && batLevel(-91, true) == 1 && batLevel(-96, true) == 0 && batLevel(0, true) == -1, "смужки Bluetooth");
  expect(batLevel(-50, false) == 4 && batLevel(-67, false) == 2 && batLevel(-90, false) == 0, "смужки Wi-Fi");

  // 1. Розряд — як на батареї власника 22.11 (2 модулі Tesla 6S, JK_B1A8S10P)
  BatView v;
  v.link = 2; v.has = true; v.soc = 97; v.mv = 24708; v.ma = -6351; v.leftMin = 2742; v.n = 6; v.lo = 4116; v.hi = 4119;
  v.hT = true; v.t = 216; v.hTm = true; v.tm = 231; v.rBms = batLevel(-44, true); v.rWifi = batLevel(-67, false);
  batRender(f, v); save(f, "1-rozryad");
  expect(rowEmpty(f, 22) && rowEmpty(f, 23) && rowEmpty(f, 38) && rowEmpty(f, 56), "1: проміжки між рядками порожні");

  // 2. Заряд, 100 % і тризначний струм не налазять
  v.soc = 100; v.mv = 25200; v.ma = 12400; v.leftMin = 95;
  batRender(f, v); save(f, "2-zaryad");
  v.soc = 64; v.mv = 53251; v.ma = -123400; v.leftMin = 25; v.n = 16; v.lo = 3327; v.hi = 3329;
  batRender(f, v); save(f, "2b-velykyi-strum");

  // 3. Спокій, мало заряду, мороз
  v = BatView(); v.link = 2; v.has = true; v.soc = 8; v.mv = 20900; v.ma = -40; v.leftMin = -1; v.n = 6; v.lo = 3480; v.hi = 3495;
  v.hT = true; v.t = -45; v.hTm = true; v.tm = 12; v.rBms = 2; v.rWifi = 1;
  batRender(f, v); save(f, "3-spokii-malo");

  // 4. Тривога BMS — дві сторінки тексту
  v.soc = 12; v.ma = -18200; v.leftMin = 14; v.alarm = "низька напруга комірки, завеликий струм розряду";
  v.alarmPage = 0; batRender(f, v); save(f, "4-tryvoha-1");
  v.alarmPage = 1; batRender(f, v); save(f, "4b-tryvoha-2");
  v.alarmPage = 2; batRender(f, v); save(f, "4c-tryvoha-3");

  // 5. Немає зв'язку з BMS: було 97 %, 12 хв тому; і зовсім без даних
  v = BatView(); v.link = 0; v.has = true; v.soc = 97; v.ageMin = 12; v.rBms = -1; v.rWifi = 3;
  batRender(f, v); save(f, "5-nemaye-bms");
  v.link = 4; v.has = false; v.rWifi = -1;
  batRender(f, v); save(f, "5b-pauza-bez-danykh");

  // 6. Сторінка налаштування (своя мережа) — з даними й без
  v = BatView(); v.link = 2; v.has = true; v.soc = 97; v.mv = 24708; v.ma = -6351; v.leftMin = 2742; v.n = 6; v.lo = 4116; v.hi = 4119;
  v.rBms = 4; v.rWifi = -1; v.setup = true; v.ap = "BMS-1A2B";
  batRender(f, v); save(f, "6-nalashtuvannya");
  v.link = 1; v.has = false;
  batRender(f, v); save(f, "6b-nalashtuvannya-bez-bms");

  // 7. Заставка й перевірки
  v = BatView(); v.ver = "1.1.0";
  batRender(f, v); save(f, "7-zastavka");
  v = BatView(); v.test = 2;
  batRender(f, v); save(f, "8-shakhivnytsia");

  // 8. Хід: заряд 100 → 99 → 9 %, цифри не лишають слідів (кожен кадр малюється з чистого)
  v = BatView(); v.link = 2; v.has = true; v.mv = 24000; v.ma = -5000; v.leftMin = 600; v.n = 6; v.lo = 4000; v.hi = 4001; v.rBms = 4; v.rWifi = 4;
  BatFb a, c;
  v.soc = 100; batRender(a, v);
  v.soc = 9; batRender(f, v); save(f, "9-zaryad-9");
  v.soc = 100; batRender(c, v);
  expect(!memcmp(a.fb, c.fb, sizeof a.fb), "кадр залежить лише від даних (100 → 9 → 100 — той самий)");

  {
    const int S = 3, B = 3, G = 4, cols = 3, rows = (int)(sheet.size() + cols - 1) / cols;
    int cw = (BatFb::W + 2 * B) * S, ch = (BatFb::H + 2 * B) * S, W = cols * cw + (cols + 1) * G * S, H = rows * ch + (rows + 1) * G * S;
    FILE* o = fopen("out/all.pgm", "wb");
    fprintf(o, "P5\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      int gx = x - G * S, gy = y - G * S, cx = gx / (cw + G * S), cy = gy / (ch + G * S);
      int lx = gx - cx * (cw + G * S), ly = gy - cy * (ch + G * S);
      int k = cy * cols + cx;
      if (gx < 0 || gy < 0 || cx >= cols || lx >= cw || ly >= ch || k >= (int)sheet.size()) { fputc(255, o); continue; }
      int X = lx / S - B, Y = ly / S - B;
      bool on = sheet[k].get(X, Y), gap = (lx % S == S - 1) || (ly % S == S - 1);
      fputc(on ? (gap ? 60 : 30) : (gap ? 205 : 218), o);
    }
    fclose(o);
  }
  printf("%s, кадрів %zu\n", bad ? "❌ є помилки" : "✅ рядки й числа в порядку", sheet.size());
  return bad ? 1 : 0;
}
