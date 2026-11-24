#pragma once
/*  Розбір відповіді YASNO «planned-outages» потоком (1.0.20). Без Arduino — той самий код перевіряє
    tests/yasno/run.sh на комп'ютері на справжній відповіді.
    Відповідь: {"1.1":{"today":{"slots":[{"start":0,"end":90,"type":"NotPlanned"},…],"date":"2026-11-18T00:00:00+02:00",
    "status":"ScheduleApplies"},"tomorrow":{…},"updatedOn":"…"},"1.2":{…},…} — 12 черг, ~7 КБ. Тримаємо лише свою чергу:
    її об'єкт (~0,6 КБ) збираємо в буфер, решту пропускаємо, не зберігаючи.  */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

struct YSlot { int16_t s, e; uint8_t t; };      // хвилини від півночі; t: 0 — світло, 1 — відключення, 2 — можливе відключення
struct YDay {
  bool ok = false;
  uint8_t n = 0;
  YSlot sl[32];
  char date[11] = "";                           // «2026-11-18»
  uint8_t st = 0;                               // 0 — інше, 1 — ScheduleApplies, 2 — WaitingForSchedule, 3 — EmergencyShutdowns
  char stRaw[28] = "";
};
struct YQueue { bool found = false; YDay d[2]; char upd[26] = ""; };   // d[0] — today, d[1] — tomorrow у відповіді

class YasnoStream {
 public:
  void begin(const char* queue) {
    strncpy(want, queue, sizeof want - 1);
    want[sizeof want - 1] = 0;
    depth = 0; inStr = esc = false; keyLen = 0; lastKeyOk = false; pendingKey = false;
    capturing = false; capDepth = 0; capLen = 0; capOverflow = false; done = false; bytes = 0;
  }
  void feed(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) put((char)b[i]); bytes += n; }
  size_t total() const { return bytes; }
  //  true — свою чергу знайдено й розібрано (навіть якщо графіка в ній немає — тоді n = 0)
  bool finish(YQueue& out) {
    out = YQueue();
    if (!done || capOverflow) return false;
    cap[capLen] = 0;
    out.found = true;
    parseDay(cap, capLen, "\"today\"", out.d[0]);
    parseDay(cap, capLen, "\"tomorrow\"", out.d[1]);
    strVal(cap, 0, capLen, "\"updatedOn\"", out.upd, sizeof out.upd);
    return true;
  }

 private:
  char want[8];
  int depth;
  bool inStr, esc;
  char key[12];
  int keyLen;
  bool lastKeyOk, pendingKey;                   // рядок на рівні 1 закінчився; чекаємо «:», щоб знати, що це ключ
  bool capturing, capOverflow, done;
  int capDepth, capLen;
  size_t bytes;
  char cap[2048];

  void capPut(char c) {
    if (capLen < (int)sizeof cap - 1) cap[capLen++] = c;
    else capOverflow = true;
  }
  void put(char c) {
    if (done) return;
    if (capturing) {
      capPut(c);
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"') inStr = false;
        return;
      }
      if (c == '"') inStr = true;
      else if (c == '{' || c == '[') capDepth++;
      else if (c == '}' || c == ']') { if (--capDepth == 0) { capturing = false; done = true; } }
      return;
    }
    if (inStr) {
      if (esc) { esc = false; return; }
      if (c == '\\') { esc = true; return; }
      if (c == '"') {
        inStr = false;
        if (depth == 1) { key[keyLen < (int)sizeof key ? keyLen : (int)sizeof key - 1] = 0; pendingKey = true; lastKeyOk = keyLen < (int)sizeof key && !strcmp(key, want); }
        return;
      }
      if (depth == 1 && keyLen < (int)sizeof key) key[keyLen++] = c;
      return;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;
    if (c == '"') { inStr = true; keyLen = 0; if (depth != 1) pendingKey = false; return; }
    if (pendingKey && c == ':') {                // ключ першого рівня; потрібний — значення збираємо
      pendingKey = false;
      if (lastKeyOk) { capturing = true; capDepth = 0; capLen = 0; }
      return;
    }
    pendingKey = false;
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') depth--;
  }

  //  кінець об'єкта чи масиву, що починається в s[i] ('{' або '['), з урахуванням рядків; -1 — немає
  static int closeOf(const char* s, int i, int L) {
    int d = 0;
    bool str = false, e = false;
    for (; i < L; i++) {
      char c = s[i];
      if (str) { if (e) e = false; else if (c == '\\') e = true; else if (c == '"') str = false; continue; }
      if (c == '"') str = true;
      else if (c == '{' || c == '[') d++;
      else if (c == '}' || c == ']') { if (--d == 0) return i; }
    }
    return -1;
  }
  static int findIn(const char* s, int from, int to, const char* k) {
    int kl = strlen(k);
    for (int i = from; i + kl <= to; i++) if (!memcmp(s + i, k, kl)) return i;
    return -1;
  }
  //  позиція першого не-пробілу після «"ключ":»
  static int valAt(const char* s, int from, int to, const char* k) {
    int i = findIn(s, from, to, k);
    if (i < 0) return -1;
    i += strlen(k);
    while (i < to && (s[i] == ' ' || s[i] == ':')) i++;
    return i < to ? i : -1;
  }
  static bool strVal(const char* s, int from, int to, const char* k, char* out, int outN) {
    out[0] = 0;
    int i = valAt(s, from, to, k);
    if (i < 0 || s[i] != '"') return false;
    int o = 0;
    for (i++; i < to && s[i] != '"'; i++) { if (s[i] == '\\') i++; if (o < outN - 1) out[o++] = s[i]; }
    out[o] = 0;
    return true;
  }
  static bool numVal(const char* s, int from, int to, const char* k, long& v) {
    int i = valAt(s, from, to, k);
    if (i < 0) return false;
    char* e = nullptr;
    v = strtol(s + i, &e, 10);
    return e && e != s + i;
  }
  static void parseDay(const char* s, int L, const char* name, YDay& d) {
    d = YDay();
    int i = valAt(s, 0, L, name);
    if (i < 0 || s[i] != '{') return;
    int e = closeOf(s, i, L);
    if (e < 0) return;
    char full[32];
    if (strVal(s, i, e, "\"date\"", full, sizeof full)) { memcpy(d.date, full, 10); d.date[10] = 0; }
    if (strVal(s, i, e, "\"status\"", d.stRaw, sizeof d.stRaw))
      d.st = !strcmp(d.stRaw, "ScheduleApplies") ? 1 : !strcmp(d.stRaw, "WaitingForSchedule") ? 2 : !strcmp(d.stRaw, "EmergencyShutdowns") ? 3 : 0;
    int a = valAt(s, i, e, "\"slots\"");
    if (a >= 0 && s[a] == '[') {
      int ae = closeOf(s, a, e + 1);
      for (int p = a + 1; ae > 0 && p < ae && d.n < 32; p++) {
        if (s[p] != '{') continue;
        int oe = closeOf(s, p, ae);
        if (oe < 0) break;
        long st = -1, en = -1;
        char ty[24];
        if (numVal(s, p, oe, "\"start\"", st) && numVal(s, p, oe, "\"end\"", en) && st >= 0 && en > st && en <= 1440) {
          strVal(s, p, oe, "\"type\"", ty, sizeof ty);
          d.sl[d.n].s = (int16_t)st;
          d.sl[d.n].e = (int16_t)en;
          d.sl[d.n].t = !strcmp(ty, "Definite") ? 1 : !strcmp(ty, "NotPlanned") ? 0 : 2;
          d.n++;
        }
        p = oe;
      }
    }
    d.ok = d.date[0] != 0;
  }
};
