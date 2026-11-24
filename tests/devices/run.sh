#!/bin/zsh
# Картка «Пристрої» (Сторож 1.0.44) зі справжнього page.cpp — стилі, розмітка, esc/ago/renderDev — на даних, як на Сторожі
# 23.11.2026 (card_now) і з усіма видами «інших» пристроїв (card_all). Картинки 780 і 360 px — у tests/devices/out/.
# Chrome без вікна вужче 500 px не робить, тож 360 px — обмеженням ширини сторінки; у заголовку — ширини таблиць
# (прокрутка = перша цифра більша за другу).
cd "${0:A:h}" || exit 1
mkdir -p out
OUT="$PWD/out" node cardtest.js || exit 1
../bat/run.sh >/dev/null 2>&1 || { echo "tests/bat не пройшов — даних для картки «Батарея» немає"; exit 1; }
OUT="$PWD/out" node batcard.js || exit 1
for f in bat_fresh bat_multi; do perl -e 'alarm 60; exec @ARGV' "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --disable-extensions --hide-scrollbars --window-size=780,900 --screenshot="$PWD/out/$f-780.png" "file://$PWD/out/$f.html" >/dev/null 2>&1; done
CH="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
for f in card_now card_all card_noavr card_multi; do
  perl -e 'alarm 60; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --hide-scrollbars --window-size=780,820 --screenshot="$PWD/out/$f-780.png" "file://$PWD/out/$f.html" >/dev/null 2>&1
  sed 's#</body>#<style>html{background:\#999}body{width:360px}</style><script>{document.title=[...document.querySelectorAll(".tbl,table")].map(e=>(e.id||e.tagName)+" "+e.scrollWidth+"/"+e.clientWidth).join(" ")}</script></body>#' "out/$f.html" > "out/${f}_360.html"
  perl -e 'alarm 60; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --hide-scrollbars --window-size=500,1000 --screenshot="$PWD/out/$f-360.png" "file://$PWD/out/${f}_360.html" >/dev/null 2>&1
  printf '%s 360 px: ' $f; perl -e 'alarm 60; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --window-size=500,1000 --dump-dom "file://$PWD/out/${f}_360.html" 2>/dev/null | grep -o '<title>[^<]*</title>'
done
ls out/*.png
