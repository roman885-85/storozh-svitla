#pragma once
#include <Arduino.h>

/*  Що означає присутність пристроїв у мережі моста:
     • АВР: його Wi-Fi живиться лише від міського вводу. Є в мережі = місто є.
     • маячок (ESP32-C3) у розетці від генератора: живий = генератор працює;
       маячок на лінії чи фазі (1.0.23): живий = на ній є напруга.            */
enum Tri : int8_t { T_UNK = -1, T_OFF = 0, T_ON = 1 };

struct Track {
  Tri st = T_UNK;
  bool exact = false;        // перехід у поточний стан бачили наживо
  int64_t since = 0;         // мкс від запуску: з якого моменту поточний стан
  bool raw = false;          // миттєвий сигнал без затримок
  int64_t rawSince = 0;
  int64_t lastTrueUs = 0;    // коли сигнал востаннє був «є»
};

void   watchBegin();
void   watchLoop();
String statusShort();        // «міське світло є» …
String statusReport();       // текст для /status (HTML Telegram)
String testMessage();        // «Перевірка зв'язку» (HTML Telegram)
String pwResetMessage();     // «🔑 Пароль сторінки скинуто» — окрема кнопка на Сторожі (1.0.15)
String factoryMessage(const String& who);   // «⚠️ Сторожа скинуто до заводських» — перед скиданням зі сторінки
String watchJson();
int    watchGrid();           // 1 — місто є, 0 — немає, -1 — невідомо або не задано ні АВР, ні маячка «Міське світло»
bool   watchCitySet();        // 1.0.57: за містом стежимо (АВР чи маячок «Міське світло»)
int    watchLine();           // лінія після АВР (маячок): 1 — є, 0 — немає, -1 — невідомо чи маячка немає
//  Для дисплея (1.0.22)
bool    watchAvrSet();                 // АВР призначено
int64_t watchGridSince(bool& exact);   // з якого моменту поточний стан міста (мкс від запуску)
int64_t watchLineSince(bool& exact);   // те саме для генератора (1.0.26)
bool    watchAvrRssi(int& rssi);       // АВР у точці: сигнал, як його чує Сторож
bool    watchMayakRssi(int& rssi);     // маячок на зв'язку: сигнал
//  Маячки (1.0.23)
void   watchReload();                  // S.beacons змінено — розібрати знову
bool   watchBeaconEdit(const uint8_t* mac, char act, char role, const String& name, String& err);
String watchBeaconsText();             // список маячків для «🛠 Сторож» у боті (HTML Telegram)
