#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Стежить за церковною мережею, поки немає міського світла.

Що робить: кожні 30 с шукає пристрої eWeLink (mDNS _ewelink._tcp), раз на 2 хв
опитує всю підмережу ping-ом і фіксує нові MAC-адреси. Перший прохід — «база»:
пристрої, що живуть від генератора. Усе нове з'являється, коли вертається місто.

Виходить, щойно знайде АВР у eWeLink (з TXT-записом), або через --hours годин.
Журнал — logs/watch-<час>.log. Запуск: /usr/bin/python3 tools/watch_avr.py --hours 10
"""
import argparse, concurrent.futures as cf, datetime, ipaddress, json, os, re, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(os.path.dirname(HERE), 'logs')


def ts():
    return datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')


class Log:
    def __init__(self, path):
        self.f = open(path, 'a', encoding='utf-8')

    def __call__(self, *a):
        line = f"{ts()}  " + ' '.join(str(x) for x in a)
        print(line, flush=True)
        self.f.write(line + '\n')
        self.f.flush()


def pty_run(cmd, sec):
    """dns-sd буферизує вивід без термінала — запускаємо через script і alarm."""
    r = subprocess.run(['script', '-q', '/dev/null', 'perl', '-e', f'alarm {sec}; exec @ARGV'] + cmd,
                       capture_output=True, text=True)
    return r.stdout.replace('\r', '')


def browse_ewelink():
    out = pty_run(['dns-sd', '-B', '_ewelink._tcp', 'local.'], 5)
    found = []
    for line in out.splitlines():
        parts = line.split(None, 6)
        if len(parts) == 7 and parts[1] == 'Add' and parts[5].startswith('_ewelink._tcp'):
            found.append(parts[6].strip())
    return sorted(set(found))


def resolve_ewelink(name):
    return pty_run(['dns-sd', '-L', name, '_ewelink._tcp', 'local.'], 4)


def gw_mac():
    gw = subprocess.run(['ipconfig', 'getoption', 'en0', 'router'], capture_output=True, text=True).stdout.strip()
    subprocess.run(['ping', '-c', '1', '-W', '700', '-q', gw], capture_output=True)
    m = re.search(r'\(%s\) at ([0-9a-f:]+)' % re.escape(gw), subprocess.run(['arp', '-an'], capture_output=True, text=True).stdout)
    return gw, (':'.join(x.zfill(2) for x in m.group(1).split(':')) if m else '')


def my_net():
    ip = subprocess.run(['ipconfig', 'getifaddr', 'en0'], capture_output=True, text=True).stdout.strip()
    mask = subprocess.run(['ipconfig', 'getoption', 'en0', 'subnet_mask'], capture_output=True, text=True).stdout.strip()
    return ip, ipaddress.ip_network(f"{ip}/{mask}", strict=False)


def ping(ip):
    r = subprocess.run(['ping', '-c', '1', '-W', '700', '-q', ip], capture_output=True)
    return r.returncode == 0


def sweep(net, me):
    hosts = [str(h) for h in net.hosts() if str(h) != me]
    with cf.ThreadPoolExecutor(64) as ex:
        alive = {h for h, ok in zip(hosts, ex.map(ping, hosts)) if ok}
    arp = subprocess.run(['arp', '-an'], capture_output=True, text=True).stdout
    macs = {}
    for m in re.finditer(r'\((\d+\.\d+\.\d+\.\d+)\) at ([0-9a-f:]+) on en0', arp):
        ip, mac = m.group(1), ':'.join(x.zfill(2) for x in m.group(2).split(':'))
        if ipaddress.ip_address(ip) in net and mac != 'ff:ff:ff:ff:ff:ff' and ip != str(net.broadcast_address):
            macs[mac] = ip
    return alive, macs


def port_open(ip, port=8081, t=0.8):
    try:
        with socket.create_connection((ip, port), timeout=t):
            return True
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hours', type=float, default=10)
    a = ap.parse_args()
    os.makedirs(LOGDIR, exist_ok=True)
    log = Log(os.path.join(LOGDIR, datetime.datetime.now().strftime('watch-%Y%m%d-%H%M.log')))
    me, net = my_net()
    gw0 = gw_mac()
    log('старт: Mac', me, 'мережа', net, 'роутер', gw0[0], gw0[1], '— шукаю АВР (eWeLink) і нові пристрої')
    t_end = time.time() + a.hours * 3600
    base = None
    seen = {}
    next_sweep = 0
    while time.time() < t_end:
        ew = browse_ewelink()
        if ew:
            for name in ew:
                log('ЗНАЙДЕНО eWeLink:', name)
                log('TXT:', resolve_ewelink(name).strip().replace('\n', ' | '))
            _, macs = sweep(net, me)
            new = {m: i for m, i in macs.items() if base is not None and m not in base}
            log('нові MAC після бази:', json.dumps(new))
            for m, i in new.items():
                log('  ', m, i, 'порт 8081 відкритий' if port_open(i) else 'порт 8081 закритий')
            log('вихід: АВР знайдено')
            return 0
        if time.time() >= next_sweep:
            #  17.11: о 16:41 Mac переїхав із церкви додому (та сама 192.168.1.0/24) — наглядач прийняв домашні
            #  пристрої за «нові». Тепер: інший роутер (MAC шлюзу) — вихід.
            gw = gw_mac()
            if gw0[1] and gw[1] and gw != gw0:
                log('мережа змінилась: роутер', gw[0], gw[1], 'замість', gw0[0], gw0[1], '— вихід')
                return 0
            alive, macs = sweep(net, me)
            if base is None:
                base = dict(macs)
                log(f'база (живе від генератора): {len(base)} пристроїв:', ' '.join(f'{i}={m}' for m, i in sorted(base.items(), key=lambda x: ipaddress.ip_address(x[1]))))
            else:
                for m, i in macs.items():
                    if m not in base and m not in seen:
                        seen[m] = (i, ts())
                        log('новий пристрій:', i, m, '8081 відкритий' if port_open(i) else '8081 закритий')
            next_sweep = time.time() + 120
        time.sleep(25)
    log('вихід за часом; нових пристроїв:', len(seen), json.dumps(seen, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
