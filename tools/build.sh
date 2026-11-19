#!/bin/bash
# Збирає прошивки АВР-моста на цьому Mac (arduino-cli, ядро esp32 3.3.3).
#   tools/build.sh          — обидві
#   tools/build.sh mist     — міст (ESP32 WROOM)
#   tools/build.sh mayak    — маячок (ESP32-C3 AirM2M Core; два варіанти: usb і uart)
# Результат — build/<назва>/…-full.bin (усе одним образом з адреси 0x0) і …-app.bin (для оновлення зі сторінки).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI=arduino-cli
CORE="$HOME/Library/Arduino15/packages/esp32/hardware/esp32/3.3.3"
ESPTOOL=$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool | sort -V | tail -1)
VER="$(tr -d ' \n\r' < "$ROOT/firmware/VERSION")"
BUILD="$(date '+%d.%m.%Y %H:%M')"
OUT="$ROOT/build"
WHAT="${1:-all}"
mkdir -p "$OUT"

ver_h() {
  printf '/* Створює tools/build.sh під час збирання — вручну не правити. */\n#pragma once\n#define FW_VERSION "%s"\n#define FW_BUILD   "%s"\n' "$VER" "$BUILD" > "$1/version.h"
}

merge() {   # chip outdir name bootaddr
  local chip=$1 dir=$2 name=$3 boot=$4
  ( cd "$dir"
    "$ESPTOOL" --chip "$chip" merge-bin -o "$name-$VER-full.bin" --flash-mode keep --flash-freq keep --flash-size keep \
      "$boot" "$name.ino.bootloader.bin" 0x8000 "$name.ino.partitions.bin" 0xe000 "$CORE/tools/partitions/boot_app0.bin" \
      0x10000 "$name.ino.bin" > /dev/null
    cp "$name.ino.bin" "$name-$VER-app.bin" )
  echo "    $dir/$name-$VER-full.bin"
}

if [ "$WHAT" = all ] || [ "$WHAT" = mist ]; then
  S="$ROOT/firmware/avr-mist"; ver_h "$S"
  echo ">>> Сторож світла $VER ($BUILD)"
  rm -f "$OUT/mist/avr-mist.ino."*       # старий результат не має «пройти» замість нового (18.11.2026)
  rm -f "$OUT/mist/avr-mist-$VER-app.bin" "$OUT/mist/avr-mist-$VER-full.bin"   # 19.11.2026: невдала збірка лишала старий .bin тієї ж версії
  rc=0
  { "$CLI" compile --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs,DebugLevel=none" \
      --build-path "$OUT/mist-bp" --output-dir "$OUT/mist" \
      --build-property "compiler.c.elf.extra_flags=-Wl,--wrap=dns_clear_cache" "$S" 2>&1 | grep -vE '^(Used |Using |$)'
    rc=${PIPESTATUS[0]}; } || true
  [ "$rc" -eq 0 ] && [ -f "$OUT/mist/avr-mist.ino.bin" ] || { echo "ЗБІРКА СТОРОЖА НЕ ВДАЛАСЬ (код $rc)"; exit 1; }
  merge esp32 "$OUT/mist" avr-mist 0x1000
fi

if [ "$WHAT" = all ] || [ "$WHAT" = mayak ]; then
  S="$ROOT/firmware/avr-mayak"; ver_h "$S"
  for v in usb uart; do
    cdc=$([ "$v" = usb ] && echo cdc || echo default)
    echo ">>> маячок $VER, варіант $v ($BUILD)"
    rm -f "$OUT/mayak-$v/avr-mayak.ino."*
    rc=0
    { "$CLI" compile --fqbn "esp32:esp32:AirM2M_CORE_ESP32C3:CDCOnBoot=$cdc" \
        --build-path "$OUT/mayak-$v-bp" --output-dir "$OUT/mayak-$v" "$S" 2>&1 | grep -vE '^(Used |Using |$)'
      rc=${PIPESTATUS[0]}; } || true
    [ "$rc" -eq 0 ] && [ -f "$OUT/mayak-$v/avr-mayak.ino.bin" ] || { echo "ЗБІРКА МАЯЧКА НЕ ВДАЛАСЬ (код $rc)"; exit 1; }
    merge esp32c3 "$OUT/mayak-$v" avr-mayak 0x0
  done
fi
