#!/bin/zsh
# Перевірка звуку й світла мосту (firmware/avr-bms/alert_core.h) на комп'ютері: tests/alert/run.sh
cd "${0:A:h}" || exit 1
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I../../firmware/avr-bms -o /tmp/alert-test test.cpp || exit 1
/tmp/alert-test
