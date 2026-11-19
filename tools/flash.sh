#!/bin/bash
# Записує прошивку моста чи маячка по USB. Спершу читає тип чипа й MAC:
# чужу плату (годинник, радіо, інші проєкти) не чіпає.
#
#   tools/flash.sh mist  [--port /dev/cu.…] [--new]
#   tools/flash.sh mayak [--port /dev/cu.…] [--new] [--wifi "назва точки" "пароль"]
#   Пароль у командному рядку видно в історії оболонки — краще прошити без --wifi, а мережу задати окремо:
#   venv/bin/python tools/mayak_wifi.py /dev/cu.usbmodem… "AVR-mist-XXXX"  (питає пароль і не показує його)
#   tools/flash.sh mist --app build/mist/avr-mist-1.0.5-app.bin      — записати саме цю версію (відкат)
#
# --new  перша прошивка нової плати: стирає весь флеш і вносить її MAC у tools/devices.txt.
# Без --new пише лише плату, що вже є в tools/devices.txt з тією ж роллю; налаштування (NVS) лишаються.
# «Готово» — лише коли esptool записав і звірив усе, а плата сама надрукувала рядок запуску цієї версії
# (tools/boot_check.py). 18.11.2026 запис 1.0.6 обірвався, а я не перевірив — міст годину вантажився по колу.
# Діючий міст краще оновлювати по мережі (сторінка «Оновлення»): там нова версія з відкатом.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPTOOL=$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool | sort -V | tail -1)
CORE="$HOME/Library/Arduino15/packages/esp32/hardware/esp32/3.3.3"
DEV="$ROOT/tools/devices.txt"
ROLE="${1:-}"; [ $# -gt 0 ] && shift
PORT=""; NEW=0; WSSID=""; WPASS=""; APP=""
while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --new) NEW=1; shift ;;
    --wifi) WSSID="$2"; WPASS="$3"; shift 3 ;;
    --app) APP="$2"; shift 2 ;;
    *) echo "Невідомий параметр: $1"; exit 2 ;;
  esac
done
case "$ROLE" in mist) WANT="ESP32" ;; mayak) WANT="ESP32-C3" ;; *) echo "Вкажіть, що прошивати: mist або mayak"; exit 2 ;; esac

if [ -z "$PORT" ]; then
  CANDS=( $(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* 2>/dev/null || true) )
  if [ ${#CANDS[@]} -ne 1 ]; then echo "Портів плат: ${#CANDS[@]} ${CANDS[*]:-}. Вкажіть --port."; exit 2; fi
  PORT="${CANDS[0]}"
fi
if lsof "$PORT" >/dev/null 2>&1; then echo "Порт $PORT зайнятий:"; lsof "$PORT"; exit 3; fi
#  Справжні програми, що тримають плати; оболонки (zsh -c «…»), де ці слова лише в тексті команди, і сам цей скрипт — не рахуються.
BUSY=$(ps -axo pid=,ppid=,command= | awk -v me=$$ -v pp=$PPID '$1!=me && $1!=pp && $2!=me' \
  | grep -E 'esptool|dump_flash|mpremote|usb_logger|serial_cmd\.py|pio (run|device)' \
  | grep -vE '^ *[0-9]+ +[0-9]+ +(/bin/|/usr/bin/)?(zsh|bash|sh) ' | grep -v grep || true)
if [ -n "$BUSY" ]; then echo "З платами вже працює інший процес (можливо, інший сеанс):"; echo "$BUSY"; exit 3; fi

echo ">>> хто на $PORT"
INFO=$("$ESPTOOL" --port "$PORT" read-mac 2>&1) || { echo "$INFO"; echo "Не вдалося прочитати плату"; exit 4; }
CHIP=$(echo "$INFO" | sed -nE 's/.*(Connected to|Detecting chip type\.\.\.) (ESP32[-A-Z0-9]*).*/\2/p' | head -1)
MAC=$(echo "$INFO" | grep -iE '^MAC' | grep -oiE '([0-9a-f]{2}:){5}[0-9a-f]{2}' | head -1 | tr 'A-F' 'a-f')
echo "    чип $CHIP, MAC $MAC"
[ -n "$CHIP" ] && [ -n "$MAC" ] || { echo "$INFO"; echo "Не розібрав відповідь esptool"; exit 4; }
[ "$CHIP" = "$WANT" ] || { echo "Це $CHIP, а для «$ROLE» потрібен $WANT — нічого не пишу"; exit 5; }

KNOWN=$(grep -iE "^$MAC[[:space:]]" "$DEV" | awk '{print $2}' | head -1 || true)
if [ "$KNOWN" = "ЧУЖИЙ" ]; then echo "Плата $MAC — інший прилад власника ($(grep -i "^$MAC" "$DEV")). Нічого не пишу."; exit 5; fi
if [ -n "$KNOWN" ] && [ "$KNOWN" != "$ROLE" ]; then echo "Плата $MAC записана як «$KNOWN», а не «$ROLE». Нічого не пишу."; exit 5; fi
if [ -z "$KNOWN" ] && [ "$NEW" != 1 ]; then echo "Плати $MAC немає в tools/devices.txt. Якщо це нова плата для «$ROLE», запустіть з --new."; exit 5; fi

if [ "$ROLE" = mist ]; then
  DIR="$ROOT/build/mist"; NAME=avr-mist; BOOT=0x1000; ECHIP=esp32
else
  case "$PORT" in */cu.usbmodem*) V=usb ;; *) V=uart ;; esac
  DIR="$ROOT/build/mayak-$V"; NAME=avr-mayak; BOOT=0x0; ECHIP=esp32c3
  echo "    варіант маячка: $V"
