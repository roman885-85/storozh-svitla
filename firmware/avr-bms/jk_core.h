/*  JK BMS через Bluetooth: запити й розбір кадрів (22.11.2026). Без Arduino — те саме на платі й у перевірках на Mac
    (tests/bms). Власник 22.11.2026: «добавляй функцию мониторинга бмс через esp 32 c3».

    Протокол — за відкритим esphome-jk-bms (syssi, Apache-2.0); розбір перевірено на кадрах із його документації
    (JK-B2A24S15P, прошивка 10.07) і з його запису ефіру (JK_B2A20S20P, прошивка 11.288H):
      • запит — 20 байт: AA 55 90 EB, команда, довжина, значення (4 байти, молодший перший), нулі, сума байтів 0–18;
        0x97 — «хто ти» (відповідь — кадр 0x03), 0x96 — «стан» (BMS шле кадр 0x01 налаштувань, далі кадр 0x02 щосекунди);
      • відповідь — кадр 300 байт (буває 320): 55 AA EB 90, тип, лічильник, дані…, сума байтів 0–298 у байті 299;
      • кадр 0x02 має дві розкладки: «24S» (прошивки BMS 6–10) і «32S» (11 і новіші: напруг комірок 32, а все далі
        зсунуте на 16 і 32 байти). Розкладку визначаємо самі: напруга батареї дорівнює сумі напруг комірок лише у своїй
        розкладці — у чужій на тому місці лічильник циклів чи опори комірок.
    Плата лише ЧИТАЄ: інших команд, крім 0x96 і 0x97, не шле — запис налаштувань BMS небезпечний і не потрібен.  */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

