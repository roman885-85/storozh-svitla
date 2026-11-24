// Сторінка мосту батареї (firmware/avr-bms/page.h, 1.2.0) з підставними відповідями плати: дві BMS «по черзі»,
// стрічка й пасивний бузер — щоб побачити картки «Які BMS слухати», «Батарея 1/2», «Світло», «Звук» без плати.
const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../../firmware/avr-bms/page.h', 'utf8');
let html = src.slice(src.indexOf('R"PAGE(') + 7, src.indexOf(')PAGE"'));
const cells = [3318, 3322, 3320, 3319, 3321, 3320, 3318, 3322, 3320, 3319, 3321, 3320, 3318, 3322, 3320, 3319];
const pack = (k, soc, i, st, lbl, age) => ({ mac: 'c8:47:80:00:00:0' + (k + 1), name: 'JK_B2A8S20P', lbl, st, rssi: -70 - k * 6, polls: 12, fails: k, next: 180 - k * 20,
  c: { age, fmt: 32, soc, v: 53100, i, rem: 210000 * soc / 100 | 0, full: 280000, cyc: 3, soh: 100, n: 16, lo: 3318, lox: 1, hi: 3322, hix: 2, chg: true, dis: true, bal: false, heat: false,
       t1: 215, tm: 263, info: 'JK_B2A8S20P, прошивка 11.26', cells, errs: k ? ['низька напруга комірки'] : [], notes: [] } });
const D = {
  state: { ver: '1.2.0', mac: '10:20:30:40:1a:2b', ssid: 'AVR-mist-1A2B', conn: true, rssi: -61, err: '', ap: 'BMS-1A2B', setup: false, pin: '', trust: true, ota: '', var: 'usb',
    heap: 150000, rst: 1, up: 7300, mode: 1, per: 300, b: { st: 5, mac: '', name: '', rssi: 0, rc: 25, cmds: 40, note: 'JK_B2A8S20P: опитано' }, fage: 95,
    found: [{ mac: 'c8:47:80:00:00:01', name: 'JK_B2A8S20P', rssi: -70, jk: true }, { mac: 'c8:47:80:00:00:02', name: 'JK_B2A8S20P', rssi: -76, jk: true },
            { mac: 'c8:47:80:00:00:09', name: 'JK-BD6A20S', rssi: -88, jk: true }],
    list: [{ mac: 'c8:47:80:00:00:01', lbl: 'Інвертор' }, { mac: 'c8:47:80:00:00:02', lbl: 'Гараж' }],
    packs: [pack(0, 76, -8400, 2, 'Інвертор', 95), pack(1, 18, -2100, 2, 'Гараж', 135)] },
  lcd: { on: false, ok: false, clk: 4, din: 6, cs: 7, rs: 5, rst: 10, bl: 3, con: 50, rat: 3, xoff: 4, b7: false, inv: false, flip: false, bri: 100, blLow: false, nf: 22, nt: 7, nb: 10,
    night: false, test: 0, clock: '21:40', pins: [0, 1, 3, 4, 5, 6, 7, 10, 20, 21, 2, 8], fb: '0'.repeat(2048) },
  alert: { light: 2, g: -1, r: -1, low: false, sp: 8, sn: 8, so: 0, bri: 40, nbri: 5, snd: 2, bz: 1, bzLow: false, vol: 80, mask: 65534 & ~(1 << 10) & ~(1 << 11), rep: 10, nf: 22, nt: 7, nm: 1,
    night: false, clock: true, muted: false, playing: false, pins: [0, 1, 3, 4, 5, 6, 7, 10, 20, 21, 2, 8], lcdPins: [],
    names: ['', 'батарея зарядилась', 'лишилась половина заряду', 'заряд 30 %', 'заряд 20 %', 'заряд 10 %', 'заряд 5 %', 'повний розряд', 'тривога BMS', 'тривог BMS більше немає',
            'почався розряд', 'почався заряд', 'вистачить менше ніж на годину', 'перекіс комірок', "BMS не на зв'язку", "BMS знову на зв'язку"],
    crit: [0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], crits: [{ pk: 2, e: 8 }],
    log: [{ t: '21:38', ago: 120, pk: 2, e: 8 }, { t: '21:31', ago: 540, pk: 2, e: 4 }, { t: '20:02', ago: 5900, pk: 1, e: 2 }] }
};
const stub = '<script>const __D=' + JSON.stringify(D) + ';window.fetch=async(u,o)=>{const k=String(u).split("?")[0];return {ok:true,status:200,json:async()=>__D[k]||{ok:true}}};</script>';
html = html.replace('</main><script>', '</main>' + stub + '<script>');
fs.writeFileSync(process.env.OUT + '/bmspage.html', html);
console.log('сторінка:', process.env.OUT + '/bmspage.html');
