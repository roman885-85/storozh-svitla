#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сторінка мосту батареї стиснута (1.2.1): tools/gz_page.py firmware/avr-bms/page.h firmware/avr-bms/page_gz.h

Міст (ESP32-C3) з постійним Bluetooth віддає 38 КБ сторінки повільно, а Сторож тримає відповідь мосту для своєї
сторінки /bms/ не довше ~9 с: 23.11.2026 сторінка через DDNS роутера приходила обрізаною (скрипт не запускався).
Стиснута — ~12 КБ. Джерело — page.h (PAGE), стиснута копія — page_gz.h, її створює tools/build.sh."""
import gzip, sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()
html = s[s.index('R"PAGE(') + 7:s.index(')PAGE"')].encode('utf-8')
g = gzip.compress(html, compresslevel=9, mtime=0)
out = ['/* Створює tools/build.sh з page.h (tools/gz_page.py) — вручну не правити. */', '#pragma once',
       '#include <stdint.h>', '#include <stddef.h>',
       'static const size_t PAGE_GZ_LEN = %d;   // зі %d байтів сторінки' % (len(g), len(html)),
       'static const uint8_t PAGE_GZ[] PROGMEM = {']
for i in range(0, len(g), 24):
    out.append('  ' + ','.join('0x%02x' % b for b in g[i:i + 24]) + ',')
out.append('};')
open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('    сторінка мосту: %d → %d байтів (gzip)' % (len(html), len(g)))
