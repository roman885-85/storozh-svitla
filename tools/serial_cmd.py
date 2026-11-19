#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Надсилає рядок у плату по USB і друкує відповідь.

    venv/bin/python tools/serial_cmd.py /dev/cu.usbserial-0001 "info" [секунд]
    venv/bin/python tools/serial_cmd.py /dev/cu.usbmodem1101 "" 30        # лише читати

Два способи відкриття порту:
  • міст USB-UART (CP2102, CH340: cu.usbserial*, cu.SLAB*, cu.wchusbserial*) — pyserial. Власне читання через
    os.read на macOS 15 з CP2102 давало «повтор хвоста»: мегабайти того самого шматка рядка (17.11.2026).
    Відкриття порту перезапускає таку плату — для журналу запуску це навіть добре.
  • рідний USB ESP32-C3/S3 (cu.usbmodem*) — os.open без смикання DTR/RTS: підняте DTR защіпає GPIO0,
    і наступне скидання йде в завантажувач (пам'ять про ES3C28P).
Захист: 115 200 бод — це ~11,5 КБ/с; більше — збій драйвера, читання зупиняється."""
import os, select, struct, sys, time


def guard_ok(total, t0):
    return total <= 12000 * (time.time() - t0 + 2)


def via_pyserial(port, line, wait):
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.3
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.2)
    if line:
        s.write((line + '\n').encode('utf-8'))
    t0, total = time.time(), 0
    while time.time() - t0 < wait:
        chunk = s.read(4096)
        if not chunk:
            continue
        total += len(chunk)
        if not guard_ok(total, t0):
            sys.stdout.write('\n[serial_cmd: даних більше, ніж пропускає порт, — збій драйвера; читання зупинено]\n')
            break
        sys.stdout.write(chunk.decode('utf-8', 'replace'))
        sys.stdout.flush()
    s.close()


def via_fd(port, line, wait):
    import fcntl, termios
    TIOCMBIC = getattr(termios, 'TIOCMBIC', 0x8004746b)
    lines = struct.pack('I', 0x002 | 0x004)      # DTR | RTS
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    fcntl.ioctl(fd, TIOCMBIC, lines)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    fcntl.ioctl(fd, TIOCMBIC, lines)
    time.sleep(0.3)
    if line:
        os.write(fd, (line + '\n').encode('utf-8'))
    t0, total = time.time(), 0
    while time.time() - t0 < wait:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        total += len(chunk)
        if not guard_ok(total, t0):
            sys.stdout.write('\n[serial_cmd: даних більше, ніж пропускає порт, — збій драйвера; читання зупинено]\n')
            break
        sys.stdout.write(chunk.decode('utf-8', 'replace'))
        sys.stdout.flush()
    os.close(fd)


def main():
    port, line = sys.argv[1], sys.argv[2]
    wait = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
    if 'usbmodem' in port:
        via_fd(port, line, wait)
    else:
        try:
            via_pyserial(port, line, wait)
        except ImportError:
            sys.exit('Потрібен pyserial: запускайте venv/bin/python (venv: /usr/bin/python3 -m venv venv; venv/bin/pip install pyserial)')


if __name__ == '__main__':
    main()
