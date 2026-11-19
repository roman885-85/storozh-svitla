// Перевірка yasno_core.h на справжній відповіді YASNO: tests/yasno/run.sh відповідь.json
#include "../../firmware/avr-mist/yasno_core.h"
#include <cstdio>
#include <vector>
int main(int, char** argv) {
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  std::vector<uint8_t> b;
  int c;
  while ((c = fgetc(f)) != EOF) b.push_back((uint8_t)c);
  fclose(f);
  const char* qs[] = {"1.1", "1.2", "2.1", "2.2", "3.1", "3.2", "4.1", "4.2", "5.1", "5.2", "6.1", "6.2", "7.1"};
  unsigned seed = 12345;
  for (const char* q : qs) {
    static YasnoStream ys;       // великий буфер — не на стеку
    ys.begin(q);
    size_t i = 0;
    while (i < b.size()) {        // шматки випадкової довжини 1…700 — як приходить з мережі
      seed = seed * 1103515245 + 12345;
      size_t n = 1 + (seed >> 8) % 700;
      if (i + n > b.size()) n = b.size() - i;
      ys.feed(b.data() + i, n);
      i += n;
    }
    YQueue y;
    bool ok = ys.finish(y);
    printf("%s %s", q, ok ? "так" : "немає");
    if (ok) for (int d = 0; d < 2; d++) {
      printf(" | %s %s(%d) %d:", y.d[d].date, y.d[d].stRaw, y.d[d].st, y.d[d].n);
      for (int k = 0; k < y.d[d].n; k++) printf(" %d-%d/%d", y.d[d].sl[k].s, y.d[d].sl[k].e, y.d[d].sl[k].t);
    }
    if (ok) printf(" | upd %s", y.upd);
    printf("\n");
  }
  return 0;
}
