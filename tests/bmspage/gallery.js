// Схеми підключення мосту (1.2.4) — усі варіанти на одній сторінці для перевірки картинкою: out/gallery.html.
// Бере out/bmspage.html (сторінка з підставними даними, її робить page.js) і малює в #schall різні виводи й типи.
const fs = require('fs');
let html = fs.readFileSync(process.env.OUT + '/bmspage.html', 'utf8');
const g = `<script>setTimeout(()=>{document.body.classList.add('schm');const cd=(h,b)=>'<div class="card"><h2>'+h+'</h2><div class="sch">'+b+'</div></div>';
$('schall').innerHTML=cd('Світлодіоди: GPIO4 і GPIO5',SC.leds(4,5,false))+cd('Лише зелений: GPIO4',SC.leds(4,-1,false))+cd('Лише червоний: GPIO5',SC.leds(-1,5,false))+
 cd('Спільний «+»: GPIO0 і GPIO1',SC.leds(0,1,true))+cd('Стрічка 30 шт. на 100 % — окремий блок',SC.strip(8,30,100))+cd('Стрічка 12 шт. на 100 % — від плати',SC.strip(8,12,100))+
 cd('Бузер через транзистор, активний',SC.bzNpn(3,'a'))+cd('Бузер через транзистор, ще не вибрано',SC.bzNpn(-1,''))+cd('Модуль бузера, низьким рівнем',SC.bzMod(10,true))+
 cd('Дисплей: RST і LED — немає',SC.lcd({clk:4,din:6,cs:7,rs:5,rst:-1,bl:-1}))},300)</script>`;
html = html.replace('</body>', g + '</body>');
fs.writeFileSync(process.env.OUT + '/gallery.html', html);
console.log('галерея:', process.env.OUT + '/gallery.html');
