#!/bin/zsh
# Перевірка маячків (watch.cpp) на комп'ютері: tests/watch/run.sh
cd "${0:A:h}" || exit 1
F=../../firmware/avr-mist
G=/tmp/watch-test-gen; mkdir -p $G
for f in strToMac htmlEsc jsonEsc macToStr; do sed -n "/^[A-Za-z].* $f(/,/^}/p" $F/util.cpp; done > $G/util_part.cpp
clang++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Ishim -I$F -I$G -o /tmp/watch-test test.cpp || exit 1
/tmp/watch-test || exit 1
python3 -c "import json; d=json.load(open('/tmp/watch-test.json')); print('JSON для сторінки — правильний:', [ (b['mac'], b.get('role'), b.get('name')) for b in d['bcn']['list'] ])"
