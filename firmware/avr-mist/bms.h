#pragma once
#include <Arduino.h>

/*  Батарея (1.0.36): дані JK BMS від мосту батареї — ESP32-C3 біля BMS (firmware/avr-bms), постійно під'єднаної до неї
    по Bluetooth; міст кожні 5 с шле Сторожу UDP «BMS1 …» на порт маячків. Сторож до BMS не під'єднується й нічого їй
    не пише — лише читає те, що прислав міст.  */
void   bmsPacket(char* msg, uint32_t ip);   // з watch.cpp: рядок «BMS1 …» (msg розбирається на місці)
void   bmsLoop();                           // з loop(): сповіщення
String bmsJson();                           // для /api/state; "" — мосту від запуску не було й сповіщення вимкнено
String bmsStatusLine();                     // рядок для «📊 Стан» ("\n🔋 …"); "" — нічого показувати
String bmsDiagLine();                       // рядок для «🛠 Сторож»
int    bmsSoc();                            // заряд, % (свіжі дані); -1 — немає
uint32_t bmsBridgeIp();                     // 1.0.39: адреса мосту в мережі Сторожа; 0 — міст не на зв'язку
bool   bmsBridgeMac(uint8_t mac[6]);        // MAC мосту; false — мосту від запуску не було
