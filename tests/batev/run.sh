#!/bin/zsh
# Перевірка подій батареї мосту (firmware/avr-bms/batev_core.h) на комп'ютері: tests/batev/run.sh
cd "${0:A:h}" || exit 1
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I../../firmware/avr-bms -o /tmp/batev-test test.cpp || exit 1
/tmp/batev-test
