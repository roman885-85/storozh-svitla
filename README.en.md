# Storozh svitla ("Power Watchman")

[Українська](README.md) · [Manual (Ukrainian)](docs/ІНСТРУКЦІЯ.md) · [Wiring diagrams (PDF, Ukrainian)](docs/СХЕМИ.pdf)

**Telegram notifications about mains power for any building with a TOMZN / eWeLink automatic transfer switch (ATS) —
without a single wire to the ATS.** An ESP32 becomes a separate Wi-Fi network for the ATS and knows whether mains power
is present from whether the ATS is on that network. Plus a generator, the YASNO outage schedule (Ukraine), a display,
an LED strip and sound. The user interface and documentation are in Ukrainian.

Built for a church in Ukraine: so people know whether there is power before they come, and the person in charge knows
when to start and stop the generator.

![Nokia 5110 display in different states](tests/lcd/out/all.png)

## How it works

The TOMZN TOQ7E ATS (eWeLink app) powers its Wi-Fi from the **mains** input only. Storozh — an ESP32 WROOM board —
joins the building's router and provides **its own** network just for the ATS, with NAT, so eWeLink keeps working.
ATS present on Storozh's network — mains is on; silent — mains is off. Storozh pings the ATS every second: "mains off"
after 12 s of silence ("on" after 10 s online), and messages carry the real moment of the event. Like a beacon: the ATS on the
network and answering means mains is on; off the network or silent means mains is off, no guessing.

A **beacon** is an optional ESP32-C3 plugged into the generator's outlet: online means the generator is running.
Beacons on individual lines or phases report where voltage is present.

A **battery bridge** (since 1.0.36) is an optional ESP32-C3 next to a battery with a **JK BMS**: it keeps a permanent
Bluetooth connection to the BMS and every 5 s passes state of charge, voltage, current, cells, temperatures and BMS alarms
to Storozh. Storozh never connects or writes to the BMS.

## Features

- **Telegram:** power lost / restored with durations and whether it matches the schedule; generator started, stopped,
  "turn it off" reminders; daily summary; up to 20 recipients (people and groups), each choosing what to receive and
  "quiet at night"; join requests with buttons; announcements from the admin to everyone. Undelivered messages wait in a
  queue that survives restarts.
- **Bot menu:** status, schedule, statistics, notification settings; for admins — users, log, device diagnostics,
  announcements.
- **YASNO outage schedule** (Dnipro — DnEM, CEK; Kyiv): warning N minutes before an outage, tomorrow's schedule and
  changes, emergency outages.
- **Statistics:** hours with and without power per day, week and month; recent outages.
- **Beacons** (up to 6): generator, line or phase, "display only".
- **Battery (JK BMS)** via the battery bridge: a battery card; Telegram alerts when charge drops below 50/30/20/10 % or
  the remaining time falls under 3 h / 1 h while mains is off, BMS alarms, "BMS offline". The bridge page opens through
  Storozh's page; the bridge drives an Open-Smart 1.8" ST7567 display (128×64) and updates itself over the network from
  a file URL (http or https) with rollback.
- **Devices:** a clear list of what is bound to Storozh (router, ATS, beacons, battery bridge) and whether each is online
  now, plus the other devices on its network with what they are (unlisted beacon, phone, unknown — maybe the ATS) and
  "add beacon" / "this is the ATS" buttons.
- **Nokia 5110 display:** big status text, "off since 13:53 / 3 h 5 min already", the schedule as a scrolling line
  (adjustable speed), flashing and blinking on events, announcements; display and wiring self-test from the web page.
- **On-site signals:** two LEDs or a WS2812B strip (colours, effects, wave), a buzzer or a speaker via the DAC
  (built-in sounds or your own file). At night the LEDs, the strip and the display backlight switch off (or dim), sound
  optionally too. Settings apply instantly.
- **Web settings page:** login, lockout after wrong passwords, wiring diagrams with the current pins, over-the-air
  updates with self-check and automatic rollback.
- **Reliability:** statistics, the Telegram queue, page logins and the current state with its start time survive
  restarts and updates (RTC memory); after a restart the state is back at once. A missing internet or Telegram never
  restarts the device: messages wait in the queue and go out as soon as Telegram answers, marked as late and without
  duplicates (before a retry the device checks whether the message is already in the chat). It restarts itself only
  when really stuck, recording the reason and the last log lines in a "black box". Telegram uses one long-lived
  connection, and memory for TLS (2 × 17 KB) is reserved at boot, so on a board without PSRAM a fragmented heap no
  longer breaks connections. The page shows the real free heap (the Arduino core also counts ~32 KB of instruction RAM
  ordinary code cannot use); the event log lives in that instruction RAM (250 lines). A browser that stops reading a
  response no longer holds the device: the connection is closed within seconds. After a crash or a watchdog reset the
  core dump can be downloaded from the page. It never writes flash on its own (except the core dump ESP-IDF writes on a
  crash).

## Build and flash

Arduino-ESP32 **3.3.3** via `arduino-cli`, partition scheme `min_spiffs` (macOS scripts):

    tools/build.sh                 # all firmwares → build/ (Storozh, beacon, battery bridge)
    tools/flash.sh mist --new      # first flash over USB (erases flash, adds the MAC to tools/devices.txt)
    tools/flash.sh mayak --new     # ESP32-C3 beacon
    tools/flash.sh bms --new       # ESP32-C3 battery bridge

After that, update over the network from the device's page. First setup from a phone: Wi-Fi "AVR-mist-XXXX",
page http://10.77.0.1.

Host tests (macOS, clang): `tests/watch`, `tests/lcd`, `tests/yasno`, `tests/bms`, `tests/bat`, `tests/batlcd`,
`tests/logring`, `tests/webguard`, `tests/devices` (each has `run.sh`).

## License

[GPL-3.0](LICENSE). Code comments and documentation are in Ukrainian.
