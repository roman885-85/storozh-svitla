#include "yasno.h"
#include "yasno_core.h"
#include "settings.h"
#include "bridge.h"
#include "watch.h"
#include "tg.h"
#include "util.h"
#include "certs.h"
#include "lcd.h"
#include "mem.h"
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <memory>
#include <new>

static SemaphoreHandle_t sm = nullptr;
static YQueue cur;                      // останній вдалий графік своєї черги (під sm)
static bool have = false;
static int64_t gotUs = 0, tryUs = 0, nextUs = 0, errUs = 0;
static String lastErr;
static volatile bool kick = true;
static int fails = 0;
static uint32_t version = 0;            // +1 з кожним вдалим запитом — schLoop бачить свіжі дані
static String haveFor;                  // для якої черги/області дані (налаштування могли змінитись)
static uint32_t lastLow = 0, lastMs = 0; // 1.0.21: найменше вільної пам'яті й тривалість останнього запиту

const char* schDsoName(int region, int dso) {
  if (region == 3 && dso == 301) return "ДнЕМ";
  if (region == 3 && dso == 303) return "ЦЕК";
  if (region == 25 && dso == 902) return "ДТЕК Київські електромережі";
  return "оператор";
}
static String key() { return String(S.schRegion) + "/" + S.schDso + "/" + S.schQueue; }

void schBegin() { sm = xSemaphoreCreateMutex(); }
void schReload() {
  xSemaphoreTake(sm, portMAX_DELAY);
  if (haveFor != key()) { have = false; fails = 0; lastErr = ""; }
  kick = true;
  xSemaphoreGive(sm);
}
void schKick() { kick = true; }

