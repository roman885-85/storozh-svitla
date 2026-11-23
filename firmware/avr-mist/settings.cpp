#include "settings.h"
#include <Preferences.h>
#include <esp_mac.h>
#include <nvs.h>

Settings S;
static const char* NS = "avrmist";

static String macTail(int n) {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  String s;
  char b[3];
  for (int i = 6 - n; i < 6; i++) { snprintf(b, sizeof b, "%02x", m[i]); s += b; }
  return s;
}
String defaultApSsid() { String t = macTail(2); t.toUpperCase(); return "AVR-mist-" + t; }
String defaultPass()   { return "avr" + macTail(4); }

void settingsLoad() {
  Preferences p;
  bool ok = p.begin(NS, true);          // простору ще нема — працюємо на типових
  auto gs = [&](const char* k, const String& d) { return ok ? p.getString(k, d) : d; };
  auto gu = [&](const char* k, uint16_t d) { return ok ? p.getUShort(k, d) : d; };
  auto gb = [&](const char* k, bool d) { return ok ? p.getBool(k, d) : d; };
  S.objName     = gs("obj", "Об'єкт");          // 1.0.46: типова — без «Церкви»; збережена лишається
  //  1.0.47: значок. Ключа ще немає: плата з налаштуваннями старих версій (назву вже збережено) лишається з ⛪, як було
  //  до 1.0.47; нова плата — 📍. Сам Сторож при цьому нічого у флеш не пише — значок збережеться з наступним «Зберегти».
  S.objIcon     = ok && p.isKey("objIco") ? p.getString("objIco", "") : String(ok && p.isKey("obj") ? "⛪" : "📍");
  S.staSsid     = gs("staSsid", "");
  S.staPass     = gs("staPass", "");
  S.staBssid    = gs("staBssid", "");
  S.apSsid      = gs("apSsid", defaultApSsid());
  S.apPass      = gs("apPass", defaultPass());
  S.adminPass   = gs("admin", defaultPass());
  S.tgToken     = gs("tgToken", "");
  S.tgChats     = gs("tgChats", "");
  S.avrMac      = gs("avrMac", "");
  S.mayakOn     = gb("mayak", false);
  S.bmsOn       = gb("bmsOn", false);
  S.mdnsOn      = gb("mdnsOn", false);
  S.notifyStart = gb("nStart", true);
  S.offSec      = gu("offSec", 12);     // 1.0.29: було 30 (і ще +8 с на ping) — власник: «не больше 15»
  S.onSec       = gu("onSec", 10);
  S.lineOffSec  = gu("lOff", 15);
  S.lineOnSec   = gu("lOn", 10);
  S.lineWarnSec = gu("lWarn", 120);
  S.genRemMin   = gu("genRem", 10);
  S.sumOn       = gb("sumOn", true);
  S.sumHour     = (uint8_t)gu("sumHr", 8);
  S.ledOn   = gb("ledOn", false);
  S.ledLow  = gb("ledLow", false);
  S.ledG    = (uint8_t)gu("ledG", 18);
  S.ledR    = (uint8_t)gu("ledR", 19);
  S.ledBri  = (uint8_t)gu("ledBri", 100);
  S.ledBlink = gu("ledBlink", 1000);
  S.sndType = (uint8_t)gu("sndType", 0);
  S.sndPin  = (uint8_t)gu("sndPin", 23);
  S.dacPin  = (uint8_t)gu("dacPin", 25);
  S.sndVol  = (uint8_t)gu("sndVol", 70);
  S.sndLost = gb("sndLost", true);
  S.sndBack = gb("sndBack", true);
  S.sndBoot = gb("sndBoot", false);
  S.sndRep  = (uint8_t)gu("sndRep", 2);
  S.qOn     = gb("qOn", false);
  S.qFrom   = (uint8_t)gu("qFrom", 22);
  S.qTo     = (uint8_t)gu("qTo", 7);
  S.melLost = gs("melLost", "E5:220 C5:220 A4:700");
  S.melBack = gs("melBack", "C5:140 E5:140 G5:140 C6:450");
  S.patLost = gs("patLost", "700,300,700,300,700");
  S.patBack = gs("patBack", "120,80,120");
  S.srcLost = (uint8_t)gu("srcLost", 0);
  S.srcBack = (uint8_t)gu("srcBack", 0);
  auto gl = [&](const char* k, uint32_t d) { return ok ? p.getUInt(k, d) : d; };
  S.ledType    = (uint8_t)gu("ledType", 0);
  S.stripPin   = (uint8_t)gu("stPin", 27);
  S.stripN     = (uint8_t)gu("stN", 8);
  S.stripBri   = (uint8_t)gu("stBri", 30);
  S.stripNight = (uint8_t)gu("stNight", 10);
  S.stripOrder = (uint8_t)gu("stOrder", 0);
  S.stripFx    = (uint8_t)gu("stFx", 1);
  S.stripAnim  = gb("stAnim", true);
  S.stripMa    = gu("stMa", 400);
  S.colOn  = gl("colOn", 0x00FF00);
  S.colOff = gl("colOff", 0xFF0000);
  S.colGen = gl("colGen", 0xFFA000);
  S.colUnk = gl("colUnk", 0x0040FF);
  S.pwBtn  = (uint8_t)gu("pwBtn", 32);
  S.schOn     = gb("schOn", false);
  S.schRegion = gu("schReg", 3);
  S.schDso    = gu("schDso", 301);
  S.schQueue  = gs("schQ", "");
  S.schWarn   = (uint8_t)gu("schWarn", 30);
  S.schTom    = gb("schTom", true);
  S.tgNightFrom = (uint8_t)gu("tgNF", 22);
  S.tgNightTo   = (uint8_t)gu("tgNT", 7);
  S.stFxOn   = (uint8_t)gu("stFxOn", 0);
  S.stFxGen  = (uint8_t)gu("stFxGen", 0);
  S.stWvSpd  = (uint8_t)gu("stWvSpd", 4);
  S.stWvInt  = (uint8_t)gu("stWvInt", 60);
  S.lcdOn    = gb("lcdOn", false);
  S.lcdBlLow = gb("lcdBlLow", false);
  S.lcdInv   = gb("lcdInv", false);
  S.lcdFlip  = gb("lcdFlip", false);
  S.lcdRst   = (uint8_t)gu("lcdRst", 4);
  S.lcdCe    = (uint8_t)gu("lcdCe", 16);
  S.lcdDc    = (uint8_t)gu("lcdDc", 17);
  S.lcdDin   = (uint8_t)gu("lcdDin", 21);
  S.lcdClk   = (uint8_t)gu("lcdClk", 22);
  S.lcdBl    = (uint8_t)gu("lcdBl", 33);
  S.lcdVop   = (uint8_t)gu("lcdVop", 56);
  S.lcdBri   = (uint8_t)gu("lcdBri", 70);
  S.lcdNight = (uint8_t)gu("lcdNgt", 15);
  S.lcdBias  = (uint8_t)gu("lcdBias", 4);
  S.lcdMq    = (uint8_t)gu("lcdMq", 4);
  S.lcdFx    = gb("lcdFx", true);
  S.lcdFxSec = gu("lcdFxSec", 30);
  S.lcdAnnMin = gu("lcdAnnMin", 10);
  S.nLedOff  = gb("nLedOff", true);
  S.nLcdOff  = gb("nLcdOff", true);
  S.beacons  = gs("bcn", "");
  if (ok) p.end();
}

