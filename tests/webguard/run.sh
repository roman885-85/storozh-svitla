#!/bin/zsh
# Перевірка відліку часу відповіді сторінки (firmware/avr-mist/webguard.h) на Mac: tests/webguard/run.sh
cd "${0:A:h}" || exit 1
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I../../firmware/avr-mist -o /tmp/webguard-test test.cpp || exit 1
/tmp/webguard-test
