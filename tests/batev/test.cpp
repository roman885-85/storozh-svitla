// Перевірка подій батареї мосту (firmware/avr-bms/batev_core.h): tests/batev/run.sh
#include "batev_core.h"
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0, checks = 0;
static void ok(bool c, const std::string& what) { checks++; if (!c) { fails++; printf("  ПОМИЛКА: %s\n", what.c_str()); } }

struct Run {
  bev::State s;
  bev::Cfg c;
  uint32_t t = 1000;
  std::vector<uint8_t> all;
  std::vector<uint8_t> step(uint8_t soc, int32_t ma, uint32_t dtMs = 10000, uint32_t alarms = 0, uint16_t dmv = 20, int32_t left = -1) {
    bev::Sample x;
    x.ms = t; x.soc = soc; x.ma = ma; x.alarms = alarms; x.dmv = dmv; x.leftMin = left;
    uint8_t out[8];
    int n = bev::feed(s, x, c, out, 8);
    t += dtMs;
    std::vector<uint8_t> v(out, out + n);
    all.insert(all.end(), v.begin(), v.end());
    return v;
  }
};
static std::string names(const std::vector<uint8_t>& v) {
  std::string r;
  for (auto e : v) { if (!r.empty()) r += ", "; r += bev::evName(e); }
  return r.empty() ? "—" : r;
}