static bool wiped = false;              // стерто все — до перезапуску нічого не пишемо, щоб не повернути старе

bool settingsSave() {
  if (wiped) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("obj", S.objName);
  p.putString("objIco", S.objIcon);
  p.putString("staSsid", S.staSsid);
  p.putString("staPass", S.staPass);
  p.putString("staBssid", S.staBssid);
  p.putString("apSsid", S.apSsid);
  p.putString("apPass", S.apPass);
  p.putString("admin", S.adminPass);
  p.putString("tgToken", S.tgToken);
  p.putString("tgChats", S.tgChats);
  p.putString("avrMac", S.avrMac);
  p.putBool("mayak", S.mayakOn);
  p.putBool("bmsOn", S.bmsOn);
  p.putBool("mdnsOn", S.mdnsOn);
  p.putBool("nStart", S.notifyStart);
  p.putUShort("offSec", S.offSec);
  p.putUShort("onSec", S.onSec);
  p.putUShort("lOff", S.lineOffSec);
  p.putBool("sumOn", S.sumOn);
  p.putUShort("sumHr", S.sumHour);
  p.putBool("ledOn", S.ledOn);
  p.putBool("ledLow", S.ledLow);
  p.putUShort("ledG", S.ledG);
  p.putUShort("ledR", S.ledR);
  p.putUShort("ledBri", S.ledBri);
  p.putUShort("ledBlink", S.ledBlink);
  p.putUShort("sndType", S.sndType);
  p.putUShort("sndPin", S.sndPin);
  p.putUShort("dacPin", S.dacPin);
  p.putUShort("sndVol", S.sndVol);
  p.putBool("sndLost", S.sndLost);
  p.putBool("sndBack", S.sndBack);
  p.putBool("sndBoot", S.sndBoot);
  p.putUShort("sndRep", S.sndRep);
  p.putBool("qOn", S.qOn);
  p.putUShort("qFrom", S.qFrom);
  p.putUShort("qTo", S.qTo);
  p.putString("melLost", S.melLost);
  p.putString("melBack", S.melBack);
  p.putString("patLost", S.patLost);
  p.putString("patBack", S.patBack);
  p.putUShort("srcLost", S.srcLost);
  p.putUShort("srcBack", S.srcBack);
  p.putUShort("ledType", S.ledType);
  p.putUShort("stPin", S.stripPin);
  p.putUShort("stN", S.stripN);
  p.putUShort("stBri", S.stripBri);
  p.putUShort("stNight", S.stripNight);
  p.putUShort("stOrder", S.stripOrder);
  p.putUShort("stFx", S.stripFx);
  p.putBool("stAnim", S.stripAnim);
  p.putUShort("stMa", S.stripMa);
  p.putUInt("colOn", S.colOn);
  p.putUInt("colOff", S.colOff);
  p.putUInt("colGen", S.colGen);
  p.putUInt("colUnk", S.colUnk);
  p.putUShort("lOn", S.lineOnSec);
  p.putUShort("lWarn", S.lineWarnSec);
  p.putUShort("genRem", S.genRemMin);
  p.putUShort("pwBtn", S.pwBtn);
  p.putBool("schOn", S.schOn);
  p.putUShort("schReg", S.schRegion);
  p.putUShort("schDso", S.schDso);
  p.putString("schQ", S.schQueue);
  p.putUShort("schWarn", S.schWarn);
  p.putBool("schTom", S.schTom);
  p.putUShort("tgNF", S.tgNightFrom);
  p.putUShort("tgNT", S.tgNightTo);
  p.putUShort("stFxOn", S.stFxOn);
  p.putUShort("stFxGen", S.stFxGen);
  p.putUShort("stWvSpd", S.stWvSpd);
  p.putUShort("stWvInt", S.stWvInt);
  p.putBool("lcdOn", S.lcdOn);
  p.putBool("lcdBlLow", S.lcdBlLow);
  p.putBool("lcdInv", S.lcdInv);
  p.putBool("lcdFlip", S.lcdFlip);
  p.putUShort("lcdRst", S.lcdRst);
  p.putUShort("lcdCe", S.lcdCe);
  p.putUShort("lcdDc", S.lcdDc);
  p.putUShort("lcdDin", S.lcdDin);
  p.putUShort("lcdClk", S.lcdClk);
  p.putUShort("lcdBl", S.lcdBl);
  p.putUShort("lcdVop", S.lcdVop);
  p.putUShort("lcdBri", S.lcdBri);
  p.putUShort("lcdNgt", S.lcdNight);
  p.putUShort("lcdBias", S.lcdBias);
  p.putUShort("lcdMq", S.lcdMq);
  p.putBool("lcdFx", S.lcdFx);
  p.putUShort("lcdFxSec", S.lcdFxSec);
  p.putUShort("lcdAnnMin", S.lcdAnnMin);
  p.putBool("nLedOff", S.nLedOff);
  p.putBool("nLcdOff", S.nLcdOff);
  p.putString("bcn", S.beacons);
  p.end();
  return true;
}

