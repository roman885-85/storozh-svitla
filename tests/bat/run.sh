#!/bin/zsh
# Перевірка батареї Сторожа (bms.cpp) на Mac: tests/bat/run.sh
cd "${0:A:h}" || exit 1
F=../../firmware/avr-mist
G=/tmp/bat-test-gen; mkdir -p $G
for f in strToMac htmlEsc jsonEsc macToStr; do sed -n "/^[A-Za-z].* $f(/,/^}/p" $F/util.cpp; done > $G/util_part.cpp
clang++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I../watch/shim -I$F -I$G -o /tmp/bat-test test.cpp || exit 1
/tmp/bat-test || exit 1
python3 -c "
import json
for n in ('fresh', 'alarm', 'lost', 'multi'):
    d = json.load(open('/tmp/bat-test-%s.json' % n))
    bs = d.get('bats', [])
    print('JSON «%s» правильний:' % n, {k: d.get(k) for k in ('on', 'ok', 'bridge', 'bn', 'per')},
          [{k: b.get(k) for k in ('k', 'lbl', 'ok', 'soc', 'alarm', 'left')} for b in bs], 'комірок', [len(b.get('cells', [])) for b in bs])
"
