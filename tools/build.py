#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Збирання прошивок «Сторожа світла» на будь-якій системі — Windows, Linux, macOS.

    python3 tools/build.py --setup                 один раз: ядра esp32 3.3.3 і esp8266 3.1.2 для arduino-cli
    python3 tools/build.py [all|mist|mayak|bms]    зібрати (усе — без аргументу)
    python3 tools/build.py mayak:esp8266,usb       лише ці варіанти маячка
    (у Windows — py tools\\build.py …)

    mist  — Сторож (ESP32 WROOM), версія — firmware/VERSION
    mayak — маячок на будь-якій недорогій платі ESP, версія — firmware/VERSION-mayak: ESP32-C3 (usb — рідний USB,
            uart — з перетворювачем USB-UART), esp32 (ESP32 DevKit), s2, s3, c6, esp8266 (ESP-01S, D1 mini, NodeMCU)
    bms   — міст батареї JK BMS (ESP32-C3, ті самі два варіанти), версія — firmware/VERSION-bms

Результат — build/<назва>/…-full.bin (усе одним образом з адреси 0x0, для прошивки кабелем) і …-app.bin (для
оновлення зі сторінки). Потрібні Python 3.8+ і arduino-cli (у PATH чи змінна ARDUINO_CLI); esptool для склеювання
образу — той, що приходить разом з ядром esp32. Теку ядра (Arduino15) питаємо в arduino-cli; інакше — змінна
ARDUINO15 чи стандартне місце системи. Раніше (до 24.11.2026) збирав лише bash-скрипт для macOS."""
import datetime, glob, os, re, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.environ.get('BUILD_OUT') or os.path.join(ROOT, 'build')   # BUILD_OUT — зібрати деінде (перевірки)
CORE_VER = '3.3.3'
INDEX = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
INDEX8266 = 'https://arduino.esp8266.com/stable/package_esp8266com_index.json'
CORE8266 = '3.1.2'
#  Маячок (1.1.0): варіант → (FQBN, чип для esptool, адреса завантажувача; None — у ESP8266 образ і так з адреси 0x0)
MAYAK = [
    ('usb', 'esp32:esp32:AirM2M_CORE_ESP32C3:CDCOnBoot=cdc', 'esp32c3', '0x0'),
    ('uart', 'esp32:esp32:AirM2M_CORE_ESP32C3:CDCOnBoot=default', 'esp32c3', '0x0'),
    ('esp32', 'esp32:esp32:esp32', 'esp32', '0x1000'),
    ('s2', 'esp32:esp32:esp32s2:CDCOnBoot=cdc', 'esp32s2', '0x1000'),
    ('s3', 'esp32:esp32:esp32s3:CDCOnBoot=cdc', 'esp32s3', '0x0'),
    ('c6', 'esp32:esp32:esp32c6:CDCOnBoot=cdc', 'esp32c6', '0x0'),
    ('esp8266', 'esp8266:esp8266:generic', 'esp8266', None),
]

if hasattr(sys.stdout, 'reconfigure'):            # консоль Windows: кирилиця не має валити скрипт
    sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)   # хід — одразу, і в журналі
    sys.stderr.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)


def die(msg, code=1):
    print(msg)
    sys.exit(code)


def cli():
    c = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli')
    if not c:
        die('Немає arduino-cli: https://arduino.github.io/arduino-cli/latest/installation/ (чи змінна ARDUINO_CLI з повним шляхом)')
    return c


def data_dir():
    """Тека Arduino15 (ядра й інструменти)."""
    env = os.environ.get('ARDUINO15')
    if env:
        return env
    try:
        r = subprocess.run([cli(), 'config', 'get', 'directories.data'], capture_output=True, text=True, timeout=60)
        d = r.stdout.strip()
        if r.returncode == 0 and d and os.path.isdir(d):
            return d
    except Exception:
        pass
    home = os.path.expanduser('~')
    if sys.platform == 'darwin':
        return os.path.join(home, 'Library', 'Arduino15')
    if os.name == 'nt':
        return os.path.join(os.environ.get('LOCALAPPDATA', home), 'Arduino15')
    return os.path.join(home, '.arduino15')


def _vkey(p):
    return [int(x) if x.isdigit() else 0 for x in re.split(r'[.\-]', os.path.basename(p))]


def core_and_esptool():
    a15 = data_dir()
    core = os.path.join(a15, 'packages', 'esp32', 'hardware', 'esp32', CORE_VER)
    if not os.path.isdir(core):
        die('Немає ядра esp32 %s у %s — спершу: python3 tools/build.py --setup' % (CORE_VER, a15))
    for d in sorted(glob.glob(os.path.join(a15, 'packages', 'esp32', 'tools', 'esptool_py', '*')), key=_vkey, reverse=True):
        for n in ('esptool.exe', 'esptool'):
            p = os.path.join(d, n)
            if os.path.isfile(p):
                return core, [p]
        p = os.path.join(d, 'esptool.py')
        if os.path.isfile(p):
            return core, [sys.executable, p]
    die('У ядрі esp32 немає esptool (%s) — перевстановіть: python3 tools/build.py --setup' % a15)


def setup():
    c = cli()
    urls = INDEX + ',' + INDEX8266
    for args in (['core', 'update-index'], ['core', 'install', 'esp32:esp32@' + CORE_VER], ['core', 'install', 'esp8266:esp8266@' + CORE8266]):
        print('>>> arduino-cli ' + ' '.join(args))
        if subprocess.run([c] + args + ['--additional-urls', urls]).returncode:
            die('arduino-cli не впорався — див. вище')
    core_and_esptool()
    print('Готово: ядра esp32 %s і esp8266 %s у %s' % (CORE_VER, CORE8266, data_dir()))


def have8266():
    return os.path.isdir(os.path.join(data_dir(), 'packages', 'esp8266', 'hardware', 'esp8266', CORE8266))


def read_ver(name):
    return open(os.path.join(ROOT, 'firmware', name), encoding='utf-8').read().strip()


def ver_h(sketch, ver, stamp):
    with open(os.path.join(sketch, 'version.h'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Створює tools/build.py під час збирання — вручну не правити. */\n#pragma once\n'
                '#define FW_VERSION "%s"\n#define FW_BUILD   "%s"\n' % (ver, stamp))


def rm(pattern):
    for p in glob.glob(pattern):
        os.remove(p)


def compile_sketch(fqbn, sketch, bp, od, props=()):
    cmd = [cli(), 'compile', '--fqbn', fqbn, '--build-path', bp, '--output-dir', od]
    for p in props:
        cmd += ['--build-property', p]
    r = subprocess.run(cmd + [sketch], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in r.stdout.decode('utf-8', 'replace').splitlines():
        if line.strip() and not line.startswith(('Used ', 'Using ')):
            print(line)
    return r.returncode


def merge(chip, d, name, boot, ver, core, esptool):
    """Повний образ з адреси 0x0 (завантажувач, розділи, boot_app0, програма) і окремо програма для оновлення."""
    full = '%s-%s-full.bin' % (name, ver)
    if boot is None:                                  # ESP8266: образ збірки вже з адреси 0x0, разом із завантажувачем
        shutil.copyfile(os.path.join(d, name + '.ino.bin'), os.path.join(d, full))
        shutil.copyfile(os.path.join(d, name + '.ino.bin'), os.path.join(d, '%s-%s-app.bin' % (name, ver)))   # той самий образ
        print('    ' + os.path.join(d, full))
        return
    cmd = esptool + ['--chip', chip, 'merge-bin', '-o', full, '--flash-mode', 'keep', '--flash-freq', 'keep',
                     '--flash-size', 'keep', boot, name + '.ino.bootloader.bin', '0x8000', name + '.ino.partitions.bin',
                     '0xe000', os.path.join(core, 'tools', 'partitions', 'boot_app0.bin'), '0x10000', name + '.ino.bin']
    r = subprocess.run(cmd, cwd=d, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode:
        print(r.stdout.decode('utf-8', 'replace'))
        die('НЕ ВДАЛОСЬ СКЛЕЇТИ ОБРАЗ %s' % full)
    shutil.copyfile(os.path.join(d, name + '.ino.bin'), os.path.join(d, '%s-%s-app.bin' % (name, ver)))
    print('    ' + os.path.join(d, full))


def build(what):
    core, esptool = core_and_esptool()
    stamp = datetime.datetime.now().strftime('%d.%m.%Y %H:%M')
    os.makedirs(OUT, exist_ok=True)
    fw = os.path.join(ROOT, 'firmware')

    if what in ('all', 'mist'):
        s, ver = os.path.join(fw, 'avr-mist'), read_ver('VERSION')
        ver_h(s, ver, stamp)
        print('>>> Сторож світла %s (%s)' % (ver, stamp))
        od = os.path.join(OUT, 'mist')
        rm(os.path.join(od, 'avr-mist.ino.*'))       # старий результат не має «пройти» замість нового (18.11.2026)
        rm(os.path.join(od, 'avr-mist-%s-*.bin' % ver))
        rc = compile_sketch('esp32:esp32:esp32:PartitionScheme=min_spiffs,DebugLevel=none', s, os.path.join(OUT, 'mist-bp'), od,
                            ['compiler.c.elf.extra_flags=-Wl,--wrap=dns_clear_cache -Wl,--wrap=esp_wifi_init'])
        if rc or not os.path.isfile(os.path.join(od, 'avr-mist.ino.bin')):
            die('ЗБІРКА СТОРОЖА НЕ ВДАЛАСЬ (код %s)' % rc)
        merge('esp32', od, 'avr-mist', '0x1000', ver, core, esptool)

    if what in ('all', 'mayak') or what.startswith('mayak:'):
        s, ver = os.path.join(fw, 'avr-mayak'), read_ver('VERSION-mayak')
        ver_h(s, ver, stamp)
        only = what.split(':', 1)[1].split(',') if what.startswith('mayak:') else None   # mayak:esp8266,s3 — лише ці
        for v, fqbn, chip, boot in MAYAK:
            if only and v not in only:
                continue
            if v == 'esp8266' and not have8266():
                print('>>> маячок %s, ESP8266 — ПРОПУЩЕНО: немає ядра esp8266 %s (python3 tools/build.py --setup)' % (ver, CORE8266))
                continue
            print('>>> маячок %s, варіант %s (%s)' % (ver, v, stamp))
            od = os.path.join(OUT, 'mayak-' + v)
            rm(os.path.join(od, 'avr-mayak.ino.*'))
            rm(os.path.join(od, 'avr-mayak-%s-*.bin' % ver))
            rc = compile_sketch(fqbn, s, os.path.join(OUT, 'mayak-%s-bp' % v), od)
            if rc or not os.path.isfile(os.path.join(od, 'avr-mayak.ino.bin')):
                die('ЗБІРКА МАЯЧКА НЕ ВДАЛАСЬ (%s, код %s)' % (v, rc))
            merge(chip, od, 'avr-mayak', boot, ver, core, esptool)

    #  Міст батареї (22.11.2026): Bluetooth і Wi-Fi разом не вміщаються в типові 1,25 МБ — схема min_spiffs (1,9 МБ на
    #  прошивку, з оновленням по мережі); у меню цієї плати її немає, тож задаємо властивостями збірки.
    if what in ('all', 'bms'):
        s, ver = os.path.join(fw, 'avr-bms'), read_ver('VERSION-bms')
        shutil.copyfile(os.path.join(fw, 'avr-mist', 'lcd_font.h'), os.path.join(s, 'lcd_font.h'))   # шрифт — один на обидві
        ver_h(s, ver, stamp)                          # версія — не файлом «VERSION» у теці скетча: без розрізнення регістру
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gz_page.py'),           # (macOS, Windows) він
                            os.path.join(s, 'page.h'), os.path.join(s, 'page_gz.h')])                  # заступає <version>
        if r.returncode:
            die('НЕ ВДАЛОСЬ СТИСНУТИ СТОРІНКУ МОСТУ')
        for v in ('usb', 'uart'):
            print('>>> міст батареї %s, варіант %s (%s)' % (ver, v, stamp))
            od = os.path.join(OUT, 'bms-' + v)
            rm(os.path.join(od, 'avr-bms.ino.*'))
            rm(os.path.join(od, 'avr-bms-%s-*.bin' % ver))
            rc = compile_sketch('esp32:esp32:AirM2M_CORE_ESP32C3:CDCOnBoot=' + ('cdc' if v == 'usb' else 'default'), s,
                                os.path.join(OUT, 'bms-%s-bp' % v), od,
                                ['build.partitions=min_spiffs', 'upload.maximum_size=1966080'])
            if rc or not os.path.isfile(os.path.join(od, 'avr-bms.ino.bin')):
                die('ЗБІРКА МОСТУ БАТАРЕЇ НЕ ВДАЛАСЬ (код %s)' % rc)
            merge('esp32c3', od, 'avr-bms', '0x0', ver, core, esptool)


def main():
    a = sys.argv[1:]
    if a[:1] == ['--setup']:
        return setup()
    what = a[0] if a else 'all'
    if what not in ('all', 'mist', 'mayak', 'bms') and not what.startswith('mayak:'):
        die(__doc__, 2)
    build(what)


if __name__ == '__main__':
    main()
