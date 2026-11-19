#pragma once
#include <Arduino.h>

/*  Wi-Fi моста: клієнт церковного роутера (STA) + своя точка (AP) для АВР і маячка.
    Трафік із точки йде в роутер через NAT — застосунок eWeLink працює як раніше.  */
struct ApDev {
  bool used = false;
  uint8_t mac[6] = {0};
  uint32_t ip = 0;              // 0 — адресу ще не видано
  bool assoc = false;           // під'єднаний до точки
  int64_t assocUs = 0, leaveUs = 0;
  int64_t lastPingOkUs = 0;     // 0 — ще не відповідав на ping
  bool pingWorks = false;       // хоч раз відповів на ping
  int8_t rssi = 0;
};
constexpr int MAXC = 8;

void    bridgeBegin();
void    bridgeLoop();
bool    staUp();
int     clientsSnapshot(ApDev* out, int maxN);
bool    findClient(const uint8_t mac[6], ApDev& out);
int     apChannel();
int64_t apChannelChangedUs();   // 0 — не змінювався з запуску
String  bridgeJson();
bool    natOn();                // NAT точки: прапорець у самому інтерфейсі lwIP (1.0.2)
String  staIpStr();
int     staRssi();
void    scanStart();
String  scanJson();