// ---------- запит (задача Telegram) ----------
//  Потік для HTTPClient::writeToStream: усе, що приходить, — у розбір; читати з нього нічого.
class YSink : public Stream {
 public:
  explicit YSink(YasnoStream& y) : ys(y) {}
  size_t write(uint8_t c) override { ys.feed(&c, 1); return 1; }
  size_t write(const uint8_t* b, size_t n) override { ys.feed(b, n); return n; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
 private:
  YasnoStream& ys;
};

void schNetStep() {
  if (!S.schOn || !S.schQueue.length() || !staUp()) return;
  int64_t now = nowUs();
  if (!kick && now < nextUs) return;
  //  Telegram у цій самій задачі — разом два шифровані з'єднання не бувають; буфери шифрування — із запасу TLS.
  //  1.0.38: поріг — за справжніми цифрами (mem.h). Старий («вільно ≥ 64 КБ» разом з ~32 КБ пам'яті команд) лишився
  //  з часів до запасу TLS і після 1.0.32 пропускав запит майже лише одразу після запуску: графік годинами не оновлювався.
  if (memFree() < 16000 || memBlock() < 4000) {
    nextUs = now + 60LL * 1000000;
    xSemaphoreTake(sm, portMAX_DELAY);
    lastErr = "мало пам'яті для запиту, спробую за хвилину";
    errUs = now;
    xSemaphoreGive(sm);
    return;
  }
  kick = false;
  tryUs = now;
  String k = key();
  String url = "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/" + String(S.schRegion) + "/dsos/" + String(S.schDso) + "/planned-outages";
  //  1.0.37 (власник: «функцию отключения для ясно, для экономии памяти и повышения стабильности»): буфер розбору
  //  (2,1 КБ) і графік (0,5 КБ) — у купі лише на час запиту, а не постійно; вимкнений YASNO їх не тримає зовсім.
  std::unique_ptr<YasnoStream> ysp(new (std::nothrow) YasnoStream);
  std::unique_ptr<YQueue> yp(new (std::nothrow) YQueue);
  if (!ysp || !yp) {
    nextUs = now + 60LL * 1000000;
    xSemaphoreTake(sm, portMAX_DELAY);
    lastErr = "мало пам'яті для запиту, спробую за хвилину";
    errUs = now;
    xSemaphoreGive(sm);
    return;
  }
  YasnoStream& ys = *ysp;
  ys.begin(S.schQueue.c_str());
  YSink sink(ys);
  int code = -100, wr = 0;
  //  1.0.21: найменше вільної пам'яті саме за час цього запиту (для сторінки) — монітор «місцевого мінімуму» ESP-IDF
  bool mon = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
  int64_t t0 = nowUs();
  {
    tgNetPause();                        // 1.0.32: з'єднання з Telegram звільняє запас пам'яті TLS — він потрібен тут
    NetworkClientSecure c;
    c.setCACert(YASNO_CA);
    c.setHandshakeTimeout(15);           // 1.0.29: типово 120 с — задача Telegram (і графік у ній) стояла б хвилинами
    HTTPClient h;
    h.setConnectTimeout(8000);
    h.setTimeout(10000);
    if (h.begin(c, url)) {
      code = h.GET();
      if (code == 200) wr = h.writeToStream(&sink);
      h.end();
    }
  }                                      // з'єднання закрито й пам'ять звільнено до будь-чого іншого
  uint32_t low = mon ? heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : 0;   // 1.0.38: без пам'яті команд
  if (mon) heap_caps_monitor_local_minimum_free_size_stop();
  lastLow = low;
  lastMs = (uint32_t)((nowUs() - t0) / 1000);
  YQueue& y = *yp;
  bool ok = code == 200 && wr > 0 && ys.finish(y);
  String err;
  if (!ok) {
    if (code == 200 && wr > 0) err = "черги " + S.schQueue + " у відповіді YASNO немає";
    else if (code == 200) err = wr == -8 ? String("бракує пам'яті, щоб прочитати відповідь (-8)") : "відповідь YASNO обірвалась (" + String(wr) + ")";
    else if (code < 0) err = "немає зв'язку з app.yasno.ua (" + String(code) + ")";
    else err = "YASNO відповів HTTP " + String(code);
  }
  int64_t t = nowUs();
  bool told = false;
  xSemaphoreTake(sm, portMAX_DELAY);
  if (ok) {
    told = fails >= 2 || !have;
    cur = y;
    have = true;
    haveFor = k;
    gotUs = t;
    fails = 0;
    lastErr = "";
    version++;
    nextUs = t + 15LL * 60 * 1000000;
  } else {
    told = err != lastErr;
    fails++;
    lastErr = err;
    errUs = t;
    static const int back[] = {1, 2, 5, 10, 15};
    nextUs = t + (int64_t)back[fails > 5 ? 4 : fails - 1] * 60 * 1000000;
  }
  xSemaphoreGive(sm);
  if (told) logEvent(ok ? "Графік YASNO: отримано, черга " + S.schQueue + " (" + String((unsigned)ys.total()) + " Б)" : "Графік YASNO: " + err);
}

// ---------- час і шкала на дві доби ----------
static bool today(struct tm& t) {
  if (!clockValid()) return false;
  time_t n = time(nullptr);
  localtime_r(&n, &t);
  return true;
}
static void dateStr(int plusDays, char* out) {          // «YYYY-MM-DD» сьогодні + plusDays (полудень — без пасток переходу на літній час)
  time_t n = time(nullptr);
  struct tm t;
  localtime_r(&n, &t);
  t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
  time_t noon = mktime(&t) + (time_t)plusDays * 86400;
  localtime_r(&noon, &t);
  strftime(out, 11, "%Y-%m-%d", &t);
}
static String hm(int m) {                               // 0…2880 → «HH:MM» (1440 → «24:00»)
  int mm = m >= 1440 && m != 1440 ? m - 1440 : m;
  char b[8];
  snprintf(b, sizeof b, "%02d:%02d", mm / 60, mm % 60);
  return b;
}
static String dm(const char* ymd) { return String(ymd + 8, 2) + "." + String(ymd + 5, 2); }   // «2026-11-18» → «18.11»

struct Seg { int16_t s, e; uint8_t c; };                // абсолютні хвилини від сьогоднішньої півночі (0…2880), c: 0 світло, 1 відкл., 2 можливе
struct Line {
  bool ok = false;                                      // є чинний графік на сьогодні
  int dToday = -1, dTom = -1;                           // індекси днів у YQueue
  uint8_t stToday = 0, stTom = 0;
  int end = 0;                                          // докуди відомо: 1440 або 2880
  int n = 0;
  Seg sg[64];
};
static void buildLine(const YQueue& y, Line& L) {
  L = Line();
  char d0[11], d1[11];
  dateStr(0, d0);
  dateStr(1, d1);
  for (int i = 0; i < 2; i++) {
    if (!y.d[i].ok) continue;
    if (!strcmp(y.d[i].date, d0)) L.dToday = i;
    if (!strcmp(y.d[i].date, d1)) L.dTom = i;
  }
  if (L.dToday >= 0) L.stToday = y.d[L.dToday].st;
  if (L.dTom >= 0) L.stTom = y.d[L.dTom].st;
  auto add = [&](const YDay& d, int off) {
    for (int k = 0; k < d.n && L.n < 64; k++) {
      Seg s{(int16_t)(d.sl[k].s + off), (int16_t)(d.sl[k].e + off), d.sl[k].t};
      if (L.n && L.sg[L.n - 1].c == s.c && L.sg[L.n - 1].e == s.s) L.sg[L.n - 1].e = s.e;   // зшити сусідні, і через північ
      else L.sg[L.n++] = s;
    }
  };
  if (L.dToday >= 0 && L.stToday == 1 && y.d[L.dToday].n) {
    add(y.d[L.dToday], 0);
    L.ok = true;
    L.end = 1440;
    if (L.dTom >= 0 && L.stTom == 1 && y.d[L.dTom].n) { add(y.d[L.dTom], 1440); L.end = 2880; }
  }
}
static int segAt(const Line& L, int m) {
  for (int i = 0; i < L.n; i++) if (L.sg[i].s <= m && m < L.sg[i].e) return i;
  return -1;
}
static int nextOff(const Line& L, int from) {         // перший відрізок відключення, що починається після from
  for (int i = 0; i < L.n; i++) if (L.sg[i].c == 1 && L.sg[i].s > from) return i;
  return -1;
}
static int nowMin() { struct tm t; if (!today(t)) return -1; return t.tm_hour * 60 + t.tm_min; }
static String endText(const Line& L, int e) { return e >= L.end && L.end < 2880 ? hm(e) + " (далі графіка ще немає)" : hm(e) + (e > 1440 ? " завтра" : ""); }
static String whenText(int m) { return m >= 1440 ? "завтра о " + hm(m) : "о " + hm(m); }
static String leftText(int mins) { return fmtDur((int64_t)mins * 60); }
static bool snapshot(YQueue& y, bool& fresh) {
  xSemaphoreTake(sm, portMAX_DELAY);
  bool h = have && haveFor == key();
  if (h) y = cur;
  fresh = h && nowUs() - gotUs < 6LL * 3600 * 1000000;   // старіше 6 год — уже не віримо
  xSemaphoreGive(sm);
  return h && fresh;
}
static String qName() { return "черга " + htmlEsc(S.schQueue); }
//  Одна копія графіка на основний цикл (усі sch* нижче, крім schNetStep, працюють лише там): по копії на функцію
//  забирало б ~3 КБ, яких бракуватиме шифрованим з'єднанням.
static YQueue ym;

bool schNext(bool& offNow, int& atMin, bool& atTomorrow, int& leftMin, int& endMin) {
  if (!S.schOn) return false;
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) return false;
  Line L;
  buildLine(y, L);
  int m = nowMin();
  if (!L.ok || m < 0) return false;
  int i = segAt(L, m);
  if (i < 0) return false;
  offNow = L.sg[i].c == 1;
  int to = offNow ? L.sg[i].e : (nextOff(L, m) >= 0 ? L.sg[nextOff(L, m)].s : -1);
  if (to < 0 || (offNow && to >= L.end && L.end < 2880)) return false;
  atMin = to >= 1440 ? to - 1440 : to;
  atTomorrow = to >= 1440;
  leftMin = to - m;
  endMin = -1;
  if (!offNow) {
    int k = nextOff(L, m);
    int e = k >= 0 ? L.sg[k].e : -1;
    endMin = e < 0 || (e >= L.end && L.end < 2880) ? -1 : (e >= 1440 ? e - 1440 : e);
  }
  return true;
}

