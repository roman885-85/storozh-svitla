//  Перевірка розбору кадрів JK BMS (firmware/avr-bms/jk_core.h) на Mac: tests/bms/run.sh
//  Кадри — tests/bms/frames.txt (зразки з esphome-jk-bms: 24S із документації, 32S із запису ефіру).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "jk_core.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  ПОМИЛКА %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct F { std::string kind; std::vector<uint8_t> b; };
static std::vector<F> load(const char* path) {
  std::vector<F> v;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    F f; std::string hex;
    ss >> f.kind >> hex;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) f.b.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16));
    v.push_back(f);
  }
  return v;
}
static const F* first(const std::vector<F>& v, const char* k) { for (auto& f : v) if (f.kind == k) return &f; return nullptr; }

//  Подати кадр шматками по chunk байт; повернути, скільки разів збирач видав готовий кадр.
static int feedChunks(jk::Assembler& a, const std::vector<uint8_t>& b, int chunk, std::vector<uint8_t>* got = nullptr) {
  int n = 0;
  for (size_t i = 0; i < b.size(); i += chunk) {
    int len = (int)std::min((size_t)chunk, b.size() - i);
    if (a.feed(b.data() + i, len)) { n++; if (got) got->assign(a.buf, a.buf + jk::FRAME); }
  }
  return n;
}

