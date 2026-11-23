/*  Сторінка моста (українською). Одна сторінка з вкладками; дані — /api/state кожні 3 с.
    Вхід — звичайна форма (LOGIN_HTML) і cookie: вікно Basic-входу показують не всі браузери (18.11.2026).  */
#include <Arduino.h>

//  Форма входу. %ERR% web.cpp замінює повідомленням про помилку (або нічим). Без скриптів — працює всюди.
extern const char LOGIN_HTML[] PROGMEM = R"PAGE(<!doctype html>
<html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Сторож світла: вхід</title>
<style>
:root{--bg:#f3f4f7;--card:#fff;--fg:#1c2230;--mut:#677085;--bad:#d24545;--acc:#2f6fde;--bd:#e1e4ea}
@media (prefers-color-scheme:dark){:root{--bg:#13161c;--card:#1c2028;--fg:#e8eaf0;--mut:#98a0b2;--bd:#2b303a}}
*{box-sizing:border-box}body{margin:0;font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:380px;margin:10vh auto 0;padding:0 16px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px}
h1{font-size:19px;margin:0}label{display:block;margin:12px 0 4px;font-size:13px;color:var(--mut)}
input{width:100%;padding:10px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
input[readonly]{color:var(--mut)}
button{margin-top:14px;width:100%;padding:10px;border:1px solid var(--acc);border-radius:8px;background:var(--acc);color:#fff;font:inherit;cursor:pointer}
.err{margin-top:12px;color:var(--bad)}.mut{color:var(--mut);font-size:13px}
</style></head><body><main><div class="card">
<h1>Сторож світла</h1><div class="mut">Вхід на сторінку</div>
<form method="post" action="/login">
<label for="u">Користувач</label><input id="u" name="u" value="admin" readonly autocomplete="username">
<label for="p">Пароль сторінки</label><input id="p" name="p" type="password" autocomplete="current-password" required autofocus>
<button type="submit">Увійти</button>
</form>%ERR%
<div class="mut" style="margin-top:14px">Забули пароль: утримайте 5 секунд окрему кнопку скидання пароля на Сторожі — пароль стане типовим (він в інструкції), решта налаштувань лишиться. Без цієї кнопки — кнопка BOOT на платі, 10 секунд: зітруться всі налаштування.</div>
</div></main></body></html>)PAGE";

extern const char INDEX_HTML[] PROGMEM = R"PAGE(<!doctype html>
<html lang="uk"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Сторож світла</title>
<style>
:root{--bg:#f3f4f7;--card:#fff;--fg:#1c2230;--mut:#677085;--ok:#1f9d57;--bad:#d24545;--gen:#d9951c;--acc:#2f6fde;--bd:#e1e4ea}
@media (prefers-color-scheme:dark){:root{--bg:#13161c;--card:#1c2028;--fg:#e8eaf0;--mut:#98a0b2;--bd:#2b303a}}
*{box-sizing:border-box}body{margin:0;font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 16px 8px;font-weight:650;font-size:18px}header span{font-weight:400}
nav{display:flex;gap:6px;padding:0 12px 6px;flex-wrap:wrap;max-width:780px;margin:0 auto}nav button{flex:1 1 auto;min-width:70px;padding:9px 10px}
main{padding:0 12px 28px;max-width:780px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin:10px 0}
.big{font-size:21px;font-weight:700;display:flex;align-items:center;gap:10px}
.dot{flex:none;width:14px;height:14px;border-radius:50%;background:var(--mut)}.on{background:var(--ok)}.off{background:var(--bad)}.gen{background:var(--gen)}
.row{display:flex;justify-content:space-between;gap:12px;padding:7px 0;border-bottom:1px solid var(--bd)}.row:last-child{border:0}.row span:last-child{text-align:right}
.cells{display:flex;flex-wrap:wrap;gap:4px;margin-top:8px}.cells span{font-size:12px;padding:2px 5px;border:1px solid var(--bd);border-radius:6px;font-variant-numeric:tabular-nums}.cells .lo{border-color:var(--bad)}.cells .hi{border-color:#2fa864}
.mut{color:var(--mut);font-size:13px}
label{display:block;margin:10px 0 4px;font-size:13px;color:var(--mut)}label.ck{display:flex;gap:8px;align-items:flex-start;color:var(--fg);font-size:14px}
input,textarea,select{width:100%;padding:9px 10px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
input[type=checkbox]{width:auto;margin-top:3px}textarea{min-height:76px}
button{padding:9px 14px;border:1px solid var(--bd);border-radius:8px;background:var(--card);color:var(--fg);font:inherit;cursor:pointer}
button.p{background:var(--acc);color:#fff;border-color:var(--acc)}button.d{background:#b3261e;color:#fff;border-color:#b3261e}.card button:not(.p):not(.d){background:var(--bg);border-color:var(--mut)}a.btn{display:inline-block;padding:9px 14px;border:1px solid var(--acc);border-radius:8px;background:var(--acc);color:#fff;text-decoration:none;font:inherit}nav button.sel{background:var(--acc);color:#fff;border-color:var(--acc)}
.tbl{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:13px}td{padding:6px 4px;border-bottom:1px solid var(--bd);vertical-align:top}
.hide{display:none}.msg{padding:9px 11px;border-radius:8px;margin:10px 0 0;background:var(--bg)}.err{color:var(--bad)}
.flex{display:flex;gap:6px;flex-wrap:wrap}.flex input{flex:1}.col{display:inline-flex;align-items:center;gap:6px;margin:6px 10px 0 0;font-size:13px;color:var(--mut)}.col input{width:42px;height:30px;padding:0;border:1px solid var(--bd);border-radius:6px;background:none}input[type=range]{width:100%;padding:0}
details{margin-top:8px}summary{cursor:pointer;color:var(--acc)}details ol{padding-left:20px;margin:8px 0}details li{margin:5px 0}
#lo{float:right;padding:3px 10px;font-size:13px;font-weight:400}
.bar{position:relative;height:20px;border-radius:5px;background:#d8f0e0;overflow:hidden;margin-top:4px}.bar i{position:absolute;top:0;bottom:0;background:var(--bad)}.bar i.p{background:var(--gen)}
.bar b{position:absolute;top:-2px;bottom:-2px;width:3px;margin-left:-1px;background:var(--fg)}.ticks{display:flex;justify-content:space-between;font-size:11px;color:var(--mut)}
.sday{margin-top:10px}.sday .mut{margin-top:2px}
.brow{padding:9px 0;border-bottom:1px solid var(--bd)}.brow:last-child{border:0}.brow select{flex:1;width:auto;min-width:150px}.brow .flex{align-items:center}
.chips{display:flex;flex-wrap:wrap;gap:4px 12px;margin-top:6px}.chips label{display:inline-flex;gap:5px;align-items:center;font-size:13px;margin:0;color:var(--fg)}.chips input{margin:0}.chips label.dim{color:var(--mut)}
@media (prefers-color-scheme:dark){.bar{background:#1e3a2a}}
</style></head><body>
<header><button data-act="logout" id="lo">Вийти</button>Сторож світла <span id="obj" class="mut"></span></header>
<nav><button data-t="st" class="sel">Стан</button><button data-t="tg">Сповіщення</button><button data-t="sg">Сигнали</button><button data-t="set">Налаштування</button><button data-t="log">Журнал</button><button data-t="ins">Інструкція</button><button data-t="upd">Оновлення</button></nav>
<main>
<section id="st">
 <div class="card"><div class="big"><span id="dot" class="dot"></span><span id="status">Зв'язуюсь зі Сторожем…</span></div><div id="since" class="mut"></div></div>
 <div class="card" id="rows"></div>
 <div class="card hide" id="batcard"><b>🔋 Батарея</b> <span id="batsub" class="mut"></span><div id="batbody" style="margin-top:6px"></div>
  <label class="ck" style="margin-top:10px"><input type="checkbox" id="bat_on"> Сповіщати про батарею</label>
  <div class="mut">Заряд нижче 50, 30, 20, 10 % і «вистачить менше ніж на 3 год / на годину» — коли міського світла немає; тривоги BMS; «BMS не на зв'язку» (15 хв без даних) — адміністраторам. Отримують адміністратори й ті, у кого в сповіщеннях увімкнено «🔋 Батарея». Як у HA.</div></div>
 <div class="card" id="schcard"><b>Графік відключень</b> <span id="schq" class="mut"></span>
  <div id="schline" style="margin-top:6px"></div><div id="schdays"></div><div id="schinfo" class="mut" style="margin-top:8px"></div><div id="schbtn"></div></div>
 <div class="card"><details id="stdet"><summary>Статистика світла: скільки було й не було</summary><div id="sttext" class="mut" style="white-space:pre-wrap;margin-top:8px"></div></details></div>
 <div class="card"><b>Пристрої</b> <span id="dvcnt" class="mut"></span>
 <div class="mut" style="margin-top:4px">Що прив'язано до Сторожа і чи воно зараз на зв'язку. Нижче — інші пристрої, під'єднані до точки Wi-Fi Сторожа: їх можна прив'язати.</div>
 <div class="tbl"><table id="dvb"></table></div>
 <div style="margin-top:14px"><b>Інші пристрої в мережі Сторожа</b></div>
 <div class="tbl"><table id="cl"></table></div><div class="mut" id="clhint"></div></div>
</section>
<section id="tg" class="hide">
 <div class="card"><b>Хто отримує сповіщення</b> <span id="rcount" class="mut"></span>
  <div class="mut" style="margin-top:4px">Позначте, що надсилати кожному. Людина може змінити це й сама — у боті кнопка «🔔 Сповіщення». «Сповіщення» вимкнено — людина не отримує нічого, але кнопки «📊 Стан», «🗓 Графік», «📈 Статистика» в боті працюють. Адміністратор отримує заявки нових людей і в боті бачить «👥 Користувачі» та «📋 Журнал».</div>
  <div class="tbl"><table id="rcp"></table></div><div id="rmsg"></div></div>
 <div class="card"><b>Додати людину чи групу</b>
  <div id="noBot" class="msg err hide">Спершу підключіть бота: блок «Бот Telegram», там покрокова інструкція.</div>
  <ol class="mut" style="margin:8px 0 0;padding-left:20px">
   <li>Надішліть людині посилання на бота: <span class="botl"></span>. Або хай знайде в пошуку Telegram <b class="botn">вашого бота</b>.</li>
   <li>Людина відкриває бота й натискає «Почати» (Start) або пише /start.</li>
   <li>За кілька секунд вона з'являється нижче, у «Писали боту». Натисніть «Додати»: їй прийде «Вас додано до сповіщень».</li>
   <li>Група: додайте бота в групу як учасника й напишіть у групі /start@<span class="botu">ім'я_бота</span>. Далі так само «Додати».</li>
  </ol>
  <div id="seen" style="margin-top:10px"></div></div>
 <div class="card" id="anncard"><b>📣 Оголошення</b> <span class="mut">ваше повідомлення людям у Telegram</span>
  <div class="mut" style="margin-top:4px">Отримають ті, у кого ввімкнено «Сповіщення» (усі чи вибрані). Те саме адміністратор може в боті: кнопка «📣 Оголошення». За бажанням — і на дисплеї Сторожа.</div>
  <textarea id="a_text" maxlength="1000" placeholder="Текст оголошення, до 1000 знаків" style="margin-top:8px"></textarea>
  <div class="flex" style="margin-top:6px;align-items:center"><select id="a_to" style="max-width:320px"><option value="all">Усім, у кого ввімкнено сповіщення</option><option value="sel">Вибраним</option></select></div>
  <div id="a_who" class="chips hide"></div>
  <label class="ck"><input type="checkbox" id="a_silent"> Без звуку</label>
  <label class="ck"><input type="checkbox" id="a_lcd" checked> Показати й на дисплеї Сторожа</label>
  <div style="margin-top:8px"><button class="p" data-act="annsend">Надіслати</button></div><div id="annmsg"></div></div>
 <div class="card" id="sumcard"><b>Щоденний підсумок</b>
  <label class="ck"><input type="checkbox" id="f_sumOn"> Щодня надсилати в Telegram підсумок за вчора: скільки годин і хвилин світло було й не було</label>
  <div class="flex" style="align-items:center;margin-top:6px"><span class="mut">о</span><input id="f_sumHour" type="number" min="0" max="23" style="max-width:80px"><span class="mut">годині, без звуку</span><button data-act="savesum">Зберегти</button></div>
  <div id="summsg"></div></div>
 <div class="card"><b>🌙 Уночі без звуку</b>
  <div class="mut">Для тих, у кого позначено «уночі тихо»: повідомлення приходять, але телефон не дзвонить.</div>
  <div class="flex" style="align-items:center;margin-top:6px"><span class="mut">з</span><input id="f_tgNF" type="number" min="0" max="23" style="max-width:80px"><span class="mut">до</span><input id="f_tgNT" type="number" min="0" max="23" style="max-width:80px"><span class="mut">години</span><button data-act="savenight">Зберегти</button></div>
  <div id="nightmsg"></div></div>
 <div class="card" id="botcard"><b>Бот Telegram</b> <span id="h_tok" class="mut"></span>
  <details id="botguide"><summary>Як створити бота й отримати токен: покроково, 3 хвилини</summary>
   <ol>
    <li>Відкрийте Telegram на телефоні чи комп'ютері.</li>
    <li>У пошуку Telegram введіть <b>@BotFather</b> і відкрийте його. Це офіційний бот Telegram, біля імені синя галочка. Натисніть «Почати» (Start).</li>
    <li>Надішліть йому повідомлення <b>/newbot</b>.</li>
    <li>BotFather попросить <b>назву</b> бота: так його бачитимуть у списку чатів. Напишіть, наприклад, <i>Світло офісу</i>.</li>
    <li>Далі він попросить <b>ім'я користувача</b>: латиницею, без пробілів, у кінці обов'язково <i>bot</i>. Наприклад, <i>svitlo_ofisu_bot</i>. Якщо ім'я зайняте, він попросить інше: додайте цифри чи назву міста.</li>
    <li>У відповіді «Done! Congratulations on your new bot» буде <b>токен</b>: рядок вигляду <i>1234567890:AAH…</i> після слів «Use this token to access the HTTP API». Натисніть на нього, і Telegram його скопіює.</li>
    <li>Вставте токен у поле нижче й натисніть <b>«Зберегти»</b>. За кілька секунд біля заголовка «Бот Telegram» з'явиться «підключено: @ім'я_бота».</li>
    <li>Тепер додайте людей: блок «Додати людину чи групу». Себе теж: відкрийте свого бота й натисніть «Почати».</li>
   </ol>
   <div class="mut">Токен — це пароль від бота. Не пересилайте його й не фотографуйте. Якщо він потрапив до чужих: у @BotFather надішліть /revoke, виберіть бота й вставте сюди новий токен.</div>
  </details>
  <label>Токен бота від @BotFather</label><div class="flex"><input id="f_tgToken" placeholder="вставте токен сюди" autocomplete="off"><button data-act="savetok">Зберегти</button></div>
  <div id="boterr" class="err" style="margin-top:6px"></div>
  <label>Назва бота в Telegram</label><div class="flex"><input id="f_botname" maxlength="64" placeholder="наприклад, Світло офісу"><button data-act="botname">Змінити</button></div>
  <div id="bnst" class="mut" style="margin-top:4px"></div>
  <div style="margin-top:10px"><button data-act="test">Надіслати пробне повідомлення всім</button></div><div id="tmsg"></div></div>
 <div class="card" id="phcard"><b>Картинка бота</b> <span class="mut">так її бачать у Telegram</span>
  <div style="display:flex;gap:14px;align-items:center;margin-top:10px">
   <canvas id="phprev" width="512" height="512" style="width:96px;height:96px;border-radius:50%;background:var(--bg);border:1px solid var(--bd);flex:none"></canvas>
   <div style="flex:1;min-width:0"><input type="file" id="f_bph" accept="image/*" style="max-width:100%">
    <div class="mut" style="margin-top:6px">Будь-яке фото чи картинка з комп'ютера або телефона: JPG, PNG, WebP. Сторінка сама виріже квадрат по центру й зменшить до 512×512.</div></div>
  </div>
  <div style="margin-top:10px"><button class="p" data-act="botphoto">Поставити картинку</button> <span id="phst" class="mut"></span></div><div id="phmsg"></div></div>
</section>
<section id="sg" class="hide">
 <div class="card" id="ngcard"><b>🌙 Ніч</b> <span class="mut" id="g_ngst"></span>
  <div class="flex" style="align-items:center;margin-top:8px"><span class="mut">Нічні години: з</span><input type="number" id="g_qFrom" min="0" max="23" style="max-width:80px;flex:none"><span class="mut">до</span><input type="number" id="g_qTo" min="0" max="23" style="max-width:80px;flex:none"><span class="mut">години</span></div>
  <div id="g_nLedRow"><label class="ck"><input type="checkbox" id="g_nLedOff"> Світлодіоди й стрічка вночі гаснуть</label>
   <div id="g_nLedPct"><label>Не гаснуть — уночі світять на <b id="g_stripNightV"></b> % від денної яскравості</label><input type="range" id="g_stripNight" min="0" max="100" step="5"></div></div>
  <div id="g_nLcdRow"><label class="ck"><input type="checkbox" id="g_nLcdOff"> Підсвітка дисплея вночі гасне <span class="mut">(екран показує далі — його видно, коли в приміщенні світло)</span></label>
   <div id="g_nLcdPct"><label>Не гасне — підсвітка вночі: <b id="g_lcdNightV"></b> %</label><input type="range" id="g_lcdNight" min="0" max="100" step="5"></div></div>
  <div id="g_nSndRow"><label class="ck"><input type="checkbox" id="g_qOn"> Уночі без звуку <span class="mut">(Telegram — як завжди: тихі нічні повідомлення кожен вибирає сам)</span></label></div>
  <div style="margin-top:10px"><button data-act="tled" data-w="night">Показати ніч — 15 с</button> <button data-act="tled" data-w="off">Звичайний режим</button></div>
  <div class="mut" style="margin-top:6px">Перевірки зі сторінки — «Показати…», «Спалахи», «Хвиля», перевірка дисплея — світять і вночі. Однакові години «з» і «до» — ночі немає.</div>
 </div>
 <div class="card"><b>Індикатор світла</b>
  <label>Що підключено</label>
  <select id="g_ind"><option value="none">Нічого</option><option value="two">Два світлодіоди: зелений і червоний</option><option value="strip">Адресна RGB-стрічка WS2812B — рекомендовано</option></select>
  <div id="g_two">
   <div class="flex"><div style="flex:1;min-width:140px"><label>Зелений — світло є, світить</label><select id="g_ledG"></select></div><div style="flex:1;min-width:140px"><label>Червоний — світла немає, блимає</label><select id="g_ledR"></select></div></div>
   <label class="ck"><input type="checkbox" id="g_ledLow"> Вмикаються низьким рівнем (спільний анод)</label>
   <label>Яскравість: <b id="g_ledBriV"></b> %</label><input type="range" id="g_ledBri" min="5" max="100" step="5">
   <div class="mut">Підключення: вивід → резистор 220–330 Ом → довга ніжка світлодіода; коротка ніжка → GND.</div>
  </div>
  <div id="g_strip">
   <div class="flex"><div style="flex:1;min-width:140px"><label>Вивід даних (DIN)</label><select id="g_stripPin"></select></div><div style="flex:1;min-width:140px"><label>Світлодіодів у стрічці</label><input type="number" id="g_stripN" min="1" max="60"></div></div>
   <label>Яскравість: <b id="g_stripBriV"></b> %</label><input type="range" id="g_stripBri" min="5" max="100" step="5">
   <div><label class="col"><input type="color" id="g_colOn"> світло є</label><label class="col"><input type="color" id="g_colOff"> світла немає</label><label class="col"><input type="color" id="g_colGen"> працює генератор</label><label class="col"><input type="color" id="g_colUnk"> визначається</label></div>
   <div class="flex"><div style="flex:1;min-width:150px"><label>Коли світло є</label><select id="g_stFxOn"><option value="0">Рівний колір</option><option value="1">Безперервна хвиля</option></select></div>
   <div style="flex:1;min-width:150px"><label>Коли світла немає</label><select id="g_stripFx"><option value="1">Дихає — плавно гасне й розгорається</option><option value="0">Блимає</option><option value="2">Біжить вогник</option><option value="3">Безперервна хвиля</option></select></div>
   <div style="flex:1;min-width:150px"><label>Коли працює генератор</label><select id="g_stFxGen"><option value="0">Рівний колір</option><option value="1">Безперервна хвиля</option></select></div></div>
   <div id="g_wvbox"><label>Хвиля: швидкість <b id="g_stWvSpdV"></b> з 10 <span class="mut">(1 — повільно, прохід 8 с; 10 — швидко, 0,8 с)</span></label><input type="range" id="g_stWvSpd" min="1" max="10" step="1">
   <label>Хвиля: інтенсивність <b id="g_stWvIntV"></b> % <span class="mut">(наскільки гасне між гребенями)</span></label><input type="range" id="g_stWvInt" min="10" max="100" step="5"></div>
   <label class="ck"><input type="checkbox" id="g_stripAnim"> Анімація подій: червоні спалахи, коли світло зникло; зелена хвиля, коли з'явилось</label>
   <div class="flex"><div style="flex:1;min-width:140px"><label>Порядок кольорів</label><select id="g_stripOrder"><option value="0">GRB — звичайний WS2812B</option><option value="1">RGB</option><option value="2">BGR</option></select></div><div style="flex:1;min-width:140px"><label>Межа струму, мА</label><input type="number" id="g_stripMa" min="50" max="2000" step="50"></div></div>
   <div class="mut" id="g_stripHint" style="margin-top:6px"></div>
  </div>
  <div id="g_blinkbox"><label>Період блимання й дихання, мс</label><input type="number" id="g_ledBlink" min="200" max="5000" step="100"></div>
  <div id="g_ledbtns" style="margin-top:10px"><button data-act="tled" data-w="green">Показати «світло є»</button> <button data-act="tled" data-w="red">Показати «немає»</button> <button class="g_stonly" data-act="tled" data-w="flash">Спалахи</button> <button class="g_stonly" data-act="tled" data-w="wave">Хвиля</button> <button data-act="tled" data-w="off">Звичайний режим</button>
  <div id="g_ledst" class="mut" style="margin-top:6px"></div></div>
 </div>
 <div class="card" id="lcdcard"><b>Дисплей Nokia 5110</b> <span class="mut">стан світла, графік відключень, сигнал</span>
  <label class="ck"><input type="checkbox" id="g_lcdOn"> Дисплей під'єднано</label>
  <div style="margin-top:8px;display:flex;gap:12px;align-items:flex-start;flex-wrap:wrap"><canvas id="lcdc" width="84" height="48" style="width:252px;height:144px;image-rendering:pixelated;background:#b2c49e;border:6px solid #3b4232;border-radius:8px;flex:none"></canvas>
   <div class="mut" style="flex:1;min-width:180px">Так виглядає екран зараз (знімок з пам'яті Сторожа, раз на 2 с) — навіть коли дисплей ще не під'єднано. Підсвітку знімок не показує.<div id="lcdst" style="margin-top:6px"></div></div></div>
  <div class="flex"><div style="flex:1;min-width:110px"><label>RST</label><select id="g_lcdRst"></select></div><div style="flex:1;min-width:110px"><label>CE (SCE)</label><select id="g_lcdCe"></select></div><div style="flex:1;min-width:110px"><label>DC (D/C)</label><select id="g_lcdDc"></select></div></div>
  <div class="flex"><div style="flex:1;min-width:110px"><label>DIN (MOSI)</label><select id="g_lcdDin"></select></div><div style="flex:1;min-width:110px"><label>CLK (SCLK)</label><select id="g_lcdClk"></select></div><div style="flex:1;min-width:110px"><label>BL (LIGHT)</label><select id="g_lcdBl"></select></div></div>
  <label class="ck"><input type="checkbox" id="g_lcdBlLow"> Підсвітка вмикається низьким рівнем: поставте, якщо повзунок підсвітки діє навпаки — більше відсотків, а світить тьмяніше (червона плата: LIGHT на GND світить)</label>
  <label>Контраст: <b id="g_lcdVopV"></b> <span class="mut">(блідо — більше, сіре тло — менше; зазвичай 50–65)</span></label><input type="range" id="g_lcdVop" min="10" max="127" step="1">
  <label>Зсув (bias) <span class="mut">— напруга для рідких кристалів; типово 4. «Усе чорне» не темніє за жодного контрасту — спробуйте 3 чи 5 або «Пошук контрасту»</span></label><select id="g_lcdBias"><option value="2">2</option><option value="3">3</option><option value="4">4 — типово</option><option value="5">5</option><option value="6">6</option></select>
  <label>Підсвітка: <b id="g_lcdBriV"></b> % <span class="mut">(уночі — картка «🌙 Ніч» угорі)</span></label><input type="range" id="g_lcdBri" min="0" max="100" step="5">
  <label class="ck"><input type="checkbox" id="g_lcdInv"> Інверсія: світлі літери на темному</label>
  <label class="ck"><input type="checkbox" id="g_lcdFlip"> Перевернути на 180° (дисплей закріплено догори дриґом)</label>
  <label>Графік на дисплеї — біжучий рядок: швидкість <b id="g_lcdMqV"></b> <span class="mut">(0 — сторінками по 3 с; 1 — повільно, 10 — швидко)</span></label><input type="range" id="g_lcdMq" min="0" max="10" step="1">
  <label class="ck"><input type="checkbox" id="g_lcdFx"> Ефекти сповіщень: світло зникло чи повернулось — спалахи й миготіння, генератор — миготіння, попередження за графіком — рядок навиворіт</label>
  <div class="flex"><div style="flex:1;min-width:150px"><label>Скільки блимати, с</label><input type="number" id="g_lcdFxSec" min="3" max="600"></div><div style="flex:1;min-width:150px"><label>Оголошення на дисплеї, хв (0 — ні)</label><input type="number" id="g_lcdAnnMin" min="0" max="240"></div></div>
  <div class="flex" style="margin-top:6px"><button data-act="lcdfx" data-k="1">Ефект «світло зникло»</button><button data-act="lcdfx" data-k="2">«Повернулось»</button><button data-act="lcdfx" data-k="4">Попередження</button><button data-act="lcdfx" data-k="5">Пробне оголошення</button></div>
  <div class="mut" style="margin-top:6px">Підключення (живлення — лише 3,3 В!): RST → GPIO4 (D4), CE → GPIO16 (RX2), DC → GPIO17 (TX2), DIN → GPIO21 (D21), CLK → GPIO22 (D22), VCC → 3V3, BL → GPIO33 (D33), GND → GND. Дроти — до 30 см. Зміни на цій вкладці діють і зберігаються одразу.</div>
  <div style="margin-top:10px"><b>Перевірка дисплея</b> <span class="mut">— на 60 с, нічого не зберігає</span></div>
  <div class="flex" style="margin-top:6px"><button data-act="lcdt" data-m="all">Усе чорне</button><button data-act="lcdt" data-m="check">Шахівниця</button><button data-act="lcdt" data-m="normal">Звичайний вигляд</button><button data-act="lcdt" data-m="sweep">Пошук контрасту</button><button data-act="lcdhere">Темніє зараз!</button><button data-act="lcdt" data-m="end">Закінчити</button><button data-act="lcdwire">Перевірити дроти</button></div>
  <div class="mut" style="margin-top:6px">«Пошук контрасту»: екран «усе чорне», а контраст сам іде від 20 до 127 (≈ 16 с), далі з іншим зсувом — дивіться на дисплей і, щойно потемніє, тисніть «Темніє зараз!»: контраст і зсув стануть у повзунок, лишиться «Зберегти». Під час перевірки повзунки контрасту й підсвітки, зсув і галочка «низьким рівнем» діють на дисплей одразу. «Усе чорне» має зробити темним увесь екран — так видно, що дисплей отримує команди. Блідо чи нічого — рухайте контраст (зазвичай 50–65). Нічого за будь-якого контрасту — перевірте дроти (особливо DIN, CLK, CE, DC) і притисніть металеву рамку дисплея до плати: у дешевих модулях гумова смужка між склом і платою погано контактує. Підібрали — «Зберегти».</div>
  <div id="lcdtmsg"></div><div id="lcdwres" class="tbl" style="margin-top:6px"></div>
 </div>
 <div class="card"><b>Звук</b>
  <label>Що підключено</label>
  <select id="g_sndType"><option value="0">Нічого</option><option value="1">Активний бузер — пищить сам від напруги</option><option value="2">Пасивний бузер — грає мелодії</option><option value="3">Динамік через ЦАП і підсилювач — найкращий звук, свої звуки</option></select>
  <div id="g_pinbox"><label>Вивід бузера</label><select id="g_sndPin"></select></div>
  <div id="g_dacbox"><label>Вивід ЦАП</label><select id="g_dacPin"><option value="25">GPIO25 (на платі D25) — ЦАП 1</option><option value="26">GPIO26 (на платі D26) — ЦАП 2</option></select></div>
  <div id="g_sndrest">
   <label>Гучність: <b id="g_sndVolV"></b> %</label><input type="range" id="g_sndVol" min="0" max="100" step="5">
   <label class="ck"><input type="checkbox" id="g_sndLost"> Коли світло зникло</label>
   <div class="flex" style="align-items:center"><span class="mut">повторів</span><input type="number" id="g_sndRep" min="1" max="10" style="max-width:80px;flex:none"><span class="mut">через 3 с; світло повернулось — повтори припиняються</span></div>
   <label class="ck"><input type="checkbox" id="g_sndBack"> Коли світло з'явилось</label>
   <label class="ck"><input type="checkbox" id="g_sndBoot"> І після перезапуску Сторожа</label>
   <div class="mut">Уночі без звуку — картка «🌙 Ніч» угорі.</div>
   <div id="g_wire" class="mut" style="margin-top:8px"></div>
   <div style="margin-top:10px"><button data-act="tsnd" data-w="lost">Програти «світло зникло»</button> <button data-act="tsnd" data-w="back">Програти «світло є»</button></div>
  </div>
  <div id="g_sndst" class="mut" style="margin-top:6px"></div>
 </div>
 <div class="card" id="g_patbox"><b>Сигнали активного бузера</b>
  <div class="mut">Числа в мілісекундах через кому: звук, пауза, звук… Наприклад, 700,300,700 — два довгі.</div>
  <label>Світло зникло</label><input id="g_patLost" maxlength="300">
  <label>Світло з'явилось</label><input id="g_patBack" maxlength="300">
 </div>
 <div class="card" id="g_melbox"><b>Мелодії пасивного бузера</b>
  <div class="mut">Ноти через пробіл, кожна «нота:мс»: C D E F G A B з октавою (C5), дієз — C#5, бемоль — Bb4; R:200 — пауза; 880:200 — частота в Гц.</div>
  <label>Світло зникло</label><input id="g_melLost" maxlength="300">
  <label>Світло з'явилось</label><input id="g_melBack" maxlength="300">
 </div>
 <div class="card" id="g_dsbox"><b>Звуки динаміка</b>
  <div class="mut">Вбудовані звуки Сторож синтезує сам: «дзвін» — три удари вниз, коли світло зникло, і чотири вгору, коли з'явилось; «м'який сигнал» — спокійніший. Свій звук — будь-який аудіофайл з комп'ютера (MP3, WAV, M4A, OGG): сторінка сама переведе його у звук для ЦАП (16 кГц, до 3 секунд), дасть прослухати й завантажить на Сторожа.</div>
  <label>Світло зникло</label><select id="g_srcLost"><option value="0">Дзвін</option><option value="1">М'який сигнал</option><option value="2">Свій звук</option></select>
  <div class="flex" style="margin-top:6px;align-items:center"><input type="file" id="g_fLost" accept="audio/*" style="max-width:100%"><button data-act="sndplay" data-w="0">Прослухати</button><button data-act="sndup" data-w="0">Завантажити</button><button data-act="snddel" data-w="0">Видалити</button></div>
  <div id="g_upLost" class="mut"></div><div id="g_infLost" class="mut"></div>
  <label>Світло з'явилось</label><select id="g_srcBack"><option value="0">Дзвін</option><option value="1">М'який сигнал</option><option value="2">Свій звук</option></select>
  <div class="flex" style="margin-top:6px;align-items:center"><input type="file" id="g_fBack" accept="audio/*" style="max-width:100%"><button data-act="sndplay" data-w="1">Прослухати</button><button data-act="sndup" data-w="1">Завантажити</button><button data-act="snddel" data-w="1">Видалити</button></div>
  <div id="g_upBack" class="mut"></div><div id="g_infBack" class="mut"></div>
 </div>
 <div class="card"><button class="p" data-act="sigsave">Зберегти</button> <span class="mut">Звук перевірки грає з гучністю повзунка; решта перевірок — збережене: змінили вивід чи звук, то спершу «Зберегти».</span><div id="g_msg"></div></div>
</section>
<section id="set" class="hide">
 <div class="card"><b>Об'єкт</b><label>Назва в повідомленнях</label><input id="f_obj" maxlength="40"></div>
 <div class="card"><b>Wi-Fi роутера</b>
  <label>Мережа</label><div class="flex"><input id="f_staSsid" maxlength="32"><button data-act="scan">Знайти</button></div>
  <select id="scanl" class="hide"></select>
  <label>Точка роутера (BSSID)</label><input id="f_staBssid" maxlength="17" placeholder="будь-яка: найсильніша">
  <div class="mut">Вибір мережі в списку «Знайти» підставляє адресу саме цієї точки. Сторож триматиметься її, а коли вона недоступна, тимчасово перейде на найсильнішу й повернеться, щойно вона знову з'явиться. Порожньо — будь-яка точка з цією назвою.</div>
  <label>Пароль <span id="h_staPass"></span></label><input id="f_staPass" type="password" placeholder="не змінювати" autocomplete="new-password">
  <label class="ck" style="margin-top:10px"><input type="checkbox" id="f_mdnsOn"> Назва http://avr-mist.local у мережі роутера (mDNS)</label>
  <div class="mut">Типово вимкнено: назва забирає близько 8 КБ пам'яті Сторожа. Сторінку відкривають за адресою з рядка «Роутер» на вкладці «Стан» (закріпіть її за Сторожем у роутері) або через KeenDNS.</div>
 </div>
 <div class="card"><b>Точка доступу для АВР</b>
  <label>Назва</label><input id="f_apSsid" maxlength="32">
  <label>Пароль, 8 і більше символів</label><input id="f_apPass" type="password" placeholder="не змінювати" autocomplete="new-password">
  <div class="mut">Після зміни назви чи пароля точки АВР треба знову підключити в застосунку eWeLink.</div>
 </div>
 <div class="card"><b>АВР</b>
  <label>MAC-адреса АВР</label><input id="f_avrMac" placeholder="aa:bb:cc:dd:ee:ff">
  <label class="ck"><input type="checkbox" id="f_notifyStart"> Повідомляти адміністраторам у Telegram, коли Сторож перезапустився несподівано (збій, сторож, зникало живлення) — з причиною. Після оновлення прошивки й перезапуску зі сторінки не пише</label>
 </div>
 <div class="card" id="bcncard"><b>Маячки</b> <span id="bcncnt" class="mut"></span>
  <div class="mut" style="margin-top:4px">Маячок — плата ESP32-C3 у розетці: кожні 2 секунди каже Сторожу «я тут» через мережу для АВР. Що означає його мовчання, задає роль:<br>
  <b>Генератор</b> — маячок у розетці від генератора: на зв'язку — генератор працює, мовчить — стоїть. Таких можна кілька, про запас.<br>
  <b>Лінія чи фаза</b> — у розетці потрібної лінії (фаза, корпус, котельня): мовчить — на ній немає напруги. У Telegram прийде «🔌» тим, у кого позначено «Маячки».<br>
  <b>Лише показувати</b> — видно тут і в «🛠 Сторож» у боті, повідомлень немає.<br>
  Усі пристрої Сторожа і чи вони на зв'язку — на вкладці «Стан», картка «Пристрої»; маячок, що вже шле «я тут», можна додати звідти кнопкою «Додати маячок».</div>
  <div id="bcnleg" class="msg hide"></div>
  <div id="bcn" style="margin-top:6px"></div>
  <label>Додати маячок за MAC-адресою — якщо його зараз немає на зв'язку (адресу видно в журналі Сторожа й у «🛠 Сторож» у боті)</label>
  <div class="flex"><input id="b_mac" placeholder="aa:bb:cc:dd:ee:ff" maxlength="17" style="max-width:180px"><input id="b_name" maxlength="24" placeholder="назва, напр. Фаза B" style="min-width:140px"><select id="b_role" style="max-width:170px"><option value="g">Генератор</option><option value="l">Лінія чи фаза</option><option value="v">Лише показувати</option></select><button data-act="bcnadd">Додати</button></div>
  <div id="bcnmsg"></div></div>
 <div class="card" id="schset"><b>Графік відключень YASNO</b>
  <label class="ck"><input type="checkbox" id="f_schOn"> Брати графік відключень з YASNO: показувати на сторінці й у боті («🗓 Графік»), попереджати перед відключенням, у повідомленнях писати, за графіком відключення чи ні</label>
  <div class="flex"><div style="flex:2;min-width:200px"><label>Оператор</label><select id="f_schDso"><option value="3/301">Дніпро — ДнЕМ (ДТЕК Дніпровські електромережі)</option><option value="3/303">Дніпро — ЦЕК</option><option value="25/902">Київ — ДТЕК Київські електромережі</option></select></div>
  <div style="flex:1;min-width:120px"><label>Черга (підчерга)</label><select id="f_schQueue"></select></div></div>
  <div class="mut" style="margin-top:6px">Свою чергу можна дізнатися за адресою на сайті yasno.ua (розділ «Графік відключень») або в застосунку YASNO. Графік Сторож бере раз на 15 хвилин і тримає лише в пам'яті.</div>
  <label>Попереджати в Telegram за … хвилин до відключення за графіком (0 — не попереджати)</label><input id="f_schWarn" type="number" min="0" max="180">
  <label class="ck"><input type="checkbox" id="f_schTom"> Надсилати в Telegram графік на завтра, щойно YASNO його оприлюднить, і зміни графіка (без звуку); аварійні відключення — зі звуком</label>
  <div style="margin-top:10px"><button data-act="schfetch">Оновити графік зараз</button> <span id="schst" class="mut"></span></div>
  <div class="mut" style="margin-top:6px">Щоб зміни почали діяти, натисніть «Зберегти» внизу сторінки.</div></div>
 <div class="card"><b>Затримки, секунди</b>
  <label>АВР мовчить (не відповідає) стільки секунд — «міста немає». Рекомендовано 10–15: так повідомлення приходить за 10–15 с після відключення</label><input id="f_offSec" type="number" min="5" max="600">
  <label>АВР знову в мережі, і це означає «місто є»</label><input id="f_onSec" type="number" min="3" max="300">
  <label>Маячок мовчить, і це означає «генератор зупинився» чи «на лінії немає напруги»</label><input id="f_lineOffSec" type="number" min="5" max="600">
  <label>Маячок на зв'язку, і це означає «генератор працює» чи «на лінії є напруга»</label><input id="f_lineOnSec" type="number" min="3" max="300">
  <label>Місто є, а генератор ще працює: нагадувати «вимкніть генератор» кожні … хв (0 — лише раз), до 6 разів</label><input id="f_genRemMin" type="number" min="0" max="240">
 </div>
 <div class="card"><b>Пароль сторінки</b> <span class="mut">користувач admin</span> <span id="h_adm" class="mut"></span>
  <input id="f_adminPass" type="password" placeholder="не змінювати" autocomplete="new-password" style="margin-top:8px">
  <label>Окрема кнопка скидання пароля</label><select id="f_pwBtn"></select>
  <div class="mut" id="h_pwBtn"></div></div>
 <div class="card"><button class="p" data-act="save">Зберегти</button> <button data-act="reboot">Перезапустити Сторожа</button><div id="smsg"></div></div>
 <div class="card"><b>Скидання</b>
  <div class="mut" style="margin-top:6px">«Скинути налаштування» повертає типові: назву об'єкта, затримки, маячок, повідомлення про запуск, щоденний підсумок, сигнали (світлодіоди, стрічка, звук) і вивід кнопки скидання пароля. Wi-Fi роутера, мережа для АВР, сам АВР, бот з одержувачами й пароль сторінки лишаються — Сторож працює далі без перезапуску.</div>
  <div><button data-act="rprefs">Скинути налаштування</button></div>
  <div class="mut" style="margin-top:14px">«Скинути все» — як нова плата, те саме, що BOOT 10 секунд: Сторож забуде Wi-Fi роутера, мережу для АВР, АВР, бота й одержувачів, пароль сторінки, свої звуки. Перед тим одержувачам прийде «Сторожа скинуто до заводських налаштувань». За 10 секунд він перезапуститься й роздаватиме мережу «<b id="r_ap"></b>» з типовим паролем <b id="r_pw"></b>; сторінка — http://10.77.0.1, вхід admin з тим самим паролем. АВР доведеться підключити заново в eWeLink (інструкція, розділ 3). Статистика світла лишиться.</div>
  <label>Щоб скинути все, введіть слово СКИНУТИ</label><div class="flex"><input id="r_word" autocomplete="off" maxlength="20"><button class="d" data-act="rall">Скинути все</button></div>
  <div id="rmsg2"></div>
 </div>
</section>
<section id="log" class="hide"><div class="card mut" id="memline"></div><div class="card"><div class="tbl"><table id="lg"></table></div></div></section>
<section id="ins" class="hide">
 <div class="card"><b>Інструкція: схеми підключення</b>
  <div class="mut" style="margin-top:4px">Як підключити до Сторожа світлодіоди чи стрічку, бузер чи динамік, кнопку скидання пароля, дисплей Nokia 5110, де ставити маячки й чим живити. Номери виводів на схемах — як задано на вкладці «Сигнали». <a href="/schemes" target="_blank" rel="noopener">Відкрити окремою сторінкою</a> — так зручніше друкувати.</div></div>
 <iframe id="insf" title="Схеми підключення" style="width:100%;height:2400px;border:0;display:block"></iframe>
</section>
<section id="upd" class="hide"><div class="card"><b>Оновлення прошивки</b><div class="mut" id="ver"></div>
 <input type="file" id="fw" accept=".bin" style="margin-top:10px"><div style="margin-top:10px"><button class="p" data-act="upload">Завантажити</button></div><div id="umsg" class="msg hide"></div></div></section>
</main>
<script>
const $=id=>document.getElementById(id);
let tab='st';
const tabs=['st','tg','sg','set','log','ins','upd'];let lastTg=null,tokSet=false;
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{tab=b.dataset.t;document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('sel',x===b));tabs.forEach(t=>$(t).classList.toggle('hide',t!==tab));if(tab==='set')loadSet();if(tab==='tg')loadTg();if(tab==='sg')loadSig();if(tab==='ins')$('insf').src='/schemes?'+Date.now();refresh();});
window.addEventListener('message',e=>{if(e.origin===location.origin&&e.data&&e.data.schemesH)$('insf').style.height=(e.data.schemesH+16)+'px'});
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function hrs(s){s=Math.max(0,s|0);const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?(m?h+' год '+m+' хв':h+' год'):(m?m+' хв':'менше хвилини')}
function ago(s){if(s==null)return '—';s=Math.max(0,s|0);if(s<60)return s+' с';if(s<3600)return Math.floor(s/60)+' хв';if(s<86400)return Math.floor(s/3600)+' год '+Math.floor(s%3600/60)+' хв';return Math.floor(s/86400)+' д '+Math.floor(s%86400/3600)+' год'}
function note(id,text,bad){const e=$(id);e.className='msg'+(bad?' err':'');e.textContent=text}
function relogin(){let t=0;try{t=+sessionStorage.getItem('rl')||0}catch(e){}if(Date.now()-t<10000)return;try{sessionStorage.setItem('rl',String(Date.now()))}catch(e){}location.reload()}
async function j(u,o){const r=await fetch(u,Object.assign({credentials:'same-origin'},o));if(r.status===401){relogin();throw new Error('Потрібен вхід: оновіть сторінку')}const t=await r.text();let d;try{d=JSON.parse(t)}catch(e){d={err:t}}if(!r.ok)throw new Error(d.err||('HTTP '+r.status));return d}
async function refresh(){try{render(await j('/api/state'+(tab==='log'?'?log=1':'')))}catch(e){$('status').textContent=e.message.startsWith('Потрібен вхід')?e.message:'Немає зв\'язку зі Сторожем';$('dot').className='dot'}}
//  1.0.42: «Пристрої». Власник 23.11.2026: «должен быть четкий список того что подвязано и подключено а также устройства
//  которые есть в сети для подключения (которые он знает — авр, маяк, бмс или другое)». До того тут була лише таблиця
//  клієнтів точки: прив'язаний АВР без зв'язку й маячок, що мовчить, у ній не з'являлись, а міст батареї й не доданий
//  маячок стояли як «невідомі» з кнопкою «Це АВР».
function renderDev(d){
 const w=d.watch,n=d.net,BL=(w.bcn&&w.bcn.list)||[],cl=n.clients||[],you=d.you||'',bat=d.bat,C={};
 for(const x of cl)C[x.mac]=x;
 const avr=(w.avr.mac||'').toLowerCase(),bm=bat&&bat.mac?bat.mac.toLowerCase():'';
 const ok=t=>'<b style="color:var(--ok);white-space:nowrap">● '+t+'</b>',bad=t=>'<b class="err" style="white-space:nowrap">● '+t+'</b>',gr=t=>'<b class="mut" style="white-space:nowrap">● '+t+'</b>',sm=t=>t?'<br><span class="mut">'+t+'</span>':'';
 const ad=(m,x)=>m?'<br><span class="mut" style="font-size:12px">'+esc(m)+(x&&x.ip?' · '+esc(x.ip):'')+'</span>':'',sg=x=>x&&x.assoc?x.rssi+' дБм':'';
 const isY=x=>!!(x&&x.assoc&&x.ip&&x.ip===you),isR=m=>(parseInt(m.slice(0,2),16)&2)!==0;
 let h='<tr class="mut"><td>Пристрій · MAC, адреса</td><td>Стан</td></tr>',nb=0,on=0;
 const s=n.sta||{};
 h+='<tr><td><b>Роутер</b>'+sm(esc(s.ssid||'не задано')+' · інтернет')+ad(s.bssid||'',null)+'</td><td>'+(s.up?ok('під\'єднано')+sm('сигнал '+s.rssi+' дБм'+(s.pref===false?' · <b class="err">не бажана точка</b>':'')+(s.ip?' · адреса Сторожа '+esc(s.ip):'')):bad('немає зв\'язку'))+'</td></tr>';
 if(!w.avr.assigned)h+='<tr><td><b>АВР</b></td><td>'+bad('не прив\'язано')+sm('підключіть АВР у застосунку eWeLink до точки «'+esc(n.ap.ssid)+'» і натисніть «Це АВР» у його рядку нижче')+'</td></tr>';
 else{const x=C[avr];let st;nb++;
  if(x&&x.assoc){on++;st=x.ping!=null&&x.ping<=10?ok('на зв\'язку')+sm('у мережі, відповідає на ping · '+sg(x)):gr('у мережі, на ping не відповідає')+sm(sg(x))}
  else st=bad('не на зв\'язку')+sm((x?'від\'єднався '+ago(x.seen)+' тому':'не під\'єднувався від запуску Сторожа')+(w.grid.st===0?' · міського світла немає'+(w.grid.exact?' з '+esc(w.grid.at):''):''));
  if(isY(x))st+=sm('<b class="err">це пристрій, з якого відкрито сторінку, — не АВР</b>');
  else if(isR(avr))st+=sm('<b class="err">адреса схожа на телефон чи комп\'ютер, а не на АВР</b>');
  h+='<tr><td><b>АВР</b>'+sm('прив\'язано')+ad(avr,x)+'</td><td>'+st+'</td></tr>'}
 const RN={g:'генератор',l:'лінія чи фаза',v:'лише показувати'};
 for(const b of BL){if(!b.cfg)continue;nb++;const x=C[b.mac];
  const st=b.seen&&b.alive?(on++,ok('на зв\'язку')+sm('вістка '+ago(b.ago)+' тому · '+b.rssi+' дБм'+(b.ver?' · версія '+esc(b.ver):''))):
   b.seen?bad('мовчить')+sm('востаннє '+ago(b.ago)+' тому'):bad('не на зв\'язку')+sm('не озивався від запуску Сторожа');
  h+='<tr><td><b>Маячок «'+esc(b.name||b.mac.slice(9))+'»</b>'+sm('прив\'язано · '+(RN[b.role]||esc(b.role)))+ad(b.mac,x)+'</td><td>'+st+'</td></tr>'}
 if(bm){nb++;const x=C[bm],BS=['шукає BMS — її не чути','під\'єднується до BMS','BMS на зв\'язку','BMS не шле стану','пауза перед новою спробою під\'єднатись до BMS'];
  const st=bat.bridge?(on++,ok('на зв\'язку')+sm((bat.ver?'версія '+esc(bat.ver)+' · ':'')+bat.rssi+' дБм · '+(bat.ok?'BMS «'+esc(bat.name||'')+'» на зв\'язку'+(bat.soc!=null?', '+bat.soc+' %':''):(BS[bat.st]||'BMS не на зв\'язку')))):
   bad('не на зв\'язку')+sm('востаннє '+ago(bat.ago)+' тому');
  h+='<tr><td><b>Міст батареї</b>'+sm('прив\'язується сам: шле Сторожу дані BMS')+ad(bm,x)+'</td><td>'+st+'</td></tr>'}
 $('dvb').innerHTML=h;
 $('dvcnt').textContent='· прив\'язано '+nb+', на зв\'язку '+on;
 const bound=new Set([avr,bm].concat(BL.filter(b=>b.cfg).map(b=>b.mac))),BU={};
 for(const b of BL)if(!b.cfg)BU[b.mac]=b;
 const ms=[...new Set(cl.map(x=>x.mac).concat(Object.keys(BU)))].filter(m=>m&&!bound.has(m));
 ms.sort((a,b)=>((C[b]&&C[b].assoc)?1:0)-((C[a]&&C[a].assoc)?1:0));
 let o='',unk=0,oth=0;
 for(const m of ms){const x=C[m],b=BU[m];let what,act='';
  if(b){what='<b>маячок</b>'+sm('шле Сторожу «я тут», але його немає в списку');act='<button class="p" data-act="bcnpick" data-mac="'+m+'">Додати маячок</button>'}
  else if(isY(x))what='<b>цей пристрій</b>'+sm('з нього відкрито сторінку');
  else if(isR(m))what='<b>телефон чи комп\'ютер</b>';
  else if(w.avr.assigned){what='<b>інший пристрій</b>'+sm('не маячок і не міст батареї');act='<button data-act="assign" data-mac="'+m+'" data-old="'+esc(avr)+'">Це новий АВР</button>';oth++}
  else{what='<b>невідомий</b>'+sm('може бути АВР');act='<button class="p" data-act="assign" data-mac="'+m+'">Це АВР</button>';if(x&&x.assoc)unk++}
  const st=x&&x.assoc?ok('у мережі')+sm(sg(x)):b&&b.alive?ok('на зв\'язку')+sm('вістка '+ago(b.ago)+' тому'):gr('від\'єднався')+sm((x?ago(x.seen):b?ago(b.ago):'—')+' тому');
  o+='<tr><td>'+what+ad(m,x)+(act?'<div style="margin-top:4px">'+act+'</div>':'')+'</td><td>'+st+'</td></tr>'}
 $('cl').innerHTML=o||'<tr><td class="mut">Інших пристроїв у мережі Сторожа немає.</td></tr>';
 const aY=w.avr.assigned&&isY(C[avr]);
 $('clhint').textContent=aY?'АВР-ом зараз призначено пристрій, з якого відкрито цю сторінку. Справжній АВР з\'явиться окремим рядком, коли його підключать у застосунку eWeLink до точки «'+n.ap.ssid+'».':
  !w.avr.assigned&&unk===1?'У мережі Сторожа один невідомий пристрій. Якщо АВР щойно підключали в eWeLink, це він: натисніть «Це АВР».':
  (!w.avr.assigned&&!unk?'Підключіть АВР у застосунку eWeLink до точки «'+n.ap.ssid+'».':
  oth?'АВР уже прив\'язано ('+avr+'). «Це новий АВР» — лише якщо АВР замінили: тоді Сторож стежитиме за новим, а не за старим.':'');
}
function render(d){
 const w=d.watch,g=w.grid.st,l=w.line.st,le=w.line.enabled;
 $('obj').textContent='· '+w.obj;
 $('status').textContent=w.status.charAt(0).toUpperCase()+w.status.slice(1);
 $('dot').className='dot '+(g===1?(le&&l===1?'gen':'on'):g===0?(le&&l===1?'gen':'off'):'');
 $('since').textContent=w.avr.assigned&&g>=0?((w.grid.exact?'з '+w.grid.at+', уже ':'щонайменше ')+ago(w.grid.ago)):'';
 let r='';const row=(a,b)=>{r+='<div class="row"><span>'+a+'</span><span>'+b+'</span></div>'};
 row('Місто: АВР на зв\'язку',!w.avr.assigned?'АВР не призначено':g===1?'є':g===0?'немає':'визначається');
 const st2=d.stats||{};if(st2.ok)row('Сьогодні','🟢 '+hrs(st2.on)+' · 🔴 '+hrs(st2.off)+' · відключень '+st2.outs);
 if(st2.text)$('sttext').textContent=st2.text;
 if(le)row('Генератор: маячок',l===1?(g===1?'<b class="err">ще працює — вимкніть</b>':'працює'):l===0?'не працює':'визначається');
 const BL=(w.bcn&&w.bcn.list)||[];
 for(const b of BL){if(!b.cfg||b.role==='g')continue;const nm=esc(b.name||('маячок '+b.mac.slice(9)));
  if(b.role==='l'){const t=b.t||{st:-1},sn=(t.exact?'з '+esc(t.at)+', ':'щонайменше ')+ago(t.ago);row('Лінія «'+nm+'»',t.st===1?'є напруга · '+sn:t.st===0?'<b class="err">немає напруги</b> · '+sn:'визначається')}
  else row('Маячок «'+nm+'»',b.seen&&b.alive?'на зв\'язку, '+b.rssi+' дБм':b.seen?'мовчить '+ago(b.ago):'не чув від запуску Сторожа')}
 const st=d.net.sta;
 row('Роутер',st.up?esc(st.ssid)+', '+st.rssi+' дБм, '+st.ip+(st.bssid?', точка '+esc(st.bssid)+(st.pref===false?' <b class="err">не бажана</b>':''):''):(st.ssid?esc(st.ssid)+': немає зв\'язку':'не налаштовано'));
 row('Мережа для АВР',esc(d.net.ap.ssid)+', канал '+d.net.ap.ch+(st.up?(d.net.ap.nat?', інтернет для пристроїв є':', <b class="err">пристрої без інтернету: NAT вимкнено</b>'):''));
 const t=d.tg;let ts;
 if(!t.token)ts='бот не підключено: вкладка «Сповіщення»';else if(!t.recipients)ts='немає одержувачів';
 //  1.0.32: «помилка» — лише поки зв'язку справді немає (невдачі поспіль); минулий збій — окремо, сірим
 else if(t.fails>0)ts='<b class="err">немає зв\'язку</b>: '+esc(t.lastErr)+(t.fails>1?' ('+t.fails+' спроб поспіль)':'');
 else{ts='працює'+(t.anyOkAgo!=null?', відповів '+ago(t.anyOkAgo)+' тому':'')+(t.lastOkAgo!=null?' · надіслано '+ago(t.lastOkAgo)+' тому':'');
  if(t.lastErr&&t.lastErrAgo!=null&&t.lastErrAgo<6*3600)ts+=' <span class="mut">· збій '+ago(t.lastErrAgo)+' тому минув</span>'}
 if(t.queue)ts+=', у черзі '+t.queue;
 row('Telegram',ts+(t.token&&t.recipients?' · одержувачів '+t.recipients:''));
 row('Годинник',esc(d.clock));
 row('Міст',esc(d.ver)+', працює '+ago(d.up)+', пам\'яті вільно '+Math.round(d.heap/1024)+' КБ'+(d.recentHeap!=null?' (найменше за 10 хв — '+Math.round(d.recentHeap/1024)+')':''));
 $('rows').innerHTML=r;
 renderBat(d.bat);
 renderSch(d.sch);
 renderDev(d);                                   // 1.0.42: картка «Пристрої»
 if(d.mem){const m=d.mem,k=x=>Math.round(x/1024),S=m.stk||{},N={loopTask:'основна',tg:'Telegram',snd:'звук',selfchk:'самоперевірка',ping:'ping'};
  $('memline').innerHTML='<b>Пам\'ять</b>: вільно '+k(m.free)+' КБ, найбільший цілий шматок '+k(m.block)+' КБ, найменше від запуску '+k(m.min)+' КБ, за 10 хв — '+k(m.recent)+' КБ'+
   ' · журнал: '+m.log.n+' рядків, '+k(m.log.used)+' з '+k(m.log.cap)+' КБ'+(m.log.iram?' у пам\'яті команд (звичайної не займає)':' у звичайній пам\'яті')+
   ' · Wi-Fi: '+(m.wifi.tuned?'ощадний режим, буферів '+m.wifi.rx+' і '+m.wifi.tx:'звичайний режим, буферів '+m.wifi.rx+' і '+m.wifi.tx)+
   ' · запас стека задач, Б: '+Object.keys(S).map(n=>(N[n]||n)+' '+S[n]).join(', ')+
   (m.boot&&m.boot.length?' · на запуску, КБ: '+m.boot.map(b=>esc(b[0])+' '+k(b[1])).join(' → '):'')+
   (d.drops?' · обірвано відповідей сторінки (клієнт не приймав): '+d.drops:'')+
   (d.crash?' · <b>дамп останнього збою</b> у флеші: '+Math.max(1,k(d.crash))+' КБ — <a href="/api/crash.bin">завантажити</a> (для розбору розробником)':'')}
 if(d.log)$('lg').innerHTML=d.log.map(e=>'<tr><td class="mut" style="white-space:nowrap">'+esc(e.t)+'</td><td>'+esc(e.x)+'</td></tr>').join('');
 $('ver').textContent='Зараз: '+d.ver+', збірка '+d.build;
 let s='';
 lastTg=t;
 for(const x of t.seen){const g=String(x.id).startsWith('-');s+='<div style="margin:6px 0">'+esc(x.name||'без назви')+' <span class="mut">'+(g?'група':'людина')+', '+ago(x.ago)+' тому: «'+esc(x.text)+'»</span> '+(x.rcp?'<span class="mut">— уже отримує</span>':'<button data-act="add" data-id="'+esc(x.id)+'" data-name="'+esc(x.name)+'">Додати</button>')+'</div>'}
 $('seen').innerHTML=s?'<b>Писали боту:</b>'+s:'<span class="mut">Поки ніхто не писав боту.</span>';
 document.querySelectorAll('.botn').forEach(e=>e.textContent=t.bot?'@'+t.bot:'вашого бота');
 document.querySelectorAll('.botu').forEach(e=>e.textContent=t.bot||'ім\'я_бота');
 document.querySelectorAll('.botl').forEach(e=>e.innerHTML=t.bot?'<a href="https://t.me/'+esc(t.bot)+'" target="_blank" rel="noopener">t.me/'+esc(t.bot)+'</a>':'з\'явиться, щойно бот підключиться');
 botStatus();
 if(d.sig&&tab==='sg'){sigStatus(d.sig);loadLcd()}
 $('phst').textContent=t.photo?'Остання: '+t.photo:'';
 $('bnst').textContent=t.nameState?'Назва: '+t.nameState:(t.title?'Зараз: «'+t.title+'»':'');
 if(t.title&&!$('f_botname').value&&document.activeElement!==$('f_botname'))$('f_botname').value=t.title;
}
//  Графік відключень YASNO (1.0.20): смужки на добу, червоне — відключення, жовте — можливе; риска — зараз
function hm(m){m=Math.max(0,Math.min(1440,m|0));return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0')}
function schDay(x,now,title){if(!x||!x.have)return '<div class="sday"><b>'+title+'</b> <span class="mut">'+(x?x.date:'')+' · даних немає</span></div>';
 const st={1:'',2:'графік ще не оприлюднено',3:'аварійні відключення — графік не діє'}[x.st];
 let bars='',txt=[],off=0;
 if(x.st===1)for(const s of x.sl){if(!s[2])continue;bars+='<i class="'+(s[2]===2?'p':'')+'" style="left:'+(s[0]/14.4)+'%;width:'+((s[1]-s[0])/14.4)+'%"></i>';txt.push((s[2]===2?'можливо ':'')+hm(s[0])+'–'+hm(s[1]));if(s[2]===1)off+=s[1]-s[0]}
 if(now!=null)bars+='<b style="left:'+(now/14.4)+'%"></b>';
 return '<div class="sday"><b>'+title+'</b> <span class="mut">'+esc(x.date)+'</span>'+(st===undefined?' <span class="mut">стан «'+esc(x.stRaw)+'»</span>':st?' <span class="err">'+st+'</span>':'')+
  '<div class="bar">'+bars+'</div><div class="ticks"><span>0</span><span>6</span><span>12</span><span>18</span><span>24</span></div>'+
  (x.st===1?'<div class="mut">'+(txt.length?'Відключення: '+txt.join(', ')+' · без світла '+hrs(off*60):'Без відключень')+'</div>':'')+'</div>'}
//  Батарея (1.0.36): дані JK BMS від мосту батареї (ESP32-C3 біля BMS)
let batPend=false;
function renderBat(b){const c=$('batcard');if(!b){c.classList.add('hide');return}c.classList.remove('hide');
 const f=(x,d,k)=>(x/d).toFixed(k).replace('.',','),hm=m=>m>=60?Math.floor(m/60)+' год'+(m%60?' '+m%60+' хв':''):m+' хв',
  ST=['міст шукає BMS — її не чути поруч','міст під\'єднується до BMS','на зв\'язку','міст під\'єднаний, але стану від BMS немає','міст чекає перед новою спробою під\'єднатись'];
 let h='';
 if(b.ok&&b.soc!=null){const i=b.i,ch=i<=-300?'розряд '+f(-i,1000,1)+' А':i>=300?'заряд '+f(i,1000,1)+' А':'спокій';
  h+='<div class="big">'+b.soc+' %</div><div>'+f(b.v,1000,2)+' В · '+ch+' · '+Math.abs(Math.round(b.v*b.i/1e6))+' Вт'+(b.left>=0?' · вистачить ~'+hm(b.left)+' (до повного розряду)':'')+'</div>';
  h+='<div class="mut">Залишок '+f(b.rem,1000,1)+' з '+f(b.full,1000,1)+' А·год · циклів '+b.cyc+' · стан '+b.soh+' %</div>';
  h+='<div style="margin-top:6px">Комірки: '+b.n+', від '+f(b.lo,1000,3)+' В (№'+b.lox+') до '+f(b.hi,1000,3)+' В (№'+b.hix+'), різниця '+(b.hi-b.lo)+' мВ</div>';
  if(b.cells&&b.cells.length)h+='<div class="cells">'+b.cells.map((v,k)=>'<span class="'+(k+1===b.lox?'lo':k+1===b.hix?'hi':'')+'">'+(k+1)+': '+f(v,1000,3)+'</span>').join('')+'</div>';
  const T=[];if(b.t1!=null)T.push('датчик 1 — '+f(b.t1,10,1)+' °C');if(b.t2!=null)T.push('датчик 2 — '+f(b.t2,10,1)+' °C');if(b.tm!=null)T.push('ключі — '+f(b.tm,10,1)+' °C');
  if(T.length)h+='<div style="margin-top:6px">Температура: '+T.join(', ')+'</div>';
  h+='<div>Заряд '+(b.chg?'дозволено':'<b class="err">вимкнено BMS</b>')+' · розряд '+(b.dis?'дозволено':'<b class="err">вимкнено BMS</b>')+(b.bal?' · вирівнює комірки':'')+(b.heat?' · підігрів':'')+'</div>';
  h+='<div>'+(b.alarm?'<b class="err">BMS повідомляє: '+esc(b.alarm)+'</b>':'Тривог BMS немає')+'</div>';
 }else h+='<div><b class="err">'+(!b.seen?'Міст батареї від запуску Сторожа не озивався':!b.bridge?'Міст батареї не на зв\'язку '+ago(b.ago):(b.soc!=null?'Даних від BMS немає '+ago(b.age)+' — ':'Даних від BMS ще немає — ')+ST[b.st||0])+'</b></div>';
 //  сторінка мосту через Сторожа працює з версії мосту 1.1.0 (старіша питає свої адреси не через /bms/)
 const bv=(b.ver||'0').split('.').map(Number),bNew=bv[0]>1||(bv[0]===1&&bv[1]>=1);
 if(b.seen&&b.bridge)h+=bNew?'<div style="margin-top:8px"><a class="btn" href="/bms/" target="_blank" rel="noopener">Сторінка мосту батареї</a> <span class="mut">дисплей мосту, яку BMS слухати, оновлення мосту'+(b.lcd?' · дисплей увімкнено':'')+'</span></div>'
  :'<div class="mut" style="margin-top:8px">Сторінка мосту (дисплей, оновлення по мережі) відкриватиметься звідси з версії мосту 1.1.0 — зараз '+esc(b.ver)+': її ставлять кабелем USB.</div>';
 if(b.seen)h+='<div class="mut" style="margin-top:6px">Міст '+esc(b.mac)+', версія '+esc(b.ver)+(b.up?', працює '+ago(b.up):'')+', Wi-Fi '+b.rssi+' дБм'+(b.name?' · BMS '+esc(b.name)+(b.model&&b.model!==b.name?' ('+esc(b.model)+')':'')+(b.sw?', прошивка '+esc(b.sw):'')+', '+b.brssi+' дБм':'')+' · з\'єднань із BMS '+b.rc+'</div>';
 $('batbody').innerHTML=h;$('batsub').textContent=b.ok?'дані '+ago(b.age)+' тому':'';
 if(!batPend)$('bat_on').checked=!!b.on}
$('bat_on').addEventListener('change',async e=>{batPend=true;const on=e.target.checked;
 try{await j('/api/bms',{method:'POST',body:new URLSearchParams({on:on?'1':'0'})});$('batsub').textContent=on?'сповіщення ввімкнено':'сповіщення вимкнено'}
 catch(err){e.target.checked=!on;$('batsub').textContent=err.message}batPend=false});
function renderSch(s){if(!s)return;const set='<button data-act="gosch" style="margin-top:8px">Налаштувати</button>';
 if(!s.on){$('schq').textContent=s.q?'· черга '+s.q+' · '+s.dsoName:'';
  $('schline').innerHTML=s.q?'<span class="mut">Графік YASNO вимкнено: Сторож не звертається до YASNO — менше пам\'яті й навантаження. Попереджень перед відключенням і графіка в боті немає.</span><br><button class="p" data-act="schon" style="margin-top:8px">Увімкнути графік YASNO</button>'
   :'<span class="mut">Не налаштовано. Виберіть свою чергу, і Сторож показуватиме графік YASNO тут і в боті, попереджатиме перед відключенням.</span><br>'+set;
  $('schdays').innerHTML='';$('schinfo').textContent='';$('schst').textContent='';$('schbtn').innerHTML='';return}
 if(!$('schbtn').firstChild)$('schbtn').innerHTML='<button data-act="schoff" style="margin-top:8px">Вимкнути графік YASNO</button> <span class="mut">менше пам\'яті й навантаження на Сторожа: без запитів до YASNO</span>';
 $('schq').textContent='· черга '+s.q+' · '+s.dsoName;
 $('schline').innerHTML=s.have?esc(s.line):'<span class="err">Даних з YASNO поки немає'+(s.err?': '+esc(s.err):'')+'</span>';
 $('schdays').innerHTML=s.have&&s.days?schDay(s.days[0],s.now,'Сьогодні')+schDay(s.days[1],null,'Завтра'):'';
 const info=(s.have?'YASNO оновив '+(s.upd||'—')+', Сторож отримав '+ago(s.ago)+' тому':'')+(s.err&&s.have?' · остання спроба: '+s.err:'');
 $('schinfo').textContent=info;$('schst').textContent=s.have?'Отримано '+ago(s.ago)+' тому':(s.err?'Помилка: '+s.err:'Ще не отримано')}
//  Маячки (1.0.23): таблиця в «Налаштуваннях». Рядки перебудовуються, лише коли змінився список і ніхто не пише в поле;
//  стан (на зв'язку, сигнал) оновлюється кожні 3 с.
const ROLE={g:'Генератор',l:'Лінія чи фаза',v:'Лише показувати'};let bcnSig='',bcnList=[];
function bid(m){return m.replace(/:/g,'')}
function bcnStat(b,w){let s=b.seen?(b.alive?'на зв\'язку · '+b.rssi+' дБм · працює '+ago(b.up):'мовчить '+ago(b.ago)):'не чув від запуску Сторожа';
 if(b.cfg&&b.role==='l'&&b.t)s=(b.t.st===1?'<b>є напруга</b>':b.t.st===0?'<b class="err">немає напруги</b>':'визначається')+' · '+s;
 if(b.cfg&&b.role==='g')s=(w.line.st===1?'<b>генератор працює</b>':w.line.st===0?'генератор стоїть':'визначається')+' · '+s;
 return s+(b.ver?' · прошивка '+esc(b.ver):'')}
function renderBcn(w,force){const c0=w.bcn||{list:[]},B=c0.list||[],sig=JSON.stringify(B.map(b=>[b.mac,b.cfg,b.role,b.name]));bcnList=B;
 const ae=document.activeElement,ed=ae&&$('bcn').contains(ae)&&ae.tagName!=='BUTTON';
 if(force||(sig!==bcnSig&&!ed)){bcnSig=sig;
  $('bcn').innerHTML=B.length?B.map(b=>{const id=bid(b.mac),r=b.cfg?b.role:(c0.legacy?'g':'l');
   return '<div class="brow"><div class="flex"><input id="bn_'+id+'" maxlength="24" value="'+esc(b.name||'')+'" placeholder="назва, напр. Фаза B" style="flex:1;min-width:140px">'+
    '<select id="br_'+id+'">'+Object.keys(ROLE).map(k=>'<option value="'+k+'"'+(k===r?' selected':'')+'>'+ROLE[k]+'</option>').join('')+'</select></div>'+
    '<div id="bs_'+id+'" style="margin:5px 0;font-size:14px"></div><div class="flex" style="justify-content:space-between"><span class="mut">'+b.mac+(b.cfg?'':' · <b>ще не в списку</b>')+'</span><span>'+
    (b.cfg?'<button data-act="bcnset" data-mac="'+b.mac+'">Зберегти</button> <button data-act="bcndel" data-mac="'+b.mac+'">Прибрати</button>':'<button class="p" data-act="bcnset" data-mac="'+b.mac+'">Додати до списку</button>')+'</span></div></div>'}).join('')
   :'<div class="mut">Список порожній, і від запуску Сторожа жоден маячок не озивався.</div>'}
 for(const b of B){const c=$('bs_'+bid(b.mac));if(c)c.innerHTML=bcnStat(b,w)}
 $('bcncnt').textContent='· у списку '+B.filter(b=>b.cfg).length+' з '+(c0.max||6);
 $('bcnleg').classList.toggle('hide',!c0.legacy);
 if(c0.legacy)$('bcnleg').innerHTML='Список порожній, тож <b>будь-який</b> маячок зараз вважається маячком генератора — як до версії 1.0.23. Додайте свій маячок із роллю «Генератор» — тоді інші маячки вже не плутатимуть Сторожа.'}
async function bcnReload(force){try{renderBcn((await j('/api/state')).watch,force!==false)}catch(e){}}
async function bcnPost(mac,role,name,okText){const b=bcnList.find(x=>x.mac===mac);
 if(role==='g'&&b&&b.alive&&!(b.cfg&&b.role==='g')&&!confirm('Маячок '+mac+' зараз на зв\'язку — Сторож вважатиме, що генератор працює. Він справді в розетці від генератора, і генератор зараз працює?'))return false;
 try{await j('/api/beacon',{method:'POST',body:new URLSearchParams({mac:mac,act:'set',role:role,name:name})});note('bcnmsg',okText);bcnReload();return true}catch(err){note('bcnmsg',err.message,1);return false}}
let curSet=null,annAll=0;
$('a_to').addEventListener('change',()=>$('a_who').classList.toggle('hide',$('a_to').value!=='sel'));
async function loadSet(){
 try{const s=await j('/api/settings');curSet=s;
  for(const k of ['obj','staSsid','staBssid','apSsid','avrMac','offSec','onSec','lineOffSec','lineOnSec','genRemMin'])$('f_'+k).value=s[k];
  $('f_mdnsOn').checked=!!s.mdnsOn;
  $('f_notifyStart').checked=s.notifyStart;bcnReload();
  $('f_schOn').checked=!!s.schOn;$('f_schTom').checked=!!s.schTom;$('f_schDso').value=s.schDso||'3/301';$('f_schQueue').value=s.schQueue||'';$('f_schWarn').value=s.schWarn;
  $('h_staPass').textContent=s.staPassSet?'(задано)':'(не задано)';
  $('h_adm').textContent=s.adminDefault?'· зараз типовий — задайте свій':'';
  $('f_pwBtn').value=String(s.pwBtn);$('r_ap').textContent=s.defAp;$('r_pw').textContent=s.defPass;
  $('h_pwBtn').textContent='Кнопка між виводом і GND, без резистора. Утримати 5 секунд — пароль сторінки стане типовим ('+s.defPass+' — запишіть його), усі входи скасуються, решта налаштувань лишиться, а в Telegram прийде «🔑 Пароль сторінки скинуто». Сховайте кнопку в корпус: хто її натисне, зможе увійти з типовим паролем. Вивід не має збігатися з виводами сигналів.';
  for(const k of ['staPass','apPass','adminPass'])$('f_'+k).value='';
 }catch(e){note('smsg',e.message,1)}
}
function botStatus(){const t=lastTg||{},set=!!(t.token||tokSet);
 $('h_tok').textContent=!set?'· ще не підключено':t.bot?'· підключено: @'+t.bot:'· токен збережено, перевіряю…';
 $('noBot').classList.toggle('hide',set);
 $('boterr').textContent=set&&!t.bot&&t.lastErr?'Telegram відповів: '+t.lastErr+'. Перевірте токен.':''}
//  Що отримує одержувач (1.0.21): біти — як RF_* у прошивці
const FLB=[[0,'Сповіщення'],[1,'Світло'],[2,'Генератор'],[3,'Графік'],[4,'Підсумок'],[5,'Службові'],[6,'Маячки'],[9,'🔋 Батарея'],[7,'🌙 уночі тихо'],[8,'👑 адмін']];
function chips(x){const on=x.fl&1;return '<div class="chips">'+FLB.map(([b,n])=>{const c=(x.fl>>b)&1,dim=(b>=1&&b<=6||b===9)&&!on;return '<label class="'+(dim?'dim':'')+'"><input type="checkbox" data-fl="'+b+'" data-id="'+esc(x.id)+'" data-v="'+x.fl+'"'+(c?' checked':'')+'>'+n+'</label>'}).join('')+'</div>'}
document.addEventListener('change',async e=>{const c=e.target.closest('input[data-fl]');if(!c)return;const b=+c.dataset.fl,v=+c.dataset.v,nv=c.checked?(v|(1<<b)):(v&~(1<<b));
 try{await j('/api/chatflags',{method:'POST',body:new URLSearchParams({id:c.dataset.id,fl:String(nv)})});note('rmsg','Збережено.');loadTg()}catch(err){note('rmsg',err.message,1);loadTg()}});
async function loadTg(){
 try{const s=await j('/api/settings');$('f_sumOn').checked=!!s.sumOn;$('f_sumHour').value=s.sumHour;
  tokSet=!!s.tgTokenSet;$('f_tgToken').value='';$('f_tgToken').placeholder=tokSet?'не змінювати (щоб замінити, вставте новий)':'вставте токен сюди';
  $('botguide').open=!tokSet;const sec=$('tg'),bc=$('botcard'),pc=$('phcard');if(!tokSet){sec.prepend(pc);sec.prepend(bc)}else{sec.append(bc);sec.append(pc)}botStatus();
  const L=s.chats||[];$('f_tgNF').value=s.tgNF;$('f_tgNT').value=s.tgNT;
  annAll=L.filter(x=>x.fl&1).length;$('a_to').options[0].textContent='Усім, у кого ввімкнено сповіщення ('+annAll+')';
  $('a_who').innerHTML=L.map(x=>'<label class="'+(x.fl&1?'':'dim')+'"><input type="checkbox" data-id="'+esc(x.id)+'"'+(x.fl&1?' checked':' disabled')+'>'+esc(x.name||x.id)+'</label>').join('');
  $('rcount').textContent='· '+L.length+' з '+((lastTg&&lastTg.max)||20);
  $('rcp').innerHTML=L.length?L.map(x=>'<tr><td>'+esc(x.name||'без назви')+' <span class="mut">'+(x.id.startsWith('-')?'група':'людина')+' · '+esc(x.id)+'</span>'+chips(x)+'</td><td style="text-align:right;white-space:nowrap"><button data-act="rtest" data-id="'+esc(x.id)+'">Перевірити</button> <button data-act="del" data-id="'+esc(x.id)+'" data-name="'+esc(x.name||x.id)+'">Видалити</button></td></tr>').join(''):'<tr><td class="mut">Поки нікого. Додайте людей нижче.</td></tr>';
 }catch(e){note('rmsg',e.message,1)}
}
async function save(){
 const p=new URLSearchParams();
 for(const k of ['obj','staSsid','staBssid','staPass','apSsid','apPass','avrMac','offSec','onSec','lineOffSec','lineOnSec','genRemMin','adminPass'])p.append(k,$('f_'+k).value);
 p.append('mdnsOn',$('f_mdnsOn').checked?'1':'0');
 p.append('notifyStart',$('f_notifyStart').checked?'1':'0');p.append('pwBtn',$('f_pwBtn').value);
 p.append('schOn',$('f_schOn').checked?'1':'0');p.append('schTom',$('f_schTom').checked?'1':'0');p.append('schDso',$('f_schDso').value);p.append('schQueue',$('f_schQueue').value);p.append('schWarn',$('f_schWarn').value);
 try{const r=await j('/api/settings',{method:'POST',body:p});
  note('smsg',r.restart?'Збережено. Сторож перезапускається: за 20–30 секунд оновіть сторінку й увійдіть знову. Якщо змінили роутер, сторінка буде за новою адресою.':'Збережено.');
  if(!r.restart)loadSet();
 }catch(e){note('smsg',e.message,1)}
}
async function scan(){
 const sl=$('scanl');sl.classList.remove('hide');sl.innerHTML='<option>Шукаю мережі…</option>';
 try{await j('/api/scan?start=1');
  for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,1000));const d=await j('/api/scan');
   if(!d.running){const l=d.list.filter(x=>x.ssid).sort((a,b)=>b.rssi-a.rssi);
    sl.innerHTML='<option value="">Виберіть мережу ('+l.length+')</option>'+l.map(x=>'<option value="'+esc(x.ssid)+'" data-b="'+esc(x.bssid||'')+'">'+esc(x.ssid)+' · '+x.rssi+' дБм · канал '+x.ch+(x.bssid?' · '+esc(x.bssid):'')+'</option>').join('');return}}
  sl.innerHTML='<option>Не вдалося знайти мережі</option>';
 }catch(e){sl.innerHTML='<option>'+esc(e.message)+'</option>'}
}
$('scanl').onchange=e=>{if(e.target.value){$('f_staSsid').value=e.target.value;const o=e.target.selectedOptions[0];$('f_staBssid').value=(o&&o.dataset.b)||''}};
//  Картинка бота (1.0.11): будь-який файл з комп'ютера → квадрат по центру 512×512 → JPG ≤ 70 КБ (сторож бере до 72 КБ)
let phBlob=null;
function loadImg(f){return new Promise((res,rej)=>{const u=URL.createObjectURL(f),i=new Image();i.onload=()=>{URL.revokeObjectURL(u);res(i)};i.onerror=()=>{URL.revokeObjectURL(u);rej(new Error('img'))};i.src=u})}
$('f_bph').onchange=async e=>{phBlob=null;const f=e.target.files[0];if(!f)return;note('phmsg','Готую картинку…');
 try{const img=await loadImg(f),c=$('phprev'),x=c.getContext('2d'),s=Math.min(img.naturalWidth,img.naturalHeight);
  x.fillStyle='#ffffff';x.fillRect(0,0,512,512);
  x.drawImage(img,(img.naturalWidth-s)/2,(img.naturalHeight-s)/2,s,s,0,0,512,512);
  let q=0.9;for(;;){phBlob=await new Promise(r=>c.toBlob(r,'image/jpeg',q));if(!phBlob||phBlob.size<=70*1024||q<=0.45)break;q-=0.1}
  if(!phBlob||phBlob.size>70*1024){phBlob=null;note('phmsg','Картинка задетальна навіть після стиснення — спробуйте іншу',1);return}
  note('phmsg','Готово: 512×512, '+Math.round(phBlob.size/1024)+' КБ. Так її бачитимуть у Telegram. Натисніть «Поставити картинку».')
 }catch(err){note('phmsg','Цей файл браузер не відкриває. Збережіть картинку як JPG або PNG і виберіть знову.',1)}};
//  Сигнали (1.0.13): світлодіоди, адресна стрічка, бузери, динамік через ЦАП
const PINS=[2,4,13,14,16,17,18,19,21,22,23,25,26,27,32,33];
$('f_schQueue').innerHTML='<option value="">не вибрано</option>'+[1,2,3,4,5,6].map(a=>[1,2].map(b=>'<option>'+a+'.'+b+'</option>').join('')).join('');
['g_ledG','g_ledR','g_stripPin','g_sndPin','g_lcdRst','g_lcdCe','g_lcdDc','g_lcdDin','g_lcdClk'].forEach(id=>$(id).innerHTML=PINS.map(p=>'<option value="'+p+'">GPIO'+p+'</option>').join(''));
$('g_lcdBl').innerHTML='<option value="0">Немає — BL на 3V3 чи GND</option>'+PINS.map(p=>'<option value="'+p+'">GPIO'+p+'</option>').join('');
$('f_pwBtn').innerHTML='<option value="0">Немає</option>'+PINS.filter(p=>p!==2).map(p=>'<option value="'+p+'">GPIO'+p+(p===32?' — типово':'')+'</option>').join('');
const WIRE={1:'Активний бузер: вивід → резистор 1 кОм → база NPN-транзистора (S8050, 2N2222); емітер → GND; колектор → «−» бузера; «+» бузера → 5 В. Маленький бузер до 20 мА можна й напряму: вивід → «+», «−» → GND. Гучність активного бузера регулюється лише частково.',
 2:'Пасивний бузер: так само через транзистор (вивід → 1 кОм → база S8050; колектор → бузер → 5 В), паралельно бузеру діод 1N4148. Напряму з виводу — тихо.',
 3:'ЦАП дає слабкий сигнал — потрібен підсилювач: модуль PAM8403 (5 В) або LM386. Вивід ЦАП → конденсатор 1–10 мкФ → вхід підсилювача (L чи R), GND плати → GND підсилювача, динамік 4–8 Ом — до виходу підсилювача. Хрипить чи «рипить» — підсилювач перевантажений: зменште гучність тут (PAM8403 без регулятора — 20–40 %). Найчистіше — регулятор на підсилювачі менше, а гучність тут 80–100 %; без регулятора — дільник перед входом: 4,7 кОм послідовно й 1 кОм на GND.'};
const SIGK=['ledG','ledR','ledBri','ledBlink','stripPin','stripN','stripBri','stripNight','stripOrder','stripFx','stripMa','stFxOn','stFxGen','stWvSpd','stWvInt','lcdRst','lcdCe','lcdDc','lcdDin','lcdClk','lcdBl','lcdVop','lcdBri','lcdNight','lcdBias','lcdMq','lcdFxSec','lcdAnnMin','sndType','sndPin','dacPin','sndVol','sndRep','qFrom','qTo','melLost','melBack','patLost','patBack','srcLost','srcBack','colOn','colOff','colGen','colUnk'];
const SIGB=['ledLow','stripAnim','sndLost','sndBack','sndBoot','qOn','lcdOn','lcdBlLow','lcdInv','lcdFlip','lcdFx','nLedOff','nLcdOff'];
function gamma(v){return Math.pow(v,2.2)}
function stripHint(){const n=Math.max(1,+$('g_stripN').value||1),b=gamma((+$('g_stripBri').value||0)/100),hx=$('g_colOn').value||'#00ff00',
 c=[1,3,5].map(i=>parseInt(hx.substr(i,2),16)/255).reduce((a,x)=>a+gamma(x),0),ma=Math.min(+$('g_stripMa').value||400,n*c*20*b);
 $('g_stripHint').textContent='Зараз у стані «світло є» стрічка бере ≈ '+Math.round(ma)+' мА (межа '+($('g_stripMa').value||400)+' мА). Живлення стрічки — 5 В; дані: вивід → резистор 330 Ом → DIN; між 5 В і GND стрічки — конденсатор 470–1000 мкФ; GND стрічки — спільний з GND плати. 8 світлодіодів на 30 % — досить, щоб бачити здалеку.'}
function sigShow(){const ind=$('g_ind').value,ty=+$('g_sndType').value;
 $('g_two').classList.toggle('hide',ind!=='two');$('g_strip').classList.toggle('hide',ind!=='strip');$('g_blinkbox').classList.toggle('hide',ind==='none');$('g_ledbtns').classList.toggle('hide',ind==='none');document.querySelectorAll('.g_stonly').forEach(x=>x.classList.toggle('hide',ind!=='strip'));
 $('g_pinbox').classList.toggle('hide',!(ty===1||ty===2));$('g_dacbox').classList.toggle('hide',ty!==3);$('g_sndrest').classList.toggle('hide',ty===0);
 $('g_patbox').classList.toggle('hide',ty!==1);$('g_melbox').classList.toggle('hide',ty!==2);$('g_dsbox').classList.toggle('hide',ty!==3);
 $('g_nLedRow').classList.toggle('hide',ind==='none');$('g_nLedPct').classList.toggle('hide',$('g_nLedOff').checked);
 $('g_nLcdRow').classList.toggle('hide',!$('g_lcdOn').checked);$('g_nLcdPct').classList.toggle('hide',$('g_nLcdOff').checked);$('g_nSndRow').classList.toggle('hide',ty===0);
 $('g_wire').textContent=WIRE[ty]||'';['ledBri','stripBri','stripNight','sndVol','stWvSpd','stWvInt','lcdVop','lcdBri','lcdNight','lcdMq'].forEach(k=>$('g_'+k+'V').textContent=$('g_'+k).value);stripHint()}
['g_ind','g_sndType','g_ledBri','g_stripBri','g_stripNight','g_sndVol','g_stripN','g_stripMa','g_colOn','g_stWvSpd','g_stWvInt','g_lcdMq','g_lcdVop','g_lcdBri','g_lcdNight'].forEach(id=>$(id).addEventListener('input',sigShow));
['g_nLedOff','g_nLcdOff','g_lcdOn','g_qFrom','g_qTo'].forEach(id=>$(id).addEventListener('change',()=>{sigShow();if(lastSt)sigStatus(lastSt)}));
//  Знімок дисплея (1.0.22): 504 байти, 6 смуг по 8 рядків, біт 0 — верхня точка
function drawLcd(hex){const c=$('lcdc'),x=c.getContext('2d'),im=x.createImageData(84,48);
 for(let p=0;p<504;p++){const b=parseInt(hex.substr(p*2,2),16),col=p%84,band=Math.floor(p/84);for(let k=0;k<8;k++){const on=(b>>k)&1,i=((band*8+k)*84+col)*4;im.data[i]=on?40:178;im.data[i+1]=on?48:196;im.data[i+2]=on?36:158;im.data[i+3]=255}}
 x.putImageData(im,0,0)}
async function loadLcd(){try{const d=await j('/api/lcd');drawLcd(d.test===1||d.test===3?'ff'.repeat(504):d.test===2?Array.from({length:504},(_,i)=>(((i%84)>>2)+Math.floor(i/84))&1?'f0':'0f').join(''):d.fb);$('lcdst').textContent=d.on?(d.live?'Дисплей увімкнено: Сторож шле на нього цей самий кадр'+(d.frames?' (надіслано кадрів: '+d.frames+', останній — за '+(d.us/1000).toFixed(1)+' мс)':'')+'.'+(d.bl>=0?' Підсвітка зараз: '+(d.bl?d.bl+' %':'вимкнена')+'.':'')+(d.test>=0?' Зараз перевірка: '+['звичайний вигляд','усе чорне','шахівниця','пошук контрасту'][d.test]+', контраст '+d.vop+', зсув '+d.bias+'.':''):'Дисплей увімкнено, але виводи не зайнято — перевірте вибір виводів.'):'Дисплей вимкнено в налаштуваннях — це лише прев\'ю.'}catch(e){$('lcdst').textContent=e.message}}
//  Перевірка дисплея (1.0.24): кнопки й живі повзунки → /api/lcdtest (лише пам'ять Сторожа)
let lcdTesting=false,lcdTT=0,lcdSw=0;
async function lcdTest(mode){const p=new URLSearchParams({vop:$('g_lcdVop').value,bri:$('g_lcdBri').value,bllow:$('g_lcdBlLow').checked?'1':'0',bias:$('g_lcdBias').value});if(mode)p.append('mode',mode);
 clearInterval(lcdSw);if(mode==='sweep')lcdSw=setInterval(loadLcd,500);
 try{await j('/api/lcdtest',{method:'POST',body:p});if(mode)lcdTesting=mode!=='end';if(mode)note('lcdtmsg',mode==='end'?'Перевірку закінчено: на дисплеї знову збережені налаштування.':'Перевірка на дисплеї — 60 с після останньої зміни. Підібрали — «Зберегти».');setTimeout(loadLcd,400)}catch(err){lcdTesting=false;note('lcdtmsg',err.message,1)}}
['g_lcdVop','g_lcdBri'].forEach(id=>$(id).addEventListener('input',()=>{clearTimeout(lcdTT);lcdTT=setTimeout(()=>lcdTest(''),150)}));
$('g_lcdBlLow').addEventListener('change',()=>{if(lcdTesting)lcdTest('')});
$('g_lcdBias').addEventListener('change',()=>{if(lcdTesting)lcdTest('')});
async function lcdHere(){try{const d=await j('/api/lcd');if(d.test!==3){note('lcdtmsg','Спершу «Пошук контрасту», потім — коли дисплей потемніє',1);return}
 $('g_lcdVop').value=d.vop;$('g_lcdBias').value=String(d.bias);sigShow();await lcdTest('all');
 note('lcdtmsg','Зупинено: контраст '+d.vop+', зсув '+d.bias+' (екран «усе чорне»). Підправте повзунком, «Звичайний вигляд» — перевірити картинку, далі «Зберегти».')}catch(err){note('lcdtmsg',err.message,1)}}
let lastSt=null;
function hh(v){return String(+v).padStart(2,'0')+':00'}
function sigStatus(st){if(!st)return;lastSt=st;
 const f=$('g_qFrom').value,t=$('g_qTo').value;
 $('g_ngst').textContent=st.demo?'— показ ночі: 15 с як уночі':+f===+t?'— години однакові: ночі немає':st.clock===false?'— годинник ще не встановлено: поки день':st.night?'— зараз ніч, до '+hh(t):'— зараз день, ніч з '+hh(f);
 $('g_ledst').textContent='Зараз індикатор: '+(st.dark&&!st.test?'погашено — ніч':st.led===1?'«світло є»':st.led===0?'«світла немає»':'нічого не показує')+(st.test?' — перевірка':'');
 $('g_sndst').textContent=(st.busy?'Грає… ':'')+(st.last?'Востаннє: '+st.last+'. ':'')+(st.quiet?'Зараз ніч — без звуку.':'');
 $('g_infLost').textContent=st.lost?'На Сторожі свій звук: '+(st.lost/16000).toFixed(1)+' с':'Свого звуку «світло зникло» ще немає';
 $('g_infBack').textContent=st.back?'На Сторожі свій звук: '+(st.back/16000).toFixed(1)+' с':'Свого звуку «світло є» ще немає'}
let curSig=null;
async function loadSig(){try{const s=await j('/api/signals');curSig=s;$('g_ind').value=!s.ledOn?'none':(s.ledType===1?'strip':'two');
 for(const k of SIGK)$('g_'+k).value=s[k];for(const k of SIGB)$('g_'+k).checked=!!s[k];sigShow();sigStatus(s.st)}catch(e){note('g_msg',e.message,1)}}
async function sigSave(auto){const p=new URLSearchParams(),ind=$('g_ind').value;p.append('ledOn',ind==='none'?'0':'1');p.append('ledType',ind==='strip'?'1':'0');
 for(const k of SIGK)p.append(k,$('g_'+k).value);for(const k of SIGB)p.append(k,$('g_'+k).checked?'1':'0');
 try{await j('/api/signals',{method:'POST',body:p});if(auto)note('g_msg','✓ Збережено й діє — '+new Date().toLocaleTimeString('uk-UA'));else{note('g_msg','Збережено.');loadSig()}return true}catch(e){note('g_msg',e.message,1);return false}}
//  1.0.26 (власник: «сохранение и применение параметров для отображения, света, звука — работать должно на лету»): будь-яка
//  зміна на вкладці «Сигнали» — за пів секунди на Сторожа: зберігається й одразу діє. Повзунки дисплея — ще й під час руху.
let sigT=0,sigBusy=false,sigAgain=false;
function sigAuto(){clearTimeout(sigT);sigT=setTimeout(sigAutoSave,500)}
async function sigAutoSave(){if(!curSig)return;if(sigBusy){sigAgain=true;return}sigBusy=true;await sigSave(true);sigBusy=false;if(sigAgain){sigAgain=false;sigAuto()}}
$('sg').addEventListener('change',e=>{const t=e.target;if(!t.id||!t.id.startsWith('g_')||t.type==='file')return;sigAuto()});
//  свій звук: будь-який файл → моно 16 кГц, 8 біт, до 3 с, пік 0,95, плавні краї, дизеринг
const pcm=[null,null];
function resample(buf,dur){const sr=buf.sampleRate,M=Math.floor(sr*dur),ch=buf.numberOfChannels,m=new Float32Array(M);
 for(let c=0;c<ch;c++){const d=buf.getChannelData(c);for(let i=0;i<M;i++)m[i]+=d[i]/ch}
 const N=Math.floor(16000*dur),r=sr/16000,o=new Float32Array(N),w=Math.max(1,Math.round(r/2));
 for(let i=0;i<N;i++){const c=Math.floor(i*r);let s=0,k=0;for(let q=c-w;q<=c+w;q++)if(q>=0&&q<M){s+=m[q];k++}o[i]=k?s/k:0}return o}
async function toPcm(file){const ab=await file.arrayBuffer(),AC=window.AudioContext||window.webkitAudioContext,ac=new AC();let buf;
 try{buf=await ac.decodeAudioData(ab.slice(0))}finally{if(ac.close)ac.close()}
 const dur=Math.min(buf.duration,3.0),N=Math.max(1,Math.floor(16000*dur));let x;
 try{const o=new OfflineAudioContext(1,N,16000),s=o.createBufferSource();s.buffer=buf;s.connect(o.destination);s.start(0,0,dur);x=(await o.startRendering()).getChannelData(0)}catch(e){x=resample(buf,dur)}
 let pk=0;for(let i=0;i<x.length;i++)pk=Math.max(pk,Math.abs(x[i]));const g=pk>0?0.95/pk:1,n=x.length,u=new Uint8Array(n);
 for(let i=0;i<n;i++){let v=x[i]*g;if(i<160)v*=i/160;if(n-i<800)v*=(n-i)/800;const d=Math.random()+Math.random()-1;u[i]=Math.max(0,Math.min(255,Math.round(128+v*126+d)))}
 return {u,full:buf.duration}}
function preview(u){const AC=window.AudioContext||window.webkitAudioContext,ac=new AC(),b=ac.createBuffer(1,u.length,16000),d=b.getChannelData(0);
 for(let i=0;i<u.length;i++)d[i]=(u[i]-128)/128;const s=ac.createBufferSource();s.buffer=b;s.connect(ac.destination);s.onended=()=>{if(ac.close)ac.close()};s.start()}
[['g_fLost',0,'g_upLost'],['g_fBack',1,'g_upBack']].forEach(([id,w,out])=>$(id).onchange=async e=>{pcm[w]=null;const f=e.target.files[0];if(!f)return;note(out,'Перетворюю…');
 try{const r=await toPcm(f);pcm[w]=r.u;note(out,'Готово: '+(r.u.length/16000).toFixed(1)+' с'+(r.full>3.05?' (обрізано з '+r.full.toFixed(1)+' с до 3 с)':'')+', '+Math.round(r.u.length/1024)+' КБ. Прослухайте, як звучатиме, і завантажте.')}
 catch(err){note(out,'Цей файл браузер не відкриває — спробуйте MP3 або WAV',1)}});
function upload(){
 const f=$('fw').files[0];if(!f){note('umsg','Виберіть файл прошивки .bin',1);return}
 const x=new XMLHttpRequest(),fd=new FormData();fd.append('fw',f,f.name);
 x.upload.onprogress=e=>note('umsg','Завантаження: '+Math.round(e.loaded*100/e.total)+'%');
 x.onload=()=>note('umsg',x.responseText,x.status!==200);x.onerror=()=>note('umsg','Обрив зв\'язку',1);
 x.open('POST','/update');x.send(fd);
}
document.addEventListener('click',async e=>{
 const b=e.target.closest('button[data-act]');if(!b)return;const a=b.dataset.act;
 if(a==='save')save();else if(a==='scan')scan();else if(a==='upload')upload();
 else if(a==='test'){try{await j('/api/test',{method:'POST'});note('tmsg','Повідомлення в черзі, перевірте Telegram.')}catch(err){note('tmsg',err.message,1)}}
 else if(a==='rtest'){try{await j('/api/test',{method:'POST',body:new URLSearchParams({id:b.dataset.id})});note('rmsg','Пробне повідомлення в черзі, перевірте Telegram.')}catch(err){note('rmsg',err.message,1)}}
 else if(a==='del'){if(!confirm('Прибрати «'+b.dataset.name+'» зі сповіщень?'))return;try{await j('/api/delchat',{method:'POST',body:new URLSearchParams({id:b.dataset.id})});note('rmsg','Прибрано. Людині прийде «Сповіщення вимкнено».');loadTg();refresh()}catch(err){note('rmsg',err.message,1)}}
 else if(a==='botphoto'){if(!phBlob){note('phmsg','Спершу виберіть картинку з комп\'ютера',1);return}const fd=new FormData();fd.append('pic',phBlob,'bot.jpg');
  try{const r=await fetch('/api/botphoto',{method:'POST',body:fd,credentials:'same-origin'});const x=await r.text();let d;try{d=JSON.parse(x)}catch(e){d={err:x}}if(!r.ok)throw new Error(d.err||('HTTP '+r.status));note('phmsg','Картинку прийнято, Сторож ставить її в Telegram…');refresh()}catch(err){note('phmsg',err.message,1)}}
 else if(a==='botname'){const v=$('f_botname').value.trim();if(!v){note('tmsg','Вкажіть назву бота',1);return}try{await j('/api/botname',{method:'POST',body:new URLSearchParams({name:v})});note('tmsg','Змінюю назву бота…');refresh()}catch(err){note('tmsg',err.message,1)}}
 else if(a==='sigsave')sigSave();
 else if(a==='lcdt')lcdTest(b.dataset.m);
 else if(a==='lcdfx'){try{await j('/api/lcdfx',{method:'POST',body:new URLSearchParams({kind:b.dataset.k})});note('lcdtmsg','Ефект на дисплеї — дивіться на Сторожа.');setTimeout(loadLcd,600)}catch(err){note('lcdtmsg',err.message,1)}}
 else if(a==='annsend'){const t=$('a_text').value.trim();if(!t){note('annmsg','Напишіть текст оголошення',1);return}
  let to='all';if($('a_to').value==='sel'){const ids=[...document.querySelectorAll('#a_who input:checked')].map(x=>x.dataset.id);if(!ids.length){note('annmsg','Позначте, кому надіслати',1);return}to=ids.join(',')}
  const n=to==='all'?annAll:to.split(',').length;if(!confirm('Надіслати оголошення '+n+' одержувачам'+($('a_silent').checked?' (без звуку)':'')+'?'))return;
  try{const r=await j('/api/announce',{method:'POST',body:new URLSearchParams({text:t,to:to,silent:$('a_silent').checked?'1':'0',lcd:$('a_lcd').checked?'1':'0'})});
   note('annmsg','Надіслано: '+r.n+' одержувачам'+(r.lcd?', і на дисплеї':'')+'.');$('a_text').value=''}catch(err){note('annmsg',err.message,1)}}
 else if(a==='lcdhere')lcdHere();
 else if(a==='lcdwire'){note('lcdtmsg','Перевіряю дроти з боку Сторожа… (дисплей на мить вимкнеться)');try{const d=await j('/api/lcdwire',{method:'POST'});
  $('lcdwres').innerHTML='<table>'+d.lines.map(x=>'<tr><td><b>'+x.n+'</b> GPIO'+x.p+'</td><td class="mut">вгору '+x.pu+' · вниз '+x.pd+' · '+(x.ns/1000).toFixed(2)+' мкс</td><td>'+esc(x.v)+'</td></tr>').join('')+'</table><div class="mut" style="margin-top:4px">Вільний вивід — '+(d.base/1000).toFixed(2)+' мкс. Усі виводи плати:</div><table>'+(d.scan||[]).map(x=>'<tr><td>GPIO'+x.p+(x.role?'<br><span class="mut">'+esc(x.role)+'</span>':'')+'</td><td class="mut">вгору '+x.pu+' · вниз '+x.pd+' · '+(x.ns/1000).toFixed(2)+' мкс</td><td>'+esc(x.v)+'</td></tr>').join('')+'</table>';
  note('lcdtmsg',d.verdict,!!d.shorts)}catch(err){note('lcdtmsg',err.message,1)}}
 else if(a==='bcnset'){const m=b.dataset.mac,id=bid(m),c=bcnList.find(x=>x.mac===m);bcnPost(m,$('br_'+id).value,$('bn_'+id).value.trim(),c&&c.cfg?'Збережено.':'Маячок додано до списку.')}
 else if(a==='bcndel'){if(!confirm('Прибрати маячок '+b.dataset.mac+' зі списку? Повідомлень про нього більше не буде.'))return;try{await j('/api/beacon',{method:'POST',body:new URLSearchParams({mac:b.dataset.mac,act:'del'})});note('bcnmsg','Прибрано зі списку.');bcnReload()}catch(err){note('bcnmsg',err.message,1)}}
 else if(a==='bcnadd'){const m=$('b_mac').value.trim().toLowerCase();if(!/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/.test(m)){note('bcnmsg','MAC-адреса — у вигляді aa:bb:cc:dd:ee:ff',1);return}
  if(await bcnPost(m,$('b_role').value,$('b_name').value.trim(),'Маячок додано до списку.')){$('b_mac').value='';$('b_name').value=''}}
 else if(a==='schoff'||a==='schon'){const on=a==='schon';if(!on&&!confirm('Вимкнути графік YASNO? Сторож перестане брати графік, попереджати перед відключенням і показувати графік у боті.'))return;
  b.disabled=true;try{await j('/api/sch',{method:'POST',body:new URLSearchParams({on:on?'1':'0'})});$('f_schOn').checked=on;$('schbtn').innerHTML='';await refresh()}catch(err){$('schinfo').textContent=err.message}b.disabled=false}
 else if(a==='gosch'){document.querySelector('nav button[data-t="set"]').click();setTimeout(()=>$('schset').scrollIntoView({behavior:'smooth'}),300)}
 else if(a==='schfetch'){try{await j('/api/schfetch',{method:'POST'});$('schst').textContent='Запитую в YASNO…';setTimeout(refresh,4000)}catch(err){$('schst').textContent=err.message}}
 else if(a==='rprefs'){if(!confirm('Повернути типові налаштування? Назва об\'єкта, затримки, підсумок, сигнали й кнопка пароля стануть типовими. Wi-Fi, АВР, маячки, бот і пароль лишаться.'))return;
  try{await j('/api/reset',{method:'POST',body:new URLSearchParams({what:'prefs'})});note('rmsg2','Налаштування стали типовими. Wi-Fi, АВР, бот і пароль — без змін.');loadSet()}catch(err){note('rmsg2',err.message,1)}}
 else if(a==='rall'){const w=$('r_word').value.trim().toUpperCase();if(w!=='СКИНУТИ'){note('rmsg2','Щоб скинути все, введіть слово СКИНУТИ',1);return}
  try{await j('/api/reset',{method:'POST',body:new URLSearchParams({what:'all',confirm:w})});$('r_word').value='';note('rmsg2','Скинуто. За 10 секунд Сторож перезапуститься з типовими налаштуваннями: під\'єднайтесь до мережі «'+$('r_ap').textContent+'» з паролем '+$('r_pw').textContent+' і відкрийте http://10.77.0.1')}catch(err){note('rmsg2',err.message,1)}}
 else if(a==='tled'){try{await j('/api/sigtest',{method:'POST',body:new URLSearchParams({what:b.dataset.w})});refresh()}catch(err){note('g_msg',err.message,1)}}
 else if(a==='tsnd'){const v=$('g_sndVol').value;try{await j('/api/sigtest',{method:'POST',body:new URLSearchParams({what:b.dataset.w,vol:v})});note('g_msg','Грає з гучністю '+v+' %'+(curSig&&String(curSig.sndVol)!==v?' — для справжніх подій натисніть «Зберегти»':'')+'…')}catch(err){note('g_msg',err.message,1)}}
 else if(a==='sndplay'){const w=+b.dataset.w;if(!pcm[w]){note(w?'g_upBack':'g_upLost','Спершу виберіть файл',1);return}preview(pcm[w])}
 else if(a==='sndup'){const w=+b.dataset.w,out=w?'g_upBack':'g_upLost';if(!pcm[w]){note(out,'Спершу виберіть файл',1);return}const fd=new FormData();fd.append('pcm',new Blob([pcm[w]],{type:'application/octet-stream'}),'snd.u8');
  try{const r=await fetch('/api/sound?w='+w,{method:'POST',body:fd,credentials:'same-origin'});const x=await r.text();let d;try{d=JSON.parse(x)}catch(e){d={err:x}}if(!r.ok)throw new Error(d.err||('HTTP '+r.status));
   $(w?'g_srcBack':'g_srcLost').value='2';note(out,'Завантажено на Сторожа. Вибрано «Свій звук» — натисніть «Зберегти».');refresh()}catch(err){note(out,err.message,1)}}
 else if(a==='snddel'){const w=+b.dataset.w,out=w?'g_upBack':'g_upLost';try{await j('/api/sounddel',{method:'POST',body:new URLSearchParams({w:String(w)})});
  const sel=$(w?'g_srcBack':'g_srcLost');if(sel.value==='2')sel.value='0';note(out,'Свій звук видалено. Вибрано «Дзвін» — натисніть «Зберегти».');refresh()}catch(err){note(out,err.message,1)}}
 else if(a==='savenight'){const f=parseInt($('f_tgNF').value,10),t=parseInt($('f_tgNT').value,10);if(!(f>=0&&f<=23&&t>=0&&t<=23)){note('nightmsg','Години — від 0 до 23',1);return}try{await j('/api/settings',{method:'POST',body:new URLSearchParams({tgNF:String(f),tgNT:String(t)})});note('nightmsg','Збережено.')}catch(err){note('nightmsg',err.message,1)}}
 else if(a==='savesum'){const h=parseInt($('f_sumHour').value,10);if(!(h>=0&&h<=23)){note('summsg','Година — від 0 до 23',1);return}try{await j('/api/settings',{method:'POST',body:new URLSearchParams({sumOn:$('f_sumOn').checked?'1':'0',sumHour:String(h)})});note('summsg','Збережено.')}catch(err){note('summsg',err.message,1)}}
 else if(a==='savetok'){const v=$('f_tgToken').value.trim();if(!v){note('tmsg','Вставте токен від @BotFather',1);return}try{await j('/api/settings',{method:'POST',body:new URLSearchParams({tgToken:v})});note('tmsg','Токен збережено.');loadTg();refresh()}catch(err){note('tmsg',err.message,1)}}
 else if(a==='reboot'){if(!confirm('Перезапустити Сторожа? АВР на кілька секунд втратить Wi-Fi.'))return;try{await j('/api/reboot',{method:'POST'})}catch(err){}note('smsg','Перезапуск…')}
 else if(a==='bcnpick'){const m=b.dataset.mac,nb=document.querySelector('nav button[data-t="set"]');if(nb)nb.click();
  setTimeout(()=>{$('b_mac').value=m;$('bcncard').scrollIntoView({behavior:'smooth'});$('b_name').focus()},300)}
 else if(a==='assign'){if(!confirm(b.dataset.old?'Вважати АВР-ом пристрій '+b.dataset.mac+' замість '+b.dataset.old+'? Сторож стежитиме за новим.':'Вважати пристрій '+b.dataset.mac+' АВР-ом?'))return;try{await j('/api/assign',{method:'POST',body:new URLSearchParams({mac:b.dataset.mac})});refresh();if(tab==='set')loadSet()}catch(err){alert(err.message)}}
 else if(a==='logout'){try{await j('/logout',{method:'POST'})}catch(err){}location.reload()}
 else if(a==='add'){try{await j('/api/addchat',{method:'POST',body:new URLSearchParams({id:b.dataset.id,name:b.dataset.name})});note('rmsg','Додано.');loadTg();refresh()}catch(err){note('rmsg',err.message,1)}}
});
//  1.0.39: поки вкладка браузера схована — стан не питаємо (забута вкладка годинами навантажувала Сторожа кожні 3 с);
//  відкрили знову — одразу оновлюємо
refresh();setInterval(()=>{if(document.hidden)return;if(tab==='set')bcnReload(false);else if(tab!=='upd'&&tab!=='ins')refresh()},3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&tab!=='upd'&&tab!=='ins'&&tab!=='set')refresh()});
</script></body></html>)PAGE";
