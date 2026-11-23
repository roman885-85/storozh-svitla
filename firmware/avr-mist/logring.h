/*  Журнал подій у кільці 32-бітних слів (1.0.38). Без Arduino — те саме на платі й у перевірці на Mac (tests/logring).

    Навіщо. До 1.0.37 журнал — 80 рядків String у звичайній пам'яті: 10–24 КБ із тих ~25 КБ, що справді вільні на платі
    без PSRAM. Водночас ~32 КБ пам'яті команд (IRAM) стоять порожні: туди можна писати лише цілими 32-бітними словами,
    тож ні String, ні Wi-Fi, ні шифрування її не займуть. Журнал кладемо туди: кожен байт дістаємо й кладемо через ціле
    слово (volatile — щоб компілятор не «звузив» читання до байта: це на IRAM аварія LoadStoreError).

    Запис: [довжина 2 Б][час 8 Б][текст][довжина 2 Б] — довжина з обох боків, щоб іти і від найстарішого, і від
    найновішого. Місця бракує — зникають найстаріші записи. Захист від двох задач — зовні (м'ютекс у util.cpp).  */
#pragma once
#include <stdint.h>
#include <stddef.h>

class LogRing {
 public:
  static constexpr uint32_t OVERHEAD = 12;        // 2 + 8 + 2
  static constexpr uint32_t MAXTEXT = 1024;

  void begin(uint32_t* words, uint32_t bytes) {
    w = words;
    cap = bytes & ~3u;
    head = used = count = 0;
  }
  bool ready() const { return w && cap >= 64; }
  uint32_t capacity() const { return cap; }
  uint32_t usedBytes() const { return used; }
  uint32_t size() const { return count; }

  void put(int64_t us, const char* text, uint32_t n) {
    if (!ready()) return;
    if (n > MAXTEXT) n = MAXTEXT;
    if (n + OVERHEAD > cap) n = cap - OVERHEAD;
    while (used + n + OVERHEAD > cap) dropOldest();
    uint32_t p = head;
    p = put16(p, (uint16_t)n);
    uint64_t t = (uint64_t)us;
    for (int i = 0; i < 8; i++) p = putb(p, (uint8_t)(t >> (8 * i)));
    for (uint32_t i = 0; i < n; i++) p = putb(p, (uint8_t)text[i]);
    p = put16(p, (uint16_t)n);
    head = p;
    used += n + OVERHEAD;
    count++;
  }

  //  i-й запис від найновішого (0 — останній). Повертає довжину тексту (у out — не більше outCap − 1 байтів і нуль)
  //  або −1, якщо такого немає.
  int get(uint32_t i, int64_t& us, char* out, uint32_t outCap) const {
    if (!ready() || i >= count || !outCap) return -1;
    uint32_t p = head;
    uint32_t len = 0;
    for (uint32_t k = 0; k <= i; k++) {
      len = get16(back(p, 2));
      if (len + OVERHEAD > used) return -1;       // зіпсований запис — не йдемо далі
      p = back(p, len + OVERHEAD);
    }
    if (get16(p) != len) return -1;
    uint32_t q = fwd(p, 2);
    uint64_t t = 0;
    for (int b = 0; b < 8; b++) { t |= (uint64_t)getb(q) << (8 * b); q = fwd(q, 1); }
    us = (int64_t)t;
    uint32_t m = len < outCap - 1 ? len : outCap - 1;
    for (uint32_t b = 0; b < m; b++) { out[b] = (char)getb(q); q = fwd(q, 1); }
    out[m] = 0;
    return (int)len;
  }

 private:
  volatile uint32_t* w = nullptr;
  uint32_t cap = 0, head = 0, used = 0, count = 0;

  uint32_t fwd(uint32_t p, uint32_t n) const { p += n; return p >= cap ? p - cap : p; }
  uint32_t back(uint32_t p, uint32_t n) const { return p >= n ? p - n : p + cap - n; }
  uint8_t getb(uint32_t i) const { return (uint8_t)(w[i >> 2] >> ((i & 3) << 3)); }
  uint32_t putb(uint32_t i, uint8_t b) {
    uint32_t sh = (i & 3) << 3;
    uint32_t v = w[i >> 2];
    w[i >> 2] = (v & ~(0xFFu << sh)) | ((uint32_t)b << sh);
    return fwd(i, 1);
  }
  uint16_t get16(uint32_t p) const { return (uint16_t)(getb(p) | (getb(fwd(p, 1)) << 8)); }
  uint32_t put16(uint32_t p, uint16_t v) { p = putb(p, (uint8_t)v); return putb(p, (uint8_t)(v >> 8)); }
  void dropOldest() {
    if (!count) { head = used = 0; return; }
    uint32_t tail = back(head, used);
    uint32_t len = get16(tail);
    if (len + OVERHEAD > used) { head = used = count = 0; return; }   // зіпсовано — починаємо з чистого
    used -= len + OVERHEAD;
    count--;
  }
};
