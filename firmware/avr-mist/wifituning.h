#pragma once
#include <Arduino.h>

/*  Ощадний на пам'ять режим драйвера Wi-Fi (1.0.38) — див. wifituning.cpp.  */
String wifiTuneJson();     // {"tuned":true,"rx":8,"tx":8,"ampdu":false} — що справді прийняв драйвер
String wifiTuneText();     // один рядок для журналу
