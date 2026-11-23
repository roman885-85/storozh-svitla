#!/bin/zsh
# Перевірка розбору кадрів JK BMS (firmware/avr-bms/jk_core.h) на Mac: tests/bms/run.sh
cd "${0:A:h}" || exit 1
clang++ -std=c++17 -O1 -Wall -Wextra -I../../firmware/avr-bms -o /tmp/bms-test test.cpp || exit 1
/tmp/bms-test
