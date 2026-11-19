#!/bin/zsh
# Картинки екрана дисплея з того самого коду, що в прошивці: tests/lcd/run.sh → tests/lcd/out/*.png
cd "${0:A:h}" || exit 1
mkdir -p out && rm -f out/*(N)
clang++ -std=c++17 -O1 -Wall -Wextra -o /tmp/lcd-test test.cpp && /tmp/lcd-test || exit 1
for f in out/*.pgm; do sips -s format png "$f" --out "${f:r}.png" >/dev/null 2>&1 && rm "$f"; done
ls out