//  Біжучий рядок дисплея (1.0.26, власник: «на дисплее не отображается график (может бегущей строкой…)»): графік простим
//  текстом — лише знаки шрифту дисплея, без емодзі (їх там немає — малювались би «?»)
static String dayPlain(const YQueue& y, int idx) {
  if (idx < 0) return "ще немає";
  const YDay& d = y.d[idx];
  if (d.st == 2) return "ще не оприлюднено";
  if (d.st == 3) return "аварійні відключення, графік не діє";
  if (d.st != 1) return "немає даних";
  String o;
  for (int k = 0; k < d.n; k++) {
    if (d.sl[k].t == 0) continue;
    if (o.length()) o += ", ";
    if (d.sl[k].t == 2) o += "можливо ";
    o += hm(d.sl[k].s) + "–" + hm(d.sl[k].e);
  }
  return o.length() ? o : String("без відключень");
}
String schLcdText() {
  if (!S.schOn || !S.schQueue.length()) return String();
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) return "Графік YASNO: даних поки немає";
  Line L;
  buildLine(y, L);
  String r = "Черга " + S.schQueue + " · сьогодні: " + dayPlain(y, L.dToday) + " · завтра: " + dayPlain(y, L.dTom);
  bool offNow, tom;
  int at, left, end;
  if (schNext(offNow, at, tom, left, end)) {
    String w = String(tom ? "завтра о " : "о ") + hm(at);
    r += String(offNow ? " · світло за графіком " : " · наступне відключення ") + w + ", через " + fmtDur((int64_t)left * 60);
  }
  return r;
}

