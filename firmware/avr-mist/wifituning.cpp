/*  Ощадний на пам'ять режим драйвера Wi-Fi (1.0.38, «капитальная оптимизация для оперативной памяти»).

    Ядро Arduino запускає драйвер із межею 32 динамічні буфери прийому й 32 передачі (по ~1,6 КБ) і з об'єднанням
    кадрів (A-MPDU): на сплеску мережі (трансляції в мережі об'єкта, завантаження прошивки, сторінка) драйвер може
    забрати до ~100 КБ — усе, що є, якраз коли пам'ять потрібна шифруванню. Плата без PSRAM має ~25–45 КБ вільних, тож
    межу ставимо свою: 8 буферів прийому й 8 передачі (щонайбільше ~26 КБ), без A-MPDU. Сторожу швидкість не потрібна
    (сторінка, Telegram, дрібний трафік АВР); рекомендований Espressif «мінімальний» профіль — ще менший (6 і 6).

    Змінити це після запуску не можна, а налаштування ядро збирає всередині себе — тому перехоплюємо сам виклик
    esp_wifi_init() компонувальником (tools/build.sh: -Wl,--wrap=esp_wifi_init). Драйвер відмовив — запускаємо його
    так, як просило ядро: без Wi-Fi Сторож не лишиться.  */
#include "wifituning.h"
#include <esp_wifi.h>

static const int RX_DYN = 8, TX_DYN = 8;
static int8_t state = 0;       // 0 — драйвер ще не запускали, 1 — ощадний режим, -1 — ощадний не прийнято, запущено як у ядра
static int err = 0;
static int coreRx = 0, coreTx = 0;

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config) {
  wifi_init_config_t c = *config;
  coreRx = c.dynamic_rx_buf_num;
  coreTx = c.dynamic_tx_buf_num;
  c.dynamic_rx_buf_num = RX_DYN;
  c.dynamic_tx_buf_num = TX_DYN;
  c.ampdu_rx_enable = 0;
  c.ampdu_tx_enable = 0;
  c.rx_ba_win = 0;             // без A-MPDU не вживається
  esp_err_t e = __real_esp_wifi_init(&c);
  if (e == ESP_OK) { state = 1; return e; }
  err = e;
  state = -1;
  return __real_esp_wifi_init(config);
}

String wifiTuneJson() {
  return String("{\"tuned\":") + (state == 1 ? "true" : "false") + ",\"rx\":" + String(state == 1 ? RX_DYN : coreRx) +
         ",\"tx\":" + String(state == 1 ? TX_DYN : coreTx) + ",\"ampdu\":" + (state == 1 ? "false" : "true") + ",\"err\":" + String(err) + "}";
}
String wifiTuneText() {
  if (state == 1) return "Wi-Fi: ощадний режим — буферів прийому " + String(RX_DYN) + ", передачі " + String(TX_DYN) + ", без A-MPDU";
  if (state == -1) return "Wi-Fi: ощадний режим драйвер не прийняв (код " + String(err) + ") — запущено як у ядра: буферів " + String(coreRx) + " і " + String(coreTx);
  return "Wi-Fi: драйвер ще не запущено";
}
