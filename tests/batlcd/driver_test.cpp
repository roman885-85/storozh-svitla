// Перевірка драйвера ST7567 (firmware/avr-bms/st7567.h) на комп'ютері: на дроти має піти рівно те, що шле робочий зразок
// власника (moto_battery_reader → U8g2 ENH_DG128064, x_offset 4), а далі — лише змінені шматки смуг.
#include "Arduino.h"
#include "SPI.h"
std::vector<Wire1> g_wire; int g_pin[40]; int g_delayMs = 0; int g_rsPin = 5, g_csPin = 7; SPIClass SPI;
#include "../../firmware/avr-bms/st7567.h"
#include <cstdio>
static int bad = 0;
static void expect(bool ok, const char* what) { printf("%s %s\n", ok ? "  ✅" : "  ❌", what); if (!ok) bad++; }
static std::vector<uint8_t> cmds() { std::vector<uint8_t> v; for (auto& w : g_wire) if (!w.data) v.push_back(w.b); return v; }
int main() {
  St7567 d;
  St7567::Cfg c;                                   // типові — як у зразку
  d.begin(c);
  // u8x8_st7567_enh_dg128064_init_seq (без «усі точки» й «вимкнено» в кінці) + вихід з економії: A4, AF
  const std::vector<uint8_t> want = {0xE2, 0xAE, 0x40, 0xA1, 0xC0, 0xA6, 0xA2, 0x2C, 0x2E, 0x2F, 0x23, 0x81, 50, 0xA4, 0xAF};
  expect(cmds() == want, "запуск: ті самі команди й у тому самому порядку, що в зразку");
  expect(g_delayMs >= 150, "паузи по 50 мс між кроками живлення скла");
  bool noData = true; for (auto& w : g_wire) if (w.data) noData = false;
  expect(noData, "під час запуску — лише команди");

  // перший кадр — увесь: 8 смуг, адреса стовпця 4 (0x10, 0x04), по 128 байтів
  uint8_t fb[1024];
  for (int i = 0; i < 1024; i++) fb[i] = (uint8_t)(i * 7 + 1);
  g_wire.clear();
  d.flush(fb);
  bool ok = g_wire.size() == 8 * (3 + 128);
  for (int p = 0; p < 8 && ok; p++) {
    const Wire1* w = &g_wire[p * 131];
    ok = !w[0].data && w[0].b == (0xB0 | p) && !w[1].data && w[1].b == 0x10 && !w[2].data && w[2].b == 0x04;
    for (int i = 0; i < 128 && ok; i++) ok = w[3 + i].data && w[3 + i].b == fb[p * 128 + i];
  }
  expect(ok, "перший кадр: 8 смуг по 128 байтів, зі стовпця 4 пам'яті (зсув картинки 4)");

  // без змін — на дроти нічого
  g_wire.clear(); d.flush(fb);
  expect(g_wire.empty(), "кадр не змінився — нічого не шлемо");

  // змінились стовпці 10…12 смуги 3 — лише вони
  fb[3 * 128 + 10] ^= 0xFF; fb[3 * 128 + 12] ^= 0x01;
  g_wire.clear(); d.flush(fb);
  ok = g_wire.size() == 6 && g_wire[0].b == 0xB3 && g_wire[1].b == (0x10 | ((10 + 4) >> 4)) && g_wire[2].b == ((10 + 4) & 15) &&
       g_wire[3].data && g_wire[3].b == fb[3 * 128 + 10] && g_wire[5].b == fb[3 * 128 + 12];
  expect(ok, "змінились 3 стовпці однієї смуги — пішли лише вони (адреса 14)");

  // стовпець 127 при зсуві 4 → адреса 131 = 0x18, 0x03 (останній стовпець пам'яті, за межу не виходимо)
  fb[127] ^= 0x80; g_wire.clear(); d.flush(fb);
  expect(g_wire.size() == 4 && g_wire[1].b == 0x18 && g_wire[2].b == 0x03, "останній стовпець — адреса 131, у межах пам'яті ST7567");

  // контраст без перезапуску; поворот — інший напрям і зсув 0, усе перемальовується
  St7567::Cfg c2 = c; c2.contrast = 33;
  g_wire.clear(); d.apply(c2);
  auto v = cmds();
  expect(v.size() >= 2 && v[v.size() - 2] == 0x81 && v.back() == 33, "контраст: 0x81, 33");
  c2.flip = true; g_wire.clear(); d.apply(c2);
  v = cmds();
  bool a0 = false, c8 = false; for (auto b : v) { if (b == 0xA0) a0 = true; if (b == 0xC8) c8 = true; }
  g_wire.clear(); d.flush(fb);
  expect(a0 && c8 && g_wire.size() == 8 * 131 && g_wire[1].b == 0x10 && g_wire[2].b == 0x00, "поворот на 180°: A0, C8, зсув 0, кадр — увесь");
  printf("%s\n", bad ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО: драйвер шле те саме, що робочий зразок");
  return bad ? 1 : 0;
}
