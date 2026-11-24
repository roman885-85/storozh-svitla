//  Прошивка мосту батареї через Сторожа (1.0.48) — див. fwrelay.h.
#include "fwrelay.h"
#include "util.h"
#include <WiFi.h>
#include <MD5Builder.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include "lwip/sockets.h"

namespace {

constexpr uint16_t PORT = 8081;
constexpr uint32_t SERVE_MS = 10UL * 60 * 1000;   // роздаємо 10 хв
constexpr size_t SEC = 4096;

const esp_partition_t* part = nullptr;
uint8_t* sec = nullptr;                // сектор, що набирається
size_t secN = 0, wOff = 0, erasedTo = 0, total = 0, size = 0;
MD5Builder md5;
char md5s[33] = "", tok[17] = "", ferr[96] = "";
volatile int state = 0;                // 0 нічого, 1 приймаю, 2 роздаю, 3 строк минув, 4 помилка роздачі
volatile uint32_t sent = 0, got = 0, untilMs = 0, tries = 0;
volatile bool stop = false;
TaskHandle_t task = nullptr;
uint32_t gotLogged = 0, triesLogged = 0;
int stateLogged = 0;

//  1.0.49: стерти блоком 64 КБ, де можна (як Update.cpp ядра), а не кожен сектор 4 КБ окремо. 23.11.2026 о 22:19 файл
//  мосту (1,5 МБ = 378 секторів) через KeenDNS не встиг записатись: проміжний сервер чекає ~23 с і рве з'єднання
//  (502), а 378 стирань по 4 КБ — довше; оновлення самого Сторожа (стирає блоками) того ж вечора — 12,6 с.
bool flushSec() {
  if (!secN) return true;
  if (wOff >= erasedTo) {
    size_t len = ((part->address + wOff) % 65536 == 0 && part->size - wOff >= 65536) ? 65536 : SEC;
    if (esp_partition_erase_range(part, wOff, len) != ESP_OK) return false;
    erasedTo = wOff + len;
  }
  if (esp_partition_write(part, wOff, sec, secN) != ESP_OK) return false;
  wOff += SEC;
  secN = 0;
  return true;
}
//  Відповідь мосту: заголовки (з x-MD5) і файл з розділу шматками по 2 КБ
void serve(int c) {
  char rq[1024];
  int n = 0;
  uint32_t t0 = millis();
  while (n < (int)sizeof rq - 1 && millis() - t0 < 5000) {   // увесь запит, до порожнього рядка: недочитане при закритті — RST
    int r = recv(c, rq + n, sizeof rq - 1 - n, 0);
    if (r <= 0) break;
    n += r;
    rq[n] = 0;
    if (strstr(rq, "\r\n\r\n")) break;
  }
  rq[n] = 0;
  tries++;
  char want[48];
  snprintf(want, sizeof want, "GET /%s/avr-bms.bin ", tok);
  if (strncmp(rq, want, strlen(want))) {
    static const char R404[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(c, R404, sizeof R404 - 1, 0);
    return;
  }
  //  1.0.50: заголовки й перші 2 КБ файлу — одним пакетом, і без затримки Нейгла. HTTPUpdate мосту після заголовків
  //  чекає лише 100 мс і дивиться перший байт (0xE9); 23.11.2026 о 22:24 заголовки пішли окремо, а файл — лише після
  //  підтвердження (затримка Нейгла + відкладене підтвердження мосту ~250 мс), і міст відповів «Verify Bin Header Failed».
  int one = 1;
  setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  uint8_t* buf = (uint8_t*)malloc(2048 + 256);
  if (!buf) return;
  int hl = snprintf((char*)buf, 256, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nx-MD5: %s\r\nConnection: close\r\n\r\n",
                    (unsigned)size, md5s);
  size_t off = 0, pre = (size_t)hl;
  bool ok = true;
  sent = 0;
  while (ok && off < size && !stop) {
    size_t k = size - off < 2048 ? size - off : 2048;
    if (esp_partition_read(part, off, buf + pre, k) != ESP_OK) break;
    size_t w = 0, all = pre + k;
    while (w < all) {
      int r = send(c, buf + w, all - w, 0);
      if (r <= 0) { ok = false; break; }
      w += (size_t)r;
    }
    if (!ok) break;
    pre = 0;
    off += k;
    sent = off;
  }
  free(buf);
  if (off >= size) got++;
}
void srvTask(void*) {
  int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(PORT);
  a.sin_addr.s_addr = (uint32_t)WiFi.softAPIP();       // лише мережа Сторожа, не мережа роутера
  if (ls < 0 || bind(ls, (struct sockaddr*)&a, sizeof a) || listen(ls, 1)) {
    if (ls >= 0) close(ls);
    strlcpy(ferr, "не вдалося відкрити порт 8081", sizeof ferr);
    state = 4;
    task = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  while (!stop && (int32_t)(millis() - untilMs) < 0) {
    fd_set f;
    FD_ZERO(&f);
    FD_SET(ls, &f);
    struct timeval tv = {1, 0};
    if (select(ls + 1, &f, nullptr, nullptr, &tv) <= 0) continue;
    int c = accept(ls, nullptr, nullptr);
    if (c < 0) continue;
    struct timeval so = {10, 0}, ro = {5, 0};
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &so, sizeof so);
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &ro, sizeof ro);
    serve(c);
    shutdown(c, SHUT_WR);                                 // чесне закриття: дочекатись, доки міст закриє сам
    char tmp[64];
    uint32_t t0 = millis();
    while (millis() - t0 < 3000 && recv(c, tmp, sizeof tmp, 0) > 0) {}
    close(c);
  }
  close(ls);
  if (state == 2) state = 3;
  task = nullptr;
  vTaskDelete(nullptr);
}
void stopServer() {
  if (!task) return;
  stop = true;
  for (int i = 0; i < 40 && task; i++) delay(100);
}

}  // namespace

