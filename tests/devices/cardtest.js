// Картка «Пристрої» зі справжнього page.cpp: стилі, розмітка, esc/ago/renderDev — і дані як на Сторожі 23.11 12:55
const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../../firmware/avr-mist/page.cpp', 'utf8');
const page = src.slice(src.indexOf('INDEX_HTML'));
const style = page.slice(page.indexOf('<style>') + 7, page.indexOf('</style>'));
const c0 = page.indexOf('<div class="card"><b>Пристрої</b>');
const c1 = page.indexOf('<div class="mut" id="clhint"></div></div>', c0) + '<div class="mut" id="clhint"></div></div>'.length;
let card = page.slice(c0, c1);
const js = page.slice(page.indexOf('<script>') + 8, page.indexOf('</script>'));
const grab = name => { const a = js.indexOf('function ' + name + '('); let i = js.indexOf('{', a), d = 0; for (; i < js.length; i++) { if (js[i] === '{') d++; else if (js[i] === '}' && --d === 0) break; } return js.slice(a, i + 1); };
const el = {}; const $ = id => (el[id] = el[id] || { innerHTML: '', textContent: '' });
eval(grab('esc') + ';' + grab('ago') + ';' + grab('renderDev') + '; globalThis.renderDev = renderDev;');
const base = () => ({
  you: '192.168.1.10',
  watch: { grid: { st: 0, exact: true, ago: 78000, at: '22.11 о 15:09' }, avr: { assigned: true, mac: '10:20:30:40:50:73' },
    bcn: { legacy: false, max: 6, list: [{ mac: '10:20:30:40:50:20', cfg: true, role: 'g', name: 'Генератор', seen: false }] } },
  net: { sta: { ssid: 'MyRouter-2,4', up: true, ip: '192.168.1.X', rssi: -60, bssid: '10:20:30:40:50:E7', pref: true },
    ap: { ssid: 'AVR-mist-5060' }, clients: [{ mac: '10:20:30:40:50:44', ip: '10.77.0.10', assoc: true, rssi: -68, ping: null, seen: 0 }] },
  bat: { on: true, ok: true, bridge: true, ago: 1, mac: '10:20:30:40:50:44', ver: '1.1.0', rssi: -67, st: 2, name: '40500000000', soc: 53 }
});
const out = (name, d) => {
  for (const k in el) delete el[k];
  renderDev(d);
  let h = card.replace('<span id="dvcnt" class="mut"></span>', '<span id="dvcnt" class="mut">' + el.dvcnt.textContent + '</span>')
    .replace('<table id="dvb"></table>', '<table id="dvb">' + el.dvb.innerHTML + '</table>')
    .replace('<table id="cl"></table>', '<table id="cl">' + el.cl.innerHTML + '</table>')
    .replace('<div class="mut" id="clhint"></div>', '<div class="mut" id="clhint">' + esc(el.clhint.textContent) + '</div>');
  fs.writeFileSync(process.env.OUT + '/' + name + '.html', '<!doctype html><html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>' + style + '</style></head><body><main>' + h + '</main></body></html>');
  console.log(name, el.dvcnt.textContent, '| рядків:', (el.dvb.innerHTML.match(/<tr/g) || []).length, (el.cl.innerHTML.match(/<tr/g) || []).length);
};
out('card_now', base());
const d = base();
d.net.clients.push({ mac: 'c4:5b:be:00:00:01', ip: '10.77.0.11', assoc: true, rssi: -51, ping: null, seen: 0 },
  { mac: 'a6:3e:11:00:00:02', ip: '10.77.0.12', assoc: true, rssi: -48, ping: null, seen: 0 },
  { mac: '80:65:99:11:22:33', ip: '10.77.0.13', assoc: true, rssi: -55, ping: null, seen: 0 },
  { mac: '10:20:30:40:50:73', ip: '10.77.0.5', assoc: true, rssi: -62, ping: 2, seen: 0 });
d.watch.grid = { st: 1, exact: true, ago: 60, at: '12:54' };
d.watch.bcn.list[0] = Object.assign({}, d.watch.bcn.list[0], { seen: true, alive: true, ago: 1, rssi: -58, ver: '1.0.17' });
d.watch.bcn.list.push({ mac: '80:65:99:11:22:33', cfg: false, seen: true, alive: true, ago: 1, rssi: -55, ver: '1.0.17' });
out('card_all', d);
const u = base(); u.watch.avr = { assigned: false, mac: '' };
u.net.clients.push({ mac: 'c4:5b:be:00:00:01', ip: '10.77.0.11', assoc: true, rssi: -51, ping: null, seen: 0 });
out('card_noavr', u);
