# -*- coding: utf-8 -*-
# ВИМОГИ.md → HTML для PDF (Chrome --print-to-pdf): таблиці, заголовки, абзаци. Запуск: md2html.py вхід.md вихід.html
import re, sys, html
src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding='utf-8').read().split('\n')
def inline(t):
    t = html.escape(t)
    t = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', t)
    t = re.sub(r'`(.+?)`', r'<code>\1</code>', t)
    t = t.replace('✅', '<span class="ok">✅</span>').replace('⚠️', '<span class="warn">⚠️</span>').replace('⏳', '<span class="wait">⏳</span>')
    return t
out = []; i = 0
while i < len(lines):
    l = lines[i]
    if l.startswith('|'):
        rows = []
        while i < len(lines) and lines[i].startswith('|'):
            rows.append([c.strip() for c in lines[i].strip().strip('|').split('|')]); i += 1
        # вузька 3-я колонка (по центру) — лише для колонки статусу (✅/🔧/👤, ≤ 4 знаки), як у ВИМОГИ/ЗАДАЧІ; у ГОЛОС.md
        # 3-я колонка — приклади українською, її стискати не можна (08.11)
        st = len(rows[0]) == 4 and all(len(r) > 2 and len(r[2]) <= 4 for r in rows[2:] if r)
        out.append('<table class="t%d%s">' % (len(rows[0]), '' if st or len(rows[0]) != 4 else 'w'))
        for k, r in enumerate(rows):
            if k == 1 and all(re.fullmatch(r':?-+:?', c) for c in r): continue
            tag = 'th' if k == 0 else 'td'
            out.append('<tr>' + ''.join('<%s>%s</%s>' % (tag, inline(c), tag) for c in r) + '</tr>')
        out.append('</table>'); continue
    m = re.match(r'(#+) (.*)', l)
    if m:
        n = len(m.group(1)); out.append('<h%d>%s</h%d>' % (n, inline(m.group(2)), n))
    elif l.startswith('    ') and l.strip():
        # блок коду: рядки з відступом 4+ пробіли (команди)
        code = []
        while i < len(lines) and (lines[i].startswith('    ') or not lines[i].strip()):
            if not lines[i].strip() and not (i + 1 < len(lines) and lines[i + 1].startswith('    ')): break
            code.append(lines[i][4:]); i += 1
        out.append('<pre>' + html.escape('\n'.join(code).strip('\n')) + '</pre>'); continue
    elif re.match(r'\d+\. ', l):
        # нумерований список; рядки з відступом — продовження пункту, блок коду всередині — окремо
        items = [re.sub(r'^\d+\. ', '', l)]
        while i + 1 < len(lines) and (re.match(r'\d+\. ', lines[i + 1]) or (lines[i + 1].startswith('   ') and lines[i + 1].strip())
                                      or (not lines[i + 1].strip() and i + 2 < len(lines) and (re.match(r'\d+\. ', lines[i + 2]) or lines[i + 2].startswith('        ')))):
            i += 1
            if not lines[i].strip(): continue
            if re.match(r'\d+\. ', lines[i]): items.append(re.sub(r'^\d+\. ', '', lines[i]))
            elif lines[i].startswith('        '): items[-1] += '\x00' + lines[i].strip()
            else: items[-1] += ' ' + lines[i].strip()
        def li(x):
            parts = x.split('\x00')
            return inline(parts[0]) + ''.join('<pre>%s</pre>' % html.escape(c) for c in parts[1:])
        out.append('<ol>' + ''.join('<li>%s</li>' % li(x) for x in items) + '</ol>')
    elif l.startswith('- '):
        # список: «- » починає пункт, рядки з відступом — його продовження
        items = [l[2:]]
        while i + 1 < len(lines) and lines[i + 1].strip() and not lines[i + 1].startswith(('|', '#')):
            i += 1
            if lines[i].startswith('- '): items.append(lines[i][2:])
            else: items[-1] += ' ' + lines[i].strip()
        out.append('<ul>' + ''.join('<li>%s</li>' % inline(x) for x in items) + '</ul>')
    elif l.strip():
        para = [l]
        while i + 1 < len(lines) and lines[i + 1].strip() and not lines[i + 1].startswith(('|', '#')):
            i += 1; para.append(lines[i])
        out.append('<p>' + inline(' '.join(para)) + '</p>')
    i += 1
css = """body{font-family:-apple-system,Helvetica,Arial,sans-serif;font-size:10pt;margin:14mm;color:#111}
h1{font-size:16pt}h2{font-size:13pt;margin-top:16pt}table{border-collapse:collapse;width:100%;margin:6pt 0}
th,td{border:1px solid #bbb;padding:3pt 5pt;vertical-align:top;text-align:left}th{background:#eee}
td:nth-child(1){width:4%;white-space:nowrap}.t4 td:nth-child(3){width:4%;text-align:center}.t3 td:nth-child(2){width:38%}code{font-size:8.5pt;background:#f3f3f3}pre{font-size:9pt;background:#f3f3f3;padding:5pt 7pt;border-radius:4pt;white-space:pre-wrap}li{margin:2pt 0}"""
open(dst, 'w', encoding='utf-8').write('<!doctype html><html lang="uk"><head><meta charset="utf-8"><title>%s</title><style>%s</style></head><body>%s</body></html>' % (html.escape(next((x[2:].strip() for x in lines if x.startswith('# ')), src)), css, '\n'.join(out)))
