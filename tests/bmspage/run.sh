#!/bin/zsh
# Сторінка мосту батареї (1.2.0) з підставними даними — картинки в tests/bmspage/out/:
#   bmspage.png (600 px), bmspage_360.png (телефон), schemes.png (/schemes — усі схеми для друку), gallery.png (усі варіанти схем),
#   bmspage_dark.png (темна тема)
cd "${0:A:h}" || exit 1
mkdir -p out
OUT="$PWD/out" node page.js || exit 1
OUT="$PWD/out" node gallery.js || exit 1
CH="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
shot() { perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --hide-scrollbars --virtual-time-budget=4000 "$@" >/dev/null 2>&1; }
shot --window-size=600,6400 --screenshot="$PWD/out/bmspage.png" "file://$PWD/out/bmspage.html"
#  Chrome без вікна вужче 500 px не робить — 360 px задаємо шириною сторінки; у заголовку — найправіший край вмісту
sed 's#</body>#<style>html{background:\#999}body{width:360px}</style><script>setTimeout(()=>{document.title="край "+Math.ceil(Math.max(...[...document.querySelectorAll("main *")].filter(e=>e.offsetParent).map(e=>e.getBoundingClientRect().right)))},1500)</script></body>#' out/bmspage.html > out/bmspage_360.html
shot --window-size=500,9000 --screenshot="$PWD/out/bmspage_360.png" "file://$PWD/out/bmspage_360.html"
edge=$(perl -e 'alarm 60; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --window-size=500,2000 --virtual-time-budget=4000 --dump-dom "file://$PWD/out/bmspage_360.html" 2>/dev/null | grep -o '<title>край [0-9]*' | grep -o '[0-9]*$')
[ -n "$edge" ] && [ "$edge" -le 360 ] && echo "  ✅ 360 px: вміст не ширший за сторінку (край $edge)" || { echo "  ❌ 360 px: вміст ширший за сторінку (край ${edge:-?})"; bad=1; }
shot --force-dark-mode --window-size=600,6400 --screenshot="$PWD/out/bmspage_dark.png" "file://$PWD/out/bmspage.html"
shot --window-size=600,4200 --screenshot="$PWD/out/schemes.png" "file://$PWD/out/bmspage.html#schemes"
shot --window-size=600,5600 --screenshot="$PWD/out/gallery.png" "file://$PWD/out/gallery.html"
perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --virtual-time-budget=4000 --dump-dom "file://$PWD/out/bmspage.html" 2>/dev/null > out/dom.html
perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --virtual-time-budget=4000 --dump-dom "file://$PWD/out/bmspage.html#schemes" 2>/dev/null > out/dom_schemes.html
for w in "Кілька BMS по черзі: 2, кожна раз на 5 хв" "Батарея 1</b> «Інвертор»" "Батарея 2</b> «Гараж»" "Повторюється: батарея 2 — тривога BMS" "батарея 2: заряд 20 %" 'data-play="8"' \
         "Як під'єднати стрічку" "8 світлодіодів на 40 % — до 0,14 А" "Модуль бузера (три виводи)" "1N4148" "Усі схеми мосту однією сторінкою" "Живлення модуля — лише 3,3 В" \
         "Сигнал BMS: -86 дБм" "на межі — зв'язок рватиметься" "Розриви з BMS від запуску: 3" "зник сигнал (8 с без відповіді)" "з'єднання не встановилось" "перед тим 1 хв без кадрів" \
         "інтервал 40 мс, нагляд 8 с" "BMS просила інші параметри 1 раз (нагляд 4 с)" "завислий стан Bluetooth скинуто 1 раз" "розірвано з'єднань-«привидів» 2"; do
  grep -q "$w" out/dom.html && echo "  ✅ $w" || { echo "  ❌ немає: $w"; bad=1; }
done
for w in "Міст батареї «Сторожа світла»: схеми підключення" "Світло: два світлодіоди" "Звук: окремий бузер через транзистор" "Виводи плати ESP32-C3" "<b>стрічка DIN</b>" "<b>бузер</b>"; do
  grep -q "$w" out/dom_schemes.html && echo "  ✅ /schemes: $w" || { echo "  ❌ /schemes, немає: $w"; bad=1; }
done
#  1.2.5: світло й звук не вибрано — запропоновані вільні виводи й номери на схемах (дисплей на 4 5 6 3 1 10)
perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --virtual-time-budget=4000 --dump-dom "file://$PWD/out/bmspage_owner.html" 2>/dev/null > out/dom_owner.html
shot --window-size=600,8000 --screenshot="$PWD/out/bmspage_owner.png" "file://$PWD/out/bmspage_owner.html"
/usr/bin/python3 - out/dom_owner.html <<'PY2' || bad=1
import re, sys
h = open(sys.argv[1], encoding='utf-8').read()
def part(a, b):
    i = h.find(a); j = h.find(b, i)
    return h[i:j] if i >= 0 and j > i else ''
L, S = part('id="a_lsch"', 'id="a_ltest"'), part('id="a_ssch"', 'Які події звучать')
def div(i):
    m = re.search(r'<div class="(?:mut|sch)" id="%s"[^>]*>(.*?)</div>\s*(?:<div|<label|\n)' % i, h, re.S)
    return re.sub('<[^>]+>', ' ', m.group(1)) if m else ''
ok = True
for nm, t, w in (('світлодіоди', L, ('>GPIO0<', '>GPIO7<')), ('стрічка', L, ('>GPIO0<', '330 Ом')), ('бузер', S, ('>GPIO20<', '1N4148'))):
    good = all(x in t for x in w) and 'GPIO…' not in t
    print('  %s схема «%s»: %s, без «GPIO…»' % ('✅' if good else '❌', nm, ', '.join(x.strip('<>') for x in w))); ok &= good
fr = div('a_lfree')
for w in ('GPIO0, GPIO7, GPIO20, GPIO21', 'Дисплей займає GPIO4, GPIO5, GPIO6, GPIO3, GPIO1, GPIO10', 'запропоновано'):
    print('  %s вільні виводи: «%s»' % ('✅' if w in fr else '❌', w)); ok &= w in fr
for w in ('Два світлодіоди — куди під', 'Стрічка WS2812B — куди під', 'Бузер — куди під'):
    print('  %s %s' % ('✅' if w in h else '❌', w)); ok &= w in h
sys.exit(0 if ok else 1)
PY2
[ -z "$bad" ] && echo "ПРОЙДЕНО: сторінка мосту" || exit 1
