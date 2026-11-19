#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Довгий запис порту моста, поки власник налаштовує міст і підключає АВР (18.11.2026).

    caffeinate -i venv/bin/python tools/mist_log.py --avr 10:20:30:40:50:73 [--hours 4] [--port /dev/cu.usbserial-0001]

Порт відкривається один раз: відкриття перезапускає плату (автоскидання CP2102), тож повторно не відкриваємо.
Пише всі рядки моста з часом Mac і кожні 10 с питає state; у журнал іде лише те, що змінилось (і повний стан
раз на 10 хв). Позначає етапи: міст у мережі роутера (+ /api/ping з мережі Mac), АВР у точці моста, АВР призначено,
«місто є», перше повідомлення в Telegram. Виходить через 60 с після всіх етапів, за --hours або коли плату
від'єднали. Пароль Wi-Fi роутера й токен міст у порт не друкує. Журнал — logs/mist-<час>.log."""
import argparse, collections, datetime, json, os, signal, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(os.path.dirname(HERE), 'logs')
STEPS = ['роутер', 'АВР у точці', 'АВР призначено', 'місто є', 'Telegram']
FRESH = 8           # як PING_FRESH у watch.cpp: відповідь на ping не старша за 8 с


def ts():
    return datetime.datetime.now().strftime('%H:%M:%S')


