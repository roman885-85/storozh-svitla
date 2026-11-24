#!/bin/zsh
# Сторінка мосту батареї (1.2.0) з підставними даними — картинка tests/bmspage/out/bmspage.png (780 px, уся висота)
cd "${0:A:h}" || exit 1
mkdir -p out
OUT="$PWD/out" node page.js || exit 1
CH="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --hide-scrollbars --virtual-time-budget=4000 --window-size=600,4200 --screenshot="$PWD/out/bmspage.png" "file://$PWD/out/bmspage.html" >/dev/null 2>&1
perl -e 'alarm 90; exec @ARGV' "$CH" --headless=new --disable-gpu --disable-extensions --virtual-time-budget=4000 --dump-dom "file://$PWD/out/bmspage.html" 2>/dev/null > out/dom.html
for w in "Кілька BMS по черзі: 2, кожна раз на 5 хв" "Батарея 1</b> «Інвертор»" "Батарея 2</b> «Гараж»" "Повторюється: батарея 2 — тривога BMS" "батарея 2: заряд 20 %" 'data-play="8"'; do
  grep -q "$w" out/dom.html && echo "  ✅ $w" || { echo "  ❌ немає: $w"; bad=1; }
done
[ -z "$bad" ] && echo "ПРОЙДЕНО: сторінка мосту" || exit 1
