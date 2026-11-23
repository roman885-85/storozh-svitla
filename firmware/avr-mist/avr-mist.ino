/*  АВР-міст — сповіщення в Telegram про міське світло за станом АВР TOMZN TOQ7E-125/4.
 *
 *  ESP32 (WROOM) одночасно:
 *   • клієнт Wi-Fi церковного роутера — вихід в інтернет і сторінка налаштувань;
 *   • власна точка доступу для АВР і маячка; трафік АВР іде далі через NAT,
 *     тож застосунок eWeLink працює як і раніше;
 *   • стежить: Wi-Fi АВР живиться лише від міського вводу — АВР у мережі моста,
 *     отже місто є; маячок (ESP32-C3 у розетці після АВР) живий — на лінії є напруга;
 *   • шле повідомлення в Telegram і відповідає на /status.
 *
 *  Правило власника: у флеш пишуть лише явні дії людини — «Зберегти» на сторінці,
 *  призначення АВР і чату, оновлення прошивки, скидання. Стан, журнал і черга — у RAM.
 *
 *  Збирати лише tools/build.sh: він додає -Wl,--wrap=dns_clear_cache (див. dnsfix.cpp).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include "settings.h"
#include "bridge.h"
#include "watch.h"
#include "tg.h"
#include "web.h"
#include "util.h"
#include "stats.h"
#include "signals.h"
#include "yasno.h"
#include "lcd.h"
#include "version.h"
#include "box.h"
#include "tlspool.h"
#include "bms.h"
#include "mem.h"
#include "crash.h"
#include "wifituning.h"

static const int RESET_BTN = 0;          // кнопка BOOT: 10 с — скинути налаштування
static String cmdLine;

static void printInfo() {
  Serial.printf("\nСторож світла %s (збірка %s)\n", FW_VERSION, FW_BUILD);
  Serial.printf("Мережа для АВР: «%s», пароль «%s», сторінка http://10.77.0.1\n", S.apSsid.c_str(), S.apPass.c_str());
  Serial.printf("Сторінка: користувач admin, пароль %s\n", S.adminPass == defaultPass() ? ("«" + S.adminPass + "» (типовий)").c_str() : "заданий на сторінці");
  if (S.staSsid.length())
    Serial.printf("Роутер «%s»: %s\n", S.staSsid.c_str(), staUp() ? ("адреса " + staIpStr() + ", також http://avr-mist.local").c_str() : "немає зв'язку");
  else
    Serial.println("Роутер не налаштовано");
  if (staUp()) Serial.printf("NAT: %s\n", natOn() ? "увімкнено, пристрої в точці мають інтернет" : "ВИМКНЕНО, пристрої в точці без інтернету");
  Serial.printf("Стан: %s\n", statusShort().c_str());
  Serial.println("Команди: info, state, reboot, factory-reset");
}

static void serialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (cmdLine.length() < 120) cmdLine += c; continue; }
    String l = cmdLine;
    cmdLine = "";
    l.trim();
    if (l == "info") printInfo();
    else if (l == "state") {          // те саме, що бачить сторінка, — для перевірки по кабелю
      Serial.println(watchJson());
      Serial.println(bridgeJson());
      Serial.println(tgJson());
    }
    else if (l == "reboot") restartWith("команда reboot через порт", true);
    else if (l == "factory-reset") {
      settingsFactoryReset();
      webLogoutAll();
      Serial.println("Налаштування стерто, перезапуск");
      restartWith("скидання до заводських через порт", true);
    } else if (l.length()) Serial.println("Команди: info, state, reboot, factory-reset");
  }
}

static void bootButton() {
  static uint32_t down = 0;
  if (digitalRead(RESET_BTN) == LOW) {
    if (!down) down = millis();
    else if (millis() - down > 10000) {
      logEvent("Кнопка BOOT 10 с: налаштування стерто");
      settingsFactoryReset();
      webLogoutAll();
      restartWith("кнопка BOOT 10 с — скидання до заводських", true);
    }
  } else down = 0;
}

//  Окрема кнопка скидання пароля сторінки (1.0.15, власник: «отдельную кнопку сброса пароля входа»). Кнопка між
//  виводом S.pwBtn і GND (внутрішня підтяжка, без резистора). Утримати 5 с — пароль сторінки стає типовим, усі входи
//  скасовано, решта налаштувань лишається; в Telegram — «🔑 Пароль сторінки скинуто». Кнопка діє лише після того, як
//  вивід побув відпущеним (1 с): замкнений від запуску вивід (помилка в проводах) пароль щоразу не скидатиме.
//  Кнопка BOOT, як і раніше, 10 с — стерти все.
static void pwButton() {
  static int pin = -1;
  static uint32_t down = 0, upSince = 0;
  static bool armed = false, fired = false, told = false;
  if (pin != (int)S.pwBtn) {
    if (pin > 0) pinMode(pin, INPUT);
    pin = S.pwBtn;
    down = upSince = 0;
    armed = fired = told = false;
    if (pin > 0) pinMode(pin, INPUT_PULLUP);
  }
  if (pin <= 0) return;
  uint32_t now = millis();
  if (digitalRead(pin) != LOW) {
    down = 0;
    fired = false;
    if (!upSince) upSince = now | 1;
    else if (now - upSince >= 1000) armed = true;
    return;
  }
  upSince = 0;
  if (!down) down = now | 1;
  if (fired || now - down < 5000) return;
  if (!armed) {
    if (!told) logEvent("Кнопка скидання пароля: GPIO" + String(pin) + " замкнений на GND від запуску — не скидаю, перевірте проводи");
    told = true;
    return;
  }
  fired = true;
  S.adminPass = defaultPass();
  bool ok = settingsSave();
  webLogoutAll();
  logEvent(ok ? "Кнопка скидання пароля 5 с: пароль сторінки став типовим, усі входи скасовано" : "Кнопка скидання пароля: не вдалося записати");
  if (ok) tgBroadcast(nowUs(), pwResetMessage(), false, RF_SYS | C_ADMIN);
}

// ---------- сторінка має відповідати завжди (1.0.7) ----------
//  18.11.2026, власник: «веб моста перестал работать — такого быть не должно и повторяться тоже».
//  • Оновлення по мережі з відкатом: нова прошивка після оновлення завантажується «на перевірку» (завантажувач ESP32
//    з відкатом увімкнено в ядрі), але ядро Arduino одразу визнавало її справною. Тепер визнаємо лише тоді, коли через
//    хвилину роботи міст сам відкрив свою сторінку /api/ping. Упала, зависла чи сторінка мовчить 3 хв — відкат на
//    попередню версію. Після прошивки кабелем відкату немає: там і так поруч людина з кабелем.
//  • Звичайна робота: раз на 5 хв те саме; 3 невдачі поспіль (15 хв без сторінки) — перезапуск моста.
extern "C" bool verifyRollbackLater() { return true; }
static bool otaPending = false;
static volatile int selfRes = -1;          // остання самоперевірка: -1 ще не було, 0 — сторінка не відповіла, 1 — відповіла
static volatile uint32_t selfN = 0;        // скільки перевірок зроблено
static void selfCheckTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(60000));
  for (;;) {
    bool ok = false;
    WiFiClient c;
    if (c.connect(IPAddress(127, 0, 0, 1), 80, 3000)) {
      c.print("GET /api/ping HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
      uint32_t t0 = millis();
      while (millis() - t0 < 8000 && c.connected() && !c.available()) vTaskDelay(pdMS_TO_TICKS(20));
      if (c.available()) ok = c.readStringUntil('\n').startsWith("HTTP/1.1 200");
      c.stop();
    }
    selfRes = ok ? 1 : 0;
    selfN++;
    vTaskDelay(pdMS_TO_TICKS(otaPending ? 20000 : 300000));
  }
}
static void otaBegin() {
  esp_ota_img_states_t st;
  otaPending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (otaPending) {
    markBootAfterUpdate();
    logEvent("Нова прошивка " FW_VERSION " на перевірці: за хвилину сторінка має відповісти, інакше — відкат");
  }
  xTaskCreatePinnedToCore(selfCheckTask, "selfchk", 4096, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
}
static void selfGuard() {
  static uint32_t seen = 0;
  static int fails = 0;
  if (otaPending) {
    //  1.0.38: справною — лише коли й Wi-Fi справді працює (точка запустилась, роутер під'єднувався). Сторінку
    //  самоперевірка відкриває через 127.0.0.1 — це вдається й без Wi-Fi, тож прошивка з поламаним Wi-Fi лишалась би назавжди.
    if (selfRes == 1 && bridgeNetOk()) {
      esp_ota_mark_app_valid_cancel_rollback();
      otaPending = false;
      logEvent("Нову прошивку " FW_VERSION " визнано справною: сторінка відповідає, Wi-Fi працює");
    } else if (millis() > 180000) {
      const char* why = selfRes == 1 ? "Wi-Fi не запрацював за 3 хв" : "сторінка не відповіла за 3 хв";
      logEvent(String("Нова прошивка " FW_VERSION ": ") + why + " — відкат на попередню версію");
      boxMark(String("нова прошивка " FW_VERSION ": ") + why + " — відкат", false);
      delay(300);
      esp_ota_mark_app_invalid_rollback_and_reboot();
      otaPending = false;                  // повернулось — відкочуватись нікуди (прошивка кабелем): працюємо далі, без повторів
      logEvent("Відкат неможливий — попередньої прошивки немає; працюю на " FW_VERSION);
    }
    return;
  }
  if (selfN == seen) return;
  seen = selfN;
  fails = selfRes == 1 ? 0 : fails + 1;
  if (fails == 1) logEvent("Самоперевірка: сторінка не відповіла");
  if (fails >= 3) restartWith("сторінка Сторожа не відповідала 15 хв", false);
}

//  1.0.29 (власник 20.11.2026: «частые отчеты о перезапуске … нет стабильности и нет предсказуемости»). Того дня о 16:19
//  місто зникло, повідомлення стало в чергу — і за секунду Сторож перезапустився: інтернету в церкві не було понад 10 хв, а
//  сторож «Telegram 10 хв мовчить, а черга не порожня» (1.0.18) вважав це завислим з'єднанням. Перезапуск інтернету не
//  повертає, зате губить журнал, будить людей «Сторож увімкнувся» й на 90 с ставить «визначається». Тому:
//  • поганий інтернет Сторожа більше не перезапускає — повідомлення чекають у черзі (вона в RTC) і йдуть, щойно зв'язок є;
//    завислий шифрований клієнт Telegram і так замінюється новим після двох невдач (tg.cpp);
//  • кожне очікування мережі обмежене (з'єднання 8 с, рукостискання TLS 15 с, відповідь 15 с), тож задача Telegram не
//    стоїть довше хвилини; перезапуск — лише якщо вона мовчить 10 хв (справді зависла);
//  • кожен перезапуск записує причину (box.h) — після нього в журналі видно, чому, і що було перед тим.
static void guards() {
  static int64_t lowSince = 0;
  int64_t now = nowUs();
  if (S.tgToken.length() && staUp() && tgHeartbeatUs() && now - tgHeartbeatUs() > 600LL * 1000000)
    restartWith("задача Telegram зависла — 10 хв без жодного кроку", false);
  //  Пам'ять тече — перезапуск, поки не впало саме. 1.0.38: за справжніми цифрами (mem.h): старий поріг «< 20 КБ»
  //  рахувався разом з ~32 КБ пам'яті команд і не міг спрацювати ніколи. Тепер: менше 6 КБ три хвилини поспіль —
  //  з такою пам'яттю ні Telegram, ні сторінка вже не працюють.
  if (memFree() < 6000) {
    if (!lowSince) lowSince = now;
    else if (now - lowSince > 180LL * 1000000) restartWith("мало пам'яті понад 3 хв: вільно " + String(memFree()) + " Б", false);
  } else lowSince = 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_BTN, INPUT_PULLUP);
  memStage("старт");
  utilBegin();
  tlsPoolBegin();                        // 1.0.32: запас пам'яті для TLS — до першого з'єднання
  settingsLoad();
  statsBegin();
  memStage("налаштування");
  sigBegin();
  lcdBegin();
  memStage("сигнали");
  logEvent(String("Запуск «Сторожа світла» ") + FW_VERSION + " (" + resetReasonText() + ")");
  boxReport();                           // 1.0.29: перезапуск був несподіваний — чому й що було перед тим
  crashBoot();                           // 1.0.41: після збою чи сторожа — підсумок дампу з флешу (де стояла програма)
  bridgeBegin();
  memStage("Wi-Fi");
  watchBegin();
  schBegin();                            // до tgBegin: задача Telegram одразу питає графік
  tgBegin();
  memStage("Telegram");
  webBegin();
  otaBegin();
  memStage("сторінка");
  logEvent(wifiTuneText());
  logEvent(memBootLine());               // 1.0.38: куди пішла пам'ять на запуску
  printInfo();
  //  Сторож основного циклу: 30 с (довше за будь-який запит сторінки).
  esp_task_wdt_config_t wdt = { .timeout_ms = 30000, .idle_core_mask = 1, .trigger_panic = true };   // і простій ядра 0 (Wi-Fi)
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
}

void loop() {
  memSample();                                   // 1.0.38: найменше вільної пам'яті — кожні 20 мс
  bridgeLoop();
  watchLoop();
  bmsLoop();                                     // 1.0.36: батарея — сповіщення
  schLoop();
  tgLoop();
  webLoop();
  serialCmd();
  bootButton();
  pwButton();
  guards();
  selfGuard();
  sigLoop(watchGrid(), watchLine());
  lcdLoop();
  delay(2);
}