class Rec:
    def __init__(self, path, avr):
        self.f = open(path, 'a', encoding='utf-8')
        self.avr = avr.lower()
        self.done = {}
        self.sig = {}
        self.full_left = 3
        self.pinged = False
        self.ping_tries = 0
        self.st = {'n': 0, 'fresh': 0, 'stale': 0, 'none': 0, 'max': 0, 'raw_off': 0}

    def log(self, *x, out=False):
        line = f'{ts()}  ' + ' '.join(str(i) for i in x)
        self.f.write(line + '\n')
        self.f.flush()
        if out:
            print(line, flush=True)

    def step(self, name, *info):
        if name not in self.done:
            self.done[name] = ts()
            self.log('ЕТАП:', name, *info, out=True)

    def changed(self, key, sig):
        full = self.full_left > 0
        if full:
            self.full_left -= 1
        if self.sig.get(key) != sig or full:
            self.sig[key] = sig
            return True
        return False

    def handle(self, d):
        if 'grid' in d:
            self.watch(d)
        elif 'sta' in d:
            self.bridge(d)
        elif 'token' in d:
            self.tg(d)

    def watch(self, d):
        g, av = d.get('grid', {}), d.get('avr', {})
        sig = (d.get('status'), g.get('st'), d.get('line', {}).get('st'), av.get('assigned'), av.get('mac'), d.get('mayak', {}).get('seen'))
        if self.changed('w', sig):
            self.log('стан:', json.dumps(d, ensure_ascii=False))
        mac = (av.get('mac') or '').lower()
        if av.get('assigned'):
            self.step('АВР призначено', mac, '' if not self.avr or mac == self.avr else f'(НЕ той, що знайдено в мережі церкви: {self.avr})')
            self.avr = self.avr or mac
        if g.get('st') == 1:
            self.step('місто є', f"({d.get('status')})")
            if g.get('raw') is False:
                self.st['raw_off'] += 1

    def bridge(self, d):
        sta, cl = d.get('sta', {}), d.get('clients', [])
        sig = (sta.get('up'), sta.get('ip'), d.get('ap', {}).get('ch'),
               tuple(sorted((c.get('mac'), c.get('ip'), c.get('assoc'), c.get('ping') is not None and c['ping'] <= FRESH) for c in cl)))
        if self.changed('b', sig):
            self.log('мережа:', json.dumps(d, ensure_ascii=False))
        if sta.get('up'):
            self.step('роутер', sta.get('ssid'), sta.get('ip'), f"{sta.get('rssi')} дБм, канал точки {d.get('ap', {}).get('ch')}")
            if not self.pinged and sta.get('ip'):
                self.try_ping(sta['ip'])
        for c in cl:
            if self.avr and (c.get('mac') or '').lower() == self.avr and c.get('assoc'):
                self.step('АВР у точці', c.get('ip'), f"{c.get('rssi')} дБм")
                p = c.get('ping')
                self.st['n'] += 1
                if p is None:
                    self.st['none'] += 1
                elif p <= FRESH:
                    self.st['fresh'] += 1
                else:
                    self.st['stale'] += 1
                if p is not None:
                    self.st['max'] = max(self.st['max'], p)

    def tg(self, d):
        sig = (d.get('token'), d.get('recipients'), d.get('sent'), d.get('dropped'), d.get('lastErr'), json.dumps(d.get('seen'), ensure_ascii=False))
        if self.changed('t', sig):
            self.log('telegram:', json.dumps(d, ensure_ascii=False))
        if (d.get('sent') or 0) >= 1:
            self.step('Telegram', f"надіслано {d.get('sent')}")

    def try_ping(self, ip):
        self.ping_tries += 1
        try:
            with urllib.request.urlopen(f'http://{ip}/api/ping', timeout=4) as r:
                self.log(f'з мережі Mac: http://{ip}/api/ping →', r.read(200).decode('utf-8', 'replace'), out=True)
                self.pinged = True
        except Exception as e:
            self.log(f'з мережі Mac: http://{ip}/api/ping не відповів:', e)
            if self.ping_tries >= 6:
                self.pinged = True

    def summary(self, why):
        parts = [f"{k} {self.done.get(k, '—')}" for k in STEPS]
        self.log('ПІДСУМОК (' + why + '): ' + ', '.join(parts), out=True)
        s = self.st
        if s['n']:
            self.log(f"ping АВР у {s['n']} опитуваннях: свіжий (≤ {FRESH} с) {s['fresh']}, застарілий {s['stale']}, жодного {s['none']}, "
                     f"найбільший вік {s['max']} с; «місто є», а миттєво «немає»: {s['raw_off']}", out=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/cu.usbserial-0001')
    ap.add_argument('--hours', type=float, default=4)
    ap.add_argument('--avr', default='', help='MAC АВР, якщо вже відомий')
    ap.add_argument('--every', type=float, default=10)
    a = ap.parse_args()
    os.makedirs(LOGDIR, exist_ok=True)
    path = os.path.join(LOGDIR, datetime.datetime.now().strftime('mist-%Y%m%d-%H%M.log'))
    r = Rec(path, a.avr)
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(SystemExit(0)))
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port, 115200, 0.3
    s.dtr = False
    s.rts = False
    s.open()
    r.log('запис порту', a.port, '→', path, '; АВР', r.avr or '(невідомий)', f'; до {a.hours} год', out=True)
    win, buf = collections.deque(), b''
    t_end = time.time() + a.hours * 3600
    next_q, next_full, finish_at = time.time() + 15, time.time() + 600, None
    why = 'за часом'
    try:
        while time.time() < t_end and (finish_at is None or time.time() < finish_at):
            chunk = s.read(4096)
            now = time.time()
            if chunk:
                win.append((now, len(chunk)))
                while win and now - win[0][0] > 10:
                    win.popleft()
                if sum(n for _, n in win) > 150000:
                    why = 'збій драйвера: понад 150 КБ за 10 с'
                    break
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    line = raw.decode('utf-8', 'replace').rstrip('\r')
                    if not line.strip():
                        continue
                    if line.startswith('{'):
                        try:
                            r.handle(json.loads(line))
                        except ValueError:
                            r.log('неповний JSON:', line[:300])
                    else:
                        r.log('міст:', line)
            if now >= next_q:
                if now >= next_full:
                    r.full_left, next_full = 3, now + 600
                s.write(b'state\n')
                next_q = now + a.every
            if finish_at is None and all(k in r.done for k in STEPS):
                finish_at = now + 60
                why = 'усі етапи пройдено'
    except (serial.SerialException, OSError) as e:
        why = f'порт зник: {e}'
    except SystemExit:
        why = 'зупинено'
    finally:
        r.summary(why)
        try:
            s.close()
        except Exception:
            pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
