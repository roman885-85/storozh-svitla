/*  Сторінка мосту батареї (http://192.168.4.1 і адреса плати в мережі Сторожа). */
#pragma once
static const char PAGE[] = R"PAGE(<!doctype html><html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Міст батареї</title><style>
:root{--bg:#14161c;--card:#1d2028;--fg:#e8e8ec;--mut:#9aa0ab;--acc:#2f7de1;--bd:#2c303b;--ok:#2fb36b;--bad:#e05252;--warn:#d9a21b;
--mod:#232937;--bd2:#59627a;--bg2:#1d2028;--gnd:#a9b1c3;--v5:#e05252;--v33:#e8902a;--s1:#4b8df0;--s2:#2fb36b;--s3:#b07ae6;--s4:#d9a21b;--s5:#1fb5c2;--s6:#e06aa3}
@media (prefers-color-scheme:light){:root{--bg:#f3f4f7;--card:#fff;--fg:#16181d;--mut:#5d6470;--bd:#d9dce3;
--mod:#eef2f8;--bd2:#9aa3b5;--bg2:#fff;--gnd:#4a5263;--v5:#d03a3a;--v33:#e07b00;--s1:#2f6fde;--s2:#1f9d57;--s3:#9b4fd9;--s4:#c98a00;--s5:#0e9aa7;--s6:#d24b8a}}
@media print{:root{--bg:#fff;--card:#fff;--fg:#16181d;--mut:#5d6470;--bd:#ccc;--mod:#eef2f8;--bd2:#9aa3b5;--bg2:#fff;--gnd:#4a5263;--v5:#d03a3a;--v33:#e07b00;--s1:#2f6fde;--s2:#1f9d57;--s3:#9b4fd9;--s4:#c98a00;--s5:#0e9aa7;--s6:#d24b8a}.card{break-inside:avoid}}
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
.sch svg{display:block;margin:8px auto 0;width:100%;max-width:400px;height:auto}.sch ul{margin:6px 0 0;padding-left:18px;font-size:13px}.sch li{margin:3px 0}
.sch h3,.sub{font-size:14px;margin:14px 0 0;font-weight:650}.sch a{color:var(--acc)}.pt{width:100%;border-collapse:collapse;font-size:13px;margin-top:6px}.pt td{border-top:1px solid var(--bd);padding:5px 6px;vertical-align:top}.pt td:first-child{white-space:nowrap;font-weight:600}
#schall{display:none}body.schm main>*{display:none}body.schm main>h1,body.schm #schall{display:block}
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
<div class="sch" id="lcdsch"></div>
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
<div class="mut" id="a_lfree" style="margin-top:8px"></div>
<div id="a_l1"><div class="sub">Два світлодіоди — куди під'єднати</div><div class="g3"><div><label>Зелений — вивід</label><select id="a_g"></select></div><div><label>Червоний — вивід</label><select id="a_r"></select></div></div>
<label class="row"><input type="checkbox" id="a_low"> Світлодіоди вмикаються низьким рівнем (спільний «+»)</label></div>
<div id="a_l2"><div class="sub">Стрічка WS2812B — куди під'єднати</div><div class="g3"><div><label>DIN — вивід</label><select id="a_sp"></select></div><div><label>Світлодіодів</label><input id="a_sn" type="number" min="1" max="60"></div><div><label>Кольори</label><select id="a_so"><option value="0">GRB — звичайна</option><option value="1">RGB</option><option value="2">BGR</option></select></div></div>
</div>
<div class="g3"><div><label>Яскравість удень, %</label><input id="a_bri" type="number" min="1" max="100"></div><div><label>Уночі, % (0 — вимкнено)</label><input id="a_nbri" type="number" min="0" max="100"></div></div>
<div class="sch" id="a_lsch"></div>
<div class="mut" style="margin-top:6px">Два світлодіоди: зелений — заряд 50 % і більше; обидва — 20–50 %; червоний — до 20 %, блимає — до 10 %, часто — до 5 %; заряд іде — зелений повільно блимає; тривога BMS — зелений і червоний по черзі; даних немає — рідкий спалах червоного. Стрічка: смуга заряду від першого світлодіода (зелена; помаранчева нижче 50 %, червона до 20 %, часто блимає до 5 %); заряд іде — угору біжить білий вогник; тривога BMS — уся блимає червоним; даних немає — перший світлодіод блимає синім. Кілька батарей — стрічка ділиться на відрізки (по 2 світлодіоди й більше на кожну), а два світлодіоди й коротка стрічка показують найгіршу.</div>
<div class="btns"><button class="s" id="a_ltest" type="button">Перевірити світло (6 с)</button><button class="asv" type="button">Зберегти світло й звук</button></div><div class="amsg" style="margin-top:8px"></div></div>
<div class="card" id="sndcard" style="display:none"><b>Звук</b> <span class="mut">бузер: зарядилась, 50 / 30 / 20 / 10 / 5 %, повний розряд, тривога BMS…</span>
<label for="a_snd">Що під'єднано</label><select id="a_snd"><option value="0">нічого</option><option value="1">активний бузер — пищить сам від живлення</option><option value="2">пасивний бузер — грає мелодії</option></select>
<div class="mut" id="a_sfree" style="margin-top:8px"></div>
<div id="a_s1"><div class="sub">Бузер — куди під'єднати</div><div class="g3"><div><label>Бузер — вивід</label><select id="a_bz"></select></div><div><label>Гучність, %</label><input id="a_vol" type="number" min="5" max="100"></div></div>
<label class="row"><input type="checkbox" id="a_bzLow"> Бузер вмикається низьким рівнем (модулі «low level trigger»)</label></div>
<div class="sch" id="a_ssch"></div>
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
<div id="schall"></div>
</main><script>
const $=i=>document.getElementById(i),W=ms=>new Promise(r=>setTimeout(r,ms));
function esc(t){return String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
const n1=(x,d)=>(x/d).toFixed(d>=1000?2:1).replace('.',','),BST=['шукаю BMS','під\'єднуюсь до BMS','на зв\'язку з BMS','на зв\'язку з BMS, але стану від неї немає','пауза перед новою спробою','чекаю черги','BMS не чути — зайнята телефоном чи далеко','BMS не відповіла — спробую пізніше'];
//  1.2.6: сигнал BMS і розриви з'єднання (причина — від стека Bluetooth: 0x200 + код HCI)
const DR={0x208:'зник сигнал',0x213:'BMS сама розірвала',0x214:'BMS розірвала (бракує ресурсів)',0x215:'BMS вимкнулась',0x216:'міст розірвав сам',0x222:'BMS не відповіла на службовий запит',0x228:'збій зміни параметрів',0x23b:'BMS не прийняла параметри',0x23d:'помилка шифрування',0x23e:'з\'єднання не встановилось'};
const rz=n=>n%10==1&&n%100!=11?'раз':n%10>=2&&n%10<=4&&(n%100<10||n%100>=20)?'рази':'разів';
function sigTxt(r){return r>=-70?'<span class="ok">добрий</span>':r>=-80?'<span class="warn">слабкий</span>':'<span class="bad">на межі — зв\'язок рватиметься</span>'}
function disHtml(b,up){const L=b.dis||[];if(!L.length)return '';
 return '<div style="margin-top:6px"><b>Розриви з BMS від запуску: '+b.dn+'</b></div>'+L.slice(0,5).map(x=>{const at=x[0],dur=x[1],r=x[2],rs=x[3],q=x[4];
  return '<div class="mut">'+(up!=null?mm(Math.max(0,up-at))+' тому':'')+' · '+(dur?mm(dur)+' на зв\'язку':'одразу після під\'єднання')+' · '+(DR[r]||'код 0x'+r.toString(16))+(r==0x208&&b.cp&&b.cp[2]?' ('+b.cp[2]/100+' с без відповіді)':'')+(rs?' · '+rs+' дБм':'')+(q>=20?' · перед тим '+mm(q)+' без кадрів':'')+'</div>'}).join('')}
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
 if(b.rssi)h+='<div style="margin-top:6px">Сигнал BMS: '+b.rssi+' дБм — '+sigTxt(b.rssi)+'</div>'+(b.rssi<-80?'<div class="mut">Добрий зв\'язок — від −70 дБм; слабший за −80 — BMS губиться, а міст може її й не почути. Поставте міст ближче до BMS, антену плати не закривайте металом, дисплеєм чи проводами.</div>':'');
 h+=disHtml(b,s.up);
 if(b.cp&&b.cp[0])h+='<div class="mut">З\'єднання: інтервал '+String(b.cp[0]*1.25).replace('.',',')+' мс, нагляд '+b.cp[2]/100+' с'+(b.pr&&b.pr[0]?' · BMS просила інші параметри '+b.pr[0]+' '+rz(b.pr[0])+' (нагляд '+b.pr[2]/100+' с)':'')+(b.sf?' · завислий стан Bluetooth скинуто '+b.sf+' '+rz(b.sf):'')+(b.gf?' · розірвано з\'єднань-«привидів» '+b.gf:'')+'</div>';
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
 if(pk.some(p=>p.mac===v)){m.innerHTML='<span class="bad">Ця BMS вже в переліку</span>';return}if(pk.length>=4){m.innerHTML='<span class="bad">Не більше 4 BMS</span>';return}
 pk.push({mac:v,lbl:''});pkDraw();m.textContent='Додано — «Зберегти режим і перелік»'};
$('pksave').onclick=async()=>{const m=$('pmsg');if(!pk)return;const p=new URLSearchParams({mode:String(pkM),per:$('pper').value});
 for(const x of pk)if(new TextEncoder().encode(x.lbl.trim()).length>23){m.innerHTML='<span class="bad">Назва «'+esc(x.lbl)+'» задовга: до 23 байтів (≈11 літер кирилицею)</span>';return}
 if(pkM==1&&!pk.length){m.innerHTML='<span class="bad">Додайте до переліку хоч одну BMS</span>';return}
 pk.forEach((x,i)=>{p.append('m'+i,x.mac);p.append('l'+i,x.lbl.trim())});
 $('pksave').disabled=true;try{const r=await fetch('packs',{method:'POST',body:p});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Збережено.'+(pkM==1?' Опитаю за кілька секунд.':'')+'</span>';pk=null}
 catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}$('pksave').disabled=false;st()};
$('pnow').onclick=async()=>{const m=$('pmsg');try{const r=await fetch('pollnow',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">Опитую всі BMS — дані оновляться за хвилину</span>'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
$('bscan').onclick=async()=>{const m=$('pmsg');try{const r=await fetch('blescan',{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.textContent='Шукаю BMS поруч (5 с)…';setTimeout(()=>{st();m.textContent=''},8000)}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}};
//  Схеми підключення (1.2.4). Власник 24.11.2026: «в мосте бмс не отображена инструкция и схема для подключения бузера,
//  светодиодов и ленты». Малюнки — з номерами виводів, як їх вибрано на сторінці зараз (ще до «Зберегти»); /schemes —
//  усі схеми однією сторінкою для друку (з файлу — приклад виводів; tools/mk_schemes.sh робить docs/СХЕМИ-МОСТУ.pdf).
const SC=(()=>{
const sv=(w,h,b)=>'<svg viewBox="0 0 '+w+' '+h+'" xmlns="http://www.w3.org/2000/svg" font-family="system-ui,-apple-system,Segoe UI,Roboto,sans-serif">'+b+'</svg>',
 tx=(x,y,s,o)=>{o=o||{};return '<text x="'+x+'" y="'+y+'" fill="'+(o.c||'var(--fg)')+'" text-anchor="'+(o.a||'middle')+'" font-size="'+(o.s||12)+'"'+(o.b?' font-weight="650"':'')+'>'+esc(s)+'</text>'},
 ln=(p,c)=>'<polyline points="'+p.map(q=>q.join(',')).join(' ')+'" fill="none" stroke="'+(c||'var(--fg)')+'" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round"/>',
 dt=(x,y,c)=>'<circle cx="'+x+'" cy="'+y+'" r="3.6" fill="'+(c||'var(--fg)')+'"/>',
 rc=(x,y,w,h)=>'<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'" rx="9" fill="var(--mod)" stroke="var(--bd2)" stroke-width="1.5"/>',
 bx=(x,y,w,h,ti,sub)=>rc(x,y,w,h)+tx(x+w/2,y+22,ti,{b:1,s:13})+(sub?tx(x+w/2,y+37,sub,{c:'var(--mut)',s:11}):''),
 sq=(x,y,c)=>'<rect x="'+(x-4.5)+'" y="'+(y-4.5)+'" width="9" height="9" rx="2" fill="'+(c||'var(--bd2)')+'"/>',
 pn=(x,y,l,c,up)=>sq(x,y,c)+tx(x,up?y-11:y+20,l),                        // вивід на нижньому (up — підпис над ним) чи верхньому краї
 ps=(x,y,l,c,r)=>sq(x,y,c)+tx(r?x-11:x+11,y+4,l,{a:r?'end':'start'}),     // вивід на правому (r) чи лівому краї
 rv=(x,y1,y2,l,c)=>{const m=(y1+y2)/2;return ln([[x,y1],[x,m-17]],c)+ln([[x,m+17],[x,y2]],c)+'<rect x="'+(x-6.5)+'" y="'+(m-17)+'" width="13" height="34" rx="2" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'+tx(x+13,m+4,l,{a:'start'})},
 rh=(x1,x2,y,l,c)=>{const m=(x1+x2)/2;return ln([[x1,y],[m-17,y]],c)+ln([[m+17,y],[x2,y]],c)+'<rect x="'+(m-17)+'" y="'+(y-6.5)+'" width="34" height="13" rx="2" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'+tx(m,y-12,l)},
 //  світлодіод: анод угорі (up — унизу)
 led=(x,y,col,up)=>{const a=up?8:-8;return ln([[x,y-20],[x,y+20]])+'<polygon points="'+(x-10)+','+(y+a)+' '+(x+10)+','+(y+a)+' '+x+','+(y-a)+'" fill="'+col+'" stroke="var(--fg)" stroke-width="1.5"/><path d="M'+(x-10)+' '+(y-a)+'h20" stroke="var(--fg)" stroke-width="2.4"/>'+
  '<path d="M'+(x+13)+' '+(y-2)+'l8 -8m-4.5 0h4.5v4.5M'+(x+15)+' '+(y+6)+'l8 -8m-4.5 0h4.5v4.5" stroke="'+col+'" stroke-width="1.8" fill="none"/>'},
 //  конденсатор між двома вертикальними дротами, «+» праворуч
 cap=(x1,x2,y,l)=>{const m=(x1+x2)/2;return ln([[x1,y],[m-5,y]])+ln([[m+5,y],[x2,y]])+'<path d="M'+(m-5)+' '+(y-12)+'v24" stroke="var(--fg)" stroke-width="2.6" stroke-dasharray="3 2"/><path d="M'+(m+5)+' '+(y-12)+'v24" stroke="var(--fg)" stroke-width="2.6"/>'+tx(m+14,y-6,'+',{b:1,s:13})+tx(m,y-18,l)+dt(x1,y)+dt(x2,y)},
 //  транзистор NPN: база ліворуч (x−24), колектор угорі (x+9, y−26), емітер унизу (x+9, y+26)
 npn=(x,y)=>'<circle cx="'+(x+4)+'" cy="'+y+'" r="18" fill="var(--bg2)" stroke="var(--fg)" stroke-width="1.5"/><path d="M'+(x-4)+' '+(y-11)+'v22" stroke="var(--fg)" stroke-width="3"/>'+ln([[x-24,y],[x-4,y]])+
  ln([[x-4,y-5],[x+9,y-14],[x+9,y-26]])+ln([[x-4,y+5],[x+9,y+14],[x+9,y+26]])+'<polygon points="'+(x+9)+','+(y+14)+' '+(x+1)+','+(y+14)+' '+(x+6)+','+(y+7)+'" fill="var(--fg)"/>',
 dio=(x,y1,y2)=>{const m=(y1+y2)/2;return ln([[x,y1],[x,y2]])+'<polygon points="'+(x-9)+','+(m+7)+' '+(x+9)+','+(m+7)+' '+x+','+(m-7)+'" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/><path d="M'+(x-9)+' '+(m-7)+'h18" stroke="var(--fg)" stroke-width="2.6"/>'},
 buz=(x,y)=>'<rect x="'+(x-34)+'" y="'+(y-20)+'" width="68" height="40" rx="6" fill="var(--bg2)" stroke="var(--fg)" stroke-width="2"/>'+tx(x+6,y+4,'бузер')+tx(x-24,y-5,'+',{b:1,s:13})+tx(x-24,y+15,'−',{b:1,s:13}),
 gn=(x,y)=>ln([[x,y],[x,y+9]],'var(--gnd)')+'<path d="M'+(x-11)+' '+(y+9)+'h22M'+(x-7)+' '+(y+13.5)+'h14M'+(x-3)+' '+(y+18)+'h6" stroke="var(--gnd)" stroke-width="2.4"/>',
 G=p=>p>=0?'GPIO'+p:'GPIO…',ul=a=>'<ul>'+a.map(x=>'<li>'+x+'</li>').join('')+'</ul>',brd=()=>bx(8,8,324,76,'Міст батареї','плата ESP32-C3'),dec=v=>String(v).replace('.',','),
 C={g:'#22b35e',r:'#e04848',s1:'var(--s1)',s2:'var(--s2)',s3:'var(--s3)',s4:'var(--s4)',s5:'var(--s5)',s6:'var(--s6)',v5:'var(--v5)',v33:'var(--v33)',gnd:'var(--gnd)'};
//  два світлодіоди; low — спільний «+» на 3V3, вивід у «0» запалює
function leds(g,r,low){const L=[];if(g>=0||r<0)L.push([70,g,C.g,'зелений']);if(r>=0||g<0)L.push([170,r,C.r,'червоний']);
 const pw=low?['3V3',C.v33]:['GND',C.gnd];let s=brd()+pn(270,84,pw[0],pw[1],1);
 for(const [x,p,c,n] of L)s+=pn(x,84,G(p),c,1)+rv(x,84,150,'220–470 Ом',c)+led(x,170,c,low)+ln([[x,190],[x,222]],pw[1])+tx(x+30,175,n,{a:'start'});
 s+=ln([[270,84],[270,222],[L[0][0],222]],pw[1])+(L.length>1?dt(170,222,pw[1]):'')+(low?'':gn(270,222));
 return sv(340,low?232:250,s)+ul(low?['Спільний «+» (анод) — на 3V3; «−» кожного кольору (катод, коротка ніжка) — через резистор 220–470 Ом до свого виводу.','Світлодіод світить, коли на виводі «0», — тому галочка «вмикаються низьким рівнем».','Звичайні окремі світлодіоди — без галочки: «+» через резистор до виводу, «−» до GND.']
  :['Довга ніжка (анод, «+») — через резистор до виводу, коротка (катод, «−») — до GND.','Резистор 220–470 Ом обов\'язковий: без нього світлодіод і вивід плати перегріваються. Більший опір — тьмяніше.','Одного кольору немає — у списку виводів «немає».','Світлодіоди зі спільним «+» — галочка «вмикаються низьким рівнем»: схема зміниться.'])}
//  стрічка WS2812B: струм — з кількості й яскравості (смуга заряду, біжучий вогник); понад 0,5 А — окремий блок 5 В
function strip(p,n,b){n=n>0?n:8;b=b>0?b:40;const A=n*(1+40*b/100)/1000,ext=A>0.5,blk=Math.max(1,Math.ceil(A*3)/2),x=ext?[60,150,304]:[70,170,270];
 let s=ext?bx(8,8,196,76,'Міст батареї','плата ESP32-C3')+bx(214,8,118,76,'Блок 5 В','від '+dec(blk)+' А')+pn(240,84,'GND',C.gnd,1)+pn(304,84,'+5V',C.v5,1):brd()+pn(270,84,'5V',C.v5,1);
 s+=pn(x[0],84,G(p),C.s1,1)+pn(x[1],84,'GND',C.gnd,1)+rv(x[0],84,232,'330 Ом',C.s1)+ln([[x[1],84],[x[1],232]],C.gnd)+ln([[x[2],84],[x[2],232]],C.v5)+cap(x[1],x[2],196,'1000 мкФ');
 if(ext)s+=ln([[240,84],[240,150],[150,150]],C.gnd)+dt(150,150,C.gnd);
 s+=rc(8,232,324,64)+pn(x[0],232,'DIN',C.s1)+pn(x[1],232,'GND',C.gnd)+pn(x[2],232,'+5V',C.v5)+tx(170,273,'Стрічка WS2812B',{b:1,s:13})+tx(170,288,'вхід — кінець, від якого йдуть стрілки',{c:'var(--mut)',s:11});
 return sv(340,300,s)+ul([n+' світлодіодів на '+b+' % — до '+dec(A.toFixed(A<1?2:1))+' А: '+(ext?'від плати забагато, тож стрічці — окремий блок 5 В (на схемі). Плату живіть як і раніше; GND блока, стрічки й плати — обов\'язково спільний.':'вистачає виводу 5V плати (плату живить USB-блок на 1 А і більше).'),
  'DIN — через резистор 330 Ом, ближче до стрічки. Під\'єднувати до входу стрічки: стрілки на ній показують від входу.','Конденсатор 1000 мкФ на 6,3–10 В — біля входу стрічки, «+» до +5V.',
  'Дріт даних — до 1–2 м. Стрічка мерехтить чи кольори випадкові — коротший дріт; не допомогло — перетворювач рівня 3,3 → 5 В (74AHCT125) на дроті даних.','Кольори переплутані (червоний замість зеленого) — змініть «Кольори».'])}
function bzMod(p,low){let s=brd()+pn(70,84,G(p),C.s3,1)+pn(170,84,'3V3',C.v33,1)+pn(270,84,'GND',C.gnd,1)+ln([[70,84],[70,190]],C.s3)+ln([[170,84],[170,190]],C.v33)+ln([[270,84],[270,190]],C.gnd);
 s+=rc(8,190,324,62)+pn(70,190,'I/O (S)',C.s3)+pn(170,190,'VCC (+)',C.v33)+pn(270,190,'GND (−)',C.gnd)+tx(170,231,'Модуль бузера',{b:1,s:13})+tx(170,246,'активний чи пасивний, три виводи',{c:'var(--mut)',s:11});
 return sv(340,258,s)+ul(['VCC (+) — до 3V3, GND (−) — до GND, I/O (S, SIG) — до виводу.','Пищить, коли на I/O «0» (на модулі транзистор S8550 чи напис «low level trigger»), — галочка «Бузер вмикається низьким рівнем»'+(low?' (стоїть)':'')+'. Такий модуль живіть лише від 3V3: від 5 В він не замовкне.','Активний бузер — один тон; пасивний — мелодії, різні на кожну подію.'])}
//  окремий бузер через транзистор; k: 'a' — активний, 'p' — пасивний (з діодом), '' — ще не вибрано
function bzNpn(p,k){let s=brd()+pn(60,84,G(p),C.s3,1)+pn(190,84,'5V',C.v5,1)+pn(310,84,'GND',C.gnd,1)+ln([[190,84],[190,104]],C.v5)+buz(190,124)+ln([[190,144],[190,170]])+npn(181,196)+
  ln([[60,84],[60,196]],C.s3)+rh(60,157,196,'1 кОм',C.s3)+ln([[190,222],[190,246],[310,246],[310,84]],C.gnd)+gn(250,246)+tx(214,198,'NPN: S8050',{a:'start'})+tx(214,213,'чи 2N2222',{a:'start'});
 if(k!=='a')s+=dt(190,96,C.v5)+ln([[190,96],[240,96],[240,104]],C.v5)+dio(240,104,150)+ln([[240,150],[240,158],[190,158]])+dt(190,158)+tx(252,131,'1N4148',{a:'start'});
 return sv(340,272,s)+ul(['«+» бузера — до 5V, «−» — до колектора; емітер — до GND; база — через 1 кОм до виводу. Галочку «низьким рівнем» не ставити.',
  'Ніжки транзистора — за довідкою на свій: у S8050 і 2N2222 (PN2222) плоским боком до себе зазвичай емітер, база, колектор зліва направо, у BC547 — навпаки.',
  k==='a'?'Активному бузеру діод не потрібен.':'Пасивний бузер (від батарейки лише клацає) — паралельно йому діод 1N4148 смужкою до 5V: гасить викиди котушки.'+(k?'':' Активному діод не потрібен.'),
  'Бузер на 3 В — «+» до 3V3 замість 5V.','П\'єзопищалка (тонкий диск) бере мало струму — її можна напряму: вивід → «+», «−» → GND.'])}
function lcd(p){const R=[['CLK',p.clk,C.s1],['DI',p.din,C.s2],['CS',p.cs,C.s3],['RS (A0)',p.rs,C.s4],['RST',p.rst,C.s5],['LED',p.bl,C.s6],['VCC','3V3',C.v33],['GND','GND',C.gnd]];
 let s=bx(8,8,120,290,'Міст','ESP32-C3')+bx(212,8,120,290,'Дисплей','ST7567, 1,8″');
 R.forEach(([n,v,c],i)=>{const y=72+i*28,num=typeof v==='number',off=num&&v<0;s+=ps(128,y,num?(off?'немає':'GPIO'+v):v,off?'var(--bd2)':c,1)+ps(212,y,n,c,0)+(off?(n==='RST'?ln([[212,y],[190,y]],C.v33)+tx(185,y+4,'3V3',{a:'end',c:C.v33}):''):ln([[128,y],[212,y]],c))});
 return sv(340,306,s)+ul(['Живлення модуля — лише 3,3 В (VCC → 3V3), не 5 В.','Порядок виводів на модулі буває інший — звіряйте підписи на платі дисплея.',
  'RST можна не під\'єднувати («немає» — тоді лише програмне скидання): RST модуля — на 3V3. LED «немає» — підсвіткою міст не керує.','Дроти — до 30 см. GPIO2 і GPIO8 — виводи запуску плати: працюють, але краще не займати.'])}
//  виводи плати: що зайнято збереженим (a — /alert, l — /lcd) і що лишити
function pins(a,l){const u={},add=(p,n)=>{if(p>=0)u[p]=(u[p]?u[p]+', ':'')+n};
 if(a.light==1){add(a.g,'зелений світлодіод');add(a.r,'червоний світлодіод')}if(a.light==2)add(a.sp,'стрічка DIN');if(a.snd)add(a.bz,'бузер');
 if(l.on)[['clk','CLK'],['din','DI'],['cs','CS'],['rs','RS'],['rst','RST'],['bl','LED']].forEach(([k,n])=>add(l[k],'дисплей '+n));
 const row=(g,t)=>'<tr><td>'+g+'</td><td>'+t+'</td></tr>',fr=p=>u[p]?'<b>'+esc(u[p])+'</b>':'';let h='';
 for(const p of [0,1,3,4,5,6,7,10])h+=row('GPIO'+p,fr(p)||'вільний');
 for(const p of [20,21])h+=row('GPIO'+p,fr(p)||'вільний на платі «usb»; на «uart» — порт USB-UART, не займати');
 for(const p of [2,8])h+=row('GPIO'+p,(fr(p)?fr(p)+' · ':'')+'вивід запуску: працює, але краще не займати'+(p==8?' (на платі SuperMini — ще й її світлодіод)':''));
 return '<table class="pt">'+h+row('GPIO9','кнопка BOOT: коротко — тиша, 3 с — сторінка налаштування')+row('GPIO18, 19','USB — не займати')+row('GPIO11–17','флеш-пам\'ять плати — не займати')+'</table>'}
const power=()=>ul(['Плату живіть від ДБЖ чи інвертора — не з міської розетки: батарея найважливіша саме без світла.','Плата — від USB-блока 5 В на 1 А і більше. Вивід 5V — те саме 5 В з USB (стрічка, бузер); 3V3 — для дисплея й модулів, до 200 мА.',
 'Усі GND — спільні: плата, стрічка, окремий блок стрічки.','Плату ставте ближче до BMS: на слабкому сигналі BMS рве з\'єднання, а кожне нове — коротко пищить.']);
return {leds,strip,bzMod,bzNpn,lcd,pins,power}})();
const SCH=/\/schemes$/.test(location.pathname)||location.hash==='#schemes',
 SCL='<div class="mut" style="margin-top:8px"><a href="schemes" target="_blank" rel="noopener">Усі схеми мосту однією сторінкою — для друку</a></div>',
 fv=k=>{const x=$(k).value;return x===''?-1:+x};
function alSch(){const l=fv('a_light'),s=fv('a_snd'),p=fv('a_bz');
 $('a_lsch').innerHTML=(l!=2?'<h3>Як під\'єднати '+(l==1?'світлодіоди':'два світлодіоди')+'</h3>'+SC.leds(fv('a_g'),fv('a_r'),$('a_low').checked):'')+(l!=1?'<h3>Як під\'єднати стрічку WS2812B</h3>'+SC.strip(fv('a_sp'),fv('a_sn'),fv('a_bri')):'')+SCL;
 $('a_ssch').innerHTML='<h3>Модуль бузера (три виводи)</h3>'+SC.bzMod(p,$('a_bzLow').checked)+'<h3>Окремий бузер через транзистор</h3>'+SC.bzNpn(p,s==2?'p':s==1?'a':'')+SCL}
async function schPage(){document.body.classList.add('schm');document.title='Міст батареї: схеми підключення';document.querySelector('h1').textContent='Міст батареї «Сторожа світла»: схеми підключення';
 const j=u=>fetch(u,{cache:'no-store'}).then(r=>r.ok?r.json():null).catch(()=>null);let [a,l]=await Promise.all([j('alert'),j('lcd')]);const live=!!(a&&l);
 if(!live){a={light:1,g:0,r:1,low:false,sp:0,sn:8,bri:40,snd:2,bz:20,bzLow:false};l={on:true,clk:4,din:6,cs:7,rs:5,rst:10,bl:3}}
 const cd=(h,b)=>'<div class="card"><h2>'+h+'</h2><div class="sch">'+b+'</div></div>';
 $('schall').innerHTML='<div class="mut">'+(live?'Номери виводів — як збережено на мості зараз (не вибрано — «GPIO…»). Змінили виводи — оновіть сторінку.'
  :'Номери виводів — приклад для плати «usb» з дисплеєм на типових виводах: світлодіоди — GPIO0 і GPIO1 (чи стрічка — GPIO0), бузер — GPIO20. Свої виводи вибирають на сторінці мосту в картках «Світло», «Звук» і «Дисплей» — схеми там показують саме їх.')+'</div>'+
  cd('Світло: два світлодіоди',SC.leds(a.g,a.r,a.low))+cd('Світло: адресна стрічка WS2812B',SC.strip(a.sp,a.sn,a.bri))+cd('Звук: модуль бузера',SC.bzMod(a.bz,a.bzLow))+
  cd('Звук: окремий бузер через транзистор',SC.bzNpn(a.bz,a.snd==2?'p':a.snd==1?'a':''))+cd('Дисплей Open-Smart 1,8″ (ST7567)',SC.lcd(l))+cd('Виводи плати ESP32-C3',SC.pins(a,l))+cd('Живлення',SC.power())}
//  Світло й звук (1.2.0)
const AP=['a_g','a_r','a_sp','a_bz'];let alFilled=false;
function alOpts(pins,lcd){return '<option value="-1">немає</option>'+pins.map(p=>'<option value="'+p+'"'+(lcd.includes(p)?' disabled':'')+'>GPIO'+p+(p==2||p==8?' (запуск)':'')+(lcd.includes(p)?' — дисплей':'')+'</option>').join('')}
function alShow(){const l=+$('a_light').value;$('a_l1').style.display=l==2?'none':'';$('a_l2').style.display=l==1?'none':''}
//  1.2.5 (власник: «не понятно куда подключать бузер, светодиоды и ленту — нет номеров выводов и их выбора»): виводи
//  видно завжди; не задані — запропоновано вільні (спершу не виводи запуску), схеми — одразу з цими номерами
const PREF=[0,1,3,4,5,6,7,10,20,21,2,8];
function alFill(d){for(const k of AP)$(k).innerHTML=alOpts(d.pins,d.lcdPins);
 const free=PREF.filter(p=>d.pins.includes(p)&&!d.lcdPins.includes(p)),pick=(n,skip)=>free.filter(p=>p!=2&&p!=8&&!skip.includes(p)).slice(0,n);   // виводи запуску не пропонуємо
 const lu=d.light==1?[d.g,d.r].filter(p=>p>=0):d.light==2&&d.sp>=0?[d.sp]:[],su=d.snd&&d.bz>=0?[d.bz]:[];let g=d.g,r=d.r,sp=d.sp,bz=d.bz,sug=0;
 if(g<0&&r<0&&d.light!=1){const t=pick(2,su);g=t.length?t[0]:-1;r=t.length>1?t[1]:-1;sug=1}
 if(sp<0&&d.light!=2){const t=pick(1,su);sp=t.length?t[0]:-1;sug=1}
 if(bz<0&&!d.snd){const t=pick(1,lu.concat(d.light?[]:[g,r,sp]));bz=t.length?t[0]:-1;sug=1}
 const fr=skip=>{const f=free.filter(p=>!skip.includes(p)),a=f.filter(p=>p!=2&&p!=8),b=f.filter(p=>p==2||p==8);
  return 'Вільні виводи плати: '+(a.length?a.map(p=>'GPIO'+p).join(', '):'немає')+(b.length?' (ще '+b.map(p=>'GPIO'+p).join(' і ')+' — виводи запуску: можна, але краще не займати)':'')+
   (d.lcdPins.length?'. Дисплей займає '+d.lcdPins.map(p=>'GPIO'+p).join(', '):'')+'.'+(sug?' Не вибрані досі виводи запропоновано з вільних — під\'єднайте за схемою нижче, виберіть «Що під\'єднано» й «Зберегти світло й звук».':'')};
 $('a_lfree').textContent=fr(su);$('a_sfree').textContent=fr(lu);
 $('a_light').value=d.light;$('a_g').value=g;$('a_r').value=r;$('a_low').checked=d.low;$('a_sp').value=sp;$('a_sn').value=d.sn;$('a_so').value=d.so;$('a_bri').value=d.bri;$('a_nbri').value=d.nbri;
 $('a_snd').value=d.snd;$('a_bz').value=bz;$('a_bzLow').checked=d.bzLow;$('a_vol').value=d.vol;$('a_rep').value=d.rep;$('a_nf').value=d.nf;$('a_nt').value=d.nt;$('a_nm').value=d.nm;
 $('a_evs').innerHTML=d.names.map((n,e)=>e?'<div class="evr"><label><input type="checkbox" data-e="'+e+'"'+((d.mask>>e)&1?' checked':'')+'>'+esc(n)+(d.crit[e]?' <span class="mut">(важливе)</span>':'')+'</label><button class="s" data-play="'+e+'" type="button">▶</button></div>':'').join('');
 alShow();alSch();alFilled=true}
async function alPoll(){try{const d=await (await fetch('alert',{cache:'no-store'})).json();if(!alFilled)alFill(d);const many=last&&last.packs&&last.packs.length>1;
 $('a_clock').textContent=d.clock?'Годинник плати є'+(d.night?' — зараз ніч.':'.'):'Годинника немає (час — з інтернету через Сторожа): ночі немає, звук і світло — як удень.';
 const L=d.log||[];$('a_log').innerHTML=(d.crits.length?'<div class="bad">Повторюється: '+d.crits.map(x=>(many?'батарея '+x.pk+' — ':'')+esc(d.names[x.e])).join(', ')+(d.muted?' (зараз тиша)':'')+'</div>':'')+
  (L.length?'<div><b>Останні події</b></div>'+L.map(x=>'<div class="mut">'+(x.t||mm(x.ago)+' тому')+' · '+(many?'батарея '+x.pk+': ':'')+esc(d.names[x.e])+'</div>').join(''):'<div class="mut">Подій від запуску плати не було.</div>')}catch(e){}}
$('a_light').onchange=alShow;$('a_snd').onchange=alShow;
for(const k of ['a_light','a_g','a_r','a_low','a_sp','a_sn','a_bri','a_snd','a_bz','a_bzLow'])for(const e of ['input','change'])$(k).addEventListener(e,alSch);
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
const LP=['clk','din','cs','rs','rst','bl'];let lcdFilled=false;
function lcdDraw(hex){const c=$('lcdc'),x=c.getContext('2d'),im=x.createImageData(128,64);
 for(let p=0;p<8;p++)for(let col=0;col<128;col++){const b=parseInt(hex.substr((p*128+col)*2,2),16);for(let r=0;r<8;r++){const on=b>>r&1,i=((p*8+r)*128+col)*4;im.data[i]=on?27:223;im.data[i+1]=on?31:230;im.data[i+2]=on?36:234;im.data[i+3]=255}}
 x.putImageData(im,0,0)}
function lcdWire(){const p={};for(const k of LP)p[k]=fv('l_'+k);$('lcdsch').innerHTML='<h3>Як під\'єднати</h3>'+SC.lcd(p)+SCL}
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
if(SCH)schPage();else{
lcdPoll();setInterval(()=>{if(!document.hidden)lcdPoll()},2000);
setInterval(()=>{if(!document.hidden&&(setup||trust))alPoll()},10000);
st().then(s=>{if(s&&s.setup)scan(false);if(s&&(s.setup||s.trust))alPoll()});setInterval(()=>{if(!document.hidden)st()},3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){st();lcdPoll();if(setup||trust)alPoll()}})}
</script></body></html>)PAGE";