int main() {
  auto v = load("frames.txt");
  printf("Кадрів у frames.txt: %zu\n", v.size());
  CHECK(v.size() == 12, "очікував 12 кадрів, є %zu", v.size());

  // 1. Запити: як у esphome-jk-bms (AA 55 90 EB cmd … сума), 0x96 → сума 0x10, 0x97 → 0x11
  {
    uint8_t q[20];
    jk::request(jk::CMD_CELLS, q);
    CHECK(q[0] == 0xAA && q[1] == 0x55 && q[2] == 0x90 && q[3] == 0xEB && q[4] == 0x96 && q[5] == 0 && q[19] == 0x10, "запит 0x96: сума %02X", q[19]);
    jk::request(jk::CMD_INFO, q);
    CHECK(q[4] == 0x97 && q[19] == 0x11, "запит 0x97: сума %02X", q[19]);
    bool zeros = true; for (int i = 5; i < 19; i++) if (q[i]) zeros = false;
    CHECK(zeros, "у запиті після команди — лише нулі");
  }

  // 2. Збирач: будь-якими шматками кадр збирається рівно один раз, сума сходиться
  for (auto& f : v) {
    for (int chunk : {20, 128, 182, 244, 300}) {
      jk::Assembler a;
      std::vector<uint8_t> got;
      int n = feedChunks(a, f.b, chunk, &got);
      CHECK(n == 1 && got == f.b, "%s шматками по %d: зібрано %d", f.kind.c_str(), chunk, n);
    }
  }
  {
    // кадр 320 байт (так теж буває) — один кадр, зайві 20 байт не заважають наступному
    const F* c = first(v, "cell24");
    std::vector<uint8_t> b = c->b; b.resize(320, 0xAA);
    jk::Assembler a;
    CHECK(feedChunks(a, b, 20) == 1, "кадр 320 байт");
    CHECK(feedChunks(a, c->b, 20) == 1, "наступний після 320 байт");
    // зіпсований байт — кадру немає, лічильник хибних +1
    std::vector<uint8_t> bad = c->b; bad[150] ^= 0x01;
    jk::Assembler a2;
    CHECK(feedChunks(a2, bad, 20) == 0 && a2.bad == 1, "зіпсований кадр відкинуто (bad=%u)", a2.bad);
    // шматок без початку кадру перед справжнім — відкинуто, сам кадр зібрано
    jk::Assembler a3;
    uint8_t junk[20]; memset(junk, 0x11, 20);
    CHECK(!a3.feed(junk, 20), "шматок без початку");
    CHECK(feedChunks(a3, c->b, 20) == 1, "кадр після сміття");
    // обірваний кадр, а далі новий — новий збирається
    jk::Assembler a4;
    a4.feed(c->b.data(), 20); a4.feed(c->b.data() + 20, 20);
    CHECK(feedChunks(a4, c->b, 20) == 1, "новий кадр після обірваного");
  }

  // 3. «Хто ти» (0x03)
  {
    jk::Info in;
    CHECK(jk::parseInfo(first(v, "dev24")->b.data(), in), "dev24 розібрано");
    CHECK(!strcmp(in.model, "JK-B2A24S15P") && !strcmp(in.hw, "10.XW") && !strcmp(in.sw, "10.07"), "dev24: %s / %s / %s", in.model, in.hw, in.sw);
    CHECK(jk::fmtHint(in) == 24, "dev24: підказка %d", jk::fmtHint(in));
    jk::Info in2;
    CHECK(jk::parseInfo(first(v, "dev32")->b.data(), in2), "dev32 розібрано");
    CHECK(!strcmp(in2.model, "JK_B2A20S20P") && !strcmp(in2.hw, "11.XW") && !strcmp(in2.sw, "11.288H"), "dev32: %s / %s / %s", in2.model, in2.hw, in2.sw);
    CHECK(jk::fmtHint(in2) == 32, "dev32: підказка %d", jk::fmtHint(in2));
    jk::Info x;
    CHECK(!jk::parseInfo(first(v, "cell24")->b.data(), x), "кадр стану — не «хто ти»");
  }

  // 4. Стан, розкладка 24S (JK-B2A24S15P): значення — як у коментарі esphome-jk-bms
  {
    jk::Cells c;
    CHECK(jk::parseCells(first(v, "cell24")->b.data(), 0, c) == 24, "cell24: розкладка %d", c.fmt);
    CHECK(c.n == 16, "cell24: комірок %d", c.n);
    CHECK(c.vmin == 3327 && c.vmax == 3329 && c.imin == 1 && c.imax == 2, "cell24: мін %u (№%u), макс %u (№%u)", c.vmin, c.imin, c.vmax, c.imax);
    CHECK(c.totalMv == 53251, "cell24: напруга %u", c.totalMv);
    CHECK(c.ma == 0, "cell24: струм %d", c.ma);
    CHECK(c.hasT1 && c.t1 == 190 && c.hasT2 && c.t2 == 191 && c.hasTmos && c.tmos == 210, "cell24: температури %d %d %d", c.t1, c.t2, c.tmos);
    CHECK(c.err == 0, "cell24: тривоги %x", c.err);
    CHECK(c.soc == 84 && c.soh == 100, "cell24: заряд %u, здоров'я %u", c.soc, c.soh);
    CHECK(c.remMah == 68494 && c.fullMah == 81000 && c.cycles == 0, "cell24: ємність %u / %u, циклів %u", c.remMah, c.fullMah, c.cycles);
    CHECK(c.chg && c.dis && !c.heat, "cell24: ключі заряду/розряду, підігрів");
    CHECK(jk::parseCells(first(v, "cell24")->b.data(), 32, c) == 24, "cell24 з хибною підказкою 32 — все одно 24");
  }

  // 5. Стан, розкладка 32S (JK_B2A20S20P, випробувальна збірка з 2 комірок, підігрів)
  {
    jk::Cells c;
    CHECK(jk::parseCells(first(v, "cell32")->b.data(), 0, c) == 32, "cell32: розкладка %d", c.fmt);
    CHECK(c.n == 2 && c.mv[0] == 1471 && c.mv[1] == 1468, "cell32: комірки %d: %u %u", c.n, c.mv[0], c.mv[1]);
    CHECK(c.totalMv == 2939, "cell32: напруга %u", c.totalMv);
    CHECK(c.ma == -197, "cell32: струм %d", c.ma);
    CHECK(c.hasT1 && c.t1 == 190 && c.hasT2 && c.t2 == 177 && c.hasTmos && c.tmos == 217, "cell32: температури %d %d %d", c.t1, c.t2, c.tmos);
    CHECK(c.err == 0x80201, "cell32: тривоги %x", c.err);
    CHECK((c.err & ~jk::ERR_INFO) == 0x201, "cell32: справжні тривоги %x (опір з'єднань, холодно для заряду)", c.err & ~jk::ERR_INFO);
    CHECK(!strcmp(jk::errName(0), "опір з'єднань комірок") && !strcmp(jk::errName(9), "холодно для заряду"), "назви тривог");
    CHECK(c.heat && !c.chg && c.dis, "cell32: підігрів так, заряд ні, розряд так");
    CHECK(c.fullMah == 40000, "cell32: ємність %u", c.fullMah);
    jk::Cells k;
    CHECK(jk::parseCells(first(v, "set32")->b.data(), 0, k) == 0, "кадр налаштувань — не стан");
  }

  // 6. Усі кадри стану з файлу розбираються, напруга батареї = сума комірок (±0,3 В)
  for (auto& f : v) {
    if (f.kind != "cell24" && f.kind != "cell32") continue;
    jk::Cells c;
    uint8_t fmt = jk::parseCells(f.b.data(), 0, c);
    uint32_t s = 0; for (int i = 0; i < 32; i++) s += c.mv[i];
    CHECK(fmt == (f.kind == "cell24" ? 24 : 32) && (s > c.totalMv ? s - c.totalMv : c.totalMv - s) <= 300, "%s лічильник %u: розкладка %d, сума %u, напруга %u", f.kind.c_str(), f.b[5], fmt, s, c.totalMv);
  }

  // 7. Штучний кадр, де напруга не сходиться ні в одній розкладці (стара JK04 тощо) — не розібрано
  {
    std::vector<uint8_t> b = first(v, "cell24")->b;
    b[118] = b[119] = b[120] = b[121] = 0;                  // напруга 24S = 0
    b[299] = jk::sum8(b.data(), 299);
    jk::Cells c;
    CHECK(jk::parseCells(b.data(), 24, c) == 0, "кадр без узгодженої напруги відкинуто");
  }

  // 8. Чи схоже оголошення на JK BMS (22.11: BMS церкви зветься «40500000000»; чужий «LEDCAR-01» теж має службу FFE0)
  CHECK(jk::looksLikeJk("JK_B1A8S10P", "aa:bb:cc:dd:ee:ff", false), "назва JK_ — так, хоч яка адреса");
  CHECK(jk::looksLikeJk("JK-B2A24S15P", "", false) && jk::looksLikeJk("jk_pb2a16s", nullptr, false), "назва JK- і малими");
  CHECK(jk::looksLikeJk("40500000000", "c8:47:80:00:00:01", true), "BMS церкви: своя назва, адреса модуля JK і служба FFE0 — так");
  CHECK(jk::looksLikeJk("", "C8:47:8C:F2:7E:03", true) && jk::looksLikeJk("", "20:21:11:28:18:dd", true), "без назви, адреси модулів JK (і великими)");
  CHECK(!jk::looksLikeJk("LEDCAR-01-0001", "ff:ff:11:00:00:01", true), "чужий пристрій зі службою FFE0 — ні");
  CHECK(!jk::looksLikeJk("40500000000", "c8:47:80:00:00:01", false), "адреса JK, але без служби FFE0 — ні");
  CHECK(!jk::looksLikeJk("", "", true) && !jk::looksLikeJk(nullptr, nullptr, true) && !jk::looksLikeJk("JK", "c8:47", true), "порожнє й обрізане");

  printf("%s: перевірок %d, помилок %d\n", fails ? "НЕ ПРОЙДЕНО" : "ПРОЙДЕНО", checks, fails);
  return fails ? 1 : 0;
}
