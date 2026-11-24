/*  Відлік часу на одну відповідь сторінки (1.0.43; сервер — GuardedServer у web.cpp). Без Arduino — те саме на платі
    й у перевірці на комп'ютері (tests/webguard).

    Навіщо. 23.11.2026 о 12:04 основна задача Сторожа 1.0.40 30 с чекала в записі відповіді клієнтові, що перестав
    приймати дані, і сторож задач перезапустив Сторожа. Тепер перед кожним записом сервер питає budget(): скільки ще
    можна чекати, поки клієнт прийме; 0 — час на відповідь вийшов, з'єднання закривають.
    У 1.0.42 тут була помилка «t0 = now | 1»: за парного millis() початок ставав на 1 мс у майбутньому, різниця без
    знака — 4 294 967 295 мс, і приблизно половину відповідей обривало одразу (через DDNS роутера — 502).  */
#pragma once
#include <stdint.h>

struct WebGuard {
  static constexpr uint32_t TOTAL_MS = 10000;     // на всю відповідь — від першого запису
  static constexpr uint32_t WAIT_MS = 1500;       // найдовше чекати, поки клієнт прийме черговий шматок

  bool started = false, dead = false;
  uint32_t t0 = 0;

  void arm() { started = false; dead = false; }   // перед кожним запитом
  uint32_t budget(uint32_t now) {                 // мс; 0 — час вийшов
    if (!started) { started = true; t0 = now; }
    uint32_t used = now - t0;                     // t0 ≤ now; перехід millis() через 0 різниця без знака витримує
    if (used >= TOTAL_MS) return 0;
    uint32_t w = TOTAL_MS - used;
    return w < WAIT_MS ? w : WAIT_MS;
  }
};