String schStatusLine() {
  if (!S.schOn) return String();
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) {
    xSemaphoreTake(sm, portMAX_DELAY);
    String e = lastErr;
    xSemaphoreGive(sm);
    return "\n🗓 Графік: даних з YASNO немає" + (e.length() ? " (" + htmlEsc(e) + ")" : String());
  }
  Line L;
  buildLine(y, L);
  int m = nowMin();
  if (L.stToday == 3) return "\n🗓 YASNO: аварійні відключення — графік зараз не діє";
  if (!L.ok || m < 0) return "\n🗓 Графіка на сьогодні немає (" + qName() + ")";
  int i = segAt(L, m);
  if (i >= 0 && L.sg[i].c == 1) return "\n🗓 За графіком зараз відключення до " + endText(L, L.sg[i].e) + " (" + qName() + ")";
  int k = nextOff(L, m);
  if (k < 0) return String("\n🗓 За графіком відключень ") + (L.end == 2880 ? "до кінця завтра" : "до кінця дня") + " немає (" + qName() + ")";
  return "\n🗓 Наступне відключення за графіком " + whenText(L.sg[k].s) + " (через " + leftText(L.sg[k].s - m) + "), до " + endText(L, L.sg[k].e) + " · " + qName();
}

String schLostNote() {
  if (!S.schOn) return String();
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) return String();
  Line L;
  buildLine(y, L);
  int m = nowMin();
  if (L.stToday == 3) return "\n⚠️ YASNO: аварійні відключення";
  if (!L.ok || m < 0) return String();
  //  ±15 хв: світло вимикають і вмикають не хвилина в хвилину
  for (int i = 0; i < L.n; i++)
    if (L.sg[i].c == 1 && L.sg[i].s - 15 <= m && m < L.sg[i].e + 15)
      return "\n🗓 За графіком: відключення до " + endText(L, L.sg[i].e) + " (" + qName() + ")";
  return "\n❓ За графіком світло мало бути (" + qName() + ") — можливо, аварійне відключення";
}

String schBackNote() {
  if (!S.schOn) return String();
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) return String();
  Line L;
  buildLine(y, L);
  int m = nowMin();
  if (!L.ok || m < 0) return String();
  int i = segAt(L, m);
  if (i >= 0 && L.sg[i].c == 1 && L.sg[i].e - m > 15) return "\n🗓 За графіком відключення мало тривати до " + endText(L, L.sg[i].e) + " — світло дали раніше";
  int k = nextOff(L, m + 15);
  if (k < 0) return String();
  return "\n🗓 Наступне відключення за графіком " + whenText(L.sg[k].s) + " (через " + leftText(L.sg[k].s - m) + ")";
}

static String dayText(const YQueue& y, int idx, const char* ymd) {
  if (idx < 0) return "немає даних";
  const YDay& d = y.d[idx];
  if (d.st == 2) return "графік ще не оприлюднено";
  if (d.st == 3) return "аварійні відключення — графік не діє";
  if (d.st != 1) return "стан «" + htmlEsc(d.stRaw) + "»";
  String o;
  int offMin = 0;
  for (int k = 0; k < d.n; k++) {
    if (d.sl[k].t == 0) continue;
    if (o.length()) o += " · ";
    o += String(d.sl[k].t == 1 ? "🔴 " : "🟡 можливо ") + hm(d.sl[k].s) + "–" + hm(d.sl[k].e);
    if (d.sl[k].t == 1) offMin += d.sl[k].e - d.sl[k].s;
  }
  if (!o.length()) return "🟢 без відключень";
  return o + "\n   без світла за графіком " + fmtDur((int64_t)offMin * 60);
}

