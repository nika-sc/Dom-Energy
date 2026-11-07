#pragma once
#include <Arduino.h>

// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ.
static const char PAGE_TMPL[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0a0f1a">
<title>Дом — энергия</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='8' fill='%2322d3ee'/%3E%3Cpath d='M18 6 9 18h6l-1 8 9-12h-6z' fill='%230a0f1a'/%3E%3C/svg%3E">
<style>
:root{
 --bg:#0a0f1a; --card:rgba(255,255,255,.035); --card2:rgba(255,255,255,.012); --ln:#1e293b;
 --ink:#e8eefc; --mut:#7f8ea8; --dim:#5f708a;
 --c1:#22d3ee; --c2:#34d399; --c3:#a78bfa; --c4:#fbbf24; --c5:#f472b6; --red:#f87171;
 --bar1:#22d3ee; --bar2:#fbbf24; --grid:#1e293b;
}
html[data-theme="light"]{
 --bg:#f4f6fb; --card:#fff; --card2:#fff; --ln:#e6ecf5;
 --ink:#0f172a; --mut:#64748b; --dim:#7c8ba1;
 --c1:#2563eb; --c2:#059669; --c3:#7c3aed; --c4:#c2410c; --c5:#be185d; --red:#dc2626;
 --bar1:#93b4f8; --bar2:#f59e0b; --grid:#e6ecf5;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;min-height:100vh;transition:background .15s,color .15s}
.wrap{max-width:1000px;margin:0 auto;padding:20px 16px 44px}
.top{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:14px}
.name{font-size:18px;font-weight:800;letter-spacing:-.02em}
.name i{color:var(--c1);font-style:normal}
.ver{font-size:10.5px;font-weight:700;color:var(--c1);background:transparent;border:1px solid var(--c1);border-radius:999px;padding:2px 8px;opacity:.95}
.chips{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap}
.chip{display:inline-flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--ln);
border-radius:999px;padding:4px 10px;font-size:12px;color:var(--mut)}
.dot{width:7px;height:7px;border-radius:50%;background:#475569}
.dot.ok{background:var(--c2)}
.iconbtn{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:999px;
padding:5px 11px;font:inherit;font-size:12px;cursor:pointer}
.iconbtn:hover{border-color:var(--c1)}
.hero{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;align-items:stretch}
@media(max-width:780px){.hero{grid-template-columns:repeat(2,minmax(0,1fr))}}
.card{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid var(--ln);border-radius:18px;padding:14px 16px}






.cap{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--mut);font-weight:700}
.pbar{height:9px;border-radius:99px;background:var(--grid);overflow:hidden;margin-top:10px}.pbar i{display:block;height:100%;width:0;background:var(--c1);border-radius:99px;transition:width .25s ease}
.tiles{display:grid;grid-template-columns:1fr;gap:10px;align-content:stretch}
@media(max-width:520px){.tiles{grid-template-columns:repeat(2,minmax(0,1fr))}}
.t{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid var(--ln);border-radius:14px;padding:11px 12px}
.t .k{font-size:9.5px;text-transform:uppercase;letter-spacing:.09em;color:var(--mut);font-weight:700}
.t .v{font-size:21px;font-weight:800;letter-spacing:-.03em;margin-top:3px;line-height:1.15}
.t .v small{font-size:11px;color:var(--mut);font-weight:600;margin-left:3px}
.t .s{font-size:10px;color:var(--dim);margin-top:2px}
.c1{color:var(--c1)}.c2{color:var(--c2)}.c3{color:var(--c3)}.c4{color:var(--c4)}.c5{color:var(--c5)}
.row4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:12px}
@media(max-width:780px){.row4{grid-template-columns:repeat(2,minmax(0,1fr))}}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}
@media(max-width:780px){.two{grid-template-columns:1fr}}
.head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:8px}
.ttl{font-size:10.5px;font-weight:800;text-transform:uppercase;letter-spacing:.09em;color:var(--mut)}
.legend{display:flex;gap:12px;font-size:10.5px;color:var(--mut);margin-left:auto}
.sw{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:4px}
.kv{display:flex;justify-content:space-between;gap:12px;font-size:13px;padding:7px 0;border-top:1px solid var(--ln)}
.kv:first-of-type{border-top:0}
svg.chart{display:block;width:100%;height:auto;touch-action:manipulation}
.ax{font-size:9px;fill:var(--dim)}
.nav{display:flex;align-items:center;gap:8px;margin-left:auto}
.nav button,.ctrl button{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:9px;
padding:4px 9px;cursor:pointer;font:inherit;font-size:12px}
.nav button:disabled{opacity:.35;cursor:default}
.ctrl{display:flex;gap:6px;margin-left:auto}
.ctrl button.on{border-color:var(--c1);color:var(--c1);font-weight:700;background:var(--card)}
.date{font-size:12px;color:var(--mut);text-align:center;min-width:110px}
.gapbox{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--c4);margin-top:8px}
input[type=range]{width:120px;accent-color:var(--c1)}
input[type=number]{width:56px;background:var(--card);border:1px solid var(--ln);color:var(--ink);
border-radius:8px;padding:2px 6px;font:inherit;text-align:right}
.tip{position:fixed;z-index:50;background:var(--bg);color:var(--ink);border:1px solid var(--ln);border-radius:10px;
padding:8px 10px;font-size:12px;box-shadow:0 8px 24px rgba(0,0,0,.35);pointer-events:none;max-width:250px;display:none}

.readout{display:flex;flex-wrap:wrap;gap:6px 16px;align-items:baseline;font-size:12.5px;color:var(--mut);
background:var(--card);border:1px solid var(--ln);border-radius:12px;padding:7px 11px;margin:0 0 8px;min-height:34px}
.readout b{color:var(--ink);font-weight:700}
.readout .h{color:var(--c1);font-weight:800}
.readout .empty{opacity:.6}
svg.chart rect.bar,svg.chart rect.bar2{cursor:pointer}svg.chart rect.hit,svg.chart rect.hit2{cursor:pointer}
svg.chart rect.bar.sel{stroke:var(--ink);stroke-width:2.5}
svg.chart rect.bar2.sel{stroke:var(--c1);stroke-width:2}
svg.chart{cursor:default}

.detail{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-bottom:12px}
@media(max-width:900px){.detail{grid-template-columns:1fr}}
.d .bigv{font-size:30px;font-weight:800;letter-spacing:-.03em;line-height:1.1}
.d .bigv small{font-size:12px;color:var(--mut);font-weight:600;margin-left:4px}
.d .dsub{font-size:10.5px;color:var(--dim);margin-top:2px}
.d .dnow{font-size:10.5px;color:var(--mut);margin-left:auto}
.d .dstats{display:flex;gap:14px;margin-top:8px;font-size:11px;color:var(--mut)}
.d .dstats b{display:block;color:var(--ink);font-size:13px;font-weight:700}
.d .dstats i{font-style:normal;font-size:9.5px;color:var(--dim)}
.d svg.dchart{display:block;width:100%;height:64px;margin-top:8px}
.build{font-size:10px;color:var(--dim);text-align:center;margin-top:14px;opacity:.7}

.qgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}@media(max-width:820px){.qgrid{grid-template-columns:1fr}}.qrow{display:flex;gap:10px;align-items:center;flex-wrap:wrap;font-size:12.5px;color:var(--mut);padding:6px 0}.qrow b{color:var(--ink)}.qbig{font-size:26px;font-weight:800;letter-spacing:-.03em;line-height:1.1}.qbig small{font-size:12px;color:var(--mut);font-weight:600;margin-left:3px}.pbar2{height:8px;border-radius:99px;background:var(--grid);overflow:hidden;margin-top:6px}.pbar2 i{display:block;height:100%;width:0;background:var(--c1);border-radius:99px}.qbtn{background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:9px;padding:4px 10px;cursor:pointer;font:inherit;font-size:12px}.qbtn.on{border-color:var(--c2);color:var(--c2);font-weight:700}.qbtn.danger.on{border-color:var(--red);color:var(--red)}.qin{width:64px;background:var(--card);border:1px solid var(--ln);color:var(--ink);border-radius:8px;padding:3px 7px;font:inherit;font-size:12px;text-align:right}.evlist{max-height:190px;overflow:auto;font-size:12px;color:var(--mut)}.ev{border-bottom:1px solid var(--ln);padding:5px 0;display:flex;gap:8px}.ev:last-child{border-bottom:0}.ev .t{color:var(--ink);font-variant-numeric:tabular-nums}.ev .sag{color:var(--c4)} .ev .swl{color:var(--red)}
.pb{display:flex;gap:6px;flex-wrap:wrap;margin-top:2px}
.pb button{background:var(--card);border:1px solid var(--ln);color:var(--mut);border-radius:999px;
 padding:5px 11px;font:inherit;font-size:12px;cursor:pointer;white-space:nowrap}
