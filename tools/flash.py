#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Прошивка плати кабелем USB на будь-якій системі — Windows, Linux, macOS. Спершу читає тип чипа й MAC: чужу плату
(інші прилади, інші проєкти) не чіпає.

    python3 tools/flash.py mist  [--port ПОРТ] [--new]
    python3 tools/flash.py mayak [--port ПОРТ] [--new] [--wifi "назва точки" "пароль"]
    python3 tools/flash.py bms   [--port ПОРТ] [--new]                — міст батареї JK BMS
    python3 tools/flash.py mist --app build/mist/avr-mist-1.0.5-app.bin   — записати саме цю версію (відкат)
    (у Windows — py tools\\flash.py …; ПОРТ — COM5, /dev/ttyUSB0, /dev/cu.usbserial-0001…)

--new  перша прошивка нової плати: стирає весь флеш і вносить її MAC у tools/devices.txt.
Без --new пише лише плату, що вже є в tools/devices.txt з тією ж роллю; налаштування (NVS) лишаються.
«Готово» — лише коли esptool записав і звірив усе, а плата сама надрукувала рядок запуску цієї версії
(tools/boot_check.py). Потрібен pyserial: python3 -m pip install pyserial. Пароль у --wifi видно в історії
командного рядка — краще задати мережу окремо: python3 tools/mayak_wifi.py ПОРТ "AVR-mist-XXXX" (питає пароль).
Діючий пристрій краще оновлювати по мережі (сторінка «Оновлення»): там нова версія з відкатом."""
import datetime, os, re, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build as B                                   # теки, esptool з ядра, версії

ROOT, DEV = B.ROOT, os.path.join(B.ROOT, 'tools', 'devices.txt')
USB_SERIAL = {0x10C4: 'CP210x', 0x1A86: 'CH34x', 0x0403: 'FTDI', 0x303A: 'Espressif (рідний USB)'}


def ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        B.die('Потрібен pyserial: python3 -m pip install pyserial (у Windows: py -m pip install pyserial)')
    return list(list_ports.comports())


def native_usb(port):
    """Рідний USB ESP32-C3 (Espressif, VID 0x303A) — варіант прошивки «usb»; міст USB-UART (CP210x, CH34x) — «uart»."""
    for p in ports():
        if p.device == port and p.vid is not None:
            return p.vid == 0x303A
    return bool(re.search(r'usbmodem|ttyACM', port))


def esptool_run(esptool, args, capture=False):
    if capture:
        r = subprocess.run(esptool + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return r.returncode, r.stdout.decode('utf-8', 'replace')
    return subprocess.run(esptool + args).returncode, ''


def busy(port):
    """Чи тримає порт інша програма (лише там, де це можна дізнатись: lsof у Linux і macOS; у Windows порт просто не відкриється)."""
    if os.name == 'nt' or not shutil.which('lsof'):
        return ''
    r = subprocess.run(['lsof', port], capture_output=True, text=True)
    return r.stdout.strip()


def main():
    a = sys.argv[1:]
    role = a.pop(0) if a else ''
    port, new, wssid, wpass, app = '', False, '', '', ''
    while a:
        x = a.pop(0)
        if x == '--port' and a: port = a.pop(0)
        elif x == '--new': new = True
        elif x == '--wifi' and len(a) >= 2: wssid, wpass = a.pop(0), a.pop(0)
        elif x == '--app' and a: app = a.pop(0)
        else: B.die('Невідомий параметр: %s\n\n%s' % (x, __doc__), 2)
    want = {'mist': 'ESP32', 'mayak': 'ESP32-C3', 'bms': 'ESP32-C3'}.get(role)
    if not want:
        B.die('Вкажіть, що прошивати: mist, mayak або bms\n\n' + __doc__, 2)
    core, esptool = B.core_and_esptool()

    if not port:
        c = [p.device for p in ports() if p.vid in USB_SERIAL]
        if len(c) != 1:
            B.die('Портів плат: %d %s. Вкажіть --port.' % (len(c), ' '.join(c)), 2)
        port = c[0]
    b = busy(port)
    if b:
        B.die('Порт %s зайнятий:\n%s' % (port, b), 3)

    print('>>> хто на %s' % port)
    rc, info = esptool_run(esptool, ['--port', port, 'read-mac'], capture=True)
    if rc:
        B.die(info + '\nНе вдалося прочитати плату', 4)
    m = re.search(r'(?:Connected to|Detecting chip type\.\.\.)\s+(ESP32[-A-Z0-9]*)', info)
    chip = m.group(1) if m else ''
    m = re.search(r'(?im)^MAC:?\s*(([0-9a-f]{2}:){5}[0-9a-f]{2})', info)
    mac = m.group(1).lower() if m else ''
    print('    чип %s, MAC %s' % (chip, mac))
    if not chip or not mac:
        B.die(info + '\nНе розібрав відповідь esptool', 4)
    if chip != want:
        B.die('Це %s, а для «%s» потрібен %s — нічого не пишу' % (chip, role, want), 5)

    known = ''
    if os.path.isfile(DEV):
        for line in open(DEV, encoding='utf-8'):
            f = line.split()
            if f and not line.startswith('#') and f[0].lower() == mac:
                known = f[1] if len(f) > 1 else ''
                break
    if known == 'ЧУЖИЙ':
        B.die('Плата %s — інший ваш прилад (tools/devices.txt). Нічого не пишу.' % mac, 5)
    if known and known != role:
        B.die('Плата %s записана як «%s», а не «%s». Нічого не пишу.' % (mac, known, role), 5)
    if not known and not new:
        B.die('Плати %s немає в tools/devices.txt. Якщо це нова плата для «%s», запустіть з --new.' % (mac, role), 5)

    if role == 'mist':
        d, name, boot, echip = os.path.join(ROOT, 'build', 'mist'), 'avr-mist', '0x1000', 'esp32'
        ver = B.read_ver('VERSION')
    else:
        v = 'usb' if native_usb(port) else 'uart'
        d = os.path.join(ROOT, 'build', ('bms-' if role == 'bms' else 'mayak-') + v)
        name, boot, echip = ('avr-bms' if role == 'bms' else 'avr-mayak'), '0x0', 'esp32c3'
        ver = B.read_ver('VERSION-bms' if role == 'bms' else 'VERSION-mayak')
        print('    варіант плати: %s' % v)
    if app:
        if new:
            B.die('--app і --new разом не можна: нова плата — лише повний образ', 2)
        if not os.path.isfile(app):
            B.die('Немає файлу %s' % app, 6)
        m = re.search(r'-(\d+\.\d+\.\d+)-app\.bin$', os.path.basename(app))
        if not m:
            B.die('З назви %s не видно версії (очікую …-1.2.3-app.bin)' % app, 6)
        ver, appbin = m.group(1), app
    else:
        if not os.path.isfile(os.path.join(d, '%s-%s-full.bin' % (name, ver))):
            B.die('Немає збірки %s — спершу python3 tools/build.py' % os.path.join(d, '%s-%s-full.bin' % (name, ver)), 6)
        appbin = os.path.join(d, '%s-%s-app.bin' % (name, ver))
    if not os.path.isfile(appbin):
        B.die('Немає %s' % appbin, 6)

    def fail(code):
        B.die('!!! ПРОШИВКА НЕ ЗАПИСАЛАСЬ (esptool, код %s). Плата може не запускатись — повторіть, не від\'єднуючи кабель.' % code, 7)

    if new and not known:
        print('>>> нова плата: стираю флеш і пишу %s %s' % (name, ver))
        rc, _ = esptool_run(esptool, ['--chip', echip, '--port', port, 'erase-flash'])
        if rc: fail(rc)
        rc, _ = esptool_run(esptool, ['--chip', echip, '--port', port, 'write-flash', '0x0', os.path.join(d, '%s-%s-full.bin' % (name, ver))])
        if rc: fail(rc)
        with open(DEV, 'a', encoding='utf-8') as f:
            f.write('%s  %s  перша прошивка %s, %s\n' % (mac, role, datetime.datetime.now().strftime('%d.%m.%Y %H:%M'), chip))
    else:
        print('>>> пишу %s %s (налаштування лишаються)' % (name, ver))
        rc, _ = esptool_run(esptool, ['--chip', echip, '--port', port, 'write-flash',
                                      boot, os.path.join(d, name + '.ino.bootloader.bin'), '0x8000', os.path.join(d, name + '.ino.partitions.bin'),
                                      '0xe000', os.path.join(core, 'tools', 'partitions', 'boot_app0.bin'), '0x10000', appbin])
        if rc: fail(rc)

    if role == 'mayak' and wssid:
        print('>>> маячку — мережа «%s»' % wssid)
        time.sleep(3)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'serial_cmd.py'), port, 'wifi "%s" "%s"' % (wssid, wpass), '6'])

    print('>>> перевіряю запуск (відкриття порту перезапускає плату)')
    q = re.escape(ver)
    wantline = {'mist': 'Запуск (АВР-моста|«Сторожа світла») ' + q, 'bms': 'Міст батареї ' + q}.get(role, 'АВР-маячок ' + q)
    if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'boot_check.py'), port, wantline, '45']).returncode:
        B.die('!!! ЗАПИСАНО, АЛЕ ПЛАТА НЕ ЗАПУСТИЛАСЬ — не готово', 8)
    print('>>> готово: %s %s на %s, запуск перевірено' % (role, ver, mac))


if __name__ == '__main__':
    main()