//  «2026-11-18T19:32:53+00:00» (UTC) → місцевий «18.11 о 21:32»
static String updText(const char* u) {
  int Y, Mo, D, h, mi, s;
  if (sscanf(u, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &s) != 6) return String();
  int y = Y - (Mo <= 2);
  int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400, mp = (Mo + 9) % 12, doy = (153 * mp + 2) / 5 + D - 1;
  long days = (long)era * 146097 + (long)(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
  time_t t = (time_t)days * 86400 + h * 3600 + mi * 60 + s;
  struct tm a, b;
  localtime_r(&t, &a);
  time_t n = time(nullptr);
  localtime_r(&n, &b);
  char buf[24];
  strftime(buf, sizeof buf, a.tm_yday == b.tm_yday && a.tm_year == b.tm_year ? "о %H:%M" : "%d.%m о %H:%M", &a);
  return buf;
}

String schReport() {
  String r = "🗓 <b>Графік відключень</b>\n" + objIco() + htmlEsc(S.objName);
  if (!S.schOn || !S.schQueue.length())
    return r + "\nГрафік не налаштовано. Адміністратор вмикає його на сторінці Сторожа: «Налаштування → Графік відключень».";
  r += " · " + qName() + " · " + schDsoName(S.schRegion, S.schDso);
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) {
    xSemaphoreTake(sm, portMAX_DELAY);
    String e = lastErr;
    xSemaphoreGive(sm);
    return r + "\nДаних з YASNO поки немає" + (e.length() ? ": " + htmlEsc(e) : String(".")) + "\nСпробую ще раз за кілька хвилин.";
  }
  Line L;
  buildLine(y, L);
  char d0[11], d1[11];
  dateStr(0, d0);
  dateStr(1, d1);
  r += "\n\n<b>Сьогодні, " + dm(d0) + "</b>: " + dayText(y, L.dToday, d0);
  r += "\n<b>Завтра, " + dm(d1) + "</b>: " + dayText(y, L.dTom, d1);
  String s = schStatusLine();
  if (s.length()) r += "\n" + s;
  xSemaphoreTake(sm, portMAX_DELAY);
  int64_t got = gotUs;
  xSemaphoreGive(sm);
  String u = updText(y.upd);
  r += "\n\n<i>YASNO оновив " + (u.length() ? u : String("—")) + ", Сторож отримав " + fmtDur((nowUs() - got) / 1000000) + " тому</i>";
  return r;
}

// ---------- сповіщення (основний цикл) ----------
//  1.0.24: з чим порівнювати свіжий графік — ще й у RTC-пам'яті: переживає перезапуск і оновлення прошивки (зникнення
//  живлення — ні). До того перший запит після запуску лише запам'ятовував графік: зміна, що припала на перезапуск чи
//  оновлення Сторожа, губилась мовчки. (19.11.2026 «Графік на сьогодні змінився» прийшло — зміну побачено до оновлення.)
struct SchMem {
  uint32_t keyH;                         // для якої черги (область/оператор/черга); 0 — нічого
  char tomDay[11], todDay[11];
  uint32_t tomHash, todHash;
  uint8_t todSt;
  int32_t warnedAt;                      // попереджене відключення, хвилина від 1970 р.; -1 — ні
};
struct RtcSchMem { uint32_t magic, crc; SchMem m; };
RTC_NOINIT_ATTR static RtcSchMem RY;     // yasno.cpp у збірці після web.cpp — адреси R, RQ, RS не зсуваються (звіряти nm)
static const uint32_t RY_MAGIC = 0x59534D31;   // «YSM1»
static void rySave(const SchMem& m) {
  RY.m = m;
  RY.crc = esp_rom_crc32_le(0, (const uint8_t*)&RY.m, sizeof RY.m);
  RY.magic = RY_MAGIC;
}
static bool ryLoad(SchMem& m) {
  if (RY.magic != RY_MAGIC || RY.crc != esp_rom_crc32_le(0, (const uint8_t*)&RY.m, sizeof RY.m)) { RY.magic = 0; return false; }
  m = RY.m;
  m.tomDay[10] = m.todDay[10] = 0;
  return true;
}
static uint32_t keyHash() {
  String k = key();
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < k.length(); i++) h = (h ^ (uint8_t)k[i]) * 16777619u;
  return h | 1;
}
static uint32_t hashDay(const YDay& d) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(d.st);
  for (int k = 0; k < d.n; k++) { mix(d.sl[k].s); mix(d.sl[k].e); mix(d.sl[k].t); }
  return h;
}
static String obj() { return htmlEsc(S.objName.length() ? S.objName : String("Об'єкт")); }

