#!/bin/zsh
# Перевірка розбору графіка YASNO на комп'ютері: tests/yasno/run.sh [відповідь.json] — без аргументу бере свіжу з YASNO.
cd "${0:A:h}" || exit 1
J="${1:-}"
if [ -z "$J" ]; then J=$(mktemp); curl -sS -A ESP32HTTPClient -o "$J" "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/3/dsos/301/planned-outages" || exit 1; fi
clang++ -std=c++17 -O1 -Wall -Wextra -o /tmp/yasno-test test.cpp && /tmp/yasno-test "$J"
