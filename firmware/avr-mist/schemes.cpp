/*  Схеми підключення (1.0.23). Власник 18.11.2026: «добавь в веб инструкцию со схемами подключения». Окрема сторінка
    /schemes: на ній малюнки (SVG) з номерами виводів, як їх задано зараз на вкладці «Сигнали» (без входу або з файлу —
    типові). Сторінка Сторожа показує її на вкладці «Інструкція»; tools/mk_schemes.sh робить з неї docs/СХЕМИ.pdf.  */
#include <Arduino.h>

extern const char SCHEMES_HTML[] PROGMEM = R"PAGE(<!doctype html>
<html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Сторож світла: схеми підключення</title>
<style>
:root{--bg:#f3f4f7;--card:#fff;--fg:#1c2230;--mut:#677085;--bd:#e1e4ea;--mod:#eef2f8;--bd2:#9aa3b5;--bg2:#fff;--gnd:#4a5263;--v5:#d03a3a;--v33:#e07b00;--s1:#2f6fde;--s2:#1f9d57;--s3:#9b4fd9;--s4:#c98a00;--s5:#0e9aa7;--s6:#d24b8a}
@media (prefers-color-scheme:dark){:root{--bg:#13161c;--card:#1c2028;--fg:#e8eaf0;--mut:#98a0b2;--bd:#2b303a;--mod:#232937;--bd2:#59627a;--bg2:#1c2028;--gnd:#a9b1c3}}
*{box-sizing:border-box}body{margin:0;font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:780px;margin:0 auto;padding:8px 12px 28px}.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin:10px 0;break-inside:avoid}
h1{font-size:20px;margin:8px 0}h2{font-size:17px;margin:0 0 6px}.mut{color:var(--mut);font-size:13px}ul{margin:6px 0;padding-left:20px}li{margin:3px 0}
.lg{display:flex;flex-wrap:wrap;gap:4px 14px;margin-top:8px;font-size:13px}.lg span{display:inline-flex;align-items:center;gap:6px}.lg i{width:12px;height:12px;border-radius:3px;display:inline-block}
svg{display:block;margin:8px auto 0;height:auto}.warn{color:var(--v5);font-weight:600}
@media print{body{background:#fff}.card{border-color:#ccc}}
</style></head><body><main>
<h1>Сторож світла: схеми підключення</h1>
<div class="mut" id="src">Номери виводів — як задано на вкладці «Сигнали».</div>
<div id="out"></div>
</main>
<script>
const $=id=>document.getElementById(id);
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
//  Плата ESP32 DevKit V1 (DOIT, 30 виводів), вид згори, антена вгорі: підписи на платі
const LEFT=['EN','VP','VN','D34','D35','D32','D33','D25','D26','D27','D14','D12','D13','GND','VIN'];
const RIGHT=['D23','D22','TX0','RX0','D21','D19','D18','D5','TX2','RX2','D4','D2','D15','GND','3V3'];
const GPIO={VP:36,VN:39,TX0:1,RX0:3,TX2:17,RX2:16};
function gp(l){return GPIO[l]!=null?GPIO[l]:(/^D\d+$/.test(l)?+l.slice(1):null)}
function lab(g){const l=[...LEFT,...RIGHT].find(x=>gp(x)===g);return 'GPIO'+g+(l&&l!=='D'+g?' ('+l+')':l?' ('+l+')':'')}
// ---------- малювання ----------
function svg(w,h,b){return '<svg viewBox="0 0 '+w+' '+h+'" width="'+w+'" style="max-width:100%" xmlns="http://www.w3.org/2000/svg" font-family="system-ui,-apple-system,Segoe UI,Roboto,sans-serif" font-size="13">'+b+'</svg>'}
function T(x,y,s,o){o=o||{};return '<text x="'+x+'" y="'+y+'" fill="'+(o.c||'var(--fg)')+'" text-anchor="'+(o.a||'start')+'" font-size="'+(o.s||13)+'"'+(o.b?' font-weight="650"':'')+'>'+esc(s)+'</text>'}
function box(x,y,w,h,title,sub){return '<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'" rx="9" fill="var(--mod)" stroke="var(--bd2)" stroke-width="1.5"/>'+T(x+w/2,y+19,title,{a:'middle',b:1})+(sub?T(x+w/2,y+35,sub,{a:'middle',c:'var(--mut)',s:11}):'')}
function W(pts,c,dash){return '<polyline points="'+pts.map(p=>p.join(',')).join(' ')+'" fill="none" stroke="'+(c||'var(--fg)')+'" stroke-width="2.6" stroke-linejoin="round" stroke-linecap="round"'+(dash?' stroke-dasharray="7 5"':'')+'/>'}
function dot(x,y,c){return '<circle cx="'+x+'" cy="'+y+'" r="3.8" fill="'+(c||'var(--fg)')+'"/>'}
//  вивід на краю модуля: квадратик і підпис усередині модуля (side: 'r' — вивід на правому краю, підпис ліворуч від нього)
function P(x,y,label,side,c){return '<rect x="'+(x-4.5)+'" y="'+(y-4.5)+'" width="9" height="9" rx="2" fill="'+(c||'var(--bd2)')+'"/>'+T(side==='r'?x-10:x+10,y+4.5,label,{a:side==='r'?'end':'start',s:12})}
function R(x1,y1,x2,y2,label,c){const hz=y1===y2,mx=(x1+x2)/2,my=(y1+y2)/2,L=38,H=13;
 if(hz)return W([[x1,y1],[mx-L/2,y1]],c)+W([[mx+L/2,y1],[x2,y2]],c)+'<rect x="'+(mx-L/2)+'" y="'+(my-H/2)+'" width="'+L+'" height="'+H+'" rx="2" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'+T(mx,my-12,label,{a:'middle',s:12});
 return W([[x1,y1],[x1,my-L/2]],c)+W([[x1,my+L/2],[x2,y2]],c)+'<rect x="'+(mx-H/2)+'" y="'+(my-L/2)+'" width="'+H+'" height="'+L+'" rx="2" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'+T(mx+13,my+4,label,{s:12})}
function G(x,y){return W([[x,y],[x,y+9]],'var(--gnd)')+'<path d="M'+(x-11)+' '+(y+9)+'h22M'+(x-7)+' '+(y+13.5)+'h14M'+(x-3)+' '+(y+18)+'h6" stroke="var(--gnd)" stroke-width="2.4"/>'}
function LED(x,y,col,label){return W([[x,y-22],[x,y-8]])+'<polygon points="'+(x-10)+','+(y-8)+' '+(x+10)+','+(y-8)+' '+x+','+(y+8)+'" fill="'+col+'" stroke="var(--fg)" stroke-width="1.5"/>'+
 '<path d="M'+(x-10)+' '+(y+8)+'h20" stroke="var(--fg)" stroke-width="2.4"/><path d="M'+(x+13)+' '+(y-3)+'l9 -9m-5 0h5v5M'+(x+16)+' '+(y+5)+'l9 -9m-5 0h5v5" stroke="'+col+'" stroke-width="1.8" fill="none"/>'+W([[x,y+8],[x,y+22]])+(label?T(x+30,y+5,label,{s:12}):'')}
function CAP(x,y,label,pol){return W([[x,y-22],[x,y-5]])+'<path d="M'+(x-13)+' '+(y-5)+'h26" stroke="var(--fg)" stroke-width="2.6"/>'+(pol?'<path d="M'+(x-13)+' '+(y+5)+'h26" stroke="var(--fg)" stroke-width="2.6" stroke-dasharray="3 2"/>'+T(x-20,y-6,'+',{s:12}):'<path d="M'+(x-13)+' '+(y+5)+'h26" stroke="var(--fg)" stroke-width="2.6"/>')+W([[x,y+5],[x,y+22]])+(label?T(x+18,y+5,label,{s:12}):'')}
function NPN(x,y,label){return '<circle cx="'+(x+4)+'" cy="'+y+'" r="18" fill="var(--bg2)" stroke="var(--fg)" stroke-width="1.5"/><path d="M'+(x-4)+' '+(y-11)+'v22" stroke="var(--fg)" stroke-width="3"/>'+
 W([[x-24,y],[x-4,y]])+W([[x-4,y-5],[x+9,y-14],[x+9,y-26]])+W([[x-4,y+5],[x+9,y+14],[x+9,y+26]])+'<polygon points="'+(x+9)+','+(y+14)+' '+(x+1)+','+(y+14)+' '+(x+6)+','+(y+7)+'" fill="var(--fg)"/>'+(label?T(x+26,y+4,label,{s:12}):'')}
function BTN(x,y){return W([[x-32,y],[x-14,y]])+W([[x+14,y],[x+32,y]])+dot(x-14,y)+dot(x+14,y)+'<path d="M'+(x-17)+' '+(y-11)+'h34M'+x+' '+(y-11)+'v-9M'+(x-6)+' '+(y-20)+'h12" stroke="var(--fg)" stroke-width="2.6" fill="none"/>'}
function SPK(x,y){return '<rect x="'+(x-6)+'" y="'+(y-11)+'" width="12" height="22" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/><polygon points="'+(x+6)+','+(y-11)+' '+(x+22)+','+(y-25)+' '+(x+22)+','+(y+25)+' '+(x+6)+','+(y+11)+'" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'}
function legend(items){return '<div class="lg">'+items.map(([c,s])=>'<span><i style="background:'+c+'"></i>'+esc(s)+'</span>').join('')+'</div>'}
function card(h,svgs,text){return '<div class="card"><h2>'+esc(h)+'</h2>'+svgs+(text||'')+'</div>'}
// ---------- схеми ----------
function esp(x,y,h,pins,title){let s=box(x,y,170,h,title||'Сторож (ESP32)','плата DevKit');pins.forEach(([py,l,c])=>{s+=P(x+170,py,l,'r',c)});return s}
function scLeds(c){const yG=86,yR=150;let s=esp(14,36,196,[[yG,lab(c.ledG),'var(--s2)'],[yR,lab(c.ledR),'var(--v5)'],[206,'GND','var(--gnd)']]);
 //  зелений далі праворуч, червоний ближче — дроти не перетинаються
 s+=R(184,yG,330,yG,'220–330 Ом','var(--s2)')+W([[330,yG],[480,yG],[480,yG+48]],'var(--s2)')+LED(480,yG+70,'#22b35e','')+T(512,yG+66,'зелений',{s:12})+T(512,yG+82,'«світло є»',{s:11,c:'var(--mut)'})+W([[480,yG+92],[480,240]],'var(--gnd)');
 s+=R(184,yR,330,yR,'220–330 Ом','var(--v5)')+W([[330,yR],[380,yR],[380,yR+18]],'var(--v5)')+LED(380,yR+40,'#e04848','')+T(412,yR+36,'червоний',{s:12})+T(412,yR+52,'«немає»',{s:11,c:'var(--mut)'})+W([[380,yR+62],[380,240]],'var(--gnd)');
 s+=W([[184,206],[240,206],[240,240],[480,240]],'var(--gnd)')+dot(380,240,'var(--gnd)')+G(240,240);
 return card('Два світлодіоди',svg(600,274,s),'<ul><li>Довга ніжка світлодіода (+, анод) — до резистора, коротка (−, катод) — до GND.</li><li>Резистор 220–330 Ом обов\'язковий: без нього світлодіод і вивід плати перегріваються.</li><li>Світлодіоди зі спільним анодом (+): анод — на 3V3, катоди через резистори — на виводи, і позначка «Вмикаються низьким рівнем».</li></ul>')}
function scStrip(c){let s=esp(14,36,206,[[90,lab(c.stripPin),'var(--s1)'],[150,'VIN (5 В)','var(--v5)'],[210,'GND','var(--gnd)']]);
 s+=box(420,36,190,206,'Стрічка WS2812B','вхід — бік зі стрілкою')+P(420,90,'DIN','l','var(--s1)')+P(420,150,'+5V','l','var(--v5)')+P(420,210,'GND','l','var(--gnd)');
 s+=R(184,90,420,90,'330 Ом','var(--s1)');
 s+=W([[184,150],[420,150]],'var(--v5)')+W([[184,210],[420,210]],'var(--gnd)');
 s+=W([[340,150],[340,158]],'var(--v5)')+CAP(340,180,'1000 мкФ, 6,3–10 В',1)+dot(340,150,'var(--v5)')+dot(340,210,'var(--gnd)');
 return card('Адресна стрічка WS2812B',svg(630,262,s),'<ul><li>8 світлодіодів на 30 % беруть ≈ 0,2 А — вистачає 5 В з виводу VIN плати (від USB). Довша стрічка чи яскравіше — окремий блок 5 В, і його GND обов\'язково з\'єднати з GND плати.</li><li>Резистор 330 Ом — у розрив проводу даних, ближче до стрічки. Конденсатор 1000 мкФ — на вході стрічки, «+» до +5V.</li><li>Дріт даних — до 1–2 м. Межу струму задайте на вкладці «Сигнали».</li></ul>')}
function scBuzzer(c){let s=esp(14,36,236,[[90,'VIN (5 В)','var(--v5)'],[196,lab(c.sndPin),'var(--s4)'],[246,'GND','var(--gnd)']]);
 s+=W([[184,90],[470,90],[470,108]],'var(--v5)')+box(430,108,80,44,'Бузер','')+T(440,124,'+',{s:13,b:1})+T(440,148,'−',{s:13,b:1});
 s+=W([[470,152],[470,170]])+NPN(461,196,'')+T(492,200,'NPN: S8050, 2N2222',{s:12});
 s+=R(184,196,437,196,'1 кОм','var(--s4)');
 s+=W([[470,222],[470,246],[184,246]],'var(--gnd)')+G(300,246);
 s+=T(482,166,'колектор',{s:11,c:'var(--mut)'})+T(482,236,'емітер',{s:11,c:'var(--mut)'})+T(410,190,'база',{s:11,c:'var(--mut)',a:'end'});
 return card('Бузер (активний або пасивний)',svg(640,282,s),'<ul><li>«+» бузера — до 5 В (VIN), «−» — до колектора транзистора; емітер — на GND, база — через 1 кОм до виводу.</li><li>Пасивний бузер: паралельно йому діод 1N4148 (смужка діода — до +5 В).</li><li>Маленький активний бузер до 20 мА можна напряму: вивід → «+», «−» → GND.</li></ul>')}
function scDac(c){let s=esp(14,36,206,[[90,lab(c.dacPin)+' — ЦАП','var(--s3)'],[150,'GND','var(--gnd)'],[210,'VIN (5 В)','var(--v5)']]);
 s+=R(184,90,320,90,'4,7 кОм','var(--s3)')+W([[320,90],[420,90]],'var(--s3)')+dot(370,90,'var(--s3)')+R(370,90,370,150,'1 кОм','var(--s3)');
 s+=box(420,36,170,206,'Підсилювач PAM8403','5 В, 2×3 Вт')+P(420,90,'L (вхід)','l','var(--s3)')+P(420,150,'GND','l','var(--gnd)')+P(420,210,'5V','l','var(--v5)')+P(590,100,'L+','r','var(--fg)')+P(590,140,'L−','r','var(--fg)');
 s+=W([[184,150],[420,150]],'var(--gnd)')+dot(370,150,'var(--gnd)')+W([[184,210],[420,210]],'var(--v5)');
 s+=W([[590,100],[628,100],[628,110]])+W([[590,140],[628,140],[628,130]])+SPK(634,120)+T(620,172,'динамік 4–8 Ом',{s:12,a:'middle'});
 return card('Динамік через ЦАП і підсилювач',svg(680,262,s),'<ul><li>ЦАП плати дає слабкий сигнал — динамік лише через підсилювач (PAM8403, LM386). Напряму до виводу динамік не підключати.</li><li>Дільник 4,7 кОм + 1 кОм зменшує сигнал у ~5,7 раза: PAM8403 тоді не хрипить і на 80–100 % гучності. Без дільника тримайте гучність 20–40 %.</li><li>Немає на модулі конденсатора на вході — додайте 1–10 мкФ послідовно перед входом L.</li></ul>')}
function scBtn(c){let s=esp(14,30,150,[[80,lab(c.pwBtn),'var(--s6)'],[150,'GND','var(--gnd)']]);
 s+=W([[184,80],[300,80]],'var(--s6)')+BTN(332,80)+W([[364,80],[420,80],[420,150],[184,150]],'var(--gnd)')+G(300,150)+T(332,46,'кнопка без фіксації',{a:'middle',s:12});
 return card('Кнопка скидання пароля сторінки',svg(480,190,s),'<ul><li>Кнопка між виводом і GND, без резистора (підтяжка всередині плати).</li><li>Утримати 5 секунд — пароль сторінки стане типовим, решта налаштувань лишається; у Telegram прийде «🔑».</li><li>Кнопка BOOT на самій платі: 10 секунд — стерти всі налаштування.</li></ul>')}
function scLcd(c){const rows=[['RST',c.lcdRst,'var(--s1)'],['CE',c.lcdCe,'var(--s2)'],['DC',c.lcdDc,'var(--s3)'],['DIN',c.lcdDin,'var(--s4)'],['CLK',c.lcdClk,'var(--s5)'],['VCC','3V3','var(--v33)'],['BL',c.lcdBl,'var(--s6)'],['GND','GND','var(--gnd)']];
 const y0=86,dy=30;let s=box(14,30,200,y0+dy*8-36,'Сторож (ESP32)','плата DevKit')+box(440,30,180,y0+dy*8-36,'Nokia 5110','PCD8544, 84×48');
 rows.forEach(([n,p,col],i)=>{const y=y0+i*dy,pl=typeof p==='number'?(p?lab(p):'3V3 або GND'):p;s+=P(214,y,pl,'r',col)+P(440,y,(i+1)+' '+n,'l',col)+W([[214,y],[440,y]],col)});
 return card('Дисплей Nokia 5110',svg(640,y0+dy*8-0,s),'<ul><li class="warn">Живлення дисплея — лише 3,3 В (вивід 3V3), не 5 В.</li><li>Порядок виводів на модулі буває інший — звіряйте підписи RST, CE, DC, DIN, CLK на платі дисплея.</li><li>BL (LIGHT) — підсвітка. Червона плата світить, коли BL на GND (позначка «вмикається низьким рівнем»), синя — коли на 3,3 В. Без керування — BL напряму на GND чи 3V3.</li><li>Дроти до 30 см. Контраст — повзунком на вкладці «Сигнали».</li></ul>')}
function scMayak(){let s='';
 s+=box(10,20,150,60,'Міська мережа','ввід А')+box(10,150,150,60,'Генератор','ввід B')+box(250,80,150,70,'АВР','з Wi-Fi чи без')+box(490,40,170,60,'Розетки приміщення','після АВР · маячки ліній');
 s+=box(250,200,150,60,'Розетка генератора','лише від генератора')+box(490,190,170,80,'Маячок','будь-яка ESP + USB 5 В');
 s+=W([[160,50],[205,50],[205,100],[250,100]],'var(--v5)')+W([[160,180],[205,180],[205,130],[250,130]],'var(--v33)')+W([[400,115],[445,115],[445,70],[490,70]],'var(--fg)');
 s+=W([[160,195],[205,195],[205,230],[250,230]],'var(--v33)')+W([[400,230],[490,230]],'var(--v33)');
 s+=box(490,300,170,60,'Роутер + Сторож','від ДБЖ')+W([[575,270],[575,300]],'var(--s1)',1)+T(585,290,'Wi-Fi',{s:12,c:'var(--s1)'});
 //  1.0.57: маячок «Міське світло» — у розетці лише від міста, до АВР (може бути замість АВР)
 s+=box(250,8,150,44,'Розетка міста','маячок «Міське світло»')+W([[205,50],[205,30],[250,30]],'var(--v5)')+dot(205,50,'var(--v5)');
 return card('Маячки',svg(680,380,s),'<ul><li>Маячок «Міське світло» — у розетку, що живиться лише від міста (до АВР чи на міській лінії): живий маячок = міське світло є. Він може замінити АВР з Wi-Fi (TOMZN, eWeLink) — тоді АВР у мережу Сторожа підключати не треба, і АВР може бути без Wi-Fi, — чи працювати разом з ним.</li><li>Маячок — будь-яка недорога плата ESP: ESP8266 (Wemos D1 mini, NodeMCU, ESP-01S з адаптером 3,3 В), ESP32, ESP32-C3, S2, S3, C6; живлення — USB-зарядник у тій самій розетці.</li><li>Маячок генератора вмикайте лише в розетку, що живиться від генератора: живий маячок = генератор працює.</li><li>Не в ДБЖ і не в міську розетку — інакше Сторож писатиме «генератор працює», коли його немає.</li><li>Маячок лінії чи фази (з 1.0.23) — у розетку тієї лінії, яку треба стерегти (фаза, корпус, котельня): мовчить — на ній немає напруги, у Telegram прийде «🔌».</li><li>Роль кожного маячка — «Налаштування → Маячки» на сторінці Сторожа: «Генератор», «Міське світло», «Лінія чи фаза» або «Лише показувати». Маячків — до 6.</li><li>Сторож і роутер — лише від ДБЖ: без живлення Сторож нічого не надішле.</li></ul>')}
//  1.0.57: ручний перемикач «Місто — 0 — Генератор» — лише маячки, без АВР
function scManual(){let s='';
 s+=box(10,20,150,60,'Міська мережа','ввід')+box(10,200,150,60,'Генератор','вихід')+box(250,95,150,80,'Перемикач','Місто — 0 — Генератор')+box(490,105,170,60,'Розетки приміщення','після перемикача');
 s+=box(250,10,150,50,'Розетка міста','маячок «Міське світло»')+box(250,215,150,50,'Розетка генератора','маячок «Генератор»')+box(490,215,170,50,'Розетка після перемикача','маячок «Лінія чи фаза»');
 s+=W([[160,50],[205,50],[205,120],[250,120]],'var(--v5)')+W([[205,50],[205,35],[250,35]],'var(--v5)')+dot(205,50,'var(--v5)');
 s+=W([[160,230],[205,230],[205,150],[250,150]],'var(--v33)')+W([[205,230],[205,240],[250,240]],'var(--v33)')+dot(205,230,'var(--v33)');
 s+=W([[400,135],[490,135]],'var(--fg)')+W([[445,135],[445,240],[490,240]],'var(--fg)')+dot(445,135);
 return card('Ручний перемикач чи АВР без Wi-Fi',svg(680,280,s),'<ul><li>«Налаштування → АВР чи перемикач»: «Вручну» — для перемикача «Місто — 0 — Генератор» чи рубильника; «Автоматично» — для АВР (з Wi-Fi чи без).</li><li>Маячок «Міське світло» — до перемикача, на вводі від міста; «Генератор» — на виході генератора, до перемикача; «Лінія чи фаза» — у розетці після перемикача.</li><li>Зникло місто — «Запустіть генератор і переключіть перемикач на «Генератор»»; повернулось — «Переключіть перемикач на «Місто» і вимкніть генератор».</li><li>З маячком після перемикача Сторож бачить, чи переключили: на лінії немає напруги, хоч є від чого живити, — «🔁 Переключіть…» за 2 хв, далі кожні 10 хв, до 6 разів. Без нього — лише в повідомленнях про події.</li></ul>')}
function scBoard(c){const use={};const add=(g,n,col,on)=>{if(g)use[g]={n,col,on}};
 add(c.ledG,'зелений світлодіод','var(--s2)',c.ledOn&&c.ledType===0);add(c.ledR,'червоний світлодіод','var(--v5)',c.ledOn&&c.ledType===0);
 add(c.stripPin,'стрічка DIN','var(--s1)',c.ledOn&&c.ledType===1);add(c.sndPin,'бузер','var(--s4)',c.sndType===1||c.sndType===2);add(c.dacPin,'ЦАП → підсилювач','var(--s3)',c.sndType===3);
 add(c.pwBtn,'кнопка пароля','var(--s6)',c.pwBtn>0);
 [['RST',c.lcdRst],['CE',c.lcdCe],['DC',c.lcdDc],['DIN',c.lcdDin],['CLK',c.lcdClk],['BL',c.lcdBl]].forEach(([n,g])=>add(g,'дисплей '+n,'var(--s5)',c.lcdOn));
 const x0=250,y0=60,dy=26,bw=150;let s='<rect x="'+x0+'" y="20" width="'+bw+'" height="'+(y0+dy*15)+'" rx="10" fill="var(--mod)" stroke="var(--bd2)" stroke-width="1.5"/>'+T(x0+bw/2,40,'ESP32 DevKit V1',{a:'middle',b:1,s:12})+'<rect x="'+(x0+bw/2-22)+'" y="'+(y0+dy*15-4)+'" width="44" height="20" rx="3" fill="var(--bd2)"/>'+T(x0+bw/2,y0+dy*15+10,'USB',{a:'middle',s:10,c:'var(--bg2)'});
 const side=(arr,left)=>arr.forEach((l,i)=>{const y=y0+i*dy,x=left?x0:x0+bw,g=gp(l),u=g!=null?use[g]:null,pw=l==='GND'||l==='VIN'||l==='3V3';
  const col=u?(u.on?u.col:'var(--bd2)'):pw?(l==='GND'?'var(--gnd)':l==='VIN'?'var(--v5)':'var(--v33)'):'var(--bd2)';
  s+=P(x,y,l,left?'l':'r',col);
  if(u)s+=W([[left?x-6:x+6,y],[left?x-40:x+40,y]],col,!u.on)+T(left?x-46:x+46,y+4.5,u.n+(u.on?'':' (вимкнено)'),{a:left?'end':'start',s:12,c:u.on?'var(--fg)':'var(--mut)'});
  else if(g===0)s+=T(left?x-46:x+46,y+4.5,'кнопка BOOT',{a:left?'end':'start',s:12,c:'var(--mut)'})});
 side(LEFT,true);side(RIGHT,false);
 s+=T(x0-46,y0+dy*15+16,'',{});
 return card('Плата Сторожа: що куди під\'єднано',svg(650,y0+dy*15+40,s),'<div class="mut">Кольором — те, що ввімкнено зараз; сірим пунктиром — задано, але вимкнено. Номери GPIO однакові на всіх платах ESP32; підписи «D…» — як на платі DevKit V1 (30 виводів). Не підключайте нічого до D12, D15, D2 і D5: вони керують запуском плати.</div>')}
const DEF={ledOn:false,ledType:0,ledG:18,ledR:19,stripPin:27,sndType:0,sndPin:23,dacPin:26,pwBtn:32,lcdOn:false,lcdRst:4,lcdCe:16,lcdDc:17,lcdDin:21,lcdClk:22,lcdBl:33};
async function load(){let c=Object.assign({},DEF),live=false;
 try{const [g,s]=await Promise.all([fetch('/api/signals',{credentials:'same-origin'}),fetch('/api/settings',{credentials:'same-origin'})]);
  if(g.ok&&s.ok){Object.assign(c,await g.json());c.pwBtn=(await s.json()).pwBtn;live=true}}catch(e){}
 $('src').textContent=live?'Номери виводів — як задано на вкладці «Сигнали» зараз. Змінили виводи — оновіть цю сторінку.':'Номери виводів — типові (Сторож не відповів або сторінку відкрито з файлу).';
 $('out').innerHTML=scBoard(c)+scLeds(c)+scStrip(c)+scBuzzer(c)+scDac(c)+scBtn(c)+scLcd(c)+scMayak()+scManual()+
  card('Живлення','','<ul><li>Сторожа й роутер живіть від ДБЖ: Сторож має працювати й тоді, коли світла немає, — інакше він не надішле «світло зникло».</li><li>Плата живиться від USB 5 В (блок від ДБЖ) або від 5 В на виводі VIN. 3V3 — лише вихід для дисплея й датчиків, до 300 мА.</li><li>Усі GND — спільні: плата, стрічка, підсилювач, окремі блоки живлення.</li></ul>');
 try{parent.postMessage({schemesH:document.documentElement.scrollHeight},'*')}catch(e){}}
load();
</script></body></html>)PAGE";
