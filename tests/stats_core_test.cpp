// Тести обліку світла: c++ -std=c++17 -I firmware/avr-mist tests/stats_core_test.cpp -o /tmp/sct && /tmp/sct
#include "stats_core.h"
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  using namespace sc;
  // дати
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(daysFromCivil(2026, 11, 18) - daysFromCivil(2026, 11, 17) == 1);
  CHECK(daysFromCivil(2027, 1, 1) - daysFromCivil(2026, 12, 31) == 1);          // перехід року
  CHECK(daysFromCivil(2028, 3, 1) - daysFromCivil(2028, 2, 28) == 2);           // 2028 — високосний
  CHECK(daysFromCivil(2027, 3, 1) - daysFromCivil(2027, 2, 28) == 1);
  CHECK(daysFromCivil(2026, 11, 18) == 20775);                                  // перевірено: 1 763 424 000 / 86400
  static Data d;
  init(d);
  CHECK(valid(d));
  d.days[0].on = 5; CHECK(!valid(d)); seal(d); CHECK(valid(d));                // CRC ловить зміни
  init(d);
  int32_t t = daysFromCivil(2026, 11, 18);
  // день: 20 год світла, відключення о 14:02 — 16:30, вхід у «немає» помічено через 30 с
  add(d, t, 1, 14 * 3600 + 2 * 60 + 30);       // до 14:02:30 «є» (30 с затримки)
  move(d, t, 1, 0, 30);                         // перенос 30 с у «немає»
  outageStart(d, t, 1763474520, false);
  add(d, t, 0, 2 * 3600 + 28 * 60 - 30);       // ще «немає» до 16:30
  outageEnd(d, 1763483400);
  Sum s = sum(d, t, t);
  CHECK(s.on == 14 * 3600 + 2 * 60);
  CHECK(s.off == 2 * 3600 + 28 * 60);
  CHECK(s.outs == 1);
  const OutRec* o = outage(d, 0);
  CHECK(o && o->start == 1763474520 && o->end == 1763483400 && !o->approx);
  CHECK(outage(d, 1) == nullptr);
  // move не йде в мінус
  add(d, t, -1, 10);
  move(d, t, -1, 1, 100);
  CHECK(sum(d, t, t).unk == 0);
  // кілька днів і кільце: день t−40 витісняється днем t (те саме місце)
  init(d);
  for (int k = 0; k < 45; k++) add(d, t - 44 + k, 1, 100);
  CHECK(sum(d, t - 29, t).on == 30 * 100);
  CHECK(sum(d, t - 6, t).on == 7 * 100);
  CHECK(sum(d, t - 44, t - 40).on == 0);       // старіші за 40 днів витіснено
  // кільце відключень: 50 записів — лишаються 48 останніх, найновіше перше
  init(d);
  for (int k = 0; k < 50; k++) outageStart(d, t, 1000 + k, k == 49);
  CHECK(d.outN == NO);
  CHECK(outage(d, 0)->start == 1049 && outage(d, 0)->approx);
  CHECK(outage(d, NO - 1)->start == 1002);
  CHECK(outage(d, NO) == nullptr);
  outageEnd(d, 2000);
  CHECK(outage(d, 0)->end == 2000);
  outageEnd(d, 3000);                          // уже закрите — не змінюємо
  CHECK(outage(d, 0)->end == 2000);
  printf("%s: %d помилок, розмір Data %zu Б\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", fails, sizeof(Data));
  return fails ? 1 : 0;
}
