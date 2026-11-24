/*  Сторінка мосту батареї (http://192.168.4.1 і адреса плати в мережі Сторожа). */
#pragma once
static const char PAGE[] = R"PAGE(<!doctype html><html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Міст батареї</title><style>
:root{--bg:#14161c;--card:#1d2028;--fg:#e8e8ec;--mut:#9aa0ab;--acc:#2f7de1;--bd:#2c303b;--ok:#2fb36b;--bad:#e05252;--warn:#d9a21b}
@media (prefers-color-scheme:light){:root{--bg:#f3f4f7;--card:#fff;--fg:#16181d;--mut:#5d6470;--bd:#d9dce3}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:12px 16px 30px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin:12px 0}
h1{font-size:20px;margin:8px 0 2px}.mut{color:var(--mut);font-size:13px}
label{display:block;margin:12px 0 4px;color:var(--mut);font-size:13px}
input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
button{margin-top:12px;width:100%;padding:11px;border:0;border-radius:8px;background:var(--acc);color:#fff;font:inherit;font-weight:600}
button.s{background:none;border:1px solid var(--bd);color:var(--fg);font-weight:400}button:disabled{opacity:.5}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.row{display:flex;gap:8px;align-items:center}.row input{width:auto}
.big{font-size:28px;font-weight:700}.cells{display:flex;flex-wrap:wrap;gap:4px;margin-top:8px}
.cells span{font-size:12px;padding:3px 5px;border:1px solid var(--bd);border-radius:6px;font-variant-numeric:tabular-nums}
.cells .lo{border-color:var(--bad)}.cells .hi{border-color:var(--ok)}
.g3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.g3 label{margin:8px 0 4px}.inl{display:flex;gap:6px;align-items:center;flex-wrap:wrap}.inl input{width:70px}
.btns{display:flex;gap:6px;flex-wrap:wrap}.btns button{width:auto;flex:1;margin-top:8px}
#lcdc{width:100%;max-width:384px;aspect-ratio:2/1;image-rendering:pixelated;background:#dfe6ea;border:6px solid #30343c;border-radius:8px;display:block}
.pkr{display:flex;gap:6px;align-items:center;margin-top:6px}.pkr input{flex:1;min-width:0}.pkr .mut{white-space:nowrap}.pkr button{width:auto;margin:0;padding:8px 12px}
.evr{display:flex;gap:6px;align-items:center;margin-top:4px}.evr label{flex:1;margin:0;color:var(--fg);font-size:14px;display:flex;gap:6px;align-items:center}.evr input{width:auto}
.evr button{width:auto;margin:0;padding:4px 12px}h2{font-size:16px;margin:0}
</style></head><body><main>
<h1>Міст батареї «Сторожа світла»</h1><div class="mut" id="who"></div>
<div class="card"><b>Зараз</b><div id="st" style="margin-top:6px">…</div>
<button class="s" id="rb" type="button" style="display:none">Перезапустити міст</button><div id="rbmsg" style="margin-top:8px"></div></div>
<div id="bats"></div>
<div class="card" id="pick" style="display:none"><b>Які BMS слухати</b>
<label class="row"><input type="radio" name="pm" id="pm0" value="0"> Одна BMS — постійне з'єднання</label>
<label class="row" style="margin-top:4px"><input type="radio" name="pm" id="pm1" value="1"> Кілька BMS (до 4) — по черзі</label>
<div class="mut" id="pmh"></div>
<div id="one"><div class="mut" style="margin-top:6px">Типово — найближчу JK BMS (назва «JK-…» чи «JK_…», найсильніший сигнал). Якщо поруч кілька чи свою BMS перейменовано — виберіть її в «Чути поруч» і «Слухати вибрану».</div></div>
<div id="many"><div id="pkl"></div>
<label for="pper">Опитувати кожну</label><select id="pper"><option value="60">щохвилини</option><option value="120">раз на 2 хв</option><option value="300">раз на 5 хв</option><option value="600">раз на 10 хв</option><option value="900">раз на 15 хв</option><option value="1800">раз на 30 хв</option></select></div>
<label for="bl">Чути поруч <span class="mut" id="fage"></span></label><select id="bl"></select>
<div class="btns"><button class="s" id="bscan" type="button">Шукати BMS</button><button class="s" id="pkadd" type="button">Додати до переліку</button><button class="s" id="pin" type="button">Слухати вибрану</button><button class="s" id="auto" type="button">Найближчу (типово)</button></div>
<button id="pksave" type="button">Зберегти режим і перелік</button><button class="s" id="pnow" type="button">Опитати всі зараз</button>
<div id="pmsg" style="margin-top:8px"></div></div>
<div class="card" id="form" style="display:none"><b>Мережа Сторожа</b>
<div class="mut">Виберіть мережу Сторожа («AVR-mist-…») і введіть її пароль — той самий, що на сторінці Сторожа в «Налаштування → Точка доступу для АВР».</div>
<label for="ss">Мережа</label><select id="ss"><option value="">шукаю мережі…</option></select>
<button class="s" id="rescan" type="button">Шукати ще раз</button>
<label for="mn">Або назва вручну</label><input id="mn" maxlength="32" placeholder="AVR-mist-XXXX" autocomplete="off" autocapitalize="off">
<label for="pw">Пароль мережі</label><input id="pw" type="password" maxlength="63" autocomplete="new-password">
<label class="row"><input type="checkbox" id="sh"> показати пароль</label>
<button id="go" type="button">Під'єднати</button><div id="msg" style="margin-top:8px"></div></div>
<div class="card" id="lcdcard"><b>Дисплей</b> <span class="mut">Open-Smart 1,8″, 128×64, ST7567</span>
<label class="row"><input type="checkbox" id="l_on"> Дисплей під'єднано</label>
<canvas id="lcdc" width="128" height="64"></canvas>
<div class="mut" style="margin-top:4px">Так виглядає екран зараз (знімок з пам'яті плати, раз на 2 с) — навіть коли дисплей ще не під'єднано. Підсвітку знімок не показує. <span id="lcdst"></span></div>
<div class="g3"><div><label>CLK</label><select id="l_clk"></select></div><div><label>DI</label><select id="l_din"></select></div><div><label>CS</label><select id="l_cs"></select></div>
<div><label>RS (A0)</label><select id="l_rs"></select></div><div><label>RST (RSE)</label><select id="l_rst"></select></div><div><label>LED</label><select id="l_bl"></select></div></div>
<div class="mut" style="margin-top:6px">Підключення (живлення модуля — 3,3 В): <span id="lcdwire"></span>, VCC → 3V3, GND → GND. Дроти — до 30 см. GPIO2 і GPIO8 — виводи запуску плати: працюють, але краще не займати.</div>
<label>Контраст: <b id="l_conV"></b> <span class="mut">(блідо — більше, темне тло — менше; типово 50)</span></label><input type="range" id="l_con" min="0" max="63" step="1">
<div class="g3"><div><label>Грубий рівень</label><select id="l_rat"><option>0</option><option>1</option><option>2</option><option value="3">3 — типово</option><option>4</option><option>5</option><option>6</option><option>7</option></select></div>
<div><label>Зсув (bias)</label><select id="l_b7"><option value="0">1/9 — типово</option><option value="1">1/7</option></select></div>
<div><label>Картинка вправо</label><select id="l_xoff"><option>0</option><option>1</option><option>2</option><option>3</option><option value="4">4 — типово</option></select></div></div>
<div class="mut">Типові значення — як у робочому проєкті moto_battery_reader (варіант ENH_DG128064: зсув 1/9; зі зсувом 1/7 екран темний і засвічений). Повзунка контрасту бракує — змініть грубий рівень: більший — темніше. Картинка зсунута вбік чи з лівого краю смужка сміття — підберіть «Картинка вправо» від 0 до 4.</div>
<label>Підсвітка: <b id="l_briV"></b> %</label><input type="range" id="l_bri" min="0" max="100" step="5">
<label class="row"><input type="checkbox" id="l_blLow"> Підсвітка вмикається низьким рівнем (повзунок діє навпаки)</label>
<label class="row"><input type="checkbox" id="l_inv"> Інверсія: світлі літери на темному</label>
<label class="row"><input type="checkbox" id="l_flip"> Перевернути на 180°</label>
<label>Ніч</label><div class="inl">з <input id="l_nf" type="number" min="0" max="23"> до <input id="l_nt" type="number" min="0" max="23"> години підсвітка <input id="l_nb" type="number" min="0" max="100" step="5"> %</div>
<div class="mut" id="lcdclock" style="margin-top:4px"></div>
<button id="l_save" type="button">Зберегти дисплей</button><div id="lmsg" style="margin-top:8px"></div>
<div style="margin-top:10px"><b>Перевірка</b> <span class="mut">— на 60 с, нічого не зберігає; повзунки контрасту й підсвітки теж діють одразу</span></div>
<div class="btns"><button class="s" data-t="all" type="button">Усе чорне</button><button class="s" data-t="check" type="button">Шахівниця</button><button class="s" data-t="normal" type="button">Звичайний вигляд</button></div>
</div>
<div class="card" id="lightcard" style="display:none"><b>Світло</b> <span class="mut">світлодіоди чи адресна стрічка — заряд і тривоги видно здалеку</span>
<label for="a_light">Що під'єднано</label><select id="a_light"><option value="0">нічого</option><option value="1">два світлодіоди — зелений і червоний</option><option value="2">адресна стрічка WS2812B</option></select>
<div id="a_l1"><div class="g3"><div><label>Зелений</label><select id="a_g"></select></div><div><label>Червоний</label><select id="a_r"></select></div></div>
<label class="row"><input type="checkbox" id="a_low"> Світлодіоди вмикаються низьким рівнем</label>
<div class="mut">Кожен світлодіод — через резистор 220–470 Ом: довга ніжка (анод) — до виводу, коротка — до GND. Одного кольору немає — «немає».</div></div>
<div id="a_l2"><div class="g3"><div><label>Вивід DIN</label><select id="a_sp"></select></div><div><label>Світлодіодів</label><input id="a_sn" type="number" min="1" max="60"></div><div><label>Кольори</label><select id="a_so"><option value="0">GRB — звичайна</option><option value="1">RGB</option><option value="2">BGR</option></select></div></div>
<div class="mut">Стрічка: 5 В і GND — від блока живлення (8 світлодіодів на 40 % — до 0,2 А), DIN — до виводу через резистор 330 Ом; GND стрічки й плати — спільний. Кольори переплутані (червоний замість зеленого) — змініть «Кольори».</div></div>
<div class="g3"><div><label>Яскравість удень, %</label><input id="a_bri" type="number" min="1" max="100"></div><div><label>Уночі, % (0 — вимкнено)</label><input id="a_nbri" type="number" min="0" max="100"></div></div>
<div class="mut" style="margin-top:6px">Два світлодіоди: зелений — заряд 50 % і більше; обидва — 20–50 %; червоний — до 20 %, блимає — до 10 %, часто — до 5 %; заряд іде — зелений повільно блимає; тривога BMS — зелений і червоний по черзі; даних немає — рідкий спалах червоного. Стрічка: смуга заряду від першого світлодіода (зелена; помаранчева нижче 50 %, червона до 20 %, часто блимає до 5 %); заряд іде — угору біжить білий вогник; тривога BMS — уся блимає червоним; даних немає — перший світлодіод блимає синім. Кілька батарей — стрічка ділиться на відрізки (по 2 світлодіоди й більше на кожну), а два світлодіоди й коротка стрічка показують найгіршу.</div>
<div class="btns"><button class="s" id="a_ltest" type="button">Перевірити світло (6 с)</button><button class="asv" type="button">Зберегти світло й звук</button></div><div class="amsg" style="margin-top:8px"></div></div>
<div class="card" id="sndcard" style="display:none"><b>Звук</b> <span class="mut">бузер: зарядилась, 50 / 30 / 20 / 10 / 5 %, повний розряд, тривога BMS…</span>
<label for="a_snd">Що під'єднано</label><select id="a_snd"><option value="0">нічого</option><option value="1">активний бузер — пищить сам від живлення</option><option value="2">пасивний бузер — грає мелодії</option></select>
<div id="a_s1"><div class="g3"><div><label>Вивід</label><select id="a_bz"></select></div><div><label>Гучність, %</label><input id="a_vol" type="number" min="5" max="100"></div></div>
<label class="row"><input type="checkbox" id="a_bzLow"> Бузер вмикається низьким рівнем (модулі «low level trigger»)</label>
<div class="mut">Бузер на 3,3 В: «+» — до виводу, «−» — до GND; модуль з транзистором: VCC — 3V3, GND — GND, I/O — до виводу. Активний (з наклейкою, пищить від батарейки) — один тон; пасивний — мелодії, різні на кожну подію.</div></div>
<div style="margin-top:10px"><b>Які події звучать</b> <span class="mut">«▶» — прослухати (звучить те, що збережено)</span></div>
<div id="a_evs"></div>
<label for="a_rep">Важливе (5 %, повний розряд, тривога BMS) повторювати, доки не мине</label><select id="a_rep"><option value="0">не повторювати</option><option value="5">кожні 5 хв</option><option value="10">кожні 10 хв</option><option value="15">кожні 15 хв</option><option value="30">кожні 30 хв</option><option value="60">щогодини</option></select>
<label>Ніч</label><div class="inl">з <input id="a_nf" type="number" min="0" max="23"> до <input id="a_nt" type="number" min="0" max="23"> години звук <select id="a_nm" style="width:auto"><option value="0">як удень</option><option value="1">лише важливе</option><option value="2">вимкнено</option></select></div>
<div class="mut" id="a_clock" style="margin-top:4px"></div>
<div class="mut" style="margin-top:6px">Кілька батарей: перед сигналом — стільки коротких «тік», який номер батареї. Тиша — коротко натиснути кнопку BOOT на платі чи «Тиша» тут: звук зупиниться, а важливе не повторюватиметься до нової події.</div>
<div class="btns"><button class="s" id="a_mute" type="button">Тиша</button><button class="asv" type="button">Зберегти світло й звук</button></div><div class="amsg" style="margin-top:8px"></div>
<div id="a_log" style="margin-top:8px"></div></div>
<div class="card" id="otacard" style="display:none"><b>Оновлення прошивки мосту</b>
<div class="mut">Адреса файлу прошивки <span id="ovar"></span> (http або https). Міст сам завантажить його через Сторожа й перезапуститься; якщо нова прошивка не запрацює — повернеться попередня. Налаштування лишаються.</div>
<label for="ou">Адреса файлу</label><input id="ou" autocomplete="off" autocapitalize="off" placeholder="https://…-app.bin">
<button id="og" type="button">Оновити</button><div id="omsg" style="margin-top:8px"></div></div>
<div class="card mut">Живіть плату від ДБЖ чи інвертора — не з міської розетки: батарея найважливіша саме без світла. Ставте її ближче до BMS: на слабкому сигналі BMS рве з'єднання, а кожне нове — коротко пищить. Поки плата на зв'язку з BMS, застосунок JK на телефоні до BMS не під'єднається.</div>
</main><script>
const $=i=>document.getElementById(i),W=ms=>new Promise(r=>setTimeout(r,ms));
function esc(t){return String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
const n1=(x,d)=>(x/d).toFixed(d>=1000?2:1).replace('.',','),BST=['шукаю BMS','під\'єднуюсь до BMS','на зв\'язку з BMS','на зв\'язку з BMS, але стану від неї немає','пауза перед новою спробою','чекаю черги','BMS не чути — зайнята телефоном чи далеко','BMS не відповіла — спробую пізніше'];
const mm=x=>x>=3600?Math.floor(x/3600)+' год '+Math.floor(x%3600/60)+' хв':x>=60?Math.floor(x/60)+' хв':x+' с';
let setup=false,trust=false,last=null;
//  Картка батареї (1.2.0: кожна BMS — своя картка)
function packCard(p,i,n,mode,per){const c=p.c;if(!mode&&!c)return '';
 let t='<div class="card"><b>'+(n>1?'Батарея '+(i+1):'Батарея')+'</b>'+(p.lbl?' «'+esc(p.lbl)+'»':'')+(n>1&&p.mac?' <span class="mut">'+esc(p.name||'')+' · '+esc(p.mac)+(p.rssi?' · '+p.rssi+' дБм':'')+'</span>':'');
 const fresh=c&&(mode?c.age<=per+120:p.st==2);
 if(mode){if(!fresh||p.st>=6)t+='<div style="margin-top:6px"><span class="warn">● '+BST[p.st||0]+'</span>'+(c?' · дані '+mm(c.age)+' тому':'')+'</div>';
  t+='<div class="mut">'+(p.polls?'Опитувань '+p.polls+(p.fails?', невдалих '+p.fails:''):'Ще не опитано')+(p.next>=0?' · наступне за '+mm(p.next):'')+'</div>'}
 if(c){const pw=c.v*c.i/1e6,ch=c.i>50?'заряджається':c.i<-50?'розряджається':'спокій';
  t+='<div class="big">'+c.soc+' %</div><div>'+n1(c.v,1000)+' В · '+(c.i/1000).toFixed(1).replace('.',',')+' А · '+Math.round(pw)+' Вт — '+ch+'</div>';
  t+='<div class="mut">Залишок '+n1(c.rem,1000)+' з '+n1(c.full,1000)+' А·год · циклів '+c.cyc+' · стан '+c.soh+' %'+(c.info?' · '+esc(c.info):'')+'</div>';
  t+='<div style="margin-top:6px">Комірки: '+c.n+', від '+n1(c.lo,1000)+' В (№'+c.lox+') до '+n1(c.hi,1000)+' В (№'+c.hix+'), різниця '+(c.hi-c.lo)+' мВ</div>';
  t+='<div class="cells">'+c.cells.map((v,k)=>'<span class="'+(k+1==c.lox?'lo':k+1==c.hix?'hi':'')+'">'+(k+1)+': '+(v/1000).toFixed(3).replace('.',',')+'</span>').join('')+'</div>';
  const T=[];if(c.t1!=null)T.push('датчик 1 — '+n1(c.t1,10)+' °C');if(c.t2!=null)T.push('датчик 2 — '+n1(c.t2,10)+' °C');if(c.tm!=null)T.push('ключі — '+n1(c.tm,10)+' °C');
  t+='<div style="margin-top:6px">'+(T.length?'Температура: '+T.join(', '):'Температури BMS не передає')+'</div>';
  t+='<div>Заряд '+(c.chg?'<span class="ok">дозволено</span>':'<span class="bad">вимкнено BMS</span>')+' · розряд '+(c.dis?'<span class="ok">дозволено</span>':'<span class="bad">вимкнено BMS</span>')+(c.bal?' · вирівнює комірки':'')+(c.heat?' · підігрів':'')+'</div>';
  t+='<div>'+(c.errs.length?'<span class="bad">Тривоги BMS: '+c.errs.map(esc).join(', ')+'</span>':'<span class="ok">Тривог BMS немає</span>')+(c.notes.length?' <span class="mut">('+c.notes.map(esc).join(', ')+')</span>':'')+'</div>';
  t+='<div class="mut">Кадр '+mm(c.age)+' тому, розкладка '+c.fmt+'S</div>'}
 return t+'</div>'}
async function st(){try{const s=await (await fetch('state',{cache:'no-store'})).json();setup=s.setup;trust=s.trust;last=s;
 $('who').textContent='Версія '+s.ver+' · MAC '+s.mac+(s.up!=null?' · працює '+(s.up>=3600?Math.floor(s.up/3600)+' год '+Math.floor(s.up%3600/60)+' хв':Math.floor(s.up/60)+' хв'):'');
 const tr=s.setup||s.trust;$('form').style.display=s.setup?'':'none';['pick','otacard','lightcard','sndcard','rb'].forEach(k=>$(k).style.display=tr?'':'none');
 $('ovar').textContent='avr-bms-…-'+(s.var||'usb')+'-app.bin';if(s.ota)$('omsg').textContent='Оновлення: '+s.ota;
 let h=s.conn?'<span class="ok">● Сторож на зв\'язку</span>: мережа «'+esc(s.ssid)+'», сигнал '+s.rssi+' дБм'
  :s.ssid?'<span class="bad">● Немає зв\'язку зі Сторожем</span> (мережа «'+esc(s.ssid)+'»)'+(s.err?': '+esc(s.err):''):'<span class="bad">● Мережу Сторожа ще не задано</span>';
 const b=s.b||{},P=s.packs||[];
 if(s.mode){const ok=P.filter(p=>p.st==2).length;h+='<div style="margin-top:6px">'+(ok==P.length?'<span class="ok">● ':'<span class="warn">● ')+'Кілька BMS по черзі: '+P.length+', кожна '+(s.per>=120?'раз на '+s.per/60+' хв':'щохвилини')+'</span>'+(b.st!=5?' · зараз: '+BST[b.st||0]+(b.name?' '+esc(b.name):''):'')+'</div>';
  h+='<div class="mut">Свіжі дані — у '+ok+' з '+P.length+' · з\'єднань від запуску: '+b.rc+(b.note?' · '+esc(b.note):'')+'</div>'}
 else{h+='<div style="margin-top:6px">'+(b.st==2?'<span class="ok">● ':'<span class="warn">● ')+BST[b.st||0]+'</span>'+(b.name?': '+esc(b.name)+' ('+esc(b.mac)+', '+b.rssi+' дБм)':'')+'</div>';
  h+='<div class="mut">'+(s.pin?'Слухаю вибрану BMS '+esc(s.pin):'Слухаю найближчу JK BMS')+' · з\'єднань від запуску: '+b.rc+(b.note?' · '+esc(b.note):'')+'</div>'}
 if(!s.setup)h+='<div class="mut">Змінити мережу Сторожа: утримати кнопку BOOT на платі 3 секунди — з\'явиться мережа «'+esc(s.ap)+'».</div>';
 $('st').innerHTML=h;
 $('bats').innerHTML=P.map((p,i)=>packCard(p,i,P.length,s.mode,s.per)).join('');
 if(tr){const L=s.found||[],sel=$('bl').value;$('bl').innerHTML=L.length?L.map(f=>'<option value="'+esc(f.mac)+'">'+esc(f.name)+' · '+esc(f.mac)+' · '+f.rssi+' дБм'+(f.jk?'':' · не схоже на JK')+'</option>').join(''):'<option value="">JK BMS поруч не чути</option>';if(sel)$('bl').value=sel;
  $('fage').textContent=s.fage>=0?'· пошук '+mm(s.fage)+' тому':'· ще не шукав';pkInit(s)}
 return s}catch(e){return null}}
async function pinTo(mac){const m=$('pmsg');try{const r=await fetch('bms',{method:'POST',body:new URLSearchParams({mac})});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">'+(mac?'Слухаю '+esc(mac):'Слухаю найближчу')+' — під\'єднаюсь за кілька секунд</span>'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}st()}
$('pin').onclick=()=>{const v=$('bl').value;if(v)pinTo(v)};$('auto').onclick=()=>pinTo('');
//  1.2.3: перезапуск мосту
$('rb').onclick=async()=>{const m=$('rbmsg');if(!confirm('Перезапустити міст батареї? Зв\'язок з BMS перерветься на хвилину, BMS коротко пискне.'))return;
 const up0=last&&last.up;$('rb').disabled=true;
 try{const r=await fetch('reboot',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.textContent='Перезапускаю…';
  for(let i=0;i<30;i++){await W(2000);const t=await st();if(t&&(up0==null||t.up<up0)){m.innerHTML='<span class="ok">Міст перезапустився.</span>';$('rb').disabled=false;return}}
  m.textContent='Міст ще не озвався — оновіть сторінку за хвилину'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}$('rb').disabled=false};
//  Режим і перелік BMS (1.2.0)
let pk=null,pkM=0;
function pkInit(s){if(pk)return;pk=(s.list||[]).map(x=>({mac:x.mac,lbl:x.lbl}));pkM=s.mode;$('pm'+pkM).checked=true;$('pper').value=String(s.per);pkDraw()}
function pkDraw(){const m=pkM==1;$('one').style.display=m?'none':'';$('many').style.display=m?'':'none';
 $('pin').style.display=$('auto').style.display=m?'none':'';$('pkadd').style.display=$('pnow').style.display=m?'':'none';
 $('pmh').textContent=m?'Раз на період міст під\'єднується до кожної BMS, бере її стан і від\'єднується: кожна BMS коротко пищить раз на період, а не на кожне перепід\'єднання. Дані — не новіші за період; Сторожу потрібна версія 1.0.48 чи новіша.':'Міст тримає з\'єднання з однією BMS постійно: дані щосекунди, BMS пищить лише на нове з\'єднання.';
 $('pkl').innerHTML=pk.length?pk.map((p,i)=>'<div class="pkr"><input data-i="'+i+'" maxlength="23" placeholder="назва, напр. Інвертор" value="'+esc(p.lbl)+'"><span class="mut">'+(i+1)+' · '+esc(p.mac)+'</span><button class="s" data-del="'+i+'" type="button">✕</button></div>').join('')
  :'<div class="mut" style="margin-top:6px">Перелік порожній: виберіть BMS у «Чути поруч» і «Додати до переліку».</div>'}
document.querySelectorAll('input[name=pm]').forEach(r=>r.onchange=()=>{pkM=+r.value;pkDraw()});
$('pkl').addEventListener('input',e=>{const i=e.target.dataset.i;if(i!=null&&pk)pk[+i].lbl=e.target.value});
$('pkl').addEventListener('click',e=>{const b=e.target.closest('button[data-del]');if(!b||!pk)return;pk.splice(+b.dataset.del,1);pkDraw();$('pmsg').textContent='Прибрано — «Зберегти режим і перелік»'});
$('pkadd').onclick=()=>{const v=$('bl').value,m=$('pmsg');if(!pk)return;if(!v){m.innerHTML='<span class="bad">Виберіть BMS у «Чути поруч» (чи «Шукати BMS»)</span>';return}
 if(pk.some(p=>p.mac===v)){m.innerHTML='<span class="bad">Ця BMS уже в переліку</span>';return}if(pk.length>=4){m.innerHTML='<span class="bad">Не більше 4 BMS</span>';return}
 pk.push({mac:v,lbl:''});pkDraw();m.textContent='Додано — «Зберегти режим і перелік»'};
$('pksave').onclick=async()=>{const m=$('pmsg');if(!pk)return;const p=new URLSearchParams({mode:String(pkM),per:$('pper').value});
 for(const x of pk)if(new TextEncoder().encode(x.lbl.trim()).length>23){m.innerHTML='<span class="bad">Назва «'+esc(x.lbl)+'» задовга: до 23 байтів (≈11 літер кирилицею)</span>';return}
 if(pkM==1&&!pk.length){m.innerHTML='<span class="bad">Додайте до переліку хоч одну BMS</span>';return}
 pk.forEach((x,i)=>{p.append('m'+i,x.mac);p.append('l'+i,x.lbl.trim())});
 $('pksave').disabled=true;try{const r=await fetch('packs',{method:'POST',body:p});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Збережено.'+(pkM==1?' Опитаю за кілька секунд.':'')+'</span>';pk=null}
 catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}$('pksave').disabled=false;st()};
$('pnow').onclick=async()=>{const m=$('pmsg');try{const r=await fetch('pollnow',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Опитую всі BMS — дані оновляться за хвилину</span>'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
$('bscan').onclick=async()=>{const m=$('pmsg');try{const r=await fetch('blescan',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.textContent='Шукаю BMS поруч (5 с)…';setTimeout(()=>{st();m.textContent=''},8000)}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
//  Світло й звук (1.2.0)
const AP=['a_g','a_r','a_sp','a_bz'];let alFilled=false;
function alOpts(pins,lcd){return '<option value="-1">немає</option>'+pins.map(p=>'<option value="'+p+'"'+(lcd.includes(p)?' disabled':'')+'>GPIO'+p+(p==2||p==8?' (запуск)':'')+(lcd.includes(p)?' — дисплей':'')+'</option>').join('')}
function alShow(){const l=+$('a_light').value,s=+$('a_snd').value;$('a_l1').style.display=l==1?'':'none';$('a_l2').style.display=l==2?'':'none';$('a_s1').style.display=s?'':'none'}
function alFill(d){for(const k of AP)$(k).innerHTML=alOpts(d.pins,d.lcdPins);
 $('a_light').value=d.light;$('a_g').value=d.g;$('a_r').value=d.r;$('a_low').checked=d.low;$('a_sp').value=d.sp;$('a_sn').value=d.sn;$('a_so').value=d.so;$('a_bri').value=d.bri;$('a_nbri').value=d.nbri;
 $('a_snd').value=d.snd;$('a_bz').value=d.bz;$('a_bzLow').checked=d.bzLow;$('a_vol').value=d.vol;$('a_rep').value=d.rep;$('a_nf').value=d.nf;$('a_nt').value=d.nt;$('a_nm').value=d.nm;
 $('a_evs').innerHTML=d.names.map((n,e)=>e?'<div class="evr"><label><input type="checkbox" data-e="'+e+'"'+((d.mask>>e)&1?' checked':'')+'>'+esc(n)+(d.crit[e]?' <span class="mut">(важливе)</span>':'')+'</label><button class="s" data-play="'+e+'" type="button">▶</button></div>':'').join('');
 alShow();alFilled=true}
async function alPoll(){try{const d=await (await fetch('alert',{cache:'no-store'})).json();if(!alFilled)alFill(d);const many=last&&last.packs&&last.packs.length>1;
 $('a_clock').textContent=d.clock?'Годинник плати є'+(d.night?' — зараз ніч.':'.'):'Годинника немає (час — з інтернету через Сторожа): ночі немає, звук і світло — як удень.';
 const L=d.log||[];$('a_log').innerHTML=(d.crits.length?'<div class="bad">Повторюється: '+d.crits.map(x=>(many?'батарея '+x.pk+' — ':'')+esc(d.names[x.e])).join(', ')+(d.muted?' (зараз тиша)':'')+'</div>':'')+
  (L.length?'<div><b>Останні події</b></div>'+L.map(x=>'<div class="mut">'+(x.t||mm(x.ago)+' тому')+' · '+(many?'батарея '+x.pk+': ':'')+esc(d.names[x.e])+'</div>').join(''):'<div class="mut">Подій від запуску плати не було.</div>')}catch(e){}}
$('a_light').onchange=alShow;$('a_snd').onchange=alShow;
async function alSave(btn){const say=h=>document.querySelectorAll('.amsg').forEach(m=>m.innerHTML=h);let mask=0;document.querySelectorAll('#a_evs input[data-e]').forEach(c=>{if(c.checked)mask|=1<<+c.dataset.e});
 const p=new URLSearchParams({light:$('a_light').value,g:$('a_g').value,r:$('a_r').value,low:$('a_low').checked?'1':'0',sp:$('a_sp').value,sn:$('a_sn').value,so:$('a_so').value,bri:$('a_bri').value,nbri:$('a_nbri').value,
  snd:$('a_snd').value,bz:$('a_bz').value,bzLow:$('a_bzLow').checked?'1':'0',vol:$('a_vol').value,mask:String(mask),rep:$('a_rep').value,nf:$('a_nf').value,nt:$('a_nt').value,nm:$('a_nm').value});
 btn.disabled=true;try{const r=await fetch('alert',{method:'POST',body:p});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');say('<span class="ok">Збережено.</span>')}catch(e){say('<span class="bad">'+esc(e.message)+'</span>')}btn.disabled=false;alPoll()}
document.querySelectorAll('.asv').forEach(b=>b.onclick=()=>alSave(b));
async function alTest(o,msg){const say=h=>document.querySelectorAll('.amsg').forEach(m=>m.innerHTML=h);try{const r=await fetch('alerttest',{method:'POST',body:new URLSearchParams(o)});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');if(msg)say('<span class="ok">'+msg+'</span>')}catch(e){say('<span class="bad">'+esc(e.message)+'</span>')}alPoll()}
$('a_ltest').onclick=()=>alTest({what:'light'},'Перевірка світла 6 с: стрічка — червоний, зелений, синій, білий; два світлодіоди — зелений, червоний, обидва.');
$('a_mute').onclick=()=>alTest({what:'mute'},'Тиша.');
$('a_evs').addEventListener('click',e=>{const b=e.target.closest('button[data-play]');if(b)alTest({what:'sound',ev:b.dataset.play},'')});
$('og').onclick=async()=>{const m=$('omsg'),u=$('ou').value.trim();if(!u){m.innerHTML='<span class="bad">Вставте адресу файлу</span>';return}
 if(!confirm('Оновити прошивку мосту? Зв\'язок з BMS на хвилину перерветься.'))return;
 try{const r=await fetch('ota',{method:'POST',body:new URLSearchParams({url:u})});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Завантажую… міст перезапуститься сам, сторінка оновиться за хвилину-дві.</span>'}
 catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
async function scan(fresh){$('rescan').disabled=true;for(let i=0;i<25;i++){let r=null;try{r=await (await fetch('scan'+(fresh&&!i?'?new=1':''),{cache:'no-store'})).json()}catch(e){}
 if(r&&!r.busy){const L=r.list||[];$('ss').innerHTML=L.length?L.map(n=>'<option value="'+esc(n.s)+'">'+esc(n.s)+(n.st?' — Сторож':'')+' · '+n.r+' дБм</option>').join(''):'<option value="">мереж не видно — «Шукати ще раз»</option>';break}
 await W(800)}$('rescan').disabled=false}
$('rescan').onclick=()=>scan(true);$('sh').onchange=()=>{$('pw').type=$('sh').checked?'text':'password'};
$('go').onclick=async()=>{const s=$('mn').value.trim()||$('ss').value,p=$('pw').value,m=$('msg');
 if(!s){m.innerHTML='<span class="bad">Виберіть мережу Сторожа</span>';return}if(p.length<8){m.innerHTML='<span class="bad">Пароль — щонайменше 8 символів</span>';return}
 $('go').disabled=true;m.textContent='Під\'єднуюсь до «'+s+'»…';
 try{const r=await fetch('save',{method:'POST',body:new URLSearchParams({ssid:s,pass:p})});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');$('pw').value='';
  for(let i=0;i<40;i++){await W(1000);const t=await st();if(!t)continue;
   if(t.conn){m.innerHTML='<span class="ok">Готово: плата на зв\'язку зі Сторожем.</span>';$('go').disabled=false;return}
   if(t.err&&i>5){m.innerHTML='<span class="bad">'+esc(t.err)+'</span>';$('go').disabled=false;return}}
  m.textContent='Ще під\'єднуюсь — оновіть сторінку за хвилину'}
 catch(e){m.textContent=e.message&&e.message!=='Failed to fetch'?e.message:'Зв\'язок обірвався — під\'єднайте телефон до мережі плати знову й оновіть сторінку'}
 $('go').disabled=false};
//  Дисплей (1.1.0)
const LP=['clk','din','cs','rs','rst','bl'],LN={clk:'CLK',din:'DI',cs:'CS',rs:'RS',rst:'RST',bl:'LED'};let lcdFilled=false;
function lcdDraw(hex){const c=$('lcdc'),x=c.getContext('2d'),im=x.createImageData(128,64);
 for(let p=0;p<8;p++)for(let col=0;col<128;col++){const b=parseInt(hex.substr((p*128+col)*2,2),16);for(let r=0;r<8;r++){const on=b>>r&1,i=((p*8+r)*128+col)*4;im.data[i]=on?27:223;im.data[i+1]=on?31:230;im.data[i+2]=on?36:234;im.data[i+3]=255}}
 x.putImageData(im,0,0)}
function lcdWire(){$('lcdwire').textContent=LP.map(k=>LN[k]+' → '+($('l_'+k).value<0?'не під\'єднано':'GPIO'+$('l_'+k).value)).join(', ')}
function lcdFill(d){for(const k of LP){const opt=(k==='rst'||k==='bl'?'<option value="-1">немає</option>':'')+d.pins.map(p=>'<option value="'+p+'">GPIO'+p+(p==2||p==8?' (запуск)':'')+'</option>').join('');$('l_'+k).innerHTML=opt;$('l_'+k).value=d[k]}
 $('l_on').checked=d.on;$('l_con').value=d.con;$('l_conV').textContent=d.con;$('l_rat').value=d.rat;$('l_xoff').value=d.xoff;$('l_b7').value=d.b7?'1':'0';$('l_bri').value=d.bri;$('l_briV').textContent=d.bri;
 $('l_blLow').checked=d.blLow;$('l_inv').checked=d.inv;$('l_flip').checked=d.flip;$('l_nf').value=d.nf;$('l_nt').value=d.nt;$('l_nb').value=d.nb;lcdWire();lcdFilled=true}
async function lcdPoll(){try{const d=await (await fetch('lcd',{cache:'no-store'})).json();lcdDraw(d.fb);if(!lcdFilled)lcdFill(d);
 $('lcdst').textContent=d.on?(d.test?'Іде перевірка.':''):'Дисплей вимкнено в налаштуваннях.';
 $('lcdclock').textContent=d.clock?'Годинник плати: '+d.clock+(d.night?' — зараз ніч':''):'Годинника немає (час — з інтернету через Сторожа): уночі підсвітка як удень.'}catch(e){}}
async function lcdTest(o){try{await fetch('lcdtest',{method:'POST',body:new URLSearchParams(o)})}catch(e){}lcdPoll()}
for(const k of LP)$('l_'+k).onchange=lcdWire;
$('l_con').oninput=()=>{$('l_conV').textContent=$('l_con').value;lcdTest({con:$('l_con').value})};
$('l_bri').oninput=()=>{$('l_briV').textContent=$('l_bri').value;lcdTest({bri:$('l_bri').value})};
document.querySelectorAll('button[data-t]').forEach(b=>b.onclick=()=>lcdTest({mode:b.dataset.t}));
$('l_save').onclick=async()=>{const m=$('lmsg'),p=new URLSearchParams();for(const k of LP)p.append(k,$('l_'+k).value);
 for(const k of ['con','rat','xoff','b7','bri','nf','nt','nb'])p.append(k,$('l_'+k).value);for(const k of ['on','blLow','inv','flip'])p.append(k,$('l_'+k).checked?'1':'0');
 $('l_save').disabled=true;try{const r=await fetch('lcd',{method:'POST',body:p});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Збережено.</span>';lcdFilled=false}
 catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}$('l_save').disabled=false;lcdPoll()};
//  поки вкладка схована — нічого не питаємо (сторінка може бути відкрита через Сторожа: кожен запит — його робота)
lcdPoll();setInterval(()=>{if(!document.hidden)lcdPoll()},2000);
setInterval(()=>{if(!document.hidden&&(setup||trust))alPoll()},10000);
st().then(s=>{if(s&&s.setup)scan(false);if(s&&(s.setup||s.trust))alPoll()});setInterval(()=>{if(!document.hidden)st()},3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){st();lcdPoll();if(setup||trust)alPoll()}});
</script></body></html>)PAGE";
