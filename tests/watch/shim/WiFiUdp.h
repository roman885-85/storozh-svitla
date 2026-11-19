// Пакети маячків для перевірки: черга (час, адреса, текст); parsePacket віддає ті, чий час настав
#pragma once
#include <deque>
#include <string>
#include <cstdint>
#include <cstring>
struct IPAddress {
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  uint8_t operator[](int i) const { return b[i]; }
  operator uint32_t() const { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
};
struct FakePkt { int64_t at; IPAddress ip; std::string text; };
extern std::deque<FakePkt> g_pkts;
int64_t esp_timer_get_time();
class WiFiUDP {
  FakePkt cur;
 public:
  void begin(uint16_t) {}
  int parsePacket() {
    if (g_pkts.empty() || g_pkts.front().at > esp_timer_get_time()) return 0;
    cur = g_pkts.front(); g_pkts.pop_front(); return (int)cur.text.size();
  }
  int read(char* d, size_t n) { size_t k = cur.text.size() < n ? cur.text.size() : n; memcpy(d, cur.text.data(), k); return (int)k; }
  IPAddress remoteIP() { return cur.ip; }
};
