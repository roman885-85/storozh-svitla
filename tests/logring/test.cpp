//  Перевірка кільця журналу (firmware/avr-mist/logring.h) на Mac: tests/logring/run.sh
//  Звіряємо з простою моделлю (std::deque) на випадкових записах; збірка з перевіркою меж пам'яті (-fsanitize=address).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "logring.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ПОМИЛКА %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rec { int64_t us; std::string t; };

//  Модель: ті самі правила — обрізати текст до MAXTEXT і до місткості, викидати найстаріші, поки не вміститься.
struct Model {
  uint32_t cap; std::deque<Rec> d; uint32_t used = 0;
  void put(int64_t us, const std::string& s) {
    std::string t = s.substr(0, LogRing::MAXTEXT);
    if (t.size() + LogRing::OVERHEAD > cap) t = t.substr(0, cap - LogRing::OVERHEAD);
    while (used + t.size() + LogRing::OVERHEAD > cap) { used -= d.front().t.size() + LogRing::OVERHEAD; d.pop_front(); }
    d.push_back({us, t}); used += t.size() + LogRing::OVERHEAD;
  }
};

static uint32_t rnd() { static uint32_t s = 12345; s = s * 1664525u + 1013904223u; return s >> 8; }
static std::string text(uint32_t n) {
  static const char* W[] = {"Місто: є (АВР на зв'язку)", "До Telegram: 🔴 Зникло міське світло", "Графік YASNO: отримано, черга 3.1", "Вхід на сторінку з 192.168.1.50", "ї", "abc"};
  std::string s;
  while (s.size() < n) { s += W[rnd() % 6]; s += ' '; }
  return s.substr(0, n);
}

static bool same(const LogRing& r, const Model& m, const char* what) {
  if (r.size() != m.d.size() || r.usedBytes() != m.used) { printf("  %s: записів %u/%zu, байтів %u/%u\n", what, r.size(), m.d.size(), r.usedBytes(), m.used); return false; }
  std::vector<char> buf(LogRing::MAXTEXT + 8);
  for (uint32_t i = 0; i < r.size(); i++) {
    int64_t us = 0;
    int n = r.get(i, us, buf.data(), (uint32_t)buf.size());
    const Rec& e = m.d[m.d.size() - 1 - i];
    if (n != (int)e.t.size() || us != e.us || memcmp(buf.data(), e.t.data(), e.t.size())) { printf("  %s: запис %u не той (довжина %d/%zu)\n", what, i, n, e.t.size()); return false; }
  }
  int64_t us;
  if (r.get(r.size(), us, buf.data(), (uint32_t)buf.size()) != -1) { printf("  %s: зайвий запис\n", what); return false; }
  return true;
}

int main() {
  // 1. Порожнє кільце й не підготовлене
  {
    LogRing r;
    char b[16]; int64_t us;
    CHECK(!r.ready() && r.get(0, us, b, sizeof b) == -1, "непідготовлене кільце нічого не віддає");
    r.put(1, "x", 1);                              // і не падає
    std::vector<uint32_t> mem(64);
    r.begin(mem.data(), 256);
    CHECK(r.ready() && r.size() == 0 && r.get(0, us, b, sizeof b) == -1, "порожнє");
  }

  // 2. Різні місткості (і не кратні 4), випадкові довжини — як модель; пам'ять рівно за розміром (ASan ловить вихід за межі)
  for (uint32_t bytes : {64u, 100u, 257u, 1024u, 6144u, 30720u}) {
    uint32_t words = bytes / 4;
    uint32_t* mem = (uint32_t*)malloc(words * 4);
    for (uint32_t i = 0; i < words; i++) mem[i] = 0xA5A5A5A5u;          // «сміття», як у справжній купі
    LogRing r;
    r.begin(mem, bytes);
    Model m{bytes & ~3u};
    bool ok = true;
    for (int k = 0; k < 6000 && ok; k++) {
      uint32_t mode = rnd() % 10;
      uint32_t n = mode < 6 ? rnd() % 140 : mode < 9 ? rnd() % 320 : rnd() % 1300;   // зрідка довші за MAXTEXT і за кільце
      std::string s = text(n);
      int64_t us = (int64_t)k * 1000003 - 5;                                       // і від'ємний час на початку
      r.put(us, s.data(), (uint32_t)s.size());
      m.put(us, s);
      if (k % 97 == 0 || k > 5990) ok = same(r, m, "звірка");
    }
    CHECK(ok, "місткість %u Б: кільце збігається з моделлю", bytes);
    CHECK(r.usedBytes() <= r.capacity(), "місткість %u Б: зайнято %u", bytes, r.usedBytes());
    free(mem);
  }

  // 3. Малий буфер читача: текст обрізано, довжина — справжня; порожній рядок теж запис
  {
    std::vector<uint32_t> mem(256);
    LogRing r;
    r.begin(mem.data(), 1024);
    r.put(42, "Привіт, журнале", (uint32_t)strlen("Привіт, журнале"));
    r.put(43, "", 0);
    char b[8]; int64_t us = 0;
    CHECK(r.get(0, us, b, sizeof b) == 0 && us == 43 && b[0] == 0, "порожній запис");
    int n = r.get(1, us, b, sizeof b);
    CHECK(n == (int)strlen("Привіт, журнале") && us == 42 && strlen(b) == 7 && !memcmp(b, "Привіт, журнале", 7), "обрізання читачем: %d, «%s»", n, b);
  }

  // 4. Скільки рядків журналу вміщає 30 КБ (типові рядки ~110 байтів)
  {
    std::vector<uint32_t> mem(30720 / 4);
    LogRing r;
    r.begin(mem.data(), 30720);
    for (int k = 0; k < 2000; k++) { std::string s = text(60 + rnd() % 100); r.put(k, s.data(), (uint32_t)s.size()); }
    printf("У 30 КБ вміщається %u рядків журналу (по 60–160 байтів)\n", r.size());
    CHECK(r.size() > 200, "щонайменше 200 рядків, є %u", r.size());
  }

  printf("%s: перевірок %d, помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", checks, fails);
  return fails ? 1 : 0;
}
