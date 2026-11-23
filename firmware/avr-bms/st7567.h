#pragma once
/*  Дисплей Open-Smart 1,8″ 128×64 на ST7567 — запис по SPI (1.1.0).

    Зразок — робочий проєкт власника github.com/roman885-85/moto_battery_reader (display.h, 22.11.2026: «в гитхабе в
    проекте battery reader есть библиотека по работе с этим дисплеем»). Там цей дисплей працює через U8g2 у варіанті
    ENH_DG128064, і прямо записано: «коректна електрика (bias 1/9, помірний контраст — варіант OS12864 з bias 1/7 дає
    темний засвічений екран)», а картинку зсунуто на 4 точки («RAM ST7567 на 132 колонки, скло показує 4..131»).
    Тут та сама послідовність запуску (u8x8_st7567_enh_dg128064_init_seq): зсув 1/9, грубий рівень 3 (0x23), контраст
    50 із 63 — але без бібліотеки: буфер 1 КБ малює batlcd_core.h, сюди йдуть лише змінені шматки смуг.

    Виводи модуля: CLK (такт), DI (дані), CS (вибір), RS (він же A0/DC: дані чи команда), RST (він же RSE: скидання),
    LED (підсвітка), VCC 3,3 В, GND. Живлення — 3,3 В.  */
#include <Arduino.h>
#include <SPI.h>

class St7567 {
 public:
  struct Cfg {
    int8_t clk = 4, din = 6, cs = 7, rs = 5, rst = 10;   // rst < 0 — вивід скидання не під'єднано (лише програмне)
    uint8_t contrast = 50;       // 0–63 (у зразку: 200 >> 2 = 50)
    uint8_t ratio = 3;           // 0–7: грубий рівень напруги скла (команда 0x20 | ratio; у зразку — 0x23)
    bool bias7 = false;          // зсув 1/9 (0xA2, як у зразку) чи 1/7 (0xA3 — з ним у власника екран темний і засвічений)
    uint8_t xoff = 4;            // зсув картинки вправо, 0–4 точки (у зразку 4: скло показує стовпці 4…131 пам'яті)
    bool flip = false, inv = false;
  };
  bool ok() const { return up; }

  void begin(const Cfg& c) {
    end();
    cfg = c;
    pinMode(cfg.cs, OUTPUT); digitalWrite(cfg.cs, HIGH);
    pinMode(cfg.rs, OUTPUT); digitalWrite(cfg.rs, HIGH);
    if (cfg.rst >= 0) {
      pinMode(cfg.rst, OUTPUT);
      digitalWrite(cfg.rst, HIGH); delay(2);
      digitalWrite(cfg.rst, LOW); delay(5);
      digitalWrite(cfg.rst, HIGH); delay(5);
    }
    SPI.begin(cfg.clk, -1, cfg.din, -1);
    up = true;
    //  Порядок — точно як у зразку (U8g2, u8x8_st7567_enh_dg128064_init_seq), далі «вийти з економії» (A4, AF)
    cmd(0xE2);                                     // програмне скидання
    cmd(0xAE);                                     // дисплей вимкнено — поки налаштовуємо
    cmd(0x40);                                     // перший рядок — нульовий
    cmd(cfg.flip ? 0xA0 : 0xA1);                   // напрям стовпців (у зразку A1)
    cmd(cfg.flip ? 0xC8 : 0xC0);                   // напрям рядків (у зразку C0)
    cmd(cfg.inv ? 0xA7 : 0xA6);                    // звичайно чи навиворіт
    cmd(cfg.bias7 ? 0xA3 : 0xA2);                  // зсув (у зразку A2 — 1/9)
    cmd(0x2C); delay(50);                          // живлення скла — поступово: підвищувач, регулятор, повторювач
    cmd(0x2E); delay(50);
    cmd(0x2F); delay(50);
    cmd(0x20 | (cfg.ratio & 7));                   // грубий рівень (у зразку 0x23)
    cmd(0x81); cmd(cfg.contrast & 63);             // контраст (у зразку 50)
    cmd(0xA4);                                     // показувати пам'ять (не «усі точки»)
    cmd(0xAF);                                     // дисплей увімкнено
    valid = false;
  }
  void end() {
    if (!up) return;
    cmd(0xAE);
    SPI.end();
    for (int8_t p : {cfg.cs, cfg.rs, cfg.rst, cfg.clk, cfg.din}) if (p >= 0) pinMode(p, INPUT);
    up = false;
  }
  //  Контраст, зсув, інверсія, поворот — без перезапуску дисплея. Те саме шлемо раз на хвилину: перешкода на дротах
  //  може збити регістри, а вміст і так перемальовується.
  void apply(const Cfg& c) {
    bool turn = c.flip != cfg.flip || c.xoff != cfg.xoff;
    cfg.contrast = c.contrast; cfg.ratio = c.ratio; cfg.bias7 = c.bias7; cfg.flip = c.flip; cfg.inv = c.inv; cfg.xoff = c.xoff;
    if (!up) return;
    registers();
    if (turn) valid = false;                       // інший зсув стовпців — перемалювати все
  }
  void refresh() { if (up) { registers(); cmd(0xA4); cmd(0xAF); valid = false; } }

  //  Вміст: fb — 8 смуг по 128 байтів (байт — стовпчик з 8 точок, біт 0 — верхня). Шлемо лише змінене.
  void flush(const uint8_t* fb) {
    if (!up) return;
    //  Пам'ять — 132 стовпці, скло — 128: у звичайному положенні (A1) видно 4…131, у перевернутому (A0) — 0…127
    const int xo = cfg.xoff > 4 ? 4 : cfg.xoff, xoff = cfg.flip ? 4 - xo : xo;
    for (int p = 0; p < 8; p++) {
      const uint8_t* row = fb + p * 128;
      uint8_t* old = sent + p * 128;
      int a = 0, b = 127;
      if (valid) {
        while (a < 128 && row[a] == old[a]) a++;
        if (a == 128) continue;
        while (b > a && row[b] == old[b]) b--;
      }
      int x = a + xoff;
      SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
      digitalWrite(cfg.cs, LOW);
      digitalWrite(cfg.rs, LOW);
      SPI.transfer(0xB0 | p);
      SPI.transfer(0x10 | (x >> 4));
      SPI.transfer(0x00 | (x & 15));
      digitalWrite(cfg.rs, HIGH);
      for (int i = a; i <= b; i++) SPI.transfer(row[i]);
      digitalWrite(cfg.cs, HIGH);
      SPI.endTransaction();
      memcpy(old + a, row + a, b - a + 1);
    }
    valid = true;
  }

 private:
  Cfg cfg;
  bool up = false, valid = false;
  uint8_t sent[1024];
  void cmd(uint8_t c) {
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cfg.cs, LOW);
    digitalWrite(cfg.rs, LOW);
    SPI.transfer(c);
    digitalWrite(cfg.cs, HIGH);
    SPI.endTransaction();
  }
  void registers() {
    cmd(0x40);                                     // перший рядок — нульовий
    cmd(cfg.flip ? 0xA0 : 0xA1);                   // напрям стовпців
    cmd(cfg.flip ? 0xC8 : 0xC0);                   // напрям рядків
    cmd(cfg.inv ? 0xA7 : 0xA6);                    // звичайно чи навиворіт
    cmd(cfg.bias7 ? 0xA3 : 0xA2);                  // зсув 1/7 чи 1/9
    cmd(0x20 | (cfg.ratio & 7));                   // грубий рівень напруги скла
    cmd(0x81); cmd(cfg.contrast & 63);             // контраст
  }
};
