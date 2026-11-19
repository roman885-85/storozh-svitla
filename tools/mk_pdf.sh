#!/bin/zsh
# PDF документів (власник: «документацию — и в PDF»): tools/mk_pdf.sh [файл.md …]
# Без аргументів — ЖУРНАЛ, ВИМОГИ й інструкція. Markdown → HTML (tools/md2html.py) → Chrome без вікна → PDF поруч з .md.
cd "${0:A:h}/.." || exit 1
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
TMP=$(mktemp -d)
files=("$@"); (( $# )) || files=(ЖУРНАЛ.md ВИМОГИ.md docs/ІНСТРУКЦІЯ.md)
for md in $files; do
  html="$TMP/${md:t:r}.html"; pdf="${md:r}.pdf"
  /usr/bin/python3 tools/md2html.py "$md" "$html" || continue
  "$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --generate-pdf-document-outline \
    --print-to-pdf="${pdf:A}" "file://$html" >/dev/null 2>&1
  print -r -- "$pdf: $(stat -f %z "$pdf") Б"
done
rm -rf "$TMP"
