# Storozh svitla ("Power Watchman")

[Українська](README.md) · [Manual (Ukrainian)](docs/ІНСТРУКЦІЯ.md) · [Wiring diagrams (PDF, Ukrainian)](docs/СХЕМИ.pdf) ·
[Releases — ready-to-flash files](https://github.com/roman885-85/storozh-svitla/releases/latest)

**Telegram notifications when mains power goes off and comes back — for any building with a TOMZN / eWeLink automatic
transfer switch (ATS), without a single wire to the ATS.** An ESP32 becomes a separate Wi-Fi network for the ATS and knows
whether mains power is present from whether the ATS is on that network. Optional: a generator, individual phases and
lines, a battery with a JK BMS, the YASNO outage schedule (Ukraine), and on-site lights, sound and a display. The user
interface and documentation are in Ukrainian.

![Nokia 5110 display in different states](tests/lcd/out/all.png)

## What it is for

| Where | What you get |
|---|---|
| Office, shop, warehouse, workshop, café | whether to open today; when to start and stop the generator; whether fridges are without power |
| School, kindergarten, shelter, community space | whether there is power before a class or a meeting; the person in charge knows about the generator |
| Apartment building, entrance, lift, pump station | residents know whether the lift and the water pump work; warnings about scheduled outages |
| House, country house | whether there is power while you are away; the inverter battery's charge |
| Boiler room, server room, farm | voltage on important lines and phases, the generator, the battery |

What it can watch:

- **Mains power** — the core: Storozh plus a TOMZN TOQ7E ATS. Instead of the ATS, any Wi-Fi device powered from mains
  only (a smart plug or relay on a mains-only outlet) works the same way — Storozh pings it every second.
- **Generator** — an ESP32-C3 "beacon" plugged into the generator's outlet: running, stopped, "turn it off — mains is back".
- **Phases and individual lines** — a beacon on each phase or line: on a three-phase input the ATS only sees its own phase.
- **Battery and inverter** — a battery with a JK BMS plus an ESP32-C3 battery bridge: charge, time left, BMS alarms.
- **YASNO outage schedule** (Dnipro — DnEM and CEK, Kyiv — DTEK): a warning before an outage, tomorrow's schedule.
- **On-site signals** — an LED strip or two LEDs, a buzzer or a speaker, a Nokia 5110 display.
- **Statistics** — hours with and without power per day, week and month; a daily summary.

Several buildings — one Storozh per building, each with its own bot.

## How it works

The TOMZN TOQ7E ATS (eWeLink app) powers its Wi-Fi from the **mains** input only. Storozh — an ESP32 WROOM board —
joins the building's router and provides **its own** network for the ATS (and beacons), with NAT, so eWeLink keeps
working. ATS on Storozh's network and answering — mains is on; off the network or silent for 12 s — mains is off; back
online for 10 s — on. No guessing, and messages carry the real moment of the event.

A **beacon** is an optional ESP32-C3 plugged into an outlet: it says "I'm here" every 2 s; silence means no voltage there.
A **battery bridge** is an optional ESP32-C3 next to a battery with a **JK BMS**: it keeps a permanent Bluetooth
connection to the BMS and passes charge, voltage, current, cells, temperatures and alarms to Storozh every 5 s. Storozh
never connects or writes to the BMS.

## Getting started

1. Download `avr-mist-<version>-full.bin` from the [releases](https://github.com/roman885-85/storozh-svitla/releases/latest)
   and flash it to an ESP32 WROOM — in the browser ([esptool-js](https://espressif.github.io/esptool-js/), address `0x0`)
   or with `esptool.py --chip esp32 write_flash 0x0 avr-mist-<version>-full.bin`.
2. Power Storozh from the router's UPS. Join the "AVR-mist-XXXX" Wi-Fi from a phone (password: "avr" plus the last eight
   characters of the board's MAC address), open http://10.77.0.1, user admin.
3. Settings: object name and its icon in messages, router Wi-Fi, your own page and access point passwords — Save. Reserve a fixed address for
   Storozh in the router.
4. While mains is on, connect the ATS to "AVR-mist-XXXX" in the eWeLink app and press "Це АВР" ("this is the ATS") in the
   "Пристрої" (devices) card.
5. Create a bot with @BotFather, paste the token on the "Сповіщення" (notifications) tab, add yourself and others.
6. Optionally — beacons, the YASNO schedule, signals, a display, a battery.

Step by step with every setting — the [manual (Ukrainian)](docs/ІНСТРУКЦІЯ.md).

## Features

- **Telegram:** power lost / restored with durations and whether it matches the schedule; generator started, stopped,
  "turn it off" reminders; daily summary; up to 20 recipients (people and groups), each choosing what to receive and
  "quiet at night"; join requests with buttons; announcements from the admin to everyone. Undelivered messages wait in a
  queue that survives restarts.
- **Bot menu:** status, schedule, statistics, notification settings; for admins — users, log, device diagnostics,
  announcements.
- **YASNO outage schedule:** warning N minutes before an outage, tomorrow's schedule and changes, emergency outages.
- **Statistics:** hours with and without power per day, week and month; recent outages.
- **Beacons** (up to 6): generator, line or phase, "display only".
- **Battery (JK BMS)** via the battery bridge: a battery card; Telegram alerts when charge drops below 50/30/20/10 % or
  the remaining time falls under 3 h / 1 h while mains is off, BMS alarms, "BMS offline". The bridge page opens through
  Storozh's page; the bridge drives an Open-Smart 1.8" ST7567 display (128×64) and updates itself over the network from
  a file URL (http or https) with rollback.
- **Devices:** a clear list of what is bound to Storozh (router, ATS, beacons, battery bridge) and whether each is online
  now, plus the other devices on its network with what they are (unlisted beacon, phone, unknown — maybe the ATS) and
  "add beacon" / "this is the ATS" buttons.
- **Nokia 5110 display:** big status text, "off since 13:53 / 3 h 5 min already", the schedule as a scrolling line,
  flashing and blinking on events, announcements; display and wiring self-test from the web page.
- **On-site signals:** two LEDs or a WS2812B strip (colours, effects, wave), a buzzer or a speaker via the DAC
  (built-in sounds or your own file). At night the LEDs, the strip and the display backlight switch off (or dim), sound
  optionally too. Settings apply instantly.
- **Web settings page:** login, lockout after wrong passwords, wiring diagrams with the current pins, over-the-air
  updates with self-check and automatic rollback.
- **Reliability:** statistics, the Telegram queue, page logins and the current state survive restarts and updates. A
  missing internet or Telegram never restarts the device: messages wait in the queue and go out as soon as Telegram
  answers, marked as late and without duplicates. It restarts itself only when really stuck, recording the reason and the
  last log lines in a "black box"; after a crash the core dump can be downloaded from the page. A browser that stops
  reading a response no longer holds the device. Memory for TLS is reserved at boot, Telegram uses one long-lived
  connection, and the page shows the real free heap. It never writes flash on its own (except the core dump ESP-IDF
  writes on a crash).

## What you need

- An **ESP32 WROOM** board (DevKit V1, 30 pins) and a 5 V USB supply — **from the router's UPS**, so Storozh keeps
  working during an outage.
- A **TOMZN TOQ7E** ATS (eWeLink) — or another Wi-Fi device powered from mains only that answers ping.
- A router with internet (2.4 GHz Wi-Fi) and a Telegram bot (token from @BotFather — step by step on Storozh's page).
- Optional: **ESP32-C3** boards for beacons and the battery bridge (a battery with a Bluetooth **JK BMS**; bridge
  display — Open-Smart 1.8" ST7567); a **Nokia 5110** display (PCD8544, 3.3 V); a **WS2812B** strip or two LEDs; an
  active/passive buzzer or a speaker with a **PAM8403** amplifier; a password-reset button.

## Build from source

Arduino-ESP32 **3.3.3** via `arduino-cli`, partition scheme `min_spiffs` (macOS scripts):

    tools/build.sh                 # all firmwares → build/ (Storozh, beacon, battery bridge)
    tools/flash.sh mist --new      # first flash over USB (erases flash, adds the MAC to tools/devices.txt)
    tools/flash.sh mayak --new     # ESP32-C3 beacon
    tools/flash.sh bms --new       # ESP32-C3 battery bridge

After that, update over the network from the device's page. Host tests (macOS, clang): `tests/watch`, `tests/lcd`,
`tests/yasno`, `tests/bms`, `tests/bat`, `tests/batlcd`, `tests/logring`, `tests/webguard`, `tests/devices` (each has
`run.sh`).

## License

[GPL-3.0](LICENSE). Code comments and documentation are in Ukrainian.
