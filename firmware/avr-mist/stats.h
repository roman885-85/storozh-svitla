#pragma once
#include <Arduino.h>
/*  Статистика світла (1.0.12): скільки годин і хвилин світло було й не було — по днях, відключення з часом.
    У RTC-пам'яті: переживає перезапуск і оновлення по мережі, зникає лише без живлення. Флеш не пишемо.  */
void   statsBegin();
void   statsTick(int gridSt);                                          // з watchLoop: 1 — є, 0 — немає, -1 — невідомо
void   statsTransition(int from, int to, int64_t atUs, bool approx);   // зміна міста; atUs — справжній момент
String statsReport();                                                  // HTML для Telegram («📈 Статистика», /stats)
String statsJson();                                                    // для сторінки