bool fwrBegin(String& err) {
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    err = "Сторож щойно оновився й ще перевіряє себе (до 10 хв) — спробуйте трохи згодом";   // вільний розділ — його відкат
    return false;
  }
  stopServer();
  if (task) { err = "Попередня передача ще не скінчилась — спробуйте за хвилину"; return false; }
  part = esp_ota_get_next_update_partition(nullptr);
  if (!part) { err = "Немає вільного розділу для файлу"; return false; }
  if (!sec) sec = (uint8_t*)malloc(SEC);
  if (!sec) { err = "Мало пам'яті — спробуйте за хвилину"; return false; }
  secN = wOff = erasedTo = total = size = 0;
  md5s[0] = tok[0] = ferr[0] = 0;
  sent = got = tries = 0;
  gotLogged = triesLogged = 0;
  md5.begin();
  state = 1;
  return true;
}
bool fwrWrite(const uint8_t* d, size_t n, String& err) {
  if (state != 1) return false;
  esp_task_wdt_reset();                                   // запис у флеш довгий — сторож циклу не має спрацювати
  if (total == 0) {
    //  Перший шматок: образ ESP (0xE9), чип ESP32-C3 (id 5), застосунок (опис програми 0xABCD5432 з адреси 0x20) —
    //  не …-full.bin (там спершу завантажувач) і не прошивка Сторожа
    uint32_t app = n >= 36 ? (uint32_t)d[32] | (uint32_t)d[33] << 8 | (uint32_t)d[34] << 16 | (uint32_t)d[35] << 24 : 0;
    uint16_t chip = n >= 14 ? (uint16_t)(d[12] | d[13] << 8) : 0xFFFF;
    if (n < 36 || d[0] != 0xE9) { err = "Це не прошивка ESP"; fwrAbort(); return false; }
    if (chip != 0x0005) { err = chip == 0 ? "Це прошивка для ESP32 (Сторожа), а міст батареї — ESP32-C3" : "Це прошивка не для ESP32-C3"; fwrAbort(); return false; }
    if (app != 0xABCD5432) { err = "Потрібен файл …-app.bin, а не …-full.bin"; fwrAbort(); return false; }
  }
  if (total + n > part->size) { err = "Файл завеликий"; fwrAbort(); return false; }
  md5.add(d, n);
  while (n) {
    size_t k = SEC - secN < n ? SEC - secN : n;
    memcpy(sec + secN, d, k);
    secN += k;
    d += k;
    n -= k;
    total += k;
    if (secN == SEC && !flushSec()) { err = "Не вдалося записати у флеш"; fwrAbort(); return false; }
  }
  return true;
}
bool fwrEnd(String& err) {
  if (state != 1) { err = "Файл не прийнято"; return false; }
  if (!flushSec()) { err = "Не вдалося записати у флеш"; fwrAbort(); return false; }
  if (total < 65536) { err = "Файл замалий для прошивки мосту"; fwrAbort(); return false; }
  md5.calculate();
  strlcpy(md5s, md5.toString().c_str(), sizeof md5s);
  size = total;
  snprintf(tok, sizeof tok, "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  free(sec);
  sec = nullptr;
  stop = false;
  untilMs = millis() + SERVE_MS;
  state = 2;
  stateLogged = 2;
  if (xTaskCreate(srvTask, "bmsfw", 4096, nullptr, 1, &task) != pdPASS) { task = nullptr; state = 4; err = "Не вдалося запустити роздачу"; return false; }
  logEvent("Прошивку мосту батареї прийнято (" + String((unsigned long)(size / 1024)) + " КБ, MD5 " + String(md5s) + ") — роздаю мосту 10 хв");
  return true;
}
void fwrAbort() {
  if (state == 1) state = 0;
  free(sec);
  sec = nullptr;
}
void fwrStop() {
  stopServer();
  if (state == 2 || state == 3 || state == 4) state = 0;
  stateLogged = state;
}
bool fwrBusy() { return state == 1 || state == 2; }
String fwrUrl() {
  if (state != 2) return String();
  return "http://" + WiFi.softAPIP().toString() + ":" + String(PORT) + "/" + tok + "/avr-bms.bin";
}
String fwrJson() {
  long left = state == 2 ? (long)((int32_t)(untilMs - millis()) / 1000) : 0;
  return "{\"st\":" + String((int)state) + ",\"size\":" + String((unsigned long)size) + ",\"md5\":\"" + String(md5s) + "\",\"sent\":" +
         String((unsigned long)sent) + ",\"got\":" + String((unsigned long)got) + ",\"tries\":" + String((unsigned long)tries) +
         ",\"left\":" + String(left < 0 ? 0 : left) + ",\"url\":\"" + fwrUrl() + "\",\"err\":\"" + String(ferr) + "\"}";
}
void fwrLoop() {
  uint32_t g = got, t = tries;
  if (g != gotLogged) { gotLogged = g; logEvent("Міст батареї завантажив прошивку зі Сторожа (" + String((unsigned long)(size / 1024)) + " КБ)"); }
  else if (t != triesLogged && t > g + 2) { triesLogged = t; logEvent("Міст батареї: завантаження прошивки зі Сторожа не дійшло до кінця (спроб " + String((unsigned long)t) + ")"); }
  int s = state;
  if (s != stateLogged) {
    stateLogged = s;
    if (s == 3) logEvent("Прошивку мосту батареї більше не роздаю: минуло 10 хв" + String(g ? "" : " — міст її так і не завантажив"));
    else if (s == 4) logEvent(String("Прошивку мосту батареї не роздано: ") + ferr);
  }
}
