#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Перевірка запуску плати після прошивки по USB (18.11.2026).

    venv/bin/python tools/boot_check.py <порт> <шаблон рядка запуску> [секунд]   — відкриває порт (це перезапускає плату)
    venv/bin/python tools/boot_check.py --file <журнал.txt> <шаблон>             — перевірка на збереженому тексті

Чому: 18.11 об 11:59 запис 1.0.6 обірвався (esptool вийшов з помилкою), а я дивився лише останній рядок виводу.
Плата до 12:15 вантажилась по колу («No bootable app partitions»), без сторінки й сповіщень. Тепер «готово» лише тоді,
коли плата сама надрукувала рядок запуску потрібної версії й не впала одразу після нього.
Код виходу: 0 — запустилась; 1 — ні (друкує докази); 2 — не вдалося прочитати порт."""
import re, sys, time

FATAL = [r'No bootable app', r'is not bootable', r'invalid segment length', r'invalid magic byte', r'Guru Meditation',
         r'abort\(\) was called', r'Backtrace:', r'Brownout detector', r'flash read err', r'Rebooting\.\.\.']


def judge(text, want):
    lines = [l.rstrip('\r') for l in text.replace('\r\n', '\n').split('\n')]
    boots = sum(1 for l in lines if l.startswith('rst:'))
    hit = next((i for i, l in enumerate(lines) if re.search(want, l)), -1)
    bad = [l for l in lines if any(re.search(p, l) for p in FATAL)]
    bad_after = [l for l in lines[hit + 1:] if any(re.search(p, l) for p in FATAL)] if hit >= 0 else bad
    ok = hit >= 0 and not bad_after
    return ok, hit >= 0, boots, bad, lines


def read_port(port, sec, want):
    import serial
    for attempt in (1, 2):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.2
        s.dtr = False
        s.rts = False
        s.open()
        t0, buf, seen_at, glitch = time.time(), b'', None, False
        #  Рідний USB (ESP32-C3/S3, cu.usbmodem*): відкриття порту плату не перезапускає, а рядок запуску вона друкує,
        #  поки порт ще ніхто не слухає, — тож питаємо «info» (маячок друкує той самий рядок з версією).
        native, asked = 'usbmodem' in port, 0
        while time.time() - t0 < sec:
            if native and seen_at is None and asked < 3 and time.time() - t0 > 1.5 + 5 * asked:
                s.write(b'info\n')
                asked += 1
            c = s.read(4096)
            if c:
                buf += c
            if len(buf) > 12000 * (time.time() - t0 + 2):         # збій драйвера CP2102: «повтор хвоста»
                glitch = True
                break
            if seen_at is None and re.search(want, buf.decode('utf-8', 'replace')):
                seen_at = time.time()
            if seen_at and time.time() - seen_at > 4:                # ще 4 с після рядка запуску: чи не впала одразу
                break
        s.close()
        if not glitch:
            return buf.decode('utf-8', 'replace')
        print('[boot_check: збій драйвера порту — читаю ще раз]')
        time.sleep(2)
    return buf.decode('utf-8', 'replace')


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == '--file':
        text, want = open(a[1], encoding='utf-8', errors='replace').read(), a[2]
    elif len(a) >= 2:
        want, sec = a[1], float(a[2]) if len(a) > 2 else 45
        try:
            text = read_port(a[0], sec, want)
        except Exception as e:
            print('ПОРТ НЕ ПРОЧИТАНО:', e)
            return 2
    else:
        print(__doc__)
        return 2
    ok, seen, boots, bad, lines = judge(text, want)
    if ok:
        line = next(l for l in lines if re.search(want, l))
        print(f'ПЛАТА ЗАПУСТИЛАСЬ: «{line.strip()}»')
        return 0
    print('ПЛАТА НЕ ЗАПУСТИЛАСЬ як треба:' + ('' if seen else f' рядка «{want}» не було;') + f' рядків «rst:» у записі: {boots} (повтори драйвера CP2102 теж рахуються)')
    shown = []
    for l in bad + [l for l in lines if l.strip()][-8:]:
        if l not in shown:
            shown.append(l)
    for l in shown[:16]:
        print('   ', l[:200])
    return 1


if __name__ == '__main__':
    sys.exit(main())