fi
VER="$(tr -d ' \n\r' < "$ROOT/firmware/VERSION")"
if [ -n "$APP" ]; then
  [ "$NEW" = 1 ] && { echo "--app і --new разом не можна: нова плата — лише повний образ"; exit 2; }
  [ -f "$APP" ] || { echo "Немає файлу $APP"; exit 6; }
  VER=$(basename "$APP" | sed -nE 's/.*-([0-9]+\.[0-9]+\.[0-9]+)-app\.bin$/\1/p')
  [ -n "$VER" ] || { echo "З назви $APP не видно версії (очікую …-1.2.3-app.bin)"; exit 6; }
  APPBIN="$APP"
else
  [ -f "$DIR/$NAME-$VER-full.bin" ] || { echo "Немає збірки $DIR/$NAME-$VER-full.bin — спершу tools/build.sh"; exit 6; }
  APPBIN="$DIR/$NAME-$VER-app.bin"
fi
[ -f "$APPBIN" ] || { echo "Немає $APPBIN"; exit 6; }
PY="$([ -x "$ROOT/venv/bin/python" ] && echo "$ROOT/venv/bin/python" || echo /usr/bin/python3)"
fail_write() { echo "!!! ПРОШИВКА НЕ ЗАПИСАЛАСЬ (esptool, код $1). Плата може не запускатись — повторіть, не від'єднуючи кабель."; exit 7; }

if [ "$NEW" = 1 ] && [ -z "$KNOWN" ]; then
  echo ">>> нова плата: стираю флеш і пишу $NAME $VER"
  "$ESPTOOL" --chip "$ECHIP" --port "$PORT" erase-flash || fail_write $?
  "$ESPTOOL" --chip "$ECHIP" --port "$PORT" write-flash 0x0 "$DIR/$NAME-$VER-full.bin" || fail_write $?
  printf '%s  %s  %s\n' "$MAC" "$ROLE" "перша прошивка $(date '+%d.%m.%Y %H:%M'), $CHIP" >> "$DEV"
else
  echo ">>> пишу $NAME $VER (налаштування лишаються)"
  "$ESPTOOL" --chip "$ECHIP" --port "$PORT" write-flash \
    "$BOOT" "$DIR/$NAME.ino.bootloader.bin" 0x8000 "$DIR/$NAME.ino.partitions.bin" \
    0xe000 "$CORE/tools/partitions/boot_app0.bin" 0x10000 "$APPBIN" || fail_write $?
fi

if [ "$ROLE" = mayak ] && [ -n "$WSSID" ]; then
  echo ">>> маячку — точка моста «$WSSID»"
  sleep 3
  "$PY" "$ROOT/tools/serial_cmd.py" "$PORT" "wifi \"$WSSID\" \"$WPASS\"" 6
fi

echo ">>> перевіряю запуск (відкриття порту перезапускає плату)"
#  до 1.0.10 рядок запуску «Запуск АВР-моста …», з 1.0.11 — «Запуск «Сторожа світла» …»
if [ "$ROLE" = mist ]; then WANTLINE="Запуск (АВР-моста|«Сторожа світла») ${VER//./\\.}"; else WANTLINE="АВР-маячок ${VER//./\\.}"; fi
"$PY" "$ROOT/tools/boot_check.py" "$PORT" "$WANTLINE" 45 || { echo "!!! ЗАПИСАНО, АЛЕ ПЛАТА НЕ ЗАПУСТИЛАСЬ — не готово"; exit 8; }
echo ">>> готово: $ROLE $VER на $MAC, запуск перевірено"
