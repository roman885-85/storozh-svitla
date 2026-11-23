#pragma once
#include "Arduino.h"
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings { SPISettings(uint32_t, int, int) {} };
extern int g_rsPin, g_csPin;
struct SPIClass {
  void begin(int, int, int, int) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  void transfer(uint8_t b) { if (g_pin[g_csPin] == LOW) g_wire.push_back({g_pin[g_rsPin] == HIGH, b}); }
};
extern SPIClass SPI;
