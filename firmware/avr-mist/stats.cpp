#include "stats.h"
#include "stats_core.h"
#include "settings.h"
#include "util.h"
#include "tg.h"
#include <time.h>

RTC_NOINIT_ATTR static sc::Data R;       // не обнуляється при перезапуску; при зникненні живлення — сміття, CRC це ловить
static int64_t lastUs = 0;

static int32_t dayOf(time_t t) {
  struct tm a;
  localtime_r(&t, &a);
  return sc::daysFromCivil(a.tm_year + 1900, (unsigned)(a.tm_mon + 1), (unsigned)a.tm_mday);
}
static int32_t today() { return dayOf(time(nullptr)); }
static uint32_t epochOf(int64_t us) { return (uint32_t)(time(nullptr) - (time_t)((nowUs() - us) / 1000000)); }
static String clockAt(uint32_t t, bool withDate) {
  time_t x = t;
  struct tm a;
  localtime_r(&x, &a);
  char b[24];
  strftime(b, sizeof b, withDate ? "%d.%m %H:%M" : "%H:%M", &a);
  return b;
}
static String dateOf(int32_t day) {                 // «17.11» для номера дня
  time_t t = (time_t)day * 86400 + 12 * 3600;
  struct tm a;
  gmtime_r(&t, &a);
  char b[8];
  strftime(b, sizeof b, "%d.%m", &a);
  return b;
}

void statsBegin() {
  bool ok = sc::valid(R);
  if (!ok) sc::init(R);
  lastUs = nowUs();
  logEvent(ok ? String("Статистика світла: відновлено з пам'яті") + (R.since ? " (рахую з " + clockAt(R.since, true) + ")" : "")
              : String("Статистика світла: почато заново (пам'ять порожня — Сторож був без живлення)"));
  if (ok) {
    int f = sc::repairOutages(R, [](uint32_t t) { return dayOf((time_t)t); });
    if (f) {
      sc::seal(R);
      logEvent("Статистика світла: виправлено записів відключень — " + String(f) + " (одне відключення, записане кількома після перезапусків)");
    }
  }
}

static String line(const char* name, const sc::Sum& s, bool always) {
  if (!always && !s.on && !s.off && !s.unk) return String();
  String r = String("\n<b>") + name + "</b>: 🟢 " + fmtHours(s.on) + " · 🔴 " + fmtHours(s.off) + " · відключень " + String(s.outs);
  if (s.unk >= 60) r += " · ⚪ без даних " + fmtHours(s.unk);
  return r;
}

static String daySummary(int32_t day, const sc::Sum& s) {
  String r = "📈 <b>Підсумок за " + dateOf(day) + "</b>\n" + objIco() + htmlEsc(S.objName) +
             "\n🟢 Світло було " + fmtHours(s.on) + "\n🔴 Не було " + fmtHours(s.off) + " · відключень " + String(s.outs);
  if (s.unk >= 60) r += "\n⚪ Без даних " + fmtHours(s.unk);
  if (R.since && dayOf(R.since) == day) r += "\n<i>Облік із " + clockAt(R.since, false) + "</i>";
  return r;
}

static void dailySummary(int32_t d) {
  if (!S.sumOn || R.lastSumDay == d) return;
  time_t n = time(nullptr);
  struct tm a;
  localtime_r(&n, &a);
  if (a.tm_hour < S.sumHour) return;
  R.lastSumDay = d;
  sc::Sum s = sc::sum(R, d - 1, d - 1);
  if (!s.on && !s.off && !s.unk) return;            // за вчора обліку немає
  tgBroadcast(nowUs(), daySummary(d - 1, s), true, RF_SUM); // без звуку
}

void statsTick(int st) {
  int64_t now = nowUs();
  uint32_t sec = (uint32_t)((now - lastUs) / 1000000);
  if (!sec) return;
  lastUs += (int64_t)sec * 1000000;
  if (!clockValid()) {                              // годинника ще немає — секунди чекають
    (st > 0 ? R.pendOn : st == 0 ? R.pendOff : R.pendUnk) += sec;
    sc::seal(R);
    return;
  }
  int32_t d = today();
  if (!R.since) R.since = (uint32_t)time(nullptr) - R.pendOn - R.pendOff - R.pendUnk - sec;
  if (R.pendOn || R.pendOff || R.pendUnk) {
    sc::add(R, d, 1, R.pendOn);
    sc::add(R, d, 0, R.pendOff);
    sc::add(R, d, -1, R.pendUnk);
    R.pendOn = R.pendOff = R.pendUnk = 0;
  }
  sc::add(R, d, st, sec);
  dailySummary(d);
  sc::seal(R);
}

void statsTransition(int from, int to, int64_t atUs, bool approx) {
  if (!clockValid()) return;
  int32_t d = today();
  uint32_t lag = (uint32_t)((nowUs() - atUs) / 1000000);   // перехід визнано із затримкою — ці секунди вже в старому стані
  if (from != -1 && lag) sc::move(R, d, from, to, lag);
  uint32_t at = epochOf(atUs);
  if (to == 0) sc::outageStart(R, dayOf(at), at, approx);       // 1.0.29: те саме відключення ще триває — нового не почне
  else if (to == 1) sc::outageEnd(R, at);   // 1.0.29: і з «невідомо» (світло повернулось за час перезапуску) — інакше «триває» назавжди
  sc::seal(R);
}

String statsReport() {
  String r = "📈 <b>Статистика світла</b>\n" + objIco() + htmlEsc(S.objName);
  if (!clockValid()) return r + "\nГодинник ще не встановлено — статистика буде за хвилину.";
  int32_t d = today();
  r += line("Сьогодні", sc::sum(R, d, d), true);
  r += line("Вчора", sc::sum(R, d - 1, d - 1), false);
  if (R.since && dayOf(R.since) < d) {          // облік довший за добу — є сенс у тижні й місяці
    r += line("7 днів", sc::sum(R, d - 6, d), false);
    r += line("30 днів", sc::sum(R, d - 29, d), false);
  }
  uint32_t now = (uint32_t)time(nullptr);
  String outs;
  for (int i = 0; i < 6; i++) {
    const sc::OutRec* o = sc::outage(R, i);
    if (!o) break;
    String from = (o->approx ? "≥ " : "") + clockAt(o->start, true);
    if (o->end) outs += "\n🔴 " + from + "–" + clockAt(o->end, dayOf(o->end) != dayOf(o->start)) + " · " + fmtHours(o->end - o->start);
    else outs += "\n🔴 з " + from + " · триває " + fmtHours(now - o->start);
  }
  r += outs.length() ? "\n\n<b>Останні відключення</b>" + outs : String("\n\nВідключень за час обліку не було.");
  if (R.since) r += "\n\n<i>Облік з " + clockAt(R.since, true) + ". Зникне живлення Сторожа — облік почнеться заново; щоденні підсумки лишаються в цьому чаті.</i>";
  return r;
}

String statsJson() {
  if (!clockValid()) return "{\"ok\":false}";
  int32_t d = today();
  sc::Sum s = sc::sum(R, d, d);
  return "{\"ok\":true,\"since\":\"" + (R.since ? clockAt(R.since, true) : String()) + "\",\"on\":" + String(s.on) + ",\"off\":" + String(s.off) +
         ",\"unk\":" + String(s.unk) + ",\"outs\":" + String(s.outs) + ",\"text\":\"" + jsonEsc(stripTags(statsReport())) + "\"}";
}
