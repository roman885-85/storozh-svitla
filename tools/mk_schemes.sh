#!/bin/zsh
# docs/СХЕМИ.pdf зі сторінки схем Сторожа (firmware/avr-mist/schemes.cpp), з типовими виводами — як на новій платі.
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
rm -rf "$TMP"
