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
  for (int k = 0; k < 50; k++) {
    CHECK(outageStart(d, t, 1000 + 2 * k, k == 49));
    if (k < 49) outageEnd(d, 1001 + 2 * k);
  }
  CHECK(d.outN == NO);
  CHECK(outage(d, 0)->start == 1098 && outage(d, 0)->approx);
  CHECK(outage(d, NO - 1)->start == 1004);
  CHECK(outage(d, NO) == nullptr);
  outageEnd(d, 2000);
  CHECK(outage(d, 0)->end == 2000);
  outageEnd(d, 3000);                          // вже закрите — не змінюємо
  CHECK(outage(d, 0)->end == 2000);

  // 1.0.29: те саме відключення після перезапуску — не нове
  init(d);
  CHECK(outageStart(d, t, 5000, false));
  CHECK(!outageStart(d, t, 5100, true));       // ще триває — продовження
  CHECK(d.outN == 1 && sum(d, t, t).outs == 1 && outage(d, 0)->start == 5000);
  outageEnd(d, 5200);
  CHECK(outageStart(d, t, 5300, true));        // закінчилось — наступне нове
  CHECK(d.outN == 2 && sum(d, t, t).outs == 2);

  // 1.0.29: лад у записах 19–20.11.2026 з об'єкта (перезапуски 16:55 і 17:12 під час відключення відкрили ще два «≥»)
  {
    init(d);
    auto put = [&](uint32_t s, uint32_t e, bool ap) {   // як писали старі версії — без перевірки
      OutRec& o = d.outs[d.outHead]; o.start = s; o.end = e; o.approx = ap ? 1 : 0;
      d.outHead = (uint16_t)((d.outHead + 1) % NO); if (d.outN < NO) d.outN++;
    };
    auto dayOf = [](uint32_t e) { return (int32_t)(e / 86400); };
    const uint32_t D19 = 20776u * 86400u, D20 = D19 + 86400u;   // 19.11 і 20.11 (за UTC — тут неважливо)
    put(D19 + 15 * 3600 + 52 * 60, 0, true);                    // a: з ≥ 15:52, «триває»
    put(D19 + 16 * 3600 + 55 * 60, 0, true);                    // b: з ≥ 16:55, «триває» — дублікат
    put(D19 + 17 * 3600 + 12 * 60, D19 + 18 * 3600 + 21 * 60, true);   // c: ≥ 17:12–18:21 — дублікат
    put(D19 + 22 * 3600 + 6 * 60, D20 + 1 * 3600 + 13 * 60, false);    // d: 22:06–01:13
    put(D20 + 15 * 3600 + 20 * 60, D20 + 15 * 3600 + 23 * 60, false);  // e: 15:20–15:23
    put(D20 + 16 * 3600 + 19 * 60, 0, false);                   // f: з 16:19, справді триває
    dayRec(d, 20776).outs = 4; dayRec(d, 20777).outs = 2;
    int f = repairOutages(d, dayOf);
    CHECK(f == 2);
    CHECK(d.outN == 4);
    const OutRec* a = outage(d, 3);
    CHECK(a && a->start == D19 + 15 * 3600 + 52 * 60 && a->end == D19 + 18 * 3600 + 21 * 60 && a->approx);   // ≥ 15:52–18:21
    CHECK(outage(d, 2)->start == D19 + 22 * 3600 + 6 * 60);
    CHECK(outage(d, 1)->start == D20 + 15 * 3600 + 20 * 60);
    CHECK(outage(d, 0)->start == D20 + 16 * 3600 + 19 * 60 && outage(d, 0)->end == 0);   // поточне лишається «триває»
    CHECK(sum(d, 20776, 20776).outs == 2 && sum(d, 20777, 20777).outs == 2);
    CHECK(repairOutages(d, dayOf) == 0);       // удруге — нічого
    CHECK(!outageStart(d, 20777, D20 + 17 * 3600, true));     // перезапуск під час поточного — не нове
    outageEnd(d, D20 + 18 * 3600);
    CHECK(outage(d, 0)->end == D20 + 18 * 3600);
    // «триває», а за ним точне: старе закінчилось до початку нового
    init(d);
    put(1000, 0, false); put(5000, 6000, false);
    dayRec(d, 0).outs = 2;
    CHECK(repairOutages(d, dayOf) == 1 && d.outN == 2 && outage(d, 1)->end == 5000 && sum(d, 0, 0).outs == 2);
  }
  printf("%s: %d помилок, розмір Data %zu Б\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", fails, sizeof(Data));
  return fails ? 1 : 0;
}
