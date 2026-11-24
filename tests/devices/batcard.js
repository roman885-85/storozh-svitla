// Картка «Батарея» зі справжнього page.cpp (1.0.48): одна батарея і дві «по черзі» — дані з tests/bat (/tmp/bat-test-*.json)
const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../../firmware/avr-mist/page.cpp', 'utf8');
const page = src.slice(src.indexOf('INDEX_HTML'));
const style = page.slice(page.indexOf('<style>') + 7, page.indexOf('</style>'));
const c0 = page.indexOf('<div class="card hide" id="batcard">');
const c1 = page.indexOf('</div></div>', page.indexOf('id="bat_on"', c0)) + '</div></div>'.length;
const card = page.slice(c0, c1).replace('card hide', 'card');
const js = page.slice(page.indexOf('<script>') + 8, page.indexOf('</script>'));
const grab = name => { const a = js.indexOf('function ' + name + '('); let i = js.indexOf('{', a), d = 0; for (; i < js.length; i++) { if (js[i] === '{') d++; else if (js[i] === '}' && --d === 0) break; } return js.slice(a, i + 1); };
const el = {}; const $ = id => (el[id] = el[id] || { innerHTML: '', textContent: '', checked: false, classList: { add() {}, remove() {}, toggle() {} } });
let batPend = false;
eval(grab('esc') + ';' + grab('ago') + ';' + grab('renderBat') + '; globalThis.renderBat = renderBat;');
let bad = 0;
for (const n of ['fresh', 'alarm', 'multi']) {
  const d = JSON.parse(fs.readFileSync('/tmp/bat-test-' + n + '.json', 'utf8'));
  for (const k in el) delete el[k];
  renderBat(d);
  const h = card.replace('<span id="batsub" class="mut"></span>', '<span id="batsub" class="mut">' + esc(el.batsub.textContent) + '</span>')
    .replace('<div id="batbody" style="margin-top:6px"></div>', '<div id="batbody" style="margin-top:6px">' + el.batbody.innerHTML + '</div>');
  fs.writeFileSync(process.env.OUT + '/bat_' + n + '.html', '<!doctype html><html lang="uk"><head><meta charset="utf-8"><style>' + style + '</style></head><body><main>' + h + '</main></body></html>');
  const want = n === 'multi' ? [/Батарея 1 «Інвертор»/, /Батарея 2 «Гараж»/, /Опитує по черзі раз на 5 хв/, /1 з 2|2 з 2/] : [/60 %/, /Тривог BMS немає|BMS вимкнула розряд/];
  for (const w of want) if (!w.test(el.batbody.innerHTML + el.batsub.textContent)) { console.log('ПОМИЛКА: «' + n + '» без ' + w); bad++; }
  console.log('bat_' + n, '·', el.batsub.textContent);
}
if (bad) process.exit(1);
console.log('ПРОЙДЕНО: картка «Батарея» — одна й дві');
