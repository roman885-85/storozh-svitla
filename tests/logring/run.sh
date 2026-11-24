#!/bin/zsh
# Перевірка кільця журналу (firmware/avr-mist/logring.h) на комп'ютері: tests/logring/run.sh
cd "${0:A:h}" || exit 1
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I../../firmware/avr-mist -o /tmp/logring-test test.cpp || exit 1
/tmp/logring-test
