// Мінімальна заміна Arduino для перевірки драйвера ST7567 на Mac (tests/batlcd): записує все, що пішло на дроти.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <initializer_list>
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
struct Wire1 { bool data; uint8_t b; };          // байт по SPI: data = рівень RS у цю мить (1 — дані, 0 — команда)
extern std::vector<Wire1> g_wire;
extern int g_pin[40];
extern int g_delayMs;
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { if (p >= 0 && p < 40) g_pin[p] = v; }
inline void delay(int ms) { g_delayMs += ms; }