void schLoop() {
  static int64_t last = 0;
  int64_t now = nowUs();
  if (now - last < 10LL * 1000000) return;
  last = now;
  if (!S.schOn || !S.schQueue.length() || !clockValid()) return;
  YQueue& y = ym;
  bool fresh;
  if (!snapshot(y, fresh)) return;
  xSemaphoreTake(sm, portMAX_DELAY);
  uint32_t ver = version;
  xSemaphoreGive(sm);
  Line L;
  buildLine(y, L);
  int m = nowMin();
  char d0[11], d1[11];
  dateStr(0, d0);
  dateStr(1, d1);

  //  0) З чим порівнювати (1.0.24): після перезапуску — з RTC, якщо там та сама черга; змінили чергу чи оператора —
  //  перший графік нової черги лише запам'ятовуємо (1.0.23): порівнювати його зі старою чергою не можна.
  static SchMem M;
  static bool base = false, tried = false;
  uint32_t kh = keyHash();
  if (!tried) {
    tried = true;
    if (ryLoad(M) && M.keyH == kh) {
      base = true;
      logEvent("Графік YASNO: з чим порівнювати — відновлено з пам'яті (зміни за час перезапуску теж прийдуть)");
    }
  }
  if (M.keyH != kh) { base = false; M = SchMem(); M.keyH = kh; M.warnedAt = -1; }

  //  1) Попередження за S.schWarn хв до відключення. Світла вже немає — не попереджаємо.
  //  Відключення запам'ятовуємо хвилиною від 1970 р., а не «хвилиною доби»: о 23:50 попередили про 00:30 — після півночі
  //  це те саме відключення, другого попередження не треба.
  if (S.schWarn && L.ok && m >= 0) {
    int k = nextOff(L, m);
    if (k >= 0) {
      int st = L.sg[k].s, left = st - m;
      long at = (long)(time(nullptr) / 60) + left;
      bool already = M.warnedAt >= 0 && labs((long)M.warnedAt - at) <= 2;
      if (left > 0 && left <= S.schWarn && !already) {
        M.warnedAt = (int32_t)at;
        rySave(M);
        if (watchGrid() != 0) {
          tgBroadcast(now, "⏰ <b>За графіком відключення " + whenText(st) + "</b>\n" + objIco() + obj() + " · " + qName() +
                           "\n🗓 через " + leftText(left) + ", до " + endText(L, L.sg[k].e), false, RF_SCHED);
          lcdAlert(LCD_A_WARN, "УВАГА: " + whenText(st) + " відключення за графіком, до " + endText(L, L.sg[k].e), left * 60);   // 1.0.26
        }
      }
    }
  }

  //  2) Новий графік на завтра, зміни на сьогодні, аварійні — лише після свіжого запиту; перший після запуску без
  //  відновленого з RTC стану (після зникнення живлення) лише запам'ятовується.
  static uint32_t seenVer = 0;
  if (ver == seenVer) return;
  seenVer = ver;
  uint32_t th = L.dTom >= 0 ? hashDay(y.d[L.dTom]) : 0, dh = L.dToday >= 0 ? hashDay(y.d[L.dToday]) : 0;
  uint8_t ds = L.dToday >= 0 ? y.d[L.dToday].st : 0;
  if (base && S.schTom) {
    if (L.dTom >= 0 && L.stTom == 1 && (strcmp(M.tomDay, d1) || th != M.tomHash))
      tgBroadcast(now, "🗓 <b>Графік на завтра, " + dm(d1) + (strcmp(M.tomDay, d1) ? String("</b>") : String(" — змінився</b>")) + "\n" + objIco() + obj() + " · " + qName() +
                       "\n" + dayText(y, L.dTom, d1), true, RF_SCHED);
    if (L.dToday >= 0 && !strcmp(M.todDay, d0) && dh != M.todHash) {
      if (ds == 3 && M.todSt != 3) tgBroadcast(now, "⚠️ <b>YASNO: аварійні відключення</b>\n" + objIco() + obj() + " · " + qName() + "\nГрафік зараз не діє: світло можуть вимкнути будь-коли.", false, RF_SCHED);
      else if (ds == 1 && M.todSt == 3) tgBroadcast(now, "🗓 <b>Графік знову діє</b>\n" + objIco() + obj() + " · " + qName() + "\nСьогодні: " + dayText(y, L.dToday, d0), false, RF_SCHED);
      else if (ds == 1) tgBroadcast(now, "🗓 <b>Графік на сьогодні змінився</b>\n" + objIco() + obj() + " · " + qName() + "\n" + dayText(y, L.dToday, d0), true, RF_SCHED);
    }
  }
  base = true;
  if (L.dTom >= 0 && L.stTom == 1) { strlcpy(M.tomDay, d1, sizeof M.tomDay); M.tomHash = th; }
  if (L.dToday >= 0) { strlcpy(M.todDay, d0, sizeof M.todDay); M.todHash = dh; M.todSt = ds; }
  rySave(M);
}

