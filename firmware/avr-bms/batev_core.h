/*  Події батареї для світла й звуку мосту (1.2.0). Без Arduino — те саме на платі й у перевірці на Mac (tests/batev).

    Власник 23.11.2026: «добавить функцию с пассивным бузером и активным, для звукового оповещения (аккумулятор зарядился,
    осталось половина заряда, 30 процентов, 20, 10, 5 и полный разряд, ошибка аккумулятора, возможно придумай еще что-то)».

    На кожну BMS — свій State; feed() отримує кожен свіжий кадр стану BMS і повертає події, що щойно сталися:
      • рівні заряду 50, 30, 20, 10, 5 % — заряд опустився до рівня чи нижче; знову діє, коли заряд піднявся на 3 % вище;
        перетнуто кілька рівнів разом (31 → 18 %) — одна подія, найнижчий рівень;
      • «повний розряд» — заряд 1 % і нижче (знову діє з 4 %);
      • «зарядилась» — заряд 100 % (знову діє, коли опустився до 95 %);
      • «тривога BMS» — з'явились нові тривоги (тримаються errHoldMs); «тривог більше немає» — коли зникли;
      • «почався розряд» / «почався заряд» — струм за межею dirMa тримається dirHoldMs (перший напрям — без події);
      • «вистачить менше ніж на годину» — за середнім розрядом (знову діє з 72 хв чи коли розряд скінчився);
      • «перекіс комірок» — різниця найвищої й найнижчої ≥ imbalMv (знову діє, коли менша на 50 мВ).
    Перший кадр після запуску лише запам'ятовує стан — крім критичного: заряд 5 % і нижче звучить одразу.
    link() — зв'язок з BMS: зник (E_LOST) і повернувся (E_BACK).  */
#pragma once
#include <stdint.h>

