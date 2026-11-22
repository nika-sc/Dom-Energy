#pragma once
#include <Arduino.h>

// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ
// (python web/build_page.py). CSS и JS — в assets.h.
static const char PAGE_TMPL[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru" data-theme="dark" data-boot-date="__DATE__" data-boot-hour="__HOUR__">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0a0f1a">
<title>Дом — энергия</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='8' fill='%2322d3ee'/%3E%3Cpath d='M18 6 9 18h6l-1 8 9-12h-6z' fill='%230a0f1a'/%3E%3C/svg%3E">
<link rel="stylesheet" href="/app.css">
</head>
<body>
<div class="wrap">
<div class="top">
  <span class="name"><i>◈</i> Дом — энергия</span><span class="ver">CYD-__FW__</span>
  <span class="chips">
    <span class="chip" id="cNet"><i class="dot __NETCLS__"></i><span>__NETTXT__</span></span>
    <span class="chip" id="cRssi">__RSSI__ дБм</span><span class="chip" id="cUpd">обновлено 0 с</span>
    <button class="iconbtn" id="themeBtn" type="button" title="скин">🌈 Радужная</button>
    <a class="iconbtn" href="/sound.html" target="_blank" title="настройка звуков">♪ Звуки</a>
  </span>
</div>

<div class="netbar" id="netbar"><b>Нет связи с платой.</b><span>проверьте Wi-Fi и питание</span><button type="button" id="retryBtn">Повторить</button></div>
<div class="daybar" id="dayBar">
  <span class="lbl" id="dayLbl">сегодня</span>
  <span class="iconbtn" id="dayPrev" role="button" tabindex="0">‹ день</span>
  <span class="iconbtn" id="dayNext" role="button" tabindex="0">день ›</span>
  <span class="iconbtn" id="dayToday" role="button" tabindex="0">сегодня</span>
  <span class="note" id="dayNote">все карточки ниже показывают данные за этот день</span>
</div>

<h2 class="sect">Сейчас<span>напряжение, ток, температура, мощность, реле, счётчик</span></h2>

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

<h2 class="sect">Итоги<span>сегодня, стоимость, вчера, месяц</span></h2>

<div class="row4">
  <div class="t"><div class="k">Сегодня</div><div class="v c1"><span id="tkwh">__TKWH__</span><small>кВт·ч</small></div><div class="s" id="tkwhAge">—</div></div>
  <div class="t"><div class="k">Стоимость</div><div class="v c2"><span id="tcost">__TCOST__</span><small>₽</small></div><div class="s">&nbsp;</div></div>
  <div class="t"><div class="k">Вчера</div><div class="v"><span id="ykwh">__YKWH__</span><small>кВт·ч</small></div><div class="s"><span id="ykost">__YKOST__</span> ₽</div></div>
  <div class="t"><div class="k">Месяц</div><div class="v"><span id="mkwh">__MKWH__</span><small>кВт·ч</small></div><div class="s"><span id="mcost">__MCOST__</span> ₽</div></div>
</div>

<h2 class="sect">За день<span>по часам выбранного дня</span></h2>

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
  <div class="gapbox" id="gapBox" style="display:none"><span id="gapTxt"></span><button class="iconbtn" id="gapDayBtn" type="button">подтянуть</button></div>
</div>

<h2 class="sect">За период<span>любой диапазон дат, день/ночь, погода</span></h2>

<div class="card" style="margin-top:12px" id="rngCard">
  <div id="printMeta"></div>
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
    <th>День</th><th>кВт·ч</th><th>₽</th><th>день</th><th>ночь</th><th>U мин–макс</th><th>t реле</th><th>пик</th><th>погода</th><th>t улицы</th>
  </tr></thead><tbody><tr><td colspan="10" class="mut">нет данных</td></tr></tbody></table></div>
</div>

<h2 class="sect">Качество и защита<span>ГОСТ 32144 и пороги автомата</span></h2>

<div class="qgrid two" style="margin-top:12px">
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



<h2 class="sect">Климат в комнате<span>датчик AHT20 + BMP280 на шине I2C</span></h2>
<div class="qgrid" id="envCards">
  <div class="card d"><div class="k">Температура</div>
    <div class="v c4"><span id="envT">—</span><small>°C</small></div>
    <div class="s" id="envTS">датчик не найден</div></div>
  <div class="card d"><div class="k">Влажность</div>
    <div class="v c1"><span id="envH">—</span><small>%</small></div>
    <div class="s">относительная</div></div>
  <div class="card d"><div class="k">Давление</div>
    <div class="v"><span id="envP">—</span><small>мм</small></div>
    <div class="s" id="envPS">—</div></div>
</div>

<h2 class="sect">Погода и тарифы</h2>

<div class="two">
  <div class="card"><div class="head"><span class="ttl">Погода · Адлер</span></div>
    <div class="kv"><span>Сейчас</span><b id="wxNow">—</b></div>
    <div class="kv"><span>Завтра</span><b id="wxTom">—</b></div></div>
  <div class="card"><div class="head"><span class="ttl">Тарифы и прогноз</span></div>
    <div class="kv"><span>День · 06–23</span><b id="tarDay">__TARDAY__ ₽/кВт·ч</b></div>
    <div class="kv"><span>Ночь · 23–06</span><b id="tarNight">__TARNIGHT__ ₽/кВт·ч</b></div>
    <div class="kv"><span>Прогноз на месяц</span><b id="fc">—</b></div></div>

  <div class="card" style="margin-top:12px;grid-column:1/-1">
    <div class="head"><span class="ttl">Тариф</span><span class="nav"><button id="tarSave" type="button">сохранить</button></span></div>
    <div class="qrow">
      <span>День, ₽/кВт·ч <b><input class="qin" id="tarDayIn" type="number" step="0.01" min="0" max="99" value="__TARDAY__"></b></span>
      <span>Ночь, ₽/кВт·ч <b><input class="qin" id="tarNightIn" type="number" step="0.01" min="0" max="99" value="__TARNIGHT__"></b></span>
      <span>ночь с <b><input class="qin" id="tarFromIn" type="number" min="0" max="23" value="23"></b> ч</span>
      <span>по <b><input class="qin" id="tarToIn" type="number" min="0" max="23" value="6"></b> ч</span>
      <span>итог <b id="tarMsg">—</b></span>
    </div>
  </div>
</div>

<h2 class="sect">Устройство<span>экран, подсветка, гашение</span></h2>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">Экран платы</span><span class="nav"><button id="scrBtn" type="button">Экран</button></span></div>
  <div class="kv"><span>Подсветка</span><b id="scrState">—</b></div>
  <div class="kv"><span>Яркость</span><b><input id="scrBrightRange" type="range" min="5" max="100" step="5" value="100"> <span id="scrBright">—</span></b></div>
  <div class="kv"><span>Гасить через</span><b><input id="scrIdle" type="number" min="0" max="60" step="1" value="2"> мин</b></div>
  <div class="kv"><span>Экономия</span><b id="scrEco">—</b></div>
  <div class="kv"><span>Плата</span><b><button class="iconbtn" id="rebootBtn" type="button">⟳ перезагрузить</button>
    <span id="rebootMsg"></span></b></div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">Звуки включения</span><span class="nav"><a class="iconbtn" href="/sound.html" target="_blank">Все звуки</a></span></div>
  <div class="kv"><span>Загрузка платы</span><b>
    <select id="sndBoot"></select> <button class="iconbtn" id="sndBootPlay" type="button" title="прослушать">▶</button></b></div>
  <div class="kv"><span>Перезагрузка</span><b>
    <select id="sndReboot"></select> <button class="iconbtn" id="sndRebootPlay" type="button" title="прослушать">▶</button></b></div>
  <div class="kv"><span>Тихие часы не глушат загрузку</span><b>по умолчанию</b></div>
</div>
<h2 class="sect">События и автоматика<span>провалы, всплески, планировщик</span></h2>

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


<div class="build">CYD-__FW__ · страница с платы · данные с карты памяти</div>
</div>
<div id="tip" class="tip"></div>
<script src="/app.js" defer></script>
</body>
</html>
)HTML";