int main() {
  printf("1) Розряд 80 → 0 %%: рівно 50, 30, 20, 10, 5 і «повний розряд», перший напрям — без «почався розряд»\n");
  {
    Run r;
    for (int soc = 80; soc >= 0; soc--) r.step((uint8_t)soc, -5000);
    std::vector<uint8_t> want = {bev::E_50, bev::E_30, bev::E_20, bev::E_10, bev::E_5, bev::E_EMPTY};
    ok(r.all == want, "події: " + names(r.all));
  }
  printf("2) Коливання біля 50 %%: знову лише після 53 %%\n");
  {
    Run r;
    r.step(55, -3000);
    ok(r.step(50, -3000) == std::vector<uint8_t>{bev::E_50}, "50 → подія");
    ok(r.step(52, 2000).empty() && r.step(50, -3000).empty(), "52 → 50: тиша");
    r.step(53, 2000);
    ok(r.step(50, -3000) == std::vector<uint8_t>{bev::E_50}, "після 53 → 50: знову");
  }
  printf("3) Стрибок 35 → 18 %%: одна подія — найнижчий перетнутий рівень (20 %%)\n");
  {
    Run r;
    r.step(35, -3000);
    auto v = r.step(18, -3000);
    ok(v == std::vector<uint8_t>{bev::E_20}, "події: " + names(v));
    ok(r.step(17, -3000).empty(), "30 теж «з'їдено» — далі тиша");
  }
  printf("4) Запуск на низькому заряді: 4 %% — одразу «5 %%», 1 %% — одразу «повний розряд», 40 %% — тиша\n");
  {
    Run a, b, c;
    ok(a.step(4, -1000) == std::vector<uint8_t>{bev::E_5}, "4 %");
    ok(b.step(1, -1000) == std::vector<uint8_t>{bev::E_EMPTY}, "1 %");
    ok(c.step(40, -1000).empty(), "40 %");
    ok(a.step(3, -1000).empty() && a.step(1, -1000) == std::vector<uint8_t>{bev::E_EMPTY}, "4 → 1 %: «повний розряд» ще попереду");
  }
  printf("5) Зарядилась: 100 %% — один раз; знову — коли опустилась до 95 %% і зарядилась\n");
  {
    Run r;
    r.step(90, 5000);
    for (int soc = 91; soc < 100; soc++) ok(r.step((uint8_t)soc, 5000).empty(), "до 100 — тиша");
    ok(r.step(100, 2000) == std::vector<uint8_t>{bev::E_FULL}, "100 %");
    ok(r.step(100, 0).empty() && r.step(96, -2000).empty() && r.step(100, 2000).empty(), "96 → 100: ще ні");
    r.step(95, -2000);
    ok(r.step(100, 2000) == std::vector<uint8_t>{bev::E_FULL}, "після 95 → 100: так");
    Run z;
    z.step(99, 3000);
    ok(z.step(100, 3000) == std::vector<uint8_t>{bev::E_FULL}, "перезапуск на 99 % → 100: так");
    Run y;
    ok(y.step(100, 0).empty() && y.step(100, 0).empty(), "запуск на 100 %: тиша");
  }
  printf("6) Тривога BMS: має протриматись 10 с; нові біти — знову; минула — «тривог немає»; миготіння — тиша\n");
  {
    Run r;
    r.step(70, -2000, 5000);
    ok(r.step(70, -2000, 5000, 1u << 11).empty(), "щойно з'явилась — ще ні");
    ok(r.step(70, -2000, 5000, 1u << 11).empty(), "5 с — ще ні");
    ok(r.step(70, -2000, 5000, 1u << 11) == std::vector<uint8_t>{bev::E_ERROR}, "10 с — тривога");
    ok(r.step(70, -2000, 5000, 1u << 11).empty(), "далі — без повтору (повтор — справа мосту)");
    r.step(70, -2000, 5000, (1u << 11) | (1u << 31));
    r.step(70, -2000, 5000, (1u << 11) | (1u << 31));
    ok(r.step(70, -2000, 5000, (1u << 11) | (1u << 31)) == std::vector<uint8_t>{bev::E_ERROR}, "нова тривога (розряд вимкнено) — знову");
    r.step(70, -2000, 5000, 0);
    r.step(70, -2000, 5000, 0);
    ok(r.step(70, -2000, 5000, 0) == std::vector<uint8_t>{bev::E_ERR_OK}, "минула — «тривог немає»");
    r.step(70, -2000, 3000, 1u << 13);
    ok(r.step(70, -2000, 3000, 0).empty() && r.step(70, -2000, 3000, 0).empty() && r.step(70, -2000, 3000, 0).empty() && r.step(70, -2000, 3000, 0).empty(),
       "тривога на 3 с — тиша");
    Run q;
    q.c.errHoldMs = 0;
    q.step(60, -1000, 300000);
    ok(q.step(60, -1000, 300000, 1u << 21) == std::vector<uint8_t>{bev::E_ERROR}, "опитування по черзі (без витримки) — одразу");
    Run b;
    ok(b.step(60, -1000, 10000, 1u << 21) == std::vector<uint8_t>{bev::E_ERROR}, "запуск з тривогою — одразу");
  }
  printf("7) Напрям струму: заряд після розряду — «почався заряд», розряд — «почався розряд»; коротке — тиша\n");
  {
    Run r;
    for (int i = 0; i < 4; i++) r.step(60, -4000, 10000);
    ok(r.step(60, 3000, 10000).empty() && r.step(60, 3000, 10000).empty(), "заряд 10–20 с — ще ні");
    ok(r.step(61, 3000, 10000) == std::vector<uint8_t>{bev::E_CHG}, "заряд 20 с — так");
    ok(r.step(61, -4000, 5000).empty() && r.step(61, 3000, 10000).empty() && r.step(61, 3000, 10000).empty(), "розряд на 5 с — тиша");
    r.step(61, 500, 10000);
    r.step(61, 500, 10000);
    ok(r.step(61, 500, 10000).empty(), "спокій — без звуку");
    r.step(61, -4000, 10000);
    r.step(61, -4000, 10000);
    ok(r.step(60, -4000, 10000) == std::vector<uint8_t>{bev::E_DIS}, "розряд після спокою — «почався розряд»");
    Run q;
    q.c.dirHoldMs = 0;
    q.step(70, 3000, 600000);
    ok(q.step(70, -3000, 600000) == std::vector<uint8_t>{bev::E_DIS}, "по черзі (без витримки): одразу");
  }
  printf("8) «Вистачить менше ніж на годину»: 59 хв — так; 65 — ні; знову після 72 хв\n");
  {
    Run r;
    r.step(40, -8000, 10000, 0, 20, 90);
    ok(r.step(40, -8000, 10000, 0, 20, 59) == std::vector<uint8_t>{bev::E_LOWTIME}, "59 хв");
    ok(r.step(40, -8000, 10000, 0, 20, 65).empty() && r.step(40, -8000, 10000, 0, 20, 50).empty(), "65 → 50: ще ні");
    r.step(40, -8000, 10000, 0, 20, 75);
    ok(r.step(40, -8000, 10000, 0, 20, 50) == std::vector<uint8_t>{bev::E_LOWTIME}, "після 75 → 50: так");
    r.step(40, 2000, 10000, 0, 20, -1);
    ok(r.step(40, -8000, 10000, 0, 20, 40) == std::vector<uint8_t>{bev::E_LOWTIME}, "після заряду (−1) → 40: так");
  }
  printf("9) Перекіс комірок 150 мВ: так; 120 — ні; знову після 99\n");
  {
    Run r;
    r.step(70, -1000, 10000, 0, 100);
    ok(r.step(70, -1000, 10000, 0, 160) == std::vector<uint8_t>{bev::E_IMBAL}, "160 мВ");
    ok(r.step(70, -1000, 10000, 0, 120).empty() && r.step(70, -1000, 10000, 0, 155).empty(), "120 → 155: ні");
    r.step(70, -1000, 10000, 0, 99);
    ok(r.step(70, -1000, 10000, 0, 151) == std::vector<uint8_t>{bev::E_IMBAL}, "після 99 → 151: так");
  }
  printf("10) Зв'язок: зник — один раз; повернувся — один раз\n");
  {
    bev::State s;
    ok(bev::link(s, true) == bev::E_LOST && bev::link(s, true) == bev::E_NONE, "зник");
    ok(bev::link(s, false) == bev::E_BACK && bev::link(s, false) == bev::E_NONE, "повернувся");
  }
  printf("11) Назви й критичність\n");
  {
    for (int e = 1; e < bev::E_COUNT; e++) ok(*bev::evName(e) != 0, "назва події " + std::to_string(e));
    ok(bev::evCritical(bev::E_5) && bev::evCritical(bev::E_EMPTY) && bev::evCritical(bev::E_ERROR) && !bev::evCritical(bev::E_50), "критичні: 5 %, повний розряд, тривога");
  }
  printf("%s: перевірок %d, помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", checks, fails);
  return fails ? 1 : 0;
}