namespace bev {

enum Ev : uint8_t {
  E_NONE = 0, E_FULL, E_50, E_30, E_20, E_10, E_5, E_EMPTY, E_ERROR, E_ERR_OK,
  E_DIS, E_CHG, E_LOWTIME, E_IMBAL, E_LOST, E_BACK, E_COUNT
};

inline const char* evName(uint8_t e) {
  switch (e) {
    case E_FULL: return "батарея зарядилась";
    case E_50: return "лишилась половина заряду";
    case E_30: return "заряд 30 %";
    case E_20: return "заряд 20 %";
    case E_10: return "заряд 10 %";
    case E_5: return "заряд 5 %";
    case E_EMPTY: return "повний розряд";
    case E_ERROR: return "тривога BMS";
    case E_ERR_OK: return "тривог BMS більше немає";
    case E_DIS: return "почався розряд";
    case E_CHG: return "почався заряд";
    case E_LOWTIME: return "вистачить менше ніж на годину";
    case E_IMBAL: return "перекіс комірок";
    case E_LOST: return "BMS не на зв'язку";
    case E_BACK: return "BMS знову на зв'язку";
    default: return "";
  }
}
//  Критичні — звучать і вночі (якщо так вибрано) й повторюються, доки стан не мине чи їх не заглушать
inline bool evCritical(uint8_t e) { return e == E_5 || e == E_EMPTY || e == E_ERROR; }

struct Sample {
  uint32_t ms = 0;        // коли (millis)
  uint8_t soc = 0;        // заряд, %
  int32_t ma = 0;         // струм, мА: + заряд, − розряд
  uint32_t alarms = 0;    // тривоги BMS без інформаційних бітів (біт 31 — BMS вимкнула розряд)
  uint16_t dmv = 0;       // різниця найвищої й найнижчої комірки, мВ
  int32_t leftMin = -1;   // на скільки вистачить за середнім розрядом, хв; −1 — не розряджається
};

struct Cfg {
  uint32_t errHoldMs = 10000;   // тривога має триматись (у режимі «по черзі» — 0: кадри рідко)
  uint32_t dirHoldMs = 20000;   // напрям струму має триматись (у режимі «по черзі» — 0)
  int32_t dirMa = 1000;         // з якого струму — розряд чи заряд, мА
  uint16_t imbalMv = 150;       // перекіс комірок, мВ
};

static const uint8_t LEVEL[5] = {50, 30, 20, 10, 5};
static const uint8_t LEVEL_EV[5] = {E_50, E_30, E_20, E_10, E_5};

struct State {
  bool init = false;
  uint8_t armed = 0;            // біти 0–4 — рівні 50…5 %, біт 5 — повний розряд: 1 — ще попереду
  bool fullArmed = false;
  int8_t dir = 0, cand = 0;     // −1 розряд, +1 заряд, 0 спокій; cand — кандидат
  bool dirKnown = false;
  uint32_t candMs = 0;
  uint32_t errShown = 0, errCand = 0, errCandMs = 0;
  bool imbal = false, lowArmed = true, lost = false;
};

inline int dirOf(int32_t ma, int32_t lim) { return ma <= -lim ? -1 : ma >= lim ? 1 : 0; }

//  Події в out[] (до max), повертає кількість
inline int feed(State& s, const Sample& x, const Cfg& c, uint8_t* out, int max) {
  int n = 0;
  auto put = [&](uint8_t e) { if (n < max) out[n++] = e; };
  if (!s.init) {
    s.init = true;
    s.armed = 0;
    for (int k = 0; k < 5; k++) if (x.soc > LEVEL[k]) s.armed |= 1u << k;
    if (x.soc > 1) s.armed |= 1u << 5;
    s.fullArmed = x.soc < 100;                 // перезапуск на 99 % під час заряду — «зарядилась» усе одно прозвучить
    if (x.soc <= 1) put(E_EMPTY);             // критичне — одразу й після запуску
    else if (x.soc <= 5) put(E_5);
    s.errShown = s.errCand = x.alarms;
    s.errCandMs = x.ms;
    if (x.alarms) put(E_ERROR);
    s.imbal = x.dmv >= c.imbalMv;
    s.cand = dirOf(x.ma, c.dirMa);
    s.candMs = x.ms;
    if (c.dirHoldMs == 0) { s.dir = s.cand; s.dirKnown = true; }   // по черзі: напрям першого кадру вже відомий
    s.lowArmed = !(x.leftMin >= 0 && x.leftMin < 60);
    return n;
  }
  //  рівні: перезвести на 3 % вище; найнижчий перетнутий — одна подія
  for (int k = 0; k < 5; k++) if (!(s.armed >> k & 1) && x.soc >= LEVEL[k] + 3) s.armed |= 1u << k;
  if (!(s.armed >> 5 & 1) && x.soc >= 4) s.armed |= 1u << 5;
  int fire = -1;
  for (int k = 0; k < 5; k++) if ((s.armed >> k & 1) && x.soc <= LEVEL[k]) { s.armed &= ~(1u << k); fire = k; }
  if ((s.armed >> 5 & 1) && x.soc <= 1) { s.armed &= ~(1u << 5); for (int k = 0; k < 5; k++) s.armed &= ~(1u << k); put(E_EMPTY); }
  else if (fire >= 0) put(LEVEL_EV[fire]);
  //  зарядилась
  if (!s.fullArmed && x.soc <= 95) s.fullArmed = true;
  if (s.fullArmed && x.soc >= 100) { s.fullArmed = false; put(E_FULL); }
  //  тривоги BMS: новий набір має протриматись errHoldMs
  if (x.alarms != s.errCand) { s.errCand = x.alarms; s.errCandMs = x.ms; }
  if (s.errCand != s.errShown && (uint32_t)(x.ms - s.errCandMs) >= c.errHoldMs) {
    bool more = (s.errCand & ~s.errShown) != 0;
    s.errShown = s.errCand;
    if (s.errShown && more) put(E_ERROR);
    else if (!s.errShown) put(E_ERR_OK);
  }
  //  напрям струму: кандидат тримається dirHoldMs — підтверджено
  int d = dirOf(x.ma, c.dirMa);
  if (d != s.cand) { s.cand = (int8_t)d; s.candMs = x.ms; }
  if ((uint32_t)(x.ms - s.candMs) >= c.dirHoldMs && (!s.dirKnown || s.cand != s.dir)) {
    int8_t was = s.dir;
    bool known = s.dirKnown;
    s.dir = s.cand;
    s.dirKnown = true;
    if (known && s.dir == -1 && was != -1) put(E_DIS);
    if (known && s.dir == 1 && was != 1) put(E_CHG);
  }
  //  вистачить менше ніж на годину
  if (!s.lowArmed && (x.leftMin < 0 || x.leftMin >= 72)) s.lowArmed = true;
  if (s.lowArmed && x.leftMin >= 0 && x.leftMin < 60) { s.lowArmed = false; put(E_LOWTIME); }
  //  перекіс комірок
  if (s.imbal && x.dmv + 50 < c.imbalMv) s.imbal = false;
  if (!s.imbal && x.dmv >= c.imbalMv) { s.imbal = true; put(E_IMBAL); }
  return n;
}

//  Зв'язок з BMS: lostNow — даних немає довше межі (її рахує міст: з урахуванням періоду опитування)
inline uint8_t link(State& s, bool lostNow) {
  if (lostNow && !s.lost) { s.lost = true; return E_LOST; }
  if (!lostNow && s.lost) { s.lost = false; return E_BACK; }
  return E_NONE;
}

}  // namespace bev
