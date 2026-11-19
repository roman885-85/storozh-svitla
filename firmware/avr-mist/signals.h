#pragma once
#include <Arduino.h>
/*  Сигнали на виводах плати (1.0.13). Власник: «вывести на выводы esp подключение светодиодов — красный мигающий — нет
    света, зеленый постоянно светящийся — свет есть; также выводы для звукового оповещения отсутствия света и его появления.
    Звук для активного или пассивного бузера и звук для динамика через встроенный ЦАП … Все с настройкой громкости,
    возможностью кастомизации звука, загруженного с компьютера, и остальными настройками».  */
void   sigBegin();
void   sigReload();                          // після збереження налаштувань
void   sigLoop(int gridSt, int lineSt);      // зі звичайного циклу: індикатор (1 — є, 0 — немає, -1 — невідомо); лінія — для генератора
void   sigEvent(int from, int to);           // зміна міста: звук
bool   sigTest(const String& what, int vol = -1);   // green | red | off | flash | wave | lost | back; vol — гучність перевірки з повзунка, -1 — збережена
String sigJson();
bool   sigPinOk(int pin);                    // вивід можна займати
bool   sigQuietNow();                        // зараз «тиша вночі» (для підсвітки дисплея, 1.0.22)
bool   sigMelodyOk(const String& m, String& err);
bool   sigPatternOk(const String& p, String& err);
//  Свій звук для динаміка: сирі 8-бітні вибірки 16 кГц моно — сторінка сама перетворює будь-який файл.
static const size_t SIG_MAX_PCM = 48000;      // 3 с
bool   sigSoundBegin(int which);             // 0 — «світло зникло», 1 — «світло є»
bool   sigSoundWrite(const uint8_t* d, size_t n);
bool   sigSoundEnd(String& err);
void   sigSoundAbort();
bool   sigSoundDelete(int which);