.pb button:hover{border-color:var(--c1);color:var(--ink)}
.pb button.on{background:var(--c1);border-color:var(--c1);color:#04202a;font-weight:700}
.rt{width:100%;border-collapse:collapse;margin-top:10px;font-size:12.5px}
.rt th{font-size:10px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);font-weight:700;
 text-align:right;padding:6px 4px;border-bottom:1px solid var(--ln);white-space:nowrap}
.rt th:first-child,.rt td:first-child{text-align:left}
.rt td{padding:6px 4px;border-bottom:1px solid var(--ln);text-align:right;color:var(--ink);white-space:nowrap}
.rt tr:hover td{background:var(--card)}
.rt td.mut{color:var(--mut)}
.rtwrap{overflow-x:auto}
@media print{
 body{background:#fff;color:#000}
 .top,.pb,.qrow,.build,#tip,.btnrow{display:none!important}
 .card{break-inside:avoid;border:1px solid #ccc;background:#fff;color:#000}
 .rt th,.rt td{color:#000;border-color:#ccc}
}
.sch{font-size:12.5px;color:var(--mut)}.sch .r{display:flex;gap:8px;align-items:center;padding:5px 0;border-bottom:1px solid var(--ln)}.sch .r:last-child{border-bottom:0}.sch .t{color:var(--ink);font-weight:700;font-variant-numeric:tabular-nums}.sch .days{font-size:11px}</style>
</head>
<body>
<div class="wrap">
<div class="top">
  <span class="name"><i>◈</i> Дом — энергия</span><span class="ver">CYD-__FW__</span>
  <span class="chips">
    <span class="chip" id="cNet"><i class="dot __NETCLS__"></i><span>__NETTXT__</span></span>
    <span class="chip" id="cRssi">__RSSI__ дБм</span><span class="chip" id="cUpd">обновлено 0 с</span>
    <button class="iconbtn" id="themeBtn" type="button">☀ Светлая</button>
  </span>
</div>

<div class="detail">
  <div class="card d">
    <div class="head"><span class="ttl">Напряжение</span><span class="dnow" id="dVnow">получено —</span></div>
    <div class="bigv c1"><span id="dVbig">—</span><small>В</small></div>
    <div class="dsub">норма 207–253 В · ГОСТ 32144-2013</div>
    <div class="dstats">
      <div><span>мин</span><b id="dVmin">—</b><i id="dVminT">—</i></div>
      <div><span>сред</span><b id="dVavg">—</b><i>за сутки</i></div>
      <div><span>макс</span><b id="dVmax">—</b><i id="dVmaxT">—</i></div>
    </div>
    <svg class="dchart" id="dChartV" viewBox="0 0 300 64" preserveAspectRatio="none"></svg>
  </div>
  <div class="card d">
    <div class="head"><span class="ttl">Ток</span><span class="dnow" id="dAnow">получено —</span></div>
    <div class="bigv c3"><span id="dAbig">—</span><small>А</small></div>
    <div class="dsub"><span id="pcos">cos φ —</span> · по мощности и напряжению</div>
    <div class="dstats">
      <div><span>мин</span><b id="dAmin">—</b><i id="dAminT">—</i></div>
      <div><span>сред</span><b id="dAavg">—</b><i>за сутки</i></div>
      <div><span>макс</span><b id="dAmax">—</b><i id="dAmaxT">—</i></div>
    </div>
    <svg class="dchart" id="dChartA" viewBox="0 0 300 64" preserveAspectRatio="none"></svg>
  </div>
  <div class="card d">
    <div class="head"><span class="ttl">Темп. реле</span><span class="dnow" id="dTnow">получено —</span></div>
    <div class="bigv c4"><span id="dTbig">—</span><small>°C</small></div>
    <div class="dsub">корпус реле · перегрев выше 60 °C</div>
    <div class="dstats">
      <div><span>мин</span><b id="dTmin">—</b><i id="dTminT">—</i></div>
      <div><span>сред</span><b id="dTavg">—</b><i>за сутки</i></div>
      <div><span>макс</span><b id="dTmax">—</b><i id="dTmaxT">—</i></div>
    </div>
    <svg class="dchart" id="dChartT" viewBox="0 0 300 64" preserveAspectRatio="none"></svg>
  </div>
</div>

<div class="hero">
  <div class="card d"><div class="head"><span class="ttl">Мощность</span><span class="dnow" id="gcap">шкала 300 Вт</span></div><div class="bigv c1"><span id="mp">__MP__</span><small>Вт</small></div><div class="pbar"><i id="pfill"></i></div><div class="dsub">шкала — по пику за сутки</div></div>
  <div class="tiles" style="display:contents">
    <div class="t"><div class="k">Реле</div><div class="v" id="rel"><i class="dot __RELCLS__"></i>__RELTXT__</div><div class="s">&nbsp;</div></div>
    <div class="t"><div class="k">Тариф</div><div class="v c5"><span id="ptar">__TAR__</span><small>₽</small></div><div class="s">&nbsp;</div></div>
    <div class="t"><div class="k">Счётчик</div><div class="v c2"><span id="pe">__PE__</span><small>кВт·ч</small></div><div class="s" id="peAge">—</div></div>
  </div>
</div>

<div class="row4">
  <div class="t"><div class="k">Сегодня</div><div class="v c1"><span id="tkwh">__TKWH__</span><small>кВт·ч</small></div><div class="s" id="tkwhAge">—</div></div>
  <div class="t"><div class="k">Стоимость</div><div class="v c2"><span id="tcost">__TCOST__</span><small>₽</small></div><div class="s">&nbsp;</div></div>
  <div class="t"><div class="k">Вчера</div><div class="v"><span id="ykwh">__YKWH__</span><small>кВт·ч</small></div><div class="s"><span id="ykost">__YKOST__</span> ₽</div></div>
  <div class="t"><div class="k">Месяц</div><div class="v"><span id="mkwh">__MKWH__</span><small>кВт·ч</small></div><div class="s"><span id="mcost">__MCOST__</span> ₽</div></div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">История</span>
    <span class="nav"><button id="prev" type="button">‹</button><span class="date" id="dateLabel">—</span><button id="next" type="button">›</button></span>
  </div>
  <div class="head"><span class="ttl">Энергия и мощность по часам</span>
    <span class="ctrl"><button id="unitBtn" type="button">кВт·ч</button><button id="numBtn" type="button" class="on">числа</button><button id="refBtn" type="button">обновить</button></span>
  </div>
  <div class="head" style="margin:-4px 0 4px"><span class="legend" style="margin-left:0"><span><i class="sw" style="background:var(--bar1)"></i>средняя</span><span><i class="sw" style="background:var(--bar2)"></i>пик, Вт</span></span></div>
  <svg class="chart" id="chE" viewBox="0 0 900 190" preserveAspectRatio="none"></svg>
  <div class="head" style="margin-top:12px"><span class="ttl">Напряжение и температура</span>
    <span class="legend"><span><i class="sw" style="background:#60a5fa"></i>В</span><span><i class="sw" style="background:#f472b6"></i>°C</span></span>
  </div>
  <svg class="chart" id="chVT" viewBox="0 0 900 140" preserveAspectRatio="none"></svg>
  <div class="gapbox" id="gapBox" style="display:none"><span id="gapTxt"></span><button class="iconbtn" id="gapBtn" type="button">подтянуть</button></div>
</div>

<div class="qgrid" style="margin-top:12px">
  <div class="card">
    <div class="head"><span class="ttl">Качество энергии · ГОСТ 32144</span><span class="legend" id="gostSrc">за сеанс</span></div>
    <div class="qbig c1"><span id="gostAvg">—</span><small>В среднее</small></div>
    <div class="pbar2"><i id="gostBar"></i></div>
    <div class="qrow"><span>мин <b id="gostMin">—</b></span><span>макс <b id="gostMax">—</b></span><span>вне нормы <b id="gostBad">—</b></span></div>
    <div class="qrow"><span>норма <b id="gostRange">207–253 В</b></span><span>замеров <b id="gostN">—</b></span></div>
  </div>
  <div class="card">
    <div class="head"><span class="ttl">Защита реле</span><span class="legend">настройки автомата Tuya</span></div>
    <div class="qrow"><span>перенапряжение &gt;</span><b><input class="qin" id="pvmax" value="250"> В</b>
      <span>просадка &lt;</span><b><input class="qin" id="pvmin" value="200"> В</b></div>
    <div class="qrow"><span>перегрев &gt;</span><b><input class="qin" id="ptmax" value="60"> °C</b>
      <span>перегруз &gt;</span><b><input class="qin" id="pimax" value="25"> А</b></div>
    <div class="qrow"><button class="qbtn" id="protApply" type="button">записать в автомат</button>
      <button class="qbtn" id="protLocal" type="button">только в плату</button>
      <span>срабатываний <b id="protTrig">0</b></span></div>
    <div class="qrow"><span>в автомате:</span><b id="tuyaS">—</b></div>
    <div class="qrow"><span>проверка:</span><input class="qin" id="ptestv" value="270"> <span>В</span>
      <button class="qbtn" id="protTest" type="button">проверить</button><b id="ptestres">—</b></div>
  </div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">События напряжения</span>
    <span class="legend"><span id="evCnt">0</span> · провалы и всплески · <a href="/csv" style="color:var(--c1)">CSV</a></span>
    <span class="ctrl"><button class="qbtn" id="weekBtn" type="button">7 дней</button></span>
  </div>
  <svg class="chart" id="chW" viewBox="0 0 900 150" preserveAspectRatio="none" style="display:none"></svg>
  <div class="evlist" id="evList"><div class="ev"><span>пока событий нет</span></div></div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">Планировщик реле</span>
    <span class="legend"><span class="qbtn danger" id="allowBtn" type="button">выключение: запрещено</span></span>
  </div>
  <div class="sch" id="schList"><div class="r"><span>правил пока нет</span></div></div>
  <div class="qrow" style="margin-top:8px">
    <input class="qin" id="schTime" value="07:30" style="width:76px">
    <select class="qbtn" id="schAct"><option value="on">включить</option><option value="off">выключить</option></select>
    <input class="qin" id="schDays" value="127" style="width:56px" title="маска дней: 127 = все, 31 = пн-пт">
    <button class="qbtn" id="schAdd" type="button">добавить</button>
    <span class="small">маска дней: 127 — все, 31 — пн–пт, 96 — сб+вс</span>
  </div>
</div>

<div class="card" style="margin-top:12px" id="rngCard">
  <div class="head"><span class="ttl">Статистика за период</span>
    <span class="legend"><span id="gapInfo">—</span> · <span id="rngNote">выбери период</span></span></div>
  <div class="pb" id="rngBtns">
    <button type="button" data-p="today">Сегодня</button>
    <button type="button" data-p="yest">Вчера</button>
    <button type="button" data-p="d2">Позавчера</button>
    <button type="button" data-p="d7">7 дней</button>
    <button type="button" data-p="d30">30 дней</button>
    <button type="button" data-p="week">Неделя</button>
    <button type="button" data-p="month">Месяц</button>
    <button type="button" data-p="quarter">Квартал</button>
    <button type="button" data-p="half">Полгода</button>
    <button type="button" data-p="prev">Прошлый месяц</button>
    <button type="button" data-p="year">Год</button>
    <button type="button" data-p="ytd">С начала года</button>
  </div>
  <div class="qrow" style="margin-top:8px">
    <input class="qin" type="date" id="rngFrom"> <span>—</span> <input class="qin" type="date" id="rngTo">
    <button class="qbtn" id="rngGo" type="button">Применить</button>
    <button class="qbtn" id="rngCsv" type="button">CSV за период</button>
    <button class="qbtn" id="rngPrint" type="button">Печать</button>
    <button class="qbtn" id="gapBtn" type="button" title="добрать пропущенные строки из облака Tuya">Заполнить пропуски</button>
  </div>
  <div class="row4" style="margin-top:10px">
    <div class="t"><div class="k">Расход</div><div class="v c1" id="rKwh">—</div><div class="s" id="rKwhS">—</div></div>
    <div class="t"><div class="k">Стоимость</div><div class="v c2" id="rCost">—</div><div class="s" id="rCostS">—</div></div>
    <div class="t"><div class="k">День / ночь</div><div class="v" id="rDN">—</div><div class="s" id="rDNS">—</div></div>
    <div class="t"><div class="k">В среднем в сутки</div><div class="v c3" id="rAvg">—</div><div class="s" id="rAvgS">—</div></div>
  </div>
  <div class="rtwrap"><table class="rt" id="rngTbl"><thead><tr>
    <th>День</th><th>кВт·ч</th><th>₽</th><th>день</th><th>ночь</th><th>U мин–макс</th><th>t мин–макс</th><th>пик</th><th>погода</th>
  </tr></thead><tbody><tr><td colspan="9" class="mut">нет данных</td></tr></tbody></table></div>
</div>

<div class="two">
  <div class="card"><div class="head"><span class="ttl">Погода · Адлер</span></div>
    <div class="kv"><span>Сейчас</span><b id="wxNow">—</b></div>
    <div class="kv"><span>Завтра</span><b id="wxTom">—</b></div></div>
  <div class="card"><div class="head"><span class="ttl">Тарифы и прогноз</span></div>
    <div class="kv"><span>День · 06–23</span><b id="tarDay">__TARDAY__ ₽/кВт·ч</b></div>
    <div class="kv"><span>Ночь · 23–06</span><b id="tarNight">__TARNIGHT__ ₽/кВт·ч</b></div>
    <div class="kv"><span>Прогноз на месяц</span><b id="fc">—</b></div></div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">Экран платы</span><span class="nav"><button id="scrBtn" type="button">Экран</button></span></div>
  <div class="kv"><span>Подсветка</span><b id="scrState">—</b></div>
  <div class="kv"><span>Яркость</span><b><input id="scrBrightRange" type="range" min="5" max="100" step="5" value="100"> <span id="scrBright">—</span></b></div>
  <div class="kv"><span>Гасить через</span><b><input id="scrIdle" type="number" min="0" max="60" step="1" value="2"> мин</b></div>
  <div class="kv"><span>Экономия</span><b id="scrEco">—</b></div>
</div>
<div class="build">сборка 05.11.2026 12:21 · CYD-4.10.1 · файл 100% с платы</div>
</div>
<div id="tip" class="tip"></div>
<script>
"use strict";
var BOOT_DATE="__DATE__",BOOT_HOUR=__HOUR__;
var S={date:"",days:[],hist:null,today:"",watt:false,nums:true,last:0,dayMax:0,sel:null};
function G(i){return document.getElementById(i)}
function txt(i,t){var e=G(i);if(e)e.textContent=t}
function num(v,n){return (v==null||isNaN(v))?"—":(+v).toFixed(n==null?0:n)}
function hhmm(ts){if(!ts)return "—";var d=new Date(ts*1000);return("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)}
function fmtDate(s){if(!s)return "—";var p=s.split("-");var m=["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"];return (+p[2])+" "+m[(+p[1])-1]+" "+p[0]}
function niceMax(v){var e=Math.pow(10,Math.floor(Math.log10(v||1))),f=(v||1)/e,c=[1,1.2,1.5,2,2.5,3,4,5,6,8,10];for(var i=0;i<c.length;i++)if(f<=c[i]+1e-9)return c[i]*e;return 10*e}

/* ---------- тема ---------- */
function setTheme(t){
  document.documentElement.setAttribute("data-theme",t);
  G("themeBtn").textContent = (t==="dark" ? "☀ Светлая" : "🌙 Тёмная");
  try{localStorage.setItem("dom.theme",t)}catch(e){}
}
G("themeBtn").addEventListener("click",function(){
  setTheme(document.documentElement.getAttribute("data-theme")==="dark"?"light":"dark");
});
try{var th=localStorage.getItem("dom.theme"); if(th) setTheme(th); else setTheme("dark");}catch(e){setTheme("dark")}

/* ---------- живое состояние ---------- */
function apply(d){
  if(!d)return;
  S.data=d; S.last=Date.now();
  txt("mv",num(d.v)); txt("mp",num(d.p)); txt("ma",num(d.amp,2)); txt("mt",num(d.temp,1));
  txt("ptar",num(d.tariff,2)); txt("pe",num(d.e,2));
  txt("tkwh",num(d.tkwh,2)); txt("tcost",num(d.tcost,2));
  txt("ykwh",num(d.ykwh,2)); txt("ykost",num(d.ykost,2));
  txt("mkwh",num(d.mkwh,2)); txt("mcost",num(d.mcost,2));
  txt("cRssi",num(d.rssi)+" дБм");
  txt("dVbig",num(d.v)); txt("dAbig",num(d.amp,2)); txt("dTbig",num(d.temp,1));
  txt("dVnow","получено "+hhmm(d.vAt)); txt("dAnow","получено "+hhmm(d.pAt)); txt("dTnow","получено "+hhmm(d.tAt));
  txt("mvAge","получено "+hhmm(d.vAt)); txt("mtAge","получено "+hhmm(d.tAt));
  txt("peAge","получено "+hhmm(d.eAt)); txt("tkwhAge","получено "+hhmm(d.eAt));
  var rel=G("rel");
  if(rel){rel.innerHTML='<i class="dot '+(d.r?"ok":"")+'"></i>'+(d.r?"ВКЛ":"ВЫКЛ");rel.style.color=d.r?"var(--c2)":"var(--red)"}
  txt("pcos","cos φ "+num((d.v>50&&d.amp>0.001)?d.p/(d.v*d.amp):0,2));
  var on=(d.disp===1||d.disp==="1");
  txt("scrState",on?"включена":"выключена");
  var bp=d.bright!=null?Math.round(d.bright/2.55):0;
  txt("scrBright",bp+" %");
  var rg=G("scrBrightRange"); if(rg&&document.activeElement!==rg)rg.value=Math.max(5,bp);
  var id=G("scrIdle"); if(id&&document.activeElement!==id)id.value=(d.idle!=null?d.idle:0);
  txt("scrEco",!on?"экран погашен":(d.idle>0?"гаснет через "+Math.round((d.idleMs||0)/60000)+" мин":"автогашение выкл"));
  txt("wxNow",(d.wDesc||"—")+" · "+num(d.wT,1)+" °C · дождь "+num(d.wPop)+" %");
  txt("wxTom",num(d.wTminT)+"…"+num(d.wTmaxT)+" °C · дождь "+num(d.wPopT)+" %");
  txt("fc",num(d.mkwh/new Date().getDate()*30,1)+" кВт·ч");
  ring(d);
  if(S.hist&&S.date===S.today)drawE(S.hist);
  if(typeof d.gaps!=="undefined") gapShow(d.gaps, d.gapMin);
}
function ring(d){
  var pw=+d.p||0;
  var mx=Math.max(S.dayMax*1.15, pw*1.3, 300);
  mx=niceMax(mx);
  var f=Math.max(0.01,Math.min(1,pw/mx));
  var el=G("pfill");
  if(el){
    el.style.width=(f*100).toFixed(1)+"%";
    el.style.background = pw>=mx*0.85 ? "var(--red)" : (pw>=mx*0.5 ? "var(--c4)" : "var(--c1)");
  }
  txt("gcap","шкала "+Math.round(mx)+" Вт");
}
function poll(){fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(apply).catch(function(){txt("cNet","нет связи")})}
setInterval(function(){var s=Math.round((Date.now()-S.last)/1000);txt("cUpd",(s>20?"нет связи ":"обновлено ")+s+" с")},1000);

/* ---------- история ---------- */
function loadDays(cb){
  fetch("/days",{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    S.days=j.days||[]; S.today=S.days[0]||""; if(!S.date) S.date=S.today;
    if(S.days.indexOf(S.date)<0) S.date=S.today;
    loadHist(cb);
  }).catch(function(){});
}

function loadHist(cb){
  if(!S.date){if(cb)cb();return}
  fetch("/history?date="+S.date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    S.hist=j;
    var mx=0,k;for(k=0;k<24;k++){var a=(j.hours||[])[k]||{};if(+a.pmax>mx)mx=+a.pmax}
    S.dayMax=mx;
    drawNav(); drawE(j); drawVT(j); renderDetails(j); if(cb)cb();
  }).catch(function(){if(cb)cb()});
}

function renderDetails(j){
  var h=(j&&j.hours)||[],i;
  var V=[],A=[],T=[];
  for(i=0;i<24;i++){
    var a=h[i]||{};
    V.push(a.v>50?a.v:null);
    A.push((a.v>50&&a.p>0)?(a.p/a.v):null);
    T.push(a.t==null?null:a.t);
  }
  stat24("dV",V,"",function(x){return Math.round(x)});
  stat24("dA",A,"",function(x){return x.toFixed(2)});
  stat24("dT",T,"",function(x){return x.toFixed(1)});
  var d=S.data||{};
  txt("dVnow","получено "+hhmm(d.vAt));
  txt("dAnow","получено "+hhmm(d.pAt));
  txt("dTnow","получено "+hhmm(d.tAt));
  txt("dVbig",num(d.v));txt("dAbig",num(d.amp,2));txt("dTbig",num(d.temp,1));
  mini("dChartV",V);
  mini("dChartA",A);
  mini("dChartT",T);
}
function stat24(prefix,arr,u,fmt){
  var mn=null,mx=null,sum=0,n=0,mnI=-1,mxI=-1,i;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;
    if(mn===null||v<mn){mn=v;mnI=i}
    if(mx===null||v>mx){mx=v;mxI=i}
    sum+=v;n++}
  txt(prefix+"min",mn===null?"—":fmt(mn));
  txt(prefix+"avg",n?fmt(sum/n):"—");
  txt(prefix+"max",mx===null?"—":fmt(mx));
  txt(prefix+"minT",mnI<0?"—":("0"+mnI).slice(-2)+":00");
  txt(prefix+"maxT",mxI<0?"—":("0"+mxI).slice(-2)+":00");
}
function mini(id,arr){
  var el=G(id); if(!el)return;
  var W=300,H=64,pl=2,pr=2,pt=6,pb=6,slot=(W-pl-pr)/24,i;
  var mn=null,mx=null;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;if(mn===null||v<mn)mn=v;if(mx===null||v>mx)mx=v}
  if(mn===null){el.innerHTML="";return}
  if(mx-mn<0.5){mx=mn+0.5}
  function y(v){return pt+(1-(v-mn)/(mx-mn))*(H-pt-pb)}
  var d="",pen=false;
  for(i=0;i<24;i++){var v=arr[i];if(v==null)continue;
    d+=(pen?"L":"M")+(pl+slot*(i+0.5)).toFixed(1)+" "+y(v).toFixed(1)+" ";pen=true}
  var s='<path d="'+d+'" fill="none" stroke="var(--c1)" stroke-width="1.8" stroke-linejoin="round"/>';
  s+='<text class="ax" x="'+(W-pr)+'" y="'+(pt+6)+'" text-anchor="end">'+num(mx,1)+'</text>';
  s+='<text class="ax" x="'+(W-pr)+'" y="'+(H-1)+'" text-anchor="end">'+num(mn,1)+'</text>';
  el.innerHTML=s;
}

function drawNav(){
  var i=S.days.indexOf(S.date);
  G("prev").disabled=!(i>=0&&i<S.days.length-1); G("next").disabled=!(i>0);
  txt("dateLabel",fmtDate(S.date)+(S.date===S.today?" · сегодня":""));
  var g=(S.hist&&S.hist.gaps)||null;
  var box=G("gapBox");
  if(box){box.style.display="none"}
}
function drawE(j){
  var h=j.hours||[],W=900,H=190,pl=12,pr=10,pt=14,pb=18,slot=(W-pl-pr)/24,base=H-pb,hh=base-pt,am=0,pm=0,i;
  for(i=0;i<24;i++){var a=h[i]||{};var vv=S.watt?+a.p||0:+a.kwh||0;if(vv>am)am=vv;if(+a.pmax>pm)pm=+a.pmax}
  var s='';
  s+='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="var(--grid)"/>';
  for(i=0;i<3;i++){var y=pt+(base-pt)*i/2.4;
    s+='<line x1="'+pl+'" y1="'+y.toFixed(1)+'" x2="'+(W-pr)+'" y2="'+y.toFixed(1)+'" stroke="var(--grid)" stroke-dasharray="2 4" opacity=".7"/>'}
  for(i=0;i<24;i++){
    var b=h[i]||{},v=b.n?(S.watt?+b.p||0:+b.kwh||0):0,x=pl+i*slot+slot*0.2,w=slot*0.6;
    if(v>0){var bh=Math.max(2,v/(am||1)*hh*0.66);
      s+='<rect class="bar" data-i="'+i+'" x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="var(--bar1)"/>';
      if(S.nums)s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(base-bh-3).toFixed(1)+'" text-anchor="middle">'+(S.watt?Math.round(v):num(v,2))+'</text>'}
    if(b.n&&+b.pmax>0){var ph=Math.max(2,(+b.pmax)/(pm||1)*hh*0.22);
      s+='<rect class="bar2" data-i="'+i+'" x="'+x.toFixed(1)+'" y="'+(base-ph).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+ph.toFixed(1)+'" rx="2" fill="var(--bar2)" opacity=".9"/>'}
  }
  for(i=0;i<24;i++){var hx=pl+i*slot;s+='<rect class="hit" data-i="'+i+'" x="'+hx.toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(base-pt)+'" fill="transparent"/>'}
  s+='<text class="ax" x="'+pl+'" y="'+(pt-4)+'">'+(S.watt?"средняя мощность, Вт":"энергия за час, кВт·ч")+'</text>';
  for(i=0;i<=3;i++){var xx=pl+(W-pl-pr)*i/3;s+='<text class="ax" x="'+xx.toFixed(1)+'" y="'+(H-3)+'" text-anchor="middle">'+(i*8)+'</text>'}
  G("chE").innerHTML=s;
  hit();
}
function hit(){
  var svg=G("chE");
  function select(i,ev){
    S.sel=+i;
    svg.querySelectorAll("rect.bar,rect.bar2").forEach(function(r){
      r.classList.toggle("sel", r.getAttribute("data-i")===i && r.classList.contains("bar"));
      r.classList.toggle("sel2", false);
    });
    tipShow(i,ev);
  }
  svg.querySelectorAll("rect.hit").forEach(function(r){
    r.addEventListener("pointerdown",function(ev){ev.preventDefault();select(this.getAttribute("data-i"),ev)});
  });
  if(S.sel!=null) select(S.sel);
}

function tipShow(i,ev){
  var h=(S.hist&&S.hist.hours)||[],x=h[+i]||{},t=G("tip");
  var from=("0"+(+i)).slice(-2)+":00",to=("0"+(+i+1)).slice(-2)+":00";
  var html="<b>"+from+"–"+to+"</b>";
  if(x.n){html+="<br>энергия <b>"+num(x.kwh,2)+" кВт·ч</b>";
    html+="<br>средняя <b>"+num(x.p,0)+" Вт</b>";
    if(x.pmax>0){html+="<br>пик <b>"+num(x.pmax,0)+" Вт</b>"+(x.pmaxT?" в "+x.pmaxT:"")+(x.pmaxDur>0?", "+Math.max(1,Math.round(x.pmaxDur/60))+" мин":"")}}
  else html+="<br>нет данных";
  t.innerHTML=html; t.style.display="block";
  var w=250,px=(ev.clientX||100),py=(ev.clientY||100);
  var left=Math.min(window.innerWidth-w-8,Math.max(8,px-w/2));
  var top=py-t.offsetHeight-16;                 // всегда ВЫШЕ курсора
  if(top<8) top=Math.min(window.innerHeight-t.offsetHeight-8,py+18);  // у верха — под курсором
  t.style.left=left+"px";
  t.style.top=top+"px";
}
function drawVT(j){
  var h=j.hours||[],W=900,H=140,pl=12,pr=10,pt=8,pb=14,slot=(W-pl-pr)/24,i;
  var vs=[],ts=[];
  for(i=0;i<24;i++){var a=h[i]||{};vs.push(+a.v||0);ts.push(a.t==null?null:+a.t)}
  var vmin=999,vmax=0,tmin=99,tmax=-99;
  for(i=0;i<24;i++){if(vs[i]>50){vmin=Math.min(vmin,vs[i]);vmax=Math.max(vmax,vs[i])}if(ts[i]!=null){tmin=Math.min(tmin,ts[i]);tmax=Math.max(tmax,ts[i])}}
  if(vmax<=0)vmax=250; if(vmin>vmax)vmin=vmax-10; if(tmax<tmin){tmin=20;tmax=25}
  function vy(v){return pt+(1-(v-vmin)/((vmax-vmin)||1))*(H-pt-pb)}
  function ty(t){return pt+(1-(t-tmin)/((tmax-tmin)||1))*(H-pt-pb)}
  var sv='',st='',pv=false,ptt=false;
  for(i=0;i<24;i++){var x=pl+slot*(i+0.5);
    if(vs[i]>50){sv+=(pv?'L':'M')+x.toFixed(1)+' '+vy(vs[i]).toFixed(1)+' ';pv=true}
    if(ts[i]!=null){st+=(ptt?'L':'M')+x.toFixed(1)+' '+ty(ts[i]).toFixed(1)+' ';ptt=true}}
  var s='<line x1="'+pl+'" y1="'+(H-pb)+'" x2="'+(W-pr)+'" y2="'+(H-pb)+'" stroke="var(--grid)"/>';
  if(sv)s+='<path d="'+sv+'" fill="none" stroke="#60a5fa" stroke-width="2"/>';
  if(st)s+='<path d="'+st+'" fill="none" stroke="#f472b6" stroke-width="2"/>';
  var sel=S.selV;
  if(sel!=null&&sel<24){
    var sx=pl+slot*(sel+0.5);
    if(vs[sel]>50)s+='<circle cx="'+sx.toFixed(1)+'" cy="'+vy(vs[sel]).toFixed(1)+'" r="3.6" fill="#60a5fa" stroke="#fff" stroke-width="1.6"/>';
    if(ts[sel]!=null)s+='<circle cx="'+sx.toFixed(1)+'" cy="'+ty(ts[sel]).toFixed(1)+'" r="3.6" fill="#f472b6" stroke="#fff" stroke-width="1.6"/>';
  }
  for(i=0;i<24;i++){s+='<rect class="hit2" data-i="'+i+'" x="'+(pl+i*slot).toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(H-pt-pb)+'" fill="transparent"/>'}
  s+='<text class="ax" x="'+pl+'" y="'+(H-1)+'">'+vmin.toFixed(0)+'–'+vmax.toFixed(0)+' В · '+tmin.toFixed(1)+'–'+tmax.toFixed(1)+' °C</text>';
  G("chVT").innerHTML=s;
  bindVT();
}
function bindVT(){
  var svg=G("chVT");
  svg.querySelectorAll("rect.hit2").forEach(function(r){
    r.addEventListener("pointerdown",function(ev){
      ev.preventDefault();
      S.selV=+this.getAttribute("data-i");
      drawVT(S.hist||{hours:[]});
      tipShowVT(S.selV,ev);
    });
  });
}
function tipShowVT(i,ev){
  var h=(S.hist&&S.hist.hours)||[],x=h[+i]||{},t=G("tip");
  var from=("0"+(+i)).slice(-2)+":00",to=("0"+(+i+1)).slice(-2)+":00";
  var html="<b>"+from+"–"+to+"</b>";
  if(x.v>50)html+="<br>напряжение <b>"+num(x.v,0)+" В</b>";
  if(x.t!=null)html+="<br>температура <b>"+num(x.t,1)+" °C</b>";
  if(!(x.v>50)&&x.t==null)html+="<br>нет данных";
  t.innerHTML=html; t.style.display="block";
  var w=250,px=(ev.clientX||100),py=(ev.clientY||100);
  var left=Math.min(window.innerWidth-w-8,Math.max(8,px-w/2));
  var top=py-t.offsetHeight-16;
  if(top<8)top=Math.min(window.innerHeight-t.offsetHeight-8,py+18);
  t.style.left=left+"px"; t.style.top=top+"px";
}

/* ---------- кнопки ---------- */
G("unitBtn").addEventListener("click",function(){
  S.watt=!S.watt; this.textContent=S.watt?"Вт":"кВт·ч"; this.classList.toggle("on",S.watt);
  if(S.hist)drawE(S.hist);
});
G("numBtn").addEventListener("click",function(){
  S.nums=!S.nums; this.classList.toggle("on",S.nums);
  if(S.hist)drawE(S.hist);
});
G("refBtn").addEventListener("click",function(){
  var b=this; b.textContent="…"; b.disabled=true;
  loadDays(function(){b.textContent="обновить";b.disabled=false;poll()});
});
G("prev").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>=0&&i<S.days.length-1){S.date=S.days[i+1];loadHist()}});
G("next").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>0){S.date=S.days[i-1];loadHist()}});
G("scrBtn").addEventListener("click",function(){
  fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
    var on=(d.disp===1||d.disp==="1");
    return fetch("/disp?"+(on?"on=0":"on=1"),{cache:"no-store"});
  }).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("scrBrightRange").addEventListener("input",function(){
  var v=+this.value; txt("scrBright",v+" %");
  fetch("/disp?bright="+v,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("scrIdle").addEventListener("change",function(){
  var n=+this.value; if(isNaN(n)||n<0||n>60)return;
  fetch("/disp?idle="+n,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("gapBtn").addEventListener("click",function(){
  var b=this; b.disabled=true; b.textContent="тяну…";
  fetch("/gapfill?run=1&from="+S.date+"&to="+S.date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var n0=(j&&j.gaps)||0,t0=Date.now(),last=-1,same=0;
    var iv=setInterval(function(){
      fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
        if(d.gaps!==last){last=d.gaps;same=0}else same++;
        b.textContent="осталось "+d.gaps+"…";
        if((!d.gapAct||same>=2)&&Date.now()-t0>5000){
          clearInterval(iv); b.textContent=(d.gaps<n0?"готово":"в облаке нет данных"); b.disabled=false; loadHist();
        }
      }).catch(function(){});
    },2500);
  }).catch(function(){b.disabled=false;b.textContent="ошибка"});
});

document.addEventListener("pointerdown",function(ev){
  var el=ev.target;
  if(el&&el.closest&&(el.closest("#chE")||el.closest("#chVT")))return;
  G("tip").style.display="none";
},true);
window.addEventListener("scroll",function(){G("tip").style.display="none"},{passive:true});
window.addEventListener("resize",function(){G("tip").style.display="none"});


var Q = {on:false, cut:false, week:false};

function gostUpdate(){
  fetch("/gost", {cache:"no-store"}).then(function(r){return r.json()}).then(function(g){
    if(!g || !g.n) return;
    txt("gostAvg", num(g.vavg,1));
    txt("gostMin", num(g.vmin,0)+" В");
    txt("gostMax", num(g.vmax,0)+" В");
    txt("gostBad", num(g.badPct,2)+" %");
    txt("gostN", g.n);
    txt("gostRange", num(g.lo,0)+"–"+num(g.hi,0)+" В");
    var bar=G("gostBar");
    if(bar){ bar.style.width = Math.min(100, Math.max(0, 100 - Math.min(100, g.badPct*10)))+"%";
             bar.style.background = g.badPct>5 ? "var(--c4)" : "var(--c2)"; }
  }).catch(function(){});
}

function vlogUpdate(){
  fetch("/vlog", {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var items=(j&&j.items)||[];
    txt("evCnt", items.length);
    var box=G("evList"); if(!box) return;
    if(!items.length){ box.innerHTML='<div class="ev"><span>пока событий нет</span></div>'; return; }
    var html="";
    for(var i=0;i<items.length && i<40;i++){
      var e=items[i], d=new Date((e.t||0)*1000);
      var hh=("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
      html += '<div class="ev"><span class="t">'+hh+'</span><span class="'+(e.k?"swl":"sag")+'">'+
              (e.k?"всплеск":"провал")+' '+e.v+' В</span><span>'+e.s+' с</span></div>';
    }
    box.innerHTML=html;
  }).catch(function(){});
}

function protRender(p){
  if(!p) return;
  Q.on = !!p.on; Q.cut = !!p.cut;
  txt("pvmax", p.vmax); txt("pvmin", p.vmin); txt("ptmax", p.tmax); txt("ppmax", p.pmax);
  var b=G("protOnBtn"); if(b){ b.textContent = Q.on ? "вкл" : "выкл"; b.className = "qbtn"+(Q.on?" on":""); }
  var c=G("protCut"); if(c){ c.textContent = "отключение реле: "+(Q.cut?"вкл":"выкл"); c.className="qbtn danger"+(Q.cut?" on":""); }
  txt("protTrig", p.trig||0);
  if(p.vmax) G("pvmax").value=p.vmax;
  if(p.vmin) G("pvmin").value=p.vmin;
  if(p.tmax) G("ptmax").value=p.tmax;
  if(p.pmax) G("pimax").value=Math.round(p.pmax/230);
}

function protUpdate(){
  fetch("/protect", {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
}

function protSet(qs){
  fetch("/protect?"+qs, {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
}

function initQuality(){
  var b=G("protOnBtn");
  if(b) b.addEventListener("click", function(){ protSet("on="+(Q.on?0:1)); });
  var c=G("protCut");
  if(c) c.addEventListener("click", function(){
    if(!Q.cut && !confirm("Разрешить плате физически отключать реле при аварии?")) return;
    protSet("cut="+(Q.cut?0:1));
  });
  function vals(){ return "vmax="+G("pvmax").value+"&vmin="+G("pvmin").value+
                          "&tmax="+G("ptmax").value+"&imax="+G("pimax").value; }
  var ap=G("protApply");
  if(ap) ap.addEventListener("click", function(){
    if(!confirm("Записать эти пороги в автомат Tuya? Они будут работать даже при выключенной плате.")) return;
    var b=this; b.disabled=true; b.textContent="пишу…";
    fetch("/protect?apply=1&"+vals(), {cache:"no-store"}).then(function(r){return r.json()}).then(function(p){
      protRender(p);
      b.textContent = (p.apply && (p.apply.a1||p.apply.a2)) ? "записано" : "ошибка";
      setTimeout(function(){ b.textContent="записать в автомат"; b.disabled=false; }, 2500);
    }).catch(function(){ b.textContent="ошибка"; b.disabled=false; });
  });
  var lo=G("protLocal");
  if(lo) lo.addEventListener("click", function(){
    fetch("/protect?"+vals(), {cache:"no-store"}).then(function(r){return r.json()}).then(protRender).catch(function(){});
  });
  var tt=G("protTest");
  if(tt) tt.addEventListener("click", function(){
    fetch("/protect?test="+G("ptestv").value, {cache:"no-store"}).then(function(r){return r.json()}).then(function(p){
      txt("ptestres", (p.test && p.test.action) ? p.test.action : "—");
    }).catch(function(){});
  });
  var wb=G("weekBtn");
  if(wb) wb.addEventListener("click", function(){
    Q.week=!Q.week; this.classList.toggle("on", Q.week);
    G("chW").style.display = Q.week ? "block" : "none";
    if(Q.week) drawWeek();
  });
  gostUpdate(); vlogUpdate(); protUpdate();
  setInterval(gostUpdate, 15000);
  setInterval(vlogUpdate, 20000);
  setInterval(protUpdate, 20000);
}

function drawWeek(){
  var days=S.days.slice(0,7);
  var sums=new Array(days.length).fill(0), loaded=0;
  days.forEach(function(d,idx){
    fetch("/history?date="+d, {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
      var h=(j&&j.hours)||[], s=0;
      for(var i=0;i<h.length;i++){ s += (+h[i].kwh||0); }
      sums[idx]=s;
    }).catch(function(){}).then(function(){
      loaded++;
      if(loaded===days.length) renderWeek(days.slice().reverse(), sums.slice().reverse());
    });
  });
}

function renderWeek(days, sums){
  var W=900,H=150,pl=14,pr=10,pt=16,pb=22,i;
  var slot=(W-pl-pr)/Math.max(1,days.length), base=H-pb, mx=0;
  for(i=0;i<sums.length;i++) if(sums[i]>mx) mx=sums[i];
  var s='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="var(--grid)"/>';
  for(i=0;i<days.length;i++){
    var v=sums[i], x=pl+i*slot+slot*0.18, w=slot*0.64;
    var bh = mx>0 ? Math.max(2, v/mx*(base-pt)*0.8) : 2;
    s+='<rect x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="var(--bar1)"/>';
    s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(base-bh-4).toFixed(1)+'" text-anchor="middle">'+v.toFixed(2)+'</text>';
    s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(H-6)+'" text-anchor="middle">'+days[i].slice(8)+'.'+days[i].slice(5,7)+'</text>';
  }
  G("chW").innerHTML=s;
}


var SCH = {cnt:0, rules:[], allowOff:0};

function schRender(j){
  if(!j) return;
  SCH = j;
  var box=G("schList"); if(!box) return;
  var b=G("allowBtn");
  if(b){ b.textContent = "выключение: " + (j.allowOff?"разрешено":"запрещено");
         b.className = "qbtn danger" + (j.allowOff?" on":""); }
  if(!j.rules || !j.rules.length){ box.innerHTML='<div class="r"><span>правил пока нет</span></div>'; return; }
  var html="";
  for(var i=0;i<j.rules.length;i++){
    var r=j.rules[i];
    var hh=("0"+r.h).slice(-2)+":"+("0"+r.m).slice(-2);
    html += '<div class="r"><span class="t">'+hh+'</span><span>'+((r.on)?"включить":"выключить")+'</span>'+
            '<span class="days">дни '+r.days+'</span><span class="days">'+(r.en?"вкл":"выкл")+'</span>'+
            '<span style="margin-left:auto"><button class="qbtn" data-del="'+i+'">удалить</button></span></div>';
  }
  box.innerHTML=html;
  box.querySelectorAll("button[data-del]").forEach(function(btn){
    btn.addEventListener("click", function(){
      var rule=SCH.rules[+this.getAttribute("data-del")];
      SCH.rules.splice(+this.getAttribute("data-del"),1);
      schSave();
    });
  });
}

function schSave(){
  var parts=[];
  for(var i=0;i<SCH.rules.length;i++){
    var r=SCH.rules[i];
    parts.push(("0"+r.h).slice(-2)+":"+("0"+r.m).slice(-2)+","+(r.on?"on":"off")+","+r.days+","+(r.en?1:0));
  }
  fetch("/sched?set="+encodeURIComponent(parts.join(";")), {cache:"no-store"})
    .then(function(r){return r.json()}).then(schRender).catch(function(){});
}

function schLoad(){
  fetch("/sched", {cache:"no-store"}).then(function(r){return r.json()}).then(schRender).catch(function(){});
}

function initSched(){
  var b=G("allowBtn");
  if(b) b.addEventListener("click", function(){
    if(!SCH.allowOff && !confirm("Разрешить плате выключать реле по расписанию и по команде из Home Assistant?")) return;
    fetch("/sched?allow="+(SCH.allowOff?0:1), {cache:"no-store"}).then(function(r){return r.json()}).then(schRender).catch(function(){});
  });
  var a=G("schAdd");
  if(a) a.addEventListener("click", function(){
    var tv=(G("schTime").value||"").trim();
    var m=/^(\d{1,2}):(\d{2})$/.exec(tv);
    if(!m){ alert("время в формате ЧЧ:ММ"); return; }
    var h=+m[1], mi=+m[2];
    if(h>23||mi>59){ alert("неверное время"); return; }
    var act = G("schAct").value === "on";
    if(!act && !SCH.allowOff){ if(!confirm("Выключение запрещено. Разрешить?")) return;
      fetch("/sched?allow=1",{cache:"no-store"}).then(function(){ SCH.allowOff=1; }); }
    SCH.rules.push({h:h,m:mi,on:act?1:0,days:(+G("schDays").value||127),en:1});
    schSave();
  });
  schLoad();
}


function tuyaUpdate(){
  fetch("/tuya", {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    function dec(b64){
      try{
        var s = atob(b64), out=[];
        for(var i=0;i+3<s.length;i+=4){
          out.push({c:s.charCodeAt(i), e:s.charCodeAt(i+1),
                    v:(s.charCodeAt(i+2)<<8)|s.charCodeAt(i+3)});
        }
        return out;
      }catch(e){ return []; }
    }
    var a1=dec(j.a1||""), a2=dec(j.a2||"");
    // набор 2 — электрика, набор 1 — температура
    var n2={1:"ток",3:"U макс",4:"U мин",8:"утечка"};
    var u2={1:" А",3:" В",4:" В",8:" мА"};
    var n1={3:"t мин",4:"t ср",5:"t макс"};
    var parts=[];
    a2.forEach(function(x){ if(x.e) parts.push((n2[x.c]||("код "+x.c))+" "+x.v+(u2[x.c]||"")); });
    a1.forEach(function(x){ if(x.e) parts.push((n1[x.c]||("код "+x.c))+" "+x.v+" °C"); });
    txt("tuyaS", parts.length ? parts.join(" · ") : "нет данных");
  }).catch(function(){});
}


// ---------------------------------------------------------------- статистика за период
var RNG={from:"",to:"",rows:[],on:""};
function pad2(n){return (n<10?"0":"")+n}
function iso(d){return d.getFullYear()+"-"+pad2(d.getMonth()+1)+"-"+pad2(d.getDate())}
function addDays(d,n){var x=new Date(d.getTime());x.setDate(x.getDate()+n);return x}
function rngPeriod(p){
  var now=new Date(), a, b=new Date(now.getTime());
  if(p=="today"){a=new Date(now.getTime());}
  else if(p=="yest"){a=addDays(now,-1);b=addDays(now,-1);}
  else if(p=="d2"){a=addDays(now,-2);b=addDays(now,-2);}
  else if(p=="d7"){a=addDays(now,-6);}
  else if(p=="d30"){a=addDays(now,-29);}
  else if(p=="week"){var w=(now.getDay()+6)%7;a=addDays(now,-w);}
  else if(p=="month"){a=new Date(now.getFullYear(),now.getMonth(),1);}
  else if(p=="quarter"){a=new Date(now.getFullYear(),Math.floor(now.getMonth()/3)*3,1);}
  else if(p=="half"){a=new Date(now.getFullYear(),now.getMonth()<6?0:6,1);}
  else if(p=="prev"){a=new Date(now.getFullYear(),now.getMonth()-1,1);b=new Date(now.getFullYear(),now.getMonth(),0);}
  else if(p=="year"){a=new Date(now.getFullYear(),0,1);}
  else if(p=="ytd"){a=new Date(now.getFullYear(),0,1);}
  else {a=new Date(now.getTime());}
  return {from:iso(a),to:iso(b)};
}
function rngMark(p){
  var bs=G("rngBtns").getElementsByTagName("button");
  for(var i=0;i<bs.length;i++) bs[i].className=(bs[i].getAttribute("data-p")===p)?"on":"";
}
function rngRender(j){
  RNG.rows=j.rows||[];
  var t=j.totals||{};
  txt("rKwh",num(t.kwh,2)); txt("rKwhS",j.days+(j.days==1?" день":" дней"));
  txt("rCost",num(t.cost,2)); txt("rCostS","средний тариф "+(t.kwh>0?(t.cost/t.kwh).toFixed(2):"—")+" ₽");
  txt("rDN",num(t.dayKwh,2)+" / "+num(t.nightKwh,2)); txt("rDNS",num(t.dayCost,2)+" ₽ / "+num(t.nightCost,2)+" ₽");
  txt("rAvg",num(t.avgDayKwh,2)); txt("rAvgS","U "+t.vmin+"–"+t.vmax+" В · пик "+t.pmax+" Вт");
  txt("rngNote",(j.from||"")+" — "+(j.to||""));
  var tb=G("rngTbl").getElementsByTagName("tbody")[0], h="";
  for(var i=0;i<RNG.rows.length;i++){
    var r=RNG.rows[i];
    h+="<tr><td>"+fmtDate(r.d)+"</td><td>"+num(r.kwh,2)+"</td><td>"+num(r.cost,2)+"</td>"
      +"<td class='mut'>"+num(r.dayKwh,2)+"</td><td class='mut'>"+num(r.nightKwh,2)+"</td>"
      +"<td>"+r.vmin+"–"+r.vmax+"</td><td>"+(r.tmin?num(r.tmin,1)+"–"+num(r.tmax,1):"—")+"</td>"
      +"<td class='mut'>"+r.pmax+"</td>"
      +"<td class='mut' title='"+(r.w||"")+"'>"+(r.w? r.w+(r.wtmin? ' · '+num(r.wtmin,0)+'…'+num(r.wtmax,0)+'°':'') : '—')+"</td></tr>";
  }
  if(!h) h="<tr><td colspan='9' class='mut'>нет данных за период</td></tr>";
  tb.innerHTML=h;
}
var GAP_BUSY=false;
function gapShow(g, m){
  if (GAP_BUSY) return;
  txt("gapInfo", (g>0) ? ("пропусков "+g+", "+(m||0)+" мин") : "пропусков нет");
}
function gapFillRun(){
  var f=RNG.from||G("rngFrom").value, t2=RNG.to||G("rngTo").value;
  if(!f||!t2){ alert("сначала выбери период"); return; }
  GAP_BUSY=true;
  txt("gapInfo","запускаю…");
  fetch("/gapfill?run=1&from="+f+"&to="+t2).then(function(r){ return r.json(); }).then(function(j){
    if(!j.ok){ GAP_BUSY=false; txt("gapInfo","не запустилось"); return; }
    var n=0;
    var tm=setInterval(function(){
      fetch("/gapfill/status").then(function(r){ return r.json(); }).then(function(s){
        if(s.active){
          txt("gapInfo","идёт: пропусков "+(s.gaps||0)+", вставлено "+(s.insTotal||0));
        } else {
          clearInterval(tm);
          GAP_BUSY=false;
          txt("gapInfo","готово: вставлено "+(s.insTotal||0)+", пропусков "+(s.gaps||0));
          rngLoad(f,t2);
        }
      }).catch(function(){ clearInterval(tm); GAP_BUSY=false; });
      if(++n>120){ clearInterval(tm); GAP_BUSY=false; txt("gapInfo","не дождался ответа"); }
    },2000);
  }).catch(function(){ GAP_BUSY=false; txt("gapInfo","ошибка запроса"); });
}
document.getElementById("gapBtn").addEventListener("click", gapFillRun);

function rngClear(){
  ["rKwh","rCost","rDN","rAvg","rKwhS","rCostS","rDNS","rAvgS"].forEach(function(i){ txt(i,"—"); });
  RNG.rows=[];
  G("rngTbl").getElementsByTagName("tbody")[0].innerHTML="<tr><td colspan='9' class='mut'>нет данных за период</td></tr>";
}
// период длиннее 31 дня плата считает порциями: 31 день за запрос
function rngLoad(from,to){
  RNG.from=from; RNG.to=to; RNG.rows=[];
  txt("rngNote","считаю…");
  var d1=new Date(from+"T00:00:00"), d2=new Date(to+"T00:00:00");
  var chunks=[], cur=new Date(d1.getTime());
  while(cur.getTime()<=d2.getTime()){
    var end=new Date(Math.min(cur.getTime()+30*86400000, d2.getTime()));
    chunks.push([iso(cur), iso(end)]);
    cur=new Date(end.getTime()+86400000);
  }
  var idx=0, all=[], tot={kwh:0,cost:0,dayKwh:0,nightKwh:0,dayCost:0,nightCost:0,vmin:0,vmax:0,pmax:0};
  function finish(){
    tot.avgDayKwh = all.length ? tot.kwh/all.length : 0;
    rngRender({from:from,to:to,days:all.length,totals:tot,rows:all});
  }
  function step(){
    if(idx>=chunks.length){ finish(); return; }
    var c=chunks[idx];
    if(chunks.length>1) txt("rngNote","считаю… "+(idx+1)+" из "+chunks.length);
    fetch("/range?from="+c[0]+"&to="+c[1]).then(function(r){ return r.json(); }).then(function(j){
      if(j.ok===false){ txt("rngNote","ошибка: "+(j.err||"?")); rngClear(); return; }
      all=all.concat(j.rows||[]);
      var q=j.totals||{};
      tot.kwh+=q.kwh||0; tot.cost+=q.cost||0; tot.dayKwh+=q.dayKwh||0; tot.nightKwh+=q.nightKwh||0;
      tot.dayCost+=q.dayCost||0; tot.nightCost+=q.nightCost||0;
      if(q.vmin>0 && (!tot.vmin || q.vmin<tot.vmin)) tot.vmin=q.vmin;
      if(q.vmax>tot.vmax) tot.vmax=q.vmax;
      if(q.pmax>tot.pmax) tot.pmax=q.pmax;
      idx++; step();
    }).catch(function(){ txt("rngNote","плата не ответила"); rngClear(); });
  }
  step();
}
function rngApply(p){
  rngMark(p);
  var r=rngPeriod(p);
  G("rngFrom").value=r.from; G("rngTo").value=r.to;
  rngLoad(r.from,r.to);
}
function rngCsv(){
  if(!RNG.rows.length){ alert("нет данных за период"); return; }
  var s="\ufeffdate;kwh;cost;day_kwh;night_kwh;day_cost;night_cost;vmin;vmax;tmin;tmax;pmax\r\n";
  RNG.rows.forEach(function(r){
    s+=[r.d,r.kwh,r.cost,r.dayKwh,r.nightKwh,r.dayCost,r.nightCost,r.vmin,r.vmax,r.tmin,r.tmax,r.pmax].join(";")+"\r\n";
  });
  var a=document.createElement("a");
  a.href="data:text/csv;charset=utf-8,"+encodeURIComponent(s);
  a.download="energy-"+RNG.from+"_"+RNG.to+".csv"; a.click();
}
document.getElementById("rngBtns").addEventListener("click",function(e){
  var p=e.target.getAttribute&&e.target.getAttribute("data-p");
  if(p) rngApply(p);
});
G("rngGo").addEventListener("click",function(){
  var a=G("rngFrom").value,b=G("rngTo").value;
  if(!a||!b){ alert("укажи даты"); return; }
  if(a>b){ var x=a;a=b;b=x; }
  rngMark(""); rngLoad(a,b);
});
G("rngCsv").addEventListener("click",rngCsv);
G("rngPrint").addEventListener("click",function(){ window.print(); });

loadDays(); poll(); setInterval(poll,5000); (function(){
  var q=(location.search.match(/[?&]p=([a-z0-9]+)/)||[])[1];
  var ok=["today","yest","d2","d7","d30","week","month","quarter","half","prev","year","ytd"];
  rngApply(ok.indexOf(q)>=0?q:"today");
})(); initQuality(); initSched(); tuyaUpdate(); setInterval(tuyaUpdate, 60000);
</script>
</body>
</html>
)HTML";
