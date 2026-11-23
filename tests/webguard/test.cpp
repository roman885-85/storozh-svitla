// Перевірка відліку часу відповіді сторінки (firmware/avr-mist/webguard.h): tests/webguard/run.sh
#include "webguard.h"
#include <cstdio>

static int fails = 0, checks = 0;
static void eq(const char* what, uint32_t got, uint32_t want) {
  checks++;
  if (got != want) { fails++; printf("ПОМИЛКА: %s — %u, а треба %u\n", what, (unsigned)got, (unsigned)want); }
}

int main() {
  WebGuard g;
  //  1.0.42: за парного millis() перший запис одразу отримував 0 («час вийшов»). Перший запис — завжди повне очікування.
  const uint32_t starts[] = {0, 1, 2, 1000, 1001, 123456, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF};
  for (uint32_t s : starts) {
    g.arm();
    char b[64];
    snprintf(b, sizeof b, "перший запис о %u", (unsigned)s);
    eq(b, g.budget(s), WebGuard::WAIT_MS);
  }
  //  Хід часу в одній відповіді
  g.arm();
  eq("0 мс", g.budget(5000), 1500);
  eq("4 с", g.budget(9000), 1500);
  eq("8,5 с", g.budget(13500), 1500);
  eq("8,8 с — лишилось 1,2 с", g.budget(13800), 1200);
  eq("9,999 с — лишилась 1 мс", g.budget(14999), 1);
  eq("10 с — час вийшов", g.budget(15000), 0);
  eq("12 с — час вийшов", g.budget(17000), 0);
  //  Новий запит — новий відлік
  g.arm();
  eq("після arm()", g.budget(17001), 1500);
  //  Перехід millis() через 0 (49,7 доби)
  g.arm();
  eq("за 256 мс до переходу", g.budget(0xFFFFFF00u), 1500);
  eq("після переходу, 512 мс", g.budget(0x00000100u), 1500);
  eq("після переходу, 9,5 с", g.budget(0xFFFFFF00u + 9500u), 500);
  eq("після переходу, 10 с", g.budget(0xFFFFFF00u + 10000u), 0);
  printf("%s: перевірок %d, помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", checks, fails);
  return fails ? 1 : 0;
}
