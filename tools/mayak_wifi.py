#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Маячку чи мосту батареї (1.0.36) — мережа Сторожа для АВР (команда wifi по USB). Пароль питає сам і не показує на екрані: його немає ні в
команді, ні в історії оболонки, ні у виводі (маячок у відповідь друкує лише назву мережі). Пише в маячок лише ця
команда (правило власника: у флеш — тільки за командою).

    venv/bin/python tools/mayak_wifi.py /dev/cu.usbmodem14601 "AVR-mist-5060"
"""
import getpass, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_cmd


def send(port, line, wait):
    if 'usbmodem' in port:
        serial_cmd.via_fd(port, line, wait)
    else:
        serial_cmd.via_pyserial(port, line, wait)


def main():
    if len(sys.argv) < 3:
        sys.exit('Запуск: venv/bin/python tools/mayak_wifi.py <порт маячка> "<назва мережі Сторожа для АВР>"')
    port, ssid = sys.argv[1], sys.argv[2]
    if not os.path.exists(port):
        sys.exit('Порту %s немає — маячок під\'єднано до Mac?' % port)
    if not sys.stdin.isatty():
        sys.exit('Пароль вводять з клавіатури — запустіть це в терміналі')
    pw = getpass.getpass('Пароль мережі «%s» (на екрані не видно), потім Enter: ' % ssid)
    if not 8 <= len(pw) <= 63:
        sys.exit('Пароль мережі — від 8 до 63 символів. Нічого не записано.')
    if '"' in pw or '"' in ssid:
        sys.exit('Лапки в назві чи паролі маячок не розбере. Нічого не записано.')
    print('>>> маячку — мережа «%s»' % ssid)
    send(port, 'wifi "%s" "%s"' % (ssid, pw), 4)
    pw = None
    print('\n>>> чекаю з\'єднання й питаю стан')
    time.sleep(8)
    send(port, 'info', 3)
    print('\n>>> якщо вище «під\'єднано» — готово. Маячок генератора — у розетку ПІСЛЯ АВР (не в ДБЖ);'
          '\n    міст батареї — навпаки, від ДБЖ чи інвертора, і ближче до BMS (на −90 дБм вона рве з\'єднання й пищить)')


if __name__ == '__main__':
    main()