bool settingsSaveChats() {
  if (wiped) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("tgChats", S.tgChats);
  p.end();
  return true;
}

bool settingsSaveBeacons() {
  if (wiped) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("bcn", S.beacons);
  p.putBool("mayak", S.mayakOn);
  p.end();
  return true;
}

bool settingsSaveSch() {
  if (wiped) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putBool("schOn", S.schOn);
  p.end();
  return true;
}

bool settingsSaveBms() {
  if (wiped) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putBool("bmsOn", S.bmsOn);
  p.end();
  return true;
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(NS, false)) { p.clear(); p.end(); }
  wiped = true;
}

//  «Скинути налаштування» зі сторінки (1.0.15): стерти з NVS усе, крім зв'язку — Wi-Fi роутера, точки для АВР, АВР,
//  бота з одержувачами й пароля сторінки — і перечитати: стерті ключі стануть типовими. Ключі зв'язку не чіпаємо взагалі
//  (не «стерти все й записати заново»), тож обрив живлення посередині зв'язку не зіпсує. Нові налаштування в майбутніх
//  версіях теж скидатимуться самі: стирається все, чого немає в KEEP.
bool settingsResetPrefs() {
  static const char* KEEP[] = {"staSsid", "staPass", "staBssid", "apSsid", "apPass", "admin", "tgToken", "tgChats", "avrMac", "bcn"};
  if (wiped) return false;
  //  1.0.38: список ключів — у купі лише на час скидання (було 1,5 КБ статичної пам'яті назавжди)
  typedef char Key[NVS_KEY_NAME_MAX_SIZE];
  Key* keys = (Key*)malloc(96 * sizeof(Key));
  if (!keys) return false;
  int n = 0;
  nvs_iterator_t it = nullptr;
  esp_err_t r = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_ANY, &it);
  while (r == ESP_OK && n < 96) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    bool keep = false;
    for (const char* k : KEEP) if (!strcmp(k, info.key)) keep = true;
    if (!keep) { strlcpy(keys[n], info.key, NVS_KEY_NAME_MAX_SIZE); n++; }
    r = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  Preferences p;
  if (!p.begin(NS, false)) { free(keys); return false; }
  for (int i = 0; i < n; i++) p.remove(keys[i]);
  p.end();
  free(keys);
  settingsLoad();
  return true;
}