//  Рядок для «🛠 Сторож» у боті (1.0.23)
String schDiagLine() {
  if (!S.schOn || !S.schQueue.length()) return "\n🗓 YASNO: вимкнено";
  int64_t now = nowUs();
  xSemaphoreTake(sm, portMAX_DELAY);
  String r = "\n🗓 YASNO, черга " + htmlEsc(S.schQueue) + ": ";
  if (have && haveFor == key()) r += "отримано " + fmtDur((now - gotUs) / 1000000) + " тому";
  else r += "ще не отримано";
  if (lastMs) r += ", запит " + String(lastMs) + " мс, пам'ять не нижче " + String(lastLow / 1024) + " КБ";
  if (lastErr.length()) r += "\n⚠️ " + htmlEsc(lastErr);
  xSemaphoreGive(sm);
  return r;
}

String schJson() {
  String o = "{\"on\":" + String(S.schOn ? "true" : "false") + ",\"q\":\"" + jsonEsc(S.schQueue) + "\",\"reg\":" + S.schRegion + ",\"dso\":" + S.schDso +
             ",\"dsoName\":\"" + jsonEsc(schDsoName(S.schRegion, S.schDso)) + "\",\"warn\":" + S.schWarn + ",\"tom\":" + (S.schTom ? "true" : "false");
  YQueue& y = ym;
  bool fresh;
  bool h = snapshot(y, fresh);
  int64_t now = nowUs();
  xSemaphoreTake(sm, portMAX_DELAY);
  o += ",\"have\":" + String(h ? "true" : "false") + ",\"ago\":" + (gotUs && have ? String((long)((now - gotUs) / 1000000)) : String("null")) +
       ",\"err\":\"" + jsonEsc(lastErr) + "\",\"errAgo\":" + (errUs ? String((long)((now - errUs) / 1000000)) : String("null")) +
       ",\"next\":" + (nextUs > now ? String((long)((nextUs - now) / 1000000)) : String("0")) +
       ",\"low\":" + String(lastLow) + ",\"ms\":" + String(lastMs);
  xSemaphoreGive(sm);
  if (h) {
    Line L;
    buildLine(y, L);
    char d0[11], d1[11];
    dateStr(0, d0);
    dateStr(1, d1);
    o += ",\"upd\":\"" + jsonEsc(updText(y.upd)) + "\",\"days\":[";
    const int idx[2] = {L.dToday, L.dTom};
    const char* ds[2] = {d0, d1};
    for (int i = 0; i < 2; i++) {
      if (i) o += ",";
      o += "{\"date\":\"" + dm(ds[i]) + "\",\"have\":" + (idx[i] >= 0 ? "true" : "false");
      if (idx[i] >= 0) {
        const YDay& d = y.d[idx[i]];
        o += ",\"st\":" + String(d.st) + ",\"stRaw\":\"" + jsonEsc(d.stRaw) + "\",\"sl\":[";
        for (int k = 0; k < d.n; k++) o += String(k ? "," : "") + "[" + d.sl[k].s + "," + d.sl[k].e + "," + d.sl[k].t + "]";
        o += "]";
      }
      o += "}";
    }
    o += "],\"line\":\"" + jsonEsc(stripTags(schStatusLine()).substring(1)) + "\"";
    int m = nowMin();
    if (m >= 0) o += ",\"now\":" + String(m);
  }
  return o + "}";
}
