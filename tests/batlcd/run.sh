#!/bin/zsh
# Картинки екрана мосту батареї з того самого коду, що в прошивці: tests/batlcd/run.sh → tests/batlcd/out/*.png
cd "${0:A:h}" || exit 1
mkdir -p out && rm -f out/*(N)
clang++ -std=c++17 -O1 -Wall -Wextra -o /tmp/batlcd-test test.cpp && /tmp/batlcd-test || exit 1
clang++ -std=c++17 -O1 -Wall -Wextra -Ishim -o /tmp/batlcd-drv driver_test.cpp && /tmp/batlcd-drv || exit 1
for f in out/*.pgm; do sips -s format png "$f" --out "${f:r}.png" >/dev/null 2>&1 && rm "$f"; done
ls out
