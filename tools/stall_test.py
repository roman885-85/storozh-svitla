#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Перевірка 1.0.42+: клієнт сторінки, що не приймає відповідь, не тримає основний цикл Сторожа.

    /usr/bin/python3 -I tools/stall_test.py 192.168.1.X [секунд]

23.11.2026 о 12:04 Сторожа 1.0.40 перезапустив сторож задач: дамп показав, що основна задача 30 с чекала в
NetworkClient::write, бо клієнт сторінки перестав приймати дані. Тут те саме навмисно: «завислий» клієнт із малим
вікном прийому шле кілька запитів сторінки входу (без пароля, ~2,3 КБ кожна) і нічого не читає. Поруч другий клієнт
кожні 0,3 с питає /api/ping і міряє, за скільки Сторож відповідає. З 1.0.42 найдовша відповідь — до ~2 с (перевірка
«чи приймає» 1,5 с), а без захисту основний цикл стояв би по 10 с на кожен запис.
Лише читає сторінку входу й /api/ping — у Сторожі нічого не міняє. Сторож 1.0.41 і старші так не перевіряти:
там цей опит може затримати основний цикл на десятки секунд (аж до перезапуску сторожем задач)."""
import socket, sys, threading, time, urllib.request

host = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.X'
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0

ver = urllib.request.urlopen(f'http://{host}/api/ping', timeout=5).read().decode()
print('Сторож:', ver)
if '"ver":"1.0.4' not in ver or any(f'"ver":"1.0.4{d}"' in ver for d in '01'):
    sys.exit('Це не 1.0.42+ — не перевіряю (без захисту опит може затримати основний цикл на десятки секунд)')

lat, stop = [], False


def pinger():
    while not stop:
        t = time.time()
        try:
            urllib.request.urlopen(f'http://{host}/api/ping', timeout=30).read()
            lat.append((t, time.time() - t, True))
        except Exception:
            lat.append((t, time.time() - t, False))
        time.sleep(0.3)


th = threading.Thread(target=pinger, daemon=True)
th.start()
time.sleep(3)                                     # спершу — як Сторож відповідає без «завислого» клієнта
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
s.connect((host, 80))
t_stall = time.time()
req = f'GET / HTTP/1.1\r\nHost: {host}\r\nConnection: keep-alive\r\n\r\n'.encode()
s.sendall(req * 6)                                # шість сторінок входу, не читаючи жодної
print(f'«завислий» клієнт: 6 запитів надіслано о {time.strftime("%H:%M:%S")}, не читаю {dur:.0f} с')
time.sleep(dur)
closed_by_server = False
try:
    s.setblocking(False)
    got = 0
    while True:
        b = s.recv(65536)
        if not b:
            closed_by_server = True
            break
        got += len(b)
except BlockingIOError:
    pass
except ConnectionResetError:
    closed_by_server = True
s.close()
time.sleep(2)
stop = True
th.join(timeout=35)

before = [d for t, d, ok in lat if t < t_stall]
during = [(t - t_stall, d, ok) for t, d, ok in lat if t >= t_stall]
mx = max(during, key=lambda x: x[1]) if during else None
print(f'ping до «завислого»: {len(before)} відповідей, найдовша {max(before) * 1000:.0f} мс' if before else 'ping до: —')
print(f'ping під час: {len(during)} запитів, невдалих {sum(1 for _, _, ok in during if not ok)}, '
      f'найдовша відповідь {mx[1] * 1000:.0f} мс (на {mx[0]:.1f} с після початку)' if mx else 'ping під час: —')
print('довгі (> 700 мс):', ', '.join(f'{t:.1f} с → {d * 1000:.0f} мс' for t, d, ok in during if d > 0.7) or 'немає')
print('Сторож закрив з\'єднання «завислого» клієнта:', 'так' if closed_by_server else 'ні (ще відкрите)')
