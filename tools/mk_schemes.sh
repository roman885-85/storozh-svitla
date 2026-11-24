#!/bin/zsh
# docs/СХЕМИ.pdf зі сторінки схем Сторожа (firmware/avr-mist/schemes.cpp), з типовими виводами — як на новій платі;
# docs/СХЕМИ-МОСТУ.pdf — схеми мосту батареї (сторінка мосту у вигляді /schemes, приклад виводів).
# Запуск: tools/mk_schemes.sh. Сторінка з файлу не має /api/signals, тож сама бере типові виводи.
cd "${0:A:h}/.." || exit 1
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
TMP=$(mktemp -d)
/usr/bin/python3 - "$TMP/schemes.html" <<'PY'
# -*- coding: utf-8 -*-
import sys
s = open('firmware/avr-mist/schemes.cpp', encoding='utf-8').read()
k = 'const char SCHEMES_HTML[] PROGMEM = R"PAGE('
a = s.index(k) + len(k)
e = s.index(')PAGE"', a)
open(sys.argv[1], 'w', encoding='utf-8').write(s[a:e])
PY
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --virtual-time-budget=6000 \
  --print-to-pdf="$PWD/docs/СХЕМИ.pdf" "file://$TMP/schemes.html" >/dev/null 2>&1
print -r -- "docs/СХЕМИ.pdf: $(stat -f %z docs/СХЕМИ.pdf) Б"
#  1.2.4: схеми мосту батареї — сторінка мосту (firmware/avr-bms/page.h) у вигляді «/schemes»; з файлу — приклад виводів
/usr/bin/python3 - "$TMP/bms.html" <<'PY2'
# -*- coding: utf-8 -*-
import sys
s = open('firmware/avr-bms/page.h', encoding='utf-8').read()
a = s.index('R"PAGE(') + 7
open(sys.argv[1], 'w', encoding='utf-8').write(s[a:s.index(')PAGE"', a)])
PY2
"$CHROME" --headless=new --disable-gpu --disable-extensions --no-pdf-header-footer --virtual-time-budget=6000 \
  --print-to-pdf="$PWD/docs/СХЕМИ-МОСТУ.pdf" "file://$TMP/bms.html#schemes" >/dev/null 2>&1
print -r -- "docs/СХЕМИ-МОСТУ.pdf: $(stat -f %z docs/СХЕМИ-МОСТУ.pdf) Б"
rm -rf "$TMP"