int parseChats(const String& s, Chat* out, int maxN) {
  int n = 0, i = 0, L = s.length();
  while (i < L && n < maxN) {
    int e = s.indexOf('\n', i);
    if (e < 0) e = L;
    String line = s.substring(i, e);
    i = e + 1;
    line.replace("\r", "");
    line.trim();
    if (!line.length()) continue;
    int sp = line.indexOf(' ');
    String idS = sp < 0 ? line : line.substring(0, sp);
    String nm = sp < 0 ? String() : line.substring(sp + 1);
    nm.trim();
    uint16_t fl = RF_DEFAULT | (n == 0 ? RF_ADMIN : 0);    // запис до 1.0.21: усе ввімкнено, перший — адміністратор
    if (nm.startsWith("!")) {
      int e2 = nm.indexOf(' ');
      String fs = e2 < 0 ? nm.substring(1) : nm.substring(1, e2);
      char* fe = nullptr;
      unsigned long v = strtoul(fs.c_str(), &fe, 16);
      if (fs.length() && fe && !*fe && v <= 0xFFFF) { fl = (uint16_t)v; nm = e2 < 0 ? String() : nm.substring(e2 + 1); }
    }
    idS.replace(",", "");
    idS.replace(";", "");
    char* end = nullptr;
    long long id = strtoll(idS.c_str(), &end, 10);
    if (!end || *end != 0 || id == 0) continue;
    bool dup = false;
    for (int k = 0; k < n; k++) if (out[k].id == id) dup = true;
    if (dup) continue;
    nm.trim();
    out[n].id = id;
    out[n].name = nm;
    out[n].fl = fl;
    n++;
  }
  return n;
}

String chatsToText(const Chat* c, int n) {
  String o;
  for (int i = 0; i < n; i++) {
    if (i) o += '\n';
    char f[8];
    snprintf(f, sizeof f, " !%x", (unsigned)c[i].fl);
    o += String(c[i].id) + f;
    if (c[i].name.length()) o += " " + c[i].name;
  }
  return o;
}
