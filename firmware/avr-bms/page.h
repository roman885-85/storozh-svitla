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
</style></head><body><main>
<h1>Міст батареї «Сторожа світла»</h1><div class="mut" id="who"></div>
<div class="card"><b>Зараз</b><div id="st" style="margin-top:6px">…</div></div>
<div class="card" id="bat" style="display:none"><b>Батарея</b><div id="bv" style="margin-top:6px"></div></div>
<div class="card" id="pick" style="display:none"><b>Яку BMS слухати</b>
<div class="mut">Типово — найближчу JK BMS (назва «JK-…» чи «JK_…», найсильніший сигнал). Якщо поруч кілька чи свою BMS перейменовано — виберіть її тут.</div>
<label for="bl">Чути поруч</label><select id="bl"></select>
<button id="pin" type="button">Слухати вибрану</button><button class="s" id="auto" type="button">Найближчу (типово)</button>
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
<div class="card" id="otacard" style="display:none"><b>Оновлення прошивки мосту</b>
<div class="mut">Адреса файлу прошивки <span id="ovar"></span> (http або https). Міст сам завантажить його через Сторожа й перезапуститься; якщо нова прошивка не запрацює — повернеться попередня. Налаштування лишаються.</div>
<label for="ou">Адреса файлу</label><input id="ou" autocomplete="off" autocapitalize="off" placeholder="https://…-app.bin">
<button id="og" type="button">Оновити</button><div id="omsg" style="margin-top:8px"></div></div>
<div class="card mut">Живіть плату від ДБЖ чи інвертора — не з міської розетки: батарея найважливіша саме без світла. Ставте її ближче до BMS: на слабкому сигналі BMS рве з'єднання, а кожне нове — коротко пищить. Поки плата на зв'язку з BMS, застосунок JK на телефоні до BMS не під'єднається.</div>
</main><script>
const $=i=>document.getElementById(i),W=ms=>new Promise(r=>setTimeout(r,ms));
function esc(t){return String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
const n1=(x,d)=>(x/d).toFixed(d>=1000?2:1).replace('.',','),BST=['шукаю BMS','під\'єднуюсь до BMS','на зв\'язку з BMS','на зв\'язку з BMS, але стану від неї немає','пауза перед новою спробою'];
let setup=false;
async function st(){try{const s=await (await fetch('state',{cache:'no-store'})).json();setup=s.setup;
 $('who').textContent='Версія '+s.ver+' · MAC '+s.mac+(s.up!=null?' · працює '+(s.up>=3600?Math.floor(s.up/3600)+' год '+Math.floor(s.up%3600/60)+' хв':Math.floor(s.up/60)+' хв'):'');
 $('form').style.display=s.setup?'':'none';$('pick').style.display=$('otacard').style.display=(s.setup||s.trust)?'':'none';
 $('ovar').textContent='avr-bms-…-'+(s.var||'usb')+'-app.bin';if(s.ota)$('omsg').textContent='Оновлення: '+s.ota;
 let h=s.conn?'<span class="ok">● Сторож на зв\'язку</span>: мережа «'+esc(s.ssid)+'», сигнал '+s.rssi+' дБм'
  :s.ssid?'<span class="bad">● Немає зв\'язку зі Сторожем</span> (мережа «'+esc(s.ssid)+'»)'+(s.err?': '+esc(s.err):''):'<span class="bad">● Мережу Сторожа ще не задано</span>';
 const b=s.b||{};h+='<div style="margin-top:6px">'+(b.st==2?'<span class="ok">● ':'<span class="warn">● ')+BST[b.st||0]+'</span>'+(b.name?': '+esc(b.name)+' ('+esc(b.mac)+', '+b.rssi+' дБм)':'')+'</div>';
 h+='<div class="mut">'+(s.pin?'Слухаю вибрану BMS '+esc(s.pin):'Слухаю найближчу JK BMS')+' · з\'єднань від запуску: '+b.rc+(b.note?' · '+esc(b.note):'')+'</div>';
 if(!s.setup)h+='<div class="mut">Змінити мережу чи BMS: утримати кнопку BOOT на платі 3 секунди — з\'явиться мережа «'+esc(s.ap)+'».</div>';
 $('st').innerHTML=h;
 const c=s.c;$('bat').style.display=c?'':'none';
 if(c){const p=c.v*c.i/1e6,ch=c.i>50?'заряджається':c.i<-50?'розряджається':'спокій';
  let t='<div class="big">'+c.soc+' %</div><div>'+n1(c.v,1000)+' В · '+(c.i/1000).toFixed(1).replace('.',',')+' А · '+Math.round(p)+' Вт — '+ch+'</div>';
  t+='<div class="mut">Залишок '+n1(c.rem,1000)+' з '+n1(c.full,1000)+' А·год · циклів '+c.cyc+' · стан '+c.soh+' %'+(c.info?' · '+esc(c.info):'')+'</div>';
  t+='<div style="margin-top:6px">Комірки: '+c.n+', від '+n1(c.lo,1000)+' В (№'+c.lox+') до '+n1(c.hi,1000)+' В (№'+c.hix+'), різниця '+(c.hi-c.lo)+' мВ</div>';
  t+='<div class="cells">'+c.cells.map((v,i)=>'<span class="'+(i+1==c.lox?'lo':i+1==c.hix?'hi':'')+'">'+(i+1)+': '+(v/1000).toFixed(3).replace('.',',')+'</span>').join('')+'</div>';
  const T=[];if(c.t1!=null)T.push('датчик 1 — '+n1(c.t1,10)+' °C');if(c.t2!=null)T.push('датчик 2 — '+n1(c.t2,10)+' °C');if(c.tm!=null)T.push('ключі — '+n1(c.tm,10)+' °C');
  t+='<div style="margin-top:6px">'+(T.length?'Температура: '+T.join(', '):'Температури BMS не передає')+'</div>';
  t+='<div>Заряд '+(c.chg?'<span class="ok">дозволено</span>':'<span class="bad">вимкнено BMS</span>')+' · розряд '+(c.dis?'<span class="ok">дозволено</span>':'<span class="bad">вимкнено BMS</span>')+(c.bal?' · вирівнює комірки':'')+(c.heat?' · підігрів':'')+'</div>';
  t+='<div>'+(c.errs.length?'<span class="bad">Тривоги BMS: '+c.errs.map(esc).join(', ')+'</span>':'<span class="ok">Тривог BMS немає</span>')+(c.notes.length?' <span class="mut">('+c.notes.map(esc).join(', ')+')</span>':'')+'</div>';
  t+='<div class="mut">Кадр '+c.age+' с тому, розкладка '+c.fmt+'S</div>';$('bv').innerHTML=t}
 if(s.setup||s.trust){const L=s.found||[],sel=$('bl').value;$('bl').innerHTML=L.length?L.map(f=>'<option value="'+esc(f.mac)+'">'+esc(f.name)+' · '+esc(f.mac)+' · '+f.rssi+' дБм'+(f.jk?'':' · не схоже на JK')+'</option>').join(''):'<option value="">JK BMS поруч не чути</option>';if(sel)$('bl').value=sel}
 return s}catch(e){return null}}
async function pinTo(mac){const m=$('pmsg');try{const r=await fetch('bms',{method:'POST',body:new URLSearchParams({mac})});const d=await r.json();if(!r.ok)throw new Error(d.err||'помилка');m.innerHTML='<span class="ok">'+(mac?'Слухаю '+esc(mac):'Слухаю найближчу')+' — під\'єднаюсь за кілька секунд</span>'}catch(e){m.innerHTML='<span class="bad">'+esc(e.message)+'</span>'}st()}
$('pin').onclick=()=>{const v=$('bl').value;if(v)pinTo(v)};$('auto').onclick=()=>pinTo('');
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
st().then(s=>{if(s&&s.setup)scan(false)});setInterval(()=>{if(!document.hidden)st()},3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){st();lcdPoll()}});
</script></body></html>)PAGE";
