#pragma once
/*  Облік світла по днях і відключень (1.0.12, власник: «запись статистики в боте — сколько времени был свет и сколько
    его не было»). Чистий C++ без Arduino — тести на Mac: tests/stats_core_test.cpp.
    Лежить у RTC-пам'яті ESP32 (stats.cpp): переживає перезапуск і оновлення, але не зникнення живлення. Флеш не пишемо —
    правило власника: автоматичне (і статистика) — лише в пам'яті. Довгий запис — щоденні підсумки в чаті Telegram.  */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace sc {

static const int ND = 40;                 // днів у кільці (досить на «30 днів»)
static const int NO = 48;                 // останніх відключень
static const uint32_t MAGIC = 0x53544154; // 'STAT'
static const uint32_t VER = 1;            // змінилась будова Data — нове число, старе обнулиться

struct DayRec { int32_t day; uint32_t on, off, unk; uint16_t outs; uint16_t pad; };
struct OutRec { uint32_t start, end; uint8_t approx; uint8_t pad[3]; };   // epoch; end 0 — триває; approx — початок «щонайменше»
struct Data {
  uint32_t magic, ver;
  uint32_t since;                         // epoch, з якого моменту рахуємо (0 — годинника ще не було)
  DayRec days[ND];
  OutRec outs[NO];
  uint16_t outHead, outN;                 // кільце відключень: outHead — куди писати наступне
  int32_t lastSumDay;                     // за який день (номер «сьогодні») уже надіслали щоденний підсумок
  uint32_t pendOn, pendOff, pendUnk;      // секунди, поки годинника ще не було
  uint32_t crc;
};

//  Номер дня за григоріанським календарем (Howard Hinnant, days_from_civil): 1970-01-01 → 0.
inline int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline uint32_t crc32(const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= b[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}
inline uint32_t dataCrc(const Data& d) { return crc32(&d, offsetof(Data, crc)); }
inline void seal(Data& d) { d.crc = dataCrc(d); }
inline bool valid(const Data& d) { return d.magic == MAGIC && d.ver == VER && d.crc == dataCrc(d) && d.outN <= NO && d.outHead < NO; }
inline void init(Data& d) {
  memset(&d, 0, sizeof d);
  d.magic = MAGIC;
  d.ver = VER;
  d.lastSumDay = -1;
  for (int i = 0; i < ND; i++) d.days[i].day = -1;
  seal(d);
}

inline DayRec& dayRec(Data& d, int32_t day) {
  DayRec& r = d.days[((day % ND) + ND) % ND];
  if (r.day != day) { memset(&r, 0, sizeof r); r.day = day; }
  return r;
}
//  st: 1 — світло є, 0 — немає, -1 — невідомо
inline void add(Data& d, int32_t day, int st, uint32_t sec) {
  DayRec& r = dayRec(d, day);
  (st > 0 ? r.on : st == 0 ? r.off : r.unk) += sec;
}
//  Перехід бачимо із затримкою (30 с без АВР — «міста немає»): ці секунди вже пішли в старий стан. Переносимо.
inline void move(Data& d, int32_t day, int from, int to, uint32_t sec) {
  DayRec& r = dayRec(d, day);
  uint32_t& a = from > 0 ? r.on : from == 0 ? r.off : r.unk;
  uint32_t& b = to > 0 ? r.on : to == 0 ? r.off : r.unk;
  if (sec > a) sec = a;
  a -= sec;
  b += sec;
}
//  1.0.29: попереднє відключення ще не закінчилось (світла «є» між ними не бачили) — це те саме відключення, не нове.
//  До 1.0.27 кожен перезапуск під час відключення відкривав нове «з ≥ …», а старе лишалось «триває» назавжди: 19.11 —
//  три записи на одне відключення, у підсумках «відключень 4» замість 2. false — нове не почато.
inline bool outageOpen(const Data& d) { return d.outN && d.outs[(d.outHead + NO - 1) % NO].end == 0; }
inline bool outageStart(Data& d, int32_t day, uint32_t start, bool approx) {
  if (outageOpen(d)) return false;
  OutRec& o = d.outs[d.outHead];
  o.start = start;
  o.end = 0;
  o.approx = approx ? 1 : 0;
  d.outHead = (uint16_t)((d.outHead + 1) % NO);
  if (d.outN < NO) d.outN++;
  dayRec(d, day).outs++;
  return true;
}
inline void outageEnd(Data& d, uint32_t end) {
  if (!d.outN) return;
  OutRec& o = d.outs[(d.outHead + NO - 1) % NO];
  if (!o.end) o.end = end;
}
//  i-те відключення від найновішого (0 — останнє); nullptr — немає
inline const OutRec* outage(const Data& d, int i) {
  if (i < 0 || i >= d.outN) return nullptr;
  return &d.outs[(d.outHead + NO - 1 - i) % NO];
}
//  1.0.29: лад у записах, що лишились від старих версій. Іти від давнього до нового: відключення «триває», а за ним є
//  новіше. Новіше з «≥» (почате перезапуском, не справжнім зникненням) — те саме відключення: злити (кінець — його кінець)
//  і відняти з лічильника того дня. Новіше точне — старе закінчилось раніше, коли — невідомо: кінець — початок новішого.
//  dayOf — номер дня за місцевим часом (у прошивці — з поясом Києва). Повертає, скільки записів виправлено.
template <class DayOf> inline int repairOutages(Data& d, DayOf dayOf) {
  OutRec a[NO];
  int n = d.outN, fixes = 0;
  for (int i = 0; i < n; i++) a[i] = d.outs[(d.outHead + NO - n + i) % NO];   // від давнього
  int w = 0;
  for (int i = 0; i < n; i++) {
    if (w && a[w - 1].end == 0) {
      OutRec& prev = a[w - 1];
      if (a[i].approx) {
        prev.end = a[i].end;
        DayRec& r = dayRec(d, dayOf(a[i].start));
        if (r.outs) r.outs--;
        fixes++;
        continue;                                   // злито — новішого запису більше немає
      }
      prev.end = a[i].start > prev.start ? a[i].start : prev.start;
      fixes++;
    }
    a[w++] = a[i];
  }
  if (!fixes) return 0;
  memset(d.outs, 0, sizeof d.outs);
  for (int i = 0; i < w; i++) d.outs[i] = a[i];
  d.outN = (uint16_t)w;
  d.outHead = (uint16_t)(w % NO);
  return fixes;
}

struct Sum { uint32_t on, off, unk, outs; };
inline Sum sum(const Data& d, int32_t fromDay, int32_t toDay) {
  Sum s = {0, 0, 0, 0};
  for (int32_t day = fromDay; day <= toDay; day++) {
    const DayRec& r = d.days[((day % ND) + ND) % ND];
    if (r.day != day) continue;
    s.on += r.on; s.off += r.off; s.unk += r.unk; s.outs += r.outs;
  }
  return s;
}

}  // namespace sc