namespace jk {

constexpr int FRAME = 300;
constexpr uint8_t CMD_CELLS = 0x96, CMD_INFO = 0x97;

inline uint8_t sum8(const uint8_t* d, int n) { uint8_t c = 0; for (int i = 0; i < n; i++) c += d[i]; return c; }
inline uint16_t u16(const uint8_t* d, int i) { return (uint16_t)(d[i] | (d[i + 1] << 8)); }
inline uint32_t u32(const uint8_t* d, int i) { return (uint32_t)u16(d, i) | ((uint32_t)u16(d, i + 2) << 16); }

inline void request(uint8_t cmd, uint8_t out[20]) {
  memset(out, 0, 20);
  out[0] = 0xAA; out[1] = 0x55; out[2] = 0x90; out[3] = 0xEB;
  out[4] = cmd;
  out[19] = sum8(out, 19);
}

//  Збирач кадру з повідомлень Bluetooth (по 20…244 байти). Кожне повідомлення, що починається з «55 AA EB 90», — новий
//  кадр; шматок без початку відкидаємо. Готовий кадр — 300 байт зі сумою, що зійшлась.
struct Assembler {
  uint8_t buf[FRAME + 24];
  int n = 0;
  uint32_t good = 0, bad = 0;      // кадрів зібрано; зі хибною сумою
  bool feed(const uint8_t* d, int len) {
    if (len >= 4 && d[0] == 0x55 && d[1] == 0xAA && d[2] == 0xEB && d[3] == 0x90) n = 0;
    else if (n == 0) return false;
    int k = len;
    if (n + k > (int)sizeof buf) k = (int)sizeof buf - n;
    if (k > 0) { memcpy(buf + n, d, k); n += k; }
    if (n < FRAME) return false;
    n = 0;
    if (sum8(buf, FRAME - 1) != buf[FRAME - 1]) { bad++; return false; }
    good++;
    return true;
  }
};

//  Кадр 0x03 — «хто ти».
struct Info {
  char model[17] = {0}, hw[9] = {0}, sw[9] = {0};
  uint32_t uptime = 0, powerOn = 0;          // секунд роботи BMS; скільки разів вмикалась
};
inline void cstr(char* dst, const uint8_t* src, int n) {     // лише друковані ASCII, до першого нуля
  int k = 0;
  for (int i = 0; i < n && src[i]; i++) if (src[i] >= 0x20 && src[i] < 0x7F && src[i] != '"' && src[i] != '\\') dst[k++] = (char)src[i];
  dst[k] = 0;
}
inline bool parseInfo(const uint8_t* f, Info& in) {
  if (f[4] != 0x03) return false;
  cstr(in.model, f + 6, 16);
  cstr(in.hw, f + 22, 8);
  cstr(in.sw, f + 30, 8);
  in.uptime = u32(f, 38);
  in.powerOn = u32(f, 42);
  return true;
}
//  Підказка розкладки за прошивкою BMS: 11+ — 32S, 6–10 — 24S, інакше не знаємо (0).
inline uint8_t fmtHint(const Info& in) {
  int major = atoi(in.sw);
  return major >= 11 ? 32 : major >= 6 ? 24 : 0;
}

//  Кадр 0x02 — стан.
struct Cells {
  uint8_t fmt = 0;                 // 24 чи 32 — розкладка; 0 — кадр не розібрано
  uint8_t n = 0;                   // комірок з напругою
  uint16_t mv[32] = {0};           // напруги комірок, мВ (0 — комірки немає)
  uint16_t vmin = 0, vmax = 0;     // найнижча й найвища, мВ
  uint8_t imin = 0, imax = 0;      // їхні номери, з 1
  uint32_t totalMv = 0;            // напруга батареї, мВ
  int32_t ma = 0;                  // струм, мА: + заряд, − розряд
  int16_t t1 = 0, t2 = 0, tmos = 0;          // температури, 0,1 °C
  bool hasT1 = false, hasT2 = false, hasTmos = false;
  uint32_t err = 0;                // біти тривог BMS (у розкладці 24S — лише 16)
  uint8_t soc = 0, soh = 0;        // заряд і «здоров'я», %
  uint32_t remMah = 0, fullMah = 0, cycles = 0;
  bool chg = false, dis = false, bal = false, heat = false;   // ключ заряду, ключ розряду, вирівнювання, підігрів
  int16_t balMa = 0;               // струм вирівнювання, мА
};

//  Чи сходиться напруга батареї (u32 на місці at) із сумою перших cellsN напруг комірок: ±0,3 В або ±3 %.
inline bool fits(const uint8_t* f, int cellsN, int at) {
  uint32_t s = 0;
  for (int i = 0; i < cellsN; i++) s += u16(f, 6 + 2 * i);
  uint32_t t = u32(f, at);
  if (t < 1000 || t > 250000 || s == 0) return false;
  uint32_t d = t > s ? t - s : s - t;
  return d <= 300 || d * 100 <= t * 3;
}
inline bool tempOk(int16_t t) { return t > -400 && t < 1200; }   // −40…120 °C; поза цим — датчика немає

//  hint — розкладка, коли обидві сходяться (буває лише в штучних кадрах): з fmtHint, або 0.
//  Повертає розкладку (24/32) або 0, якщо кадр не стану чи жодна не сходиться (стара JK04 — не підтримується).
inline uint8_t parseCells(const uint8_t* f, uint8_t hint, Cells& c) {
  if (f[4] != 0x02) return 0;
  bool f32 = fits(f, 32, 150), f24 = fits(f, 24, 118);
  uint8_t fmt = (f32 && !f24) ? 32 : (f24 && !f32) ? 24 : (f32 && f24) ? (hint == 24 ? 24 : 32) : 0;
  if (!fmt) return 0;
  c = Cells();
  c.fmt = fmt;
  const int off = fmt == 32 ? 16 : 0, o2 = off * 2, cells = fmt;
  for (int i = 0; i < cells; i++) {
    uint16_t v = u16(f, 6 + 2 * i);
    c.mv[i] = v;
    if (!v) continue;
    c.n++;
    if (!c.vmin || v < c.vmin) { c.vmin = v; c.imin = i + 1; }
    if (v > c.vmax) { c.vmax = v; c.imax = i + 1; }
  }
  c.totalMv = u32(f, 118 + o2);
  c.ma = (int32_t)u32(f, 126 + o2);
  c.t1 = (int16_t)u16(f, 130 + o2);
  c.t2 = (int16_t)u16(f, 132 + o2);
  if (fmt == 32) {
    c.tmos = (int16_t)u16(f, 112 + o2);
    c.err = u32(f, 134 + o2);
    uint8_t m = f[182 + o2];                 // які датчики є: біт 0 — ключів, 1 — T1, 2 — T2
    c.hasTmos = (m & 1) && tempOk(c.tmos);
    c.hasT1 = (m & 2) && tempOk(c.t1);
    c.hasT2 = (m & 4) && tempOk(c.t2);
  } else {
    c.tmos = (int16_t)u16(f, 134);
    c.err = u16(f, 136);
    c.hasTmos = tempOk(c.tmos);
    c.hasT1 = tempOk(c.t1);
    c.hasT2 = tempOk(c.t2);
  }
  c.balMa = (int16_t)u16(f, 138 + o2);
  c.bal = f[140 + o2] != 0;
  c.soc = f[141 + o2];
  c.remMah = u32(f, 142 + o2);
  c.fullMah = u32(f, 146 + o2);
  c.cycles = u32(f, 150 + o2);
  c.soh = f[158 + o2];
  c.chg = f[166 + o2] != 0;
  c.dis = f[167 + o2] != 0;
  c.heat = f[183 + o2] != 0;
  return fmt;
}

//  Чи схоже оголошення Bluetooth на JK BMS (щоб міст сам не ліз до чужих пристроїв зі службою FFE0 — 22.11 то був
//  світлодіодний контролер «LEDCAR-01»). Так — якщо назва «JK-…»/«JK_…» (як у мосту HA), АБО служба FFE0 разом з
//  адресою модуля Bluetooth, який ставить JK: C8:47:8C, C8:47:80 (обидві BMS у HA власника й BMS церкви), 20:21:11.
//  Назва при цьому може бути будь-яка: BMS церкви зветься «40500000000» (серійний номер). У 1.0.1 пристрій з
//  «не тією» назвою відкидався — і міст о 19:19 22.11 перестав її знаходити (під'єднувався, лише коли пошук не
//  встигав отримати назву). mac — малими чи великими, з двокрапками.
inline bool looksLikeJk(const char* name, const char* mac, bool svcFFE0) {
  if (name && (name[0] == 'J' || name[0] == 'j') && (name[1] == 'K' || name[1] == 'k') && (name[2] == '-' || name[2] == '_')) return true;
  if (!svcFFE0 || !mac) return false;
  static const char* const OUI[] = {"c8:47:8c", "c8:47:80", "20:21:11"};
  for (const char* o : OUI) {
    bool same = true;
    for (int i = 0; i < 8 && same; i++) {
      char c = mac[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      same = c == o[i];
    }
    if (same) return true;
  }
  return false;
}

//  Тривоги BMS (біти c.err). Назви — коротко, для людей.
constexpr int ERR_BITS = 29;
inline const char* errName(int bit) {
  static const char* const N[ERR_BITS] = {
    "опір з'єднань комірок",                 // 0  Wire resistance
    "перегрів ключів BMS",                 // 1  MOSFET overtemperature
    "комірок не стільки, як у налаштуваннях",// 2
    "",                                    // 3  (не вживається)
    "батарея повністю заряджена",          // 4  — не тривога
    "перенапруга батареї",                 // 5
    "завеликий струм заряду",              // 6
    "коротке замикання під час заряду",    // 7
    "перегрів під час заряду",             // 8
    "холодно для заряду",                  // 9
    "збій зв'язку всередині BMS",          // 10 Coprocessor communication error
    "низька напруга комірки",               // 11
    "низька напруга батареї",              // 12
    "завеликий струм розряду",             // 13
    "коротке замикання під час розряду",   // 14
    "перегрів під час розряду",            // 15
    "несправний ключ заряду",              // 16
    "несправний ключ розряду",             // 17
    "GPS від'єднано",                      // 18 — не тривога
    "час змінити пароль BMS",              // 19 — не тривога
    "розряд не вмикається",                // 20
    "перегрів батареї",                    // 21
    "несправний датчик температури",       // 22
    "несправний модуль PL",                // 23
    "захист від замикання не знімається",  // 24
    "завеликий струм розряду (II)",        // 25
    "завеликий струм розряду (III)",       // 26
    "холодно для розряду",                 // 27
    "GPS: віддалене блокування",           // 28 — не тривога
  };
  return bit >= 0 && bit < ERR_BITS ? N[bit] : "";
}
//  Біти, що не є тривогою: 3 (не вживається), 4 (повністю заряджена), 18, 19, 28 (GPS і пароль), 29–31 (невідомі).
constexpr uint32_t ERR_INFO = (1u << 3) | (1u << 4) | (1u << 18) | (1u << 19) | (1u << 28) | (7u << 29);

}  // namespace jk
