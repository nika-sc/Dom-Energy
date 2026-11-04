#pragma once
#include <Arduino.h>

// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ.
static const char PAGE_TMPL[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#F8FAFF">
<title>Дом — энергия</title>
<style>:root{--bg:#F8FAFF;--card:#fff;--ln:#DBE4F5;--ink:#0F172A;--mut:#5A6478;--acc:#2563EB;--acc2:#BFD3FB;--grn:#0F9F6E;--pnk:#DB2777} *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;font-variant-numeric:tabular-nums;-webkit-text-size-adjust:100%} main{max-width:880px;margin:0 auto;padding:18px 16px 44px} .top{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;margin-bottom:14px} h1{font-size:21px;line-height:1.2;margin:0;letter-spacing:-.02em} .chips{display:flex;flex-wrap:wrap;gap:6px;margin-left:auto} .chip{display:inline-flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--ln);color:var(--mut);font-size:12px;padding:4px 9px;border-radius:999px;white-space:nowrap} .dot{width:7px;height:7px;border-radius:50%;background:#94A3B8;display:inline-block;flex:none} .dot.ok{background:var(--grn)} .dot.bad{background:var(--pnk)} .card{background:var(--card);border:1px solid var(--ln);border-radius:16px;box-shadow:0 1px 2px rgba(15,23,42,.03)} .hero{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px} .kpi{padding:14px 16px} .lbl{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.07em;font-weight:600} .big{font-size:30px;font-weight:750;letter-spacing:-.03em;line-height:1.15;margin-top:3px} .big small{font-size:14px;font-weight:600;color:var(--mut);letter-spacing:0;margin-left:3px} .acc{color:var(--acc)} .grn{color:var(--grn)} .metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));padding:14px 6px;margin-bottom:10px} .m{padding:0 10px;border-right:1px solid #EDF1F9;min-width:0} .m:last-child{border-right:0} .mv{font-size:19px;font-weight:700;letter-spacing:-.02em;margin-top:2px;white-space:nowrap;overflow:hidden} .mv small{font-size:11px;color:var(--mut);font-weight:600;margin-left:1px} .age{font-size:10px;color:var(--mut);font-weight:600;margin-top:1px;white-space:nowrap} .age.old{color:#B45309} .age.bad{color:#DC2626} .pa{font-size:9px;color:var(--mut);font-weight:600;margin-left:4px} .pa.old{color:#B45309} .pa.bad{color:#DC2626} .ls{display:none} .pills{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:18px} .pill{display:inline-flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--ln);border-radius:999px;padding:5px 11px;font-size:12.5px;color:var(--mut)} .pill b{color:var(--ink);font-weight:650} .sec{padding:14px 16px 8px;margin-bottom:10px} .head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:4px} .ttl{font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--mut)} .nav{display:flex;align-items:center;gap:6px;margin-left:auto} .arr{width:34px;height:34px;border-radius:10px;border:1px solid var(--ln);background:#fff;color:var(--acc);font-size:20px;line-height:1;cursor:pointer;display:grid;place-items:center;padding:0 0 3px} .arr:disabled{color:#C7D2E8;cursor:default} .date{min-width:150px;text-align:center;font-weight:650;font-size:14px} .date em{display:block;font-style:normal;font-size:11px;color:var(--mut);font-weight:500;margin-top:-2px} .date .gapmark{display:block;font-style:normal;font-size:10.5px;font-weight:600;color:#B45309;background:#FEF3C7;border:1px solid #FDE68A;border-radius:6px;padding:0 5px;margin-top:2px;white-space:nowrap} .chart{margin-top:10px} .chart+.chart{border-top:1px solid #EDF1F9;padding-top:12px} .ct{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px;font-size:13px;font-weight:600;color:#334155} .ct .u{font-size:11px;color:var(--mut);font-weight:500} .cs{font-size:10.5px;color:var(--mut);margin-top:1px} .legend{margin-left:auto;display:flex;align-items:center;gap:5px;font-size:11px;color:var(--mut);font-weight:500} .nbtn{font:inherit;font-size:10px;font-weight:600;color:var(--mut);background:var(--card);border:1px solid var(--ln);border-radius:999px;padding:1px 7px;line-height:1.45;cursor:pointer} .nbtn[aria-pressed=true]{color:#fff;border-color:var(--acc);background:var(--acc);box-shadow:0 0 0 3px rgba(37,99,235,.18)} .nbtn:hover{border-color:var(--acc);color:var(--acc)} .nbtn[aria-pressed=true]:hover{color:#fff} .nbtn{background:#fff;transition:all .12s ease;font-size:11.5px;font-weight:700;padding:4px 12px;border-width:1.5px;cursor:pointer;letter-spacing:.02em} .nbtn[aria-pressed=true]{color:#fff !important;border-color:var(--acc) !important;background:var(--acc) !important;box-shadow:0 0 0 3px rgba(37,99,235,.20);font-weight:800} .nbtn[aria-pressed=false]{color:var(--mut);background:#fff;border-style:dashed} .sw{width:9px;height:9px;border-radius:3px;display:inline-block;margin-left:6px} .sw.b{background:var(--acc2)} .sw.p{background:#F59E0B;height:9px;border-radius:3px;opacity:1} .sw.v{background:var(--acc)} .sw.t{background:var(--pnk)} .plot{margin-top:4px;min-height:60px} .plot svg{display:block;width:100%;height:auto;overflow:visible} .ph{color:var(--mut);font-size:13px;padding:26px 0;text-align:center} .grid{stroke:#E7EDF9;stroke-width:1} .yl,.xl{fill:#94A3B8;font-size:9.5px;pointer-events:none} .yl.a{fill:#6B8FD0} .yl.p{fill:#64748B} .cap{font-size:9px;font-weight:700;letter-spacing:.02em;pointer-events:none} .cap.a{fill:#3F6BC4} .cap.p{fill:#475569} .nl{font-size:8px;fill:#475569;font-weight:600;pointer-events:none} .nl.p{fill:#B45309} .bar{fill:var(--acc2)} .bar.cur{fill:var(--acc);fill-opacity:.55;stroke:var(--acc)} .bar0{fill:#E7EDF9} .pbar{fill:#F59E0B} .pbar.cur{fill:#EA580C;fill-opacity:.55;stroke:#EA580C} .hit{cursor:pointer} .vline{fill:none;stroke:var(--acc);stroke-width:2;stroke-linejoin:round;stroke-linecap:round} .tline{fill:none;stroke:var(--pnk);stroke-width:2;stroke-linejoin:round;stroke-linecap:round} .vdot{fill:var(--acc)} .tdot{fill:var(--pnk)} .nowband{fill:var(--acc);opacity:.055} .totals{padding:6px 16px} .row{display:flex;justify-content:space-between;gap:10px;padding:9px 0;font-size:14px;color:var(--mut)} .row+.row{border-top:1px solid #EDF1F9} .row b{color:var(--ink);font-weight:650;text-align:right} .block{padding:14px 16px;margin-bottom:10px} .ttl2{font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin-bottom:8px} .frow{display:flex;align-items:baseline;gap:6px;flex-wrap:wrap} .fval{font-size:24px;font-weight:750;letter-spacing:-.02em} .funit{font-size:12px;color:var(--mut);margin-right:10px;font-weight:600} .note{font-size:12px;color:var(--mut);margin-top:4px} .tgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px} .titem{background:#F7FAFF;border:1px solid #E7EDF9;border-radius:12px;padding:10px 12px} .tval{font-size:20px;font-weight:700;margin-top:2px} .tval small{font-size:11px;color:var(--mut);font-weight:600} .ev{list-style:none;margin:0;padding:0} .ev li{display:flex;justify-content:space-between;gap:10px;padding:8px 0;font-size:13.5px;border-top:1px solid #EDF1F9} .ev li:first-child{border-top:0} .ev .when{color:var(--mut);font-size:12px;white-space:nowrap} .ev .on{color:var(--grn);font-weight:650} .ev .off{color:var(--pnk);font-weight:650} .ev .muted{color:var(--mut);padding:10px 0} .tip{position:fixed;z-index:50;background:#0F172A;color:#fff;font-size:12px;line-height:1.35;padding:7px 10px;border-radius:9px;pointer-events:none;box-shadow:0 6px 20px rgba(15,23,42,.25);max-width:250px} .tip b{font-size:13px} .tip .k{color:#94A3B8} .tip[hidden]{display:none} @media(max-width:560px){ main{padding:14px 12px 36px} h1{font-size:19px} .chips{margin-left:0;width:100%} .big{font-size:26px} .metrics{padding:11px 2px} .m{padding:0 7px} .m .lbl{font-size:9px;letter-spacing:.02em;white-space:nowrap} .lf{display:none} .ls{display:inline} .mv{font-size:16px} .mv small{font-size:10px} .sec{padding:12px 12px 6px} .date{min-width:122px;font-size:13px} .row{font-size:13px} } @media(max-width:350px){ .big{font-size:23px} .mv{font-size:15px} .m .lbl{font-size:9px} } body{background:#EDF1F7}.bar{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:9px 14px;background:#fff;border-bottom:1px solid var(--ln);box-shadow:0 1px 3px rgba(15,23,42,.05)}.nowhead{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;gap:8px;padding:10px 12px 4px;font-size:16px}.nowhead .chips{margin:0}.rl{display:inline-flex;align-items:center;gap:7px}.ver{font-size:10.5px;font-weight:700;color:var(--mut);background:#fff;border:1px solid var(--ln);border-radius:999px;padding:2px 7px;vertical-align:middle}.bar .chips{margin-left:auto}main{max-width:960px;margin:0 auto;padding:12px;display:flex;flex-direction:column;gap:10px}.kpis{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.now8{margin:2px 0 0}.kpis .kpi{padding:9px 11px}.grid2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;align-items:start}@media(max-width:900px){}@media(max-width:760px){.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}.grid2{grid-template-columns:1fr}}
main{max-width:760px}.kpi{border-radius:20px;background:#fff;border:1px solid #E8EEF8;padding:10px 12px;box-shadow:0 1px 3px rgba(15,23,42,.05)}.big{font-size:26px}.amb{color:#B45309}.pnk{color:var(--pnk)}.bad{color:#DC2626}</style>
</head>
<body> <main><section class="panel"><div class="nowhead"><b>Дом — энергия</b><span class="ver">CYD-__FW__</span><span class="chips"><span class="chip" id="cNet"><i class="dot __NETCLS__"></i><span>__NETTXT__</span></span><span class="chip" id="cRssi">__RSSI__ дБм</span><span class="chip" id="cUpd">обновлено 0 с</span></span></div><div class="kpis now8"><div class="card kpi"><div class="lbl">Напряжение</div><div class="big acc"><span id="mv">__MV__</span><small>В</small></div><div class="age" id="mvAge">—</div></div><div class="card kpi"><div class="lbl">Мощность</div><div class="big grn"><span id="mp">__MP__</span><small>Вт</small></div><div class="age" id="mpAge">—</div></div><div class="card kpi"><div class="lbl">Ток</div><div class="big pnk"><span id="ma">__MA__</span><small>А</small></div><div class="age">&nbsp;</div></div><div class="card kpi"><div class="lbl">Темп. реле</div><div class="big amb"><span id="mt">__MT__</span><small>°C</small></div><div class="age" id="mtAge">—</div></div><div class="card kpi"><div class="lbl">Реле</div><div class="big grn"><span class="rl" id="pRelay"><i class="dot __RELCLS__"></i>__RELTXT__</span></div><div class="age">&nbsp;</div></div><div class="card kpi"><div class="lbl">cos φ</div><div class="big acc"><span id="pcos">—</span></div><div class="age">&nbsp;</div></div><div class="card kpi"><div class="lbl">Тариф</div><div class="big pnk"><span id="ptar">__TAR__</span><small>₽</small></div><div class="age">&nbsp;</div></div><div class="card kpi"><div class="lbl">Счётчик</div><div class="big grn"><span id="pe">__PE__</span><small>кВт·ч</small></div><div class="age" id="peAge">—</div></div></div></section><div class="kpis"><div class="card kpi"><div class="lbl">Сегодня</div><div class="big acc"><span id="tkwh">__TKWH__</span><small>кВт·ч</small></div><div class="age" id="tkwhAge">—</div></div><div class="card kpi"><div class="lbl">Стоимость</div><div class="big grn"><span id="tcost">__TCOST__</span><small>₽</small></div></div><div class="card kpi"><div class="lbl">Вчера</div><div class="big"><span id="ykwh">__YKWH__</span><small>кВт·ч</small></div><div class="age"><span id="ykost">__YKOST__</span> ₽</div></div><div class="card kpi"><div class="lbl">Месяц</div><div class="big"><span id="mkwh">__MKWH__</span><small>кВт·ч</small></div><div class="age"><span id="mcost">__MCOST__</span> ₽</div></div></div><section class="card block" id="wxCard" style="display:none"><div class="ttl2">Погода · Адлер</div><div class="row"><span>Сейчас</span><b id="wxNow">—</b></div><div class="row"><span>Завтра</span><b id="wxTom">—</b></div></section><section class="card sec"><div class="head"><div class="ttl">История</div><div class="nav"><button class="arr" id="prev" type="button" aria-label="Предыдущий день">‹</button><div class="date" id="dateLabel">—</div><button class="arr" id="next" type="button" aria-label="Следующий день">›</button></div></div><div class="chart"><div class="ct"><span>Энергия и мощность по часам</span><span class="legend"><i class="sw b"></i>средняя<i class="sw p"></i>пик</span><button class="nbtn" id="unitBtn" type="button" aria-pressed="false">кВт·ч</button><button class="nbtn" id="numBtn" type="button" aria-pressed="false">числа</button></div><div class="cs" id="eCap">высота — энергия за час, кВт·ч; средняя мощность за тот же час = кВт·ч × 1000, Вт</div><div class="plot" id="chE"><div class="ph">загрузка…</div></div></div><div class="chart"><div class="ct"><span>Напряжение и температура</span><span class="legend"><i class="sw v"></i>В<i class="sw t"></i>°C</span></div><div class="plot" id="chVT"></div></div></section><div class="grid2"><section class="card block"><div class="ttl2">Прогноз на месяц</div><div class="frow"><b class="fval acc" id="fkwh">—</b><span class="funit">кВт·ч</span><b class="fval grn" id="fcost">—</b><span class="funit">₽</span></div><div class="note" id="fnote">по текущему темпу расхода</div></section><section class="card block"><div class="ttl2">Тарифы</div><div class="tgrid"><div class="titem"><div class="lbl">День · 06–23</div><div class="tval">__TARDAY__ <small>₽/кВт·ч</small></div></div><div class="titem"><div class="lbl">Ночь · 23–06</div><div class="tval">__TARNIGHT__ <small>₽/кВт·ч</small></div></div></div></section></div><section class="card sec" id="scrCard"><div class="head"><div class="ttl">Экран платы</div><div class="nav"><button class="nbtn" id="scrBtn" type="button" aria-pressed="false" onclick="scrToggle()">Экран</button></div></div><div class="row"><span>Подсветка</span><b id="scrState">—</b></div><div class="row"><span>Яркость</span><b><input id="scrBrightRange" type="range" min="5" max="100" step="5" value="100" style="width:110px;vertical-align:middle" oninput="scrBrightSet(this.value)"><span id="scrBright">—</span></b></div><div class="row"><span>Гасить через</span><b><input id="scrIdle" type="number" min="0" max="60" step="1" inputmode="numeric" value="2" style="width:58px;text-align:right;border:1px solid #E2E8F0;border-radius:8px;padding:2px 6px;font:inherit;color:inherit;background:#fff" onchange="scrIdleSet(this.value)" onkeydown="if(event.key==='Enter'){this.blur()}"> мин</b></div><div class="row"><span>Экономия</span><b id="scrEco">—</b></div><div class="cs" style="margin-top:6px">0 — не гасить. Данные и запись на SD идут как обычно; касание возвращает подсветку.</div></section><section class="card block"><div class="ttl2">События реле</div><ul class="ev" id="events"><li class="muted">загрузка…</li></ul></section></main><div id="tip" class="tip" hidden></div> <script>
(function(){
"use strict";
function gd(i){return document.getElementById(i)}
var MON=["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"];
var S={date:"__DATE__",today:"__DATE__",hour:__HOUR__,days:[],gaps:[],hist:{},cur:null,last:Date.now(),net:true,tipT:0,busy:false,poll:0,fn:120,nums:0};
var AT={v:0,t:0,p:0,e:0};   // epoch-секунды последнего получения показателя
function pad2(n){return (n<10?"0":"")+n}
function fmtDate(d){var p=d.split("-");return (+p[2])+" "+(MON[(+p[1])-1]||"")+" "+p[0]}
function num(v,d){return (v===null||v===undefined||!isFinite(v))?"—":Number(v).toFixed(d)}
function eNum(v){if(!(v>0))return "0";return v<0.1?Number(v).toFixed(3):Number(v).toFixed(2)}
function j(u){return fetch(u,{cache:"no-store"}).then(function(r){if(!r.ok)throw new Error("HTTP "+r.status);return r.json()})}
var Q=Promise.resolve();
function jt(u){
var c=new AbortController(),t=setTimeout(function(){c.abort()},15000);
return fetch(u,{cache:"no-store",signal:c.signal}).then(function(r){
  clearTimeout(t);
  if(!r.ok)throw new Error("HTTP "+r.status);
  return r.json();
},function(e){clearTimeout(t);throw e});
}
function jq(u){
var p=Q.then(function(){return jt(u)});
Q=p.then(function(){},function(){});
return p;
}
function retry(u,n){
return jq(u).catch(function(e){
  if(n<=0)throw e;
  return new Promise(function(res){setTimeout(res,1200)}).then(function(){return retry(u,n-1)});
});
}
function txt(id,t){var e=gd(id);if(e)e.textContent=t}
function hourRange(i){return pad2(i)+":00–"+pad2(i+1)+":00"}
function fullN(hs){var s=0,c=0,i,a;for(i=0;i<24;i++){a=hs[i]||{};if(a.n&&!isCur(i)){s+=a.n;c++}}return c?Math.round(s/c):120}
function izm(n){var m=n%100,d=n%10;return (m>10&&m<15)||d>4||d===0?"измерений":d===1?"измерение":"измерения"}
function measN(x,i){if(!x.n)return "нет измерений";return isCur(i)?"час идёт: "+x.n+" из ~"+S.fn+" "+izm(S.fn):x.n+" "+izm(x.n)}
function niceMax(v){if(!(v>0))return 1;var e=Math.pow(10,Math.floor(Math.log10(v))),f=v/e;var n=f<=1?1:f<=2?2:f<=2.5?2.5:f<=5?5:10;return n*e}
function axisNum(v,mx){if(mx<0.1)return v.toFixed(3);if(mx<1)return v.toFixed(2);if(mx<10)return v.toFixed(1);return String(Math.round(v))}

function two(n){return (n<10?"0":"")+n}
function stampTxt(at){
 if(!at)return "нет данных";
 var d=new Date(at*1000),now=new Date();
 var hm=two(d.getHours())+":"+two(d.getMinutes());
 if(d.toDateString()===now.toDateString())return "получено "+hm;
 var y=new Date(now.getFullYear(),now.getMonth(),now.getDate()-1);
 if(d.toDateString()===y.toDateString())return "получено вчера "+hm;
 return "получено "+two(d.getDate())+"."+two(d.getMonth()+1)+" "+hm;
}
function ageCls(at){
 if(!at)return "";
 var s=Math.max(0,Date.now()/1000-at);
 return s>3600?" bad":(s>300?" old":"");
}
function paintAge(id,at,base){
 var e=gd(id);if(!e)return;
 e.className=(base||"age")+ageCls(at);
 e.textContent=stampTxt(at);
}
function paintAges(){
 paintAge("mvAge",AT.v);paintAge("mpAge",AT.p);paintAge("mtAge",AT.t);
 paintAge("tkwhAge",AT.e);paintAge("peAge",AT.e,"pa");
}
function wxCard(d){
var c=gd("wxCard");if(!c)return;
if(!d||!d.wOk){c.style.display="none";return}
c.style.display="";
var t=(d.wT===null||d.wT===undefined)?"—":num(d.wT,1)+" °C";
if(d.wDesc)t+=" · "+String(d.wDesc).toLowerCase();
if(d.wPop!==undefined)t+=" · дождь "+num(d.wPop,0)+" %";
txt("wxNow",t);
var s="—";
if(d.wTminT!==undefined&&d.wTmaxT!==undefined){
s=num(d.wTminT,0)+"…"+num(d.wTmaxT,0)+" °C";
if(d.wPopT!==undefined)s+=", дождь "+num(d.wPopT,0)+" %";
if(d.wDescT)s+=", "+String(d.wDescT).toLowerCase();
if(d.wRainT>0.05)s+=", "+num(d.wRainT,1)+" мм";
}
txt("wxTom",s);
}
function apply(d){
if(!d)return;
S.last=Date.now();
S.net=d.net!==false;
txt("tkwh",eNum(d.tkwh));txt("tcost",num(d.tcost,2));
txt("mv",num(d.v,0));txt("mp",num(d.p,0));txt("ma",num(d.amp,2));txt("mt",num(d.temp,1));
AT={v:+d.vAt||0,t:+d.tAt||0,p:+d.pAt||0,e:+d.eAt||0};paintAges();
txt("ptar",num(d.tariff,2));txt("pe",num(d.e,2));
txt("ykwh",eNum(d.ykwh));txt("ykost",num(d.ykost,2));
txt("mkwh",eNum(d.mkwh));txt("mcost",num(d.mcost,2));
txt("cRssi",num(d.rssi,0)+" дБм");
var cf=(d.v>50&&d.amp>0.001)?Math.min(1,Math.max(0,d.p/(d.v*d.amp))):null;
txt("pcos",cf===null?"—":cf.toFixed(2));
try{scrPaint(d)}catch(e){}
  try{wxCard(d)}catch(e){}
  try{forecast(d)}catch(e){}
var r=gd("pRelay");
r.innerHTML='<i class="dot '+(d.r?"ok":"bad")+'"></i>'+(d.r?"ВКЛ":"ВЫКЛ");r.parentNode.className='big '+(d.r?"grn":"bad");
if(d.now){
  var nd=d.now.slice(0,10),nh=parseInt(d.now.slice(11,13),10);
  if(nd!==S.today){S.today=nd;if(S.days.length)loadDays();}
  if(nh!==S.hour){S.hour=nh;if(S.cur&&S.date===S.today)draw();}
}
chipNet();
}
function chipNet(){
var e=gd("cNet");
e.innerHTML='<i class="dot '+(S.net?"ok":"bad")+'"></i><span>'+(S.net?"сеть ok":"нет сети")+'</span>';
}
function forecast(d){
if(!d||!d.now||!(d.mkwh>0)){txt("fkwh","—");txt("fcost","—");txt("fnote","пока мало данных за месяц");return}
var dp=d.now.slice(0,10).split("-"),tp=d.now.slice(11).split(":");
var y=+dp[0],mo=+dp[1],day=+dp[2],elapsed=(day-1)+(+tp[0]+ +tp[1]/60)/24;
if(!(elapsed>0.15)){txt("fkwh","—");txt("fcost","—");txt("fnote","пока мало данных за месяц");return}
var dim=new Date(y,mo,0).getDate();
var avg=d.mkwh/elapsed,kwh=avg*dim;
var price=(d.mkwh>0.01)?d.mcost/d.mkwh:(d.tariff||0);
txt("fkwh",kwh.toFixed(1));txt("fcost",(kwh*price).toFixed(2));
txt("fnote","средний расход "+avg.toFixed(2)+" кВт·ч в сутки · "+price.toFixed(2)+" ₽/кВт·ч");
}
function loadEvents(){
retry("/events",1).then(function(d){
  var ev=(d&&d.events)||[],ul=gd("events");
  if(!ev.length){ul.innerHTML='<li class="muted">за последние 3 дня переключений не было</li>';return}
  ul.innerHTML=ev.map(function(e){
    var when=String(e.t||"").slice(5,16).replace("-",".");
    return '<li><span class="'+(e.r?"on":"off")+'">'+(e.r?"включено":"выключено")+'</span><span class="when">'+when+(e.v>0?" · "+Math.round(e.v)+" В":"")+'</span></li>';
  }).join("");
}).catch(function(){gd("events").innerHTML='<li class="muted">не удалось загрузить события</li>'});
}
function loadData(){
clearTimeout(S.poll);
if(S.busy){S.poll=setTimeout(loadData,900);return}
jq("/data").then(apply).catch(function(){S.net=false;chipNet()}).then(function(){S.poll=setTimeout(loadData,5000)});
}
function loadDays(){
return retry("/days",1).then(function(d){
  S.days=(d&&d.days)||[];
  S.gaps=(d&&d.gaps)||[];
  if(!S.days.length){emptyAll("на карте пока нет истории");gd("dateLabel").textContent="—";return}
  var start=S.days.indexOf(S.today)>=0?S.today:S.days[0];
  return showDay(start);
}).catch(function(){emptyAll("не удалось прочитать список дней")});
}
function emptyAll(msg){
S.cur=null;
["chE","chVT"].forEach(function(id){gd(id).innerHTML='<div class="ph">'+msg+'</div>'});
}
function showDay(d,force){
if(S.busy&&!force)return;
S.date=d;updateNav();
if(S.hist[d]&&!force){S.cur=S.hist[d];draw();return}
S.busy=true;
["chE","chVT"].forEach(function(id){gd(id).innerHTML='<div class="ph">загрузка…</div>'});
retry("/history?date="+encodeURIComponent(d),1).then(function(h){
  S.hist[d]=h;
  if(S.date===d){S.cur=h;draw()}
}).catch(function(){
  if(S.date===d)emptyAll("не удалось загрузить историю")
}).then(function(){S.busy=false});
}
function updateNav(){
var i=S.days.indexOf(S.date);
gd("prev").disabled=!(i>=0&&i<S.days.length-1);
gd("next").disabled=!(i>0);
var g=(i>=0&&S.gaps[i])||null;
var mark=(g&&g[0]>0)?'<i class="gapmark">есть пропуски, '+g[1]+' мин</i><button class="nbtn" id="gapBtn" type="button" onclick="gapFill(\''+S.date+'\')" title="подтянуть пропуски из облака Tuya">подтянуть</button>':"";
gd("dateLabel").innerHTML=fmtDate(S.date)+(S.date===S.today?"<em>сегодня</em>":"")+mark;
}
function isCur(i){return S.date===S.today&&i===S.hour}
function draw(){
if(!S.cur)return;
var h=S.cur.hours||[];
S.fn=fullN(h);
var anyE=false,anyP=false,anyV=false,anyT=false;
for(var i=0;i<24;i++){var a=h[i]||{n:0};if(a.n&&a.kwh>0)anyE=true;if(a.n&&a.p>0)anyP=true;if(a.v>50)anyV=true;if(a.t!==null&&a.t!==undefined)anyT=true}
if(anyE||anyP)drawEnergy(gd("chE"),h);
else gd("chE").innerHTML='<div class="ph">за этот день нет записей</div>';
if(anyV||anyT)drawVT(gd("chVT"),h);
else gd("chVT").innerHTML='<div class="ph">напряжение и температура за этот день не записаны</div>';
}
function niceMaxP(v){
if(!(v>0))return 1;
var e=Math.pow(10,Math.floor(Math.log10(v))),f=v/e,c=[1,1.2,1.5,2,2.5,3,4,5,6,7,8,9,10];
for(var i=0;i<c.length;i++)if(f<=c[i]+1e-9)return c[i]*e;
return 10*e;
}
function axN(v){return Math.abs(v-Math.round(v))<0.05?String(Math.round(v)):v.toFixed(1)}
function drawEnergy(el,hours){
var W=Math.max(280,el.clientWidth||280),H=W<520?216:248;
var pl=36,pr=10,pt=14,pb=22,gap=20,h2=W<520?46:58;
var slot=(W-pl-pr)/24;
var base2=H-pb,top2=base2-h2,base1=top2-gap,top1=pt,h1=base1-top1;
var am=0,pm=0,i,x,y1,y2,a,b,v;
for(i=0;i<24;i++){a=hours[i]||{};if(a.n){v=S.watt?+a.p||0:+a.kwh||0;if(v>am)am=v;if(+a.pmax>pm)pm=+a.pmax}}
var amx=S.watt?niceMaxP(am||1):niceMax(am||0.01),pmx=niceMaxP(pm||am||1);
var s='<svg viewBox="0 0 '+W+' '+H+'" width="100%" height="'+H+'" aria-hidden="true">';
s+='<text x="'+pl+'" y="'+(top1-5)+'" class="cap a">'+(S.watt?"средняя, Вт":"энергия, кВт·ч")+'</text>';
for(i=0;i<=2;i++){
  y1=(top1+h1*i/2).toFixed(1);
  s+='<line x1="'+pl+'" y1="'+y1+'" x2="'+(W-pr)+'" y2="'+y1+'" class="grid"/>';
  s+='<text x="'+(pl-5)+'" y="'+(+y1+3.5).toFixed(1)+'" class="yl a" text-anchor="end">'+(S.watt?axN(amx*(1-i/2)):axisNum(amx*(1-i/2),amx))+'</text>';
}
s+='<text x="'+pl+'" y="'+(top2-5)+'" class="cap p">пик, Вт</text>';
for(i=0;i<=2;i++){
  y2=(top2+h2*i/2).toFixed(1);
  s+='<line x1="'+pl+'" y1="'+y2+'" x2="'+(W-pr)+'" y2="'+y2+'" class="grid"/>';
  s+='<text x="'+(pl-5)+'" y="'+(+y2+3.5).toFixed(1)+'" class="yl p" text-anchor="end">'+axN(pmx*(1-i/2))+'</text>';
}
for(i=0;i<24;i++){
  b=hours[i]||{};v=b.n?(S.watt?+b.p||0:+b.kwh||0):0;x=pl+i*slot;
  var bw=(slot*0.68).toFixed(1),bx=(x+slot*0.16).toFixed(1);
  if(v>0){
    var bh=Math.min(h1,Math.max(1.5,v/amx*h1));
    s+='<rect x="'+bx+'" y="'+(base1-bh).toFixed(1)+'" width="'+bw+'" height="'+bh.toFixed(1)+'" rx="'+Math.min(2.5,slot*0.2).toFixed(1)+'" class="bar'+(isCur(i)?" cur":"")+'"/>';
  }else{
    s+='<rect x="'+bx+'" y="'+(base1-2)+'" width="'+bw+'" height="2" class="bar0"/>';
  }
  if(b.n&&b.pmax>0){
    var ph=Math.min(h2,Math.max(1.5,(+b.pmax)/pmx*h2));
    s+='<rect x="'+bx+'" y="'+(base2-ph).toFixed(1)+'" width="'+bw+'" height="'+ph.toFixed(1)+'" rx="'+Math.min(2.5,slot*0.2).toFixed(1)+'" class="pbar'+(isCur(i)?" cur":"")+'"/>';
  }
}
if(S.nums){
  var mi=0,mv=0,q,nh;
  for(i=0;i<24;i++){q=hours[i]&&hours[i].n?(S.watt?+hours[i].p||0:+hours[i].kwh||0):0;if(q>mv){mv=q;mi=i}}
  // на узком экране — через один (та же чётность, что у максимума), иначе числа наезжают
  var par=W<520?mi%2:0;
  for(i=0;i<24;i++){
    q=hours[i]&&hours[i].n?(S.watt?+hours[i].p||0:+hours[i].kwh||0):0;
    if(q>0&&(W>=520||i%2===par)){
      nh=Math.min(h1,Math.max(1.5,q/amx*h1));
      s+='<text x="'+(pl+slot*(i+.5)).toFixed(1)+'" y="'+Math.max(top1+9,base1-nh-2.5).toFixed(1)+'" class="nl" text-anchor="middle">'+(S.watt?Math.round(q):num(q,2))+'</text>';
    }
  }
  // числа на нижней панели «пик, Вт»
  var pv=0,pg=0;
  for(i=0;i<24;i++){
    b=hours[i]||{};
    pv=(b.n&&+b.pmax>0)?+b.pmax:0;
    if(!pv)continue;
    if(W<520&&i%2!==par)continue;
    pg=Math.min(h2,Math.max(1.5,pv/pmx*h2));
    s+='<text x="'+(pl+slot*(i+.5)).toFixed(1)+'" y="'+Math.max(top2+9,base2-pg-2.5).toFixed(1)+'" class="nl p" text-anchor="middle">'+Math.round(pv)+'</text>';
  }
}
for(i=0;i<24;i++)s+='<rect x="'+(pl+i*slot).toFixed(1)+'" y="'+top1+'" width="'+slot.toFixed(1)+'" height="'+(base2-top1)+'" fill="transparent" class="hit" data-i="'+i+'"/>';
[0,6,12,18,24].forEach(function(k){
  var x3=pl+k*slot,anc=k===0?"start":k===24?"end":"middle";
  if(k===24)x3-=1;
  s+='<text x="'+x3.toFixed(1)+'" y="'+(H-6)+'" class="xl" text-anchor="'+anc+'">'+(k<24?pad2(k):"24")+'</text>';
});
s+='</svg>';
el.innerHTML=s;
var opt={tip:function(x,i){
  var out="<b>"+hourRange(i)+"</b> · "+measN(x,i);
  if(!x.n)return out;
  out+="<br><b>"+num(x.kwh,2)+" кВт·ч</b> = средняя <b>"+num(x.p,0)+" Вт</b>";
  if(x.pmax>0){
    var tp="<br>пик <b>"+num(x.pmax,0)+" Вт</b>";
    if(x.pmaxT)tp+=" в "+x.pmaxT;
    // pmaxDur в /history — СЕКУНДЫ (так удобнее и прошивке, и приложению),
    // на экран выводим минуты: 180 → «длился 3 мин».
    if(x.pmaxDur>0)tp+=", длился "+Math.max(1,Math.round(x.pmaxDur/60))+" мин";
    out+=tp;
  }
  return out;
}};
el.querySelectorAll(".hit").forEach(function(r){
  r.addEventListener("pointerdown",function(ev){ev.preventDefault();showTip(r,hours[+r.getAttribute("data-i")]||{n:0},opt,ev)});
});
}
function linePath(vals,pl,slot,y){
var d="",pen=false;
for(var i=0;i<24;i++){
  var v=vals[i];
  if(v===null||v===undefined||!isFinite(v)){pen=false;continue}
  var x=pl+slot*(i+0.5);
  d+=(pen?"L":"M")+x.toFixed(1)+" "+y(v).toFixed(1);
  pen=true;
}
return d;
}
function drawVT(el,hours){
var W=Math.max(280,el.clientWidth||280),H=W<520?170:200;
var pl=34,pr=34,pt=12,pb=22,pw=W-pl-pr,ph=H-pt-pb,base=pt+ph,slot=pw/24;
var vmin=1e9,vmax=-1e9,tmin=1e9,tmax=-1e9,anyV=false,anyT=false,i;
for(i=0;i<24;i++){
  var a=hours[i]||{};
  if(a.v>50){anyV=true;if(a.v<vmin)vmin=a.v;if(a.v>vmax)vmax=a.v}
  if(a.t!==null&&a.t!==undefined){anyT=true;if(a.t<tmin)tmin=a.t;if(a.t>tmax)tmax=a.t}
}
var s='<svg viewBox="0 0 '+W+' '+H+'" width="100%" height="'+H+'" aria-hidden="true">';
if(anyV){
  var vr=vmax-vmin;
  if(vr<8){var c=(vmax+vmin)/2;vmin=c-4;vmax=c+4}else{vmin-=vr*0.12;vmax+=vr*0.12}
}
if(anyT){
  var tr=tmax-tmin;
  if(tr<2){var c2=(tmax+tmin)/2;tmin=c2-1;tmax=c2+1}else{tmin-=tr*0.15;tmax+=tr*0.15}
}
function yV(v){return base-(v-vmin)/(vmax-vmin)*ph}
function yT(v){return base-(v-tmin)/(tmax-tmin)*ph}
if(isCur(S.hour)){
  var bx=pl+slot*S.hour;
  s+='<rect x="'+bx.toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+ph+'" class="nowband"/>';
}
for(i=0;i<=2;i++){
  var y=pt+ph*i/2,yy=y.toFixed(1);
  s+='<line x1="'+pl+'" y1="'+yy+'" x2="'+(W-pr)+'" y2="'+yy+'" class="grid"/>';
  if(anyV)s+='<text x="'+(pl-5)+'" y="'+(y+3.5).toFixed(1)+'" class="yl" text-anchor="end">'+Math.round(vmax-(vmax-vmin)*i/2)+'</text>';
  if(anyT)s+='<text x="'+(W-pr+5)+'" y="'+(y+3.5).toFixed(1)+'" class="yl" text-anchor="start">'+(tmax-(tmax-tmin)*i/2).toFixed(1)+'</text>';
}
var vv=[],tt=[];
for(i=0;i<24;i++){var b=hours[i]||{};vv.push(b.v>50?b.v:null);tt.push(b.t===null||b.t===undefined?null:b.t)}
if(anyV){
  s+='<path d="'+linePath(vv,pl,slot,yV)+'" class="vline"/>';
  for(i=0;i<24;i++)if(vv[i]!==null)s+='<circle cx="'+(pl+slot*(i+0.5)).toFixed(1)+'" cy="'+yV(vv[i]).toFixed(1)+'" r="1.7" class="vdot"/>';
}
if(anyT){
  s+='<path d="'+linePath(tt,pl,slot,yT)+'" class="tline"/>';
  for(i=0;i<24;i++)if(tt[i]!==null)s+='<circle cx="'+(pl+slot*(i+0.5)).toFixed(1)+'" cy="'+yT(tt[i]).toFixed(1)+'" r="1.7" class="tdot"/>';
}
for(i=0;i<24;i++)s+='<rect x="'+(pl+i*slot).toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+ph+'" fill="transparent" class="hit" data-i="'+i+'"/>';
[0,6,12,18,24].forEach(function(k){
  var x4=pl+k*slot,anc=k===0?"start":k===24?"end":"middle";
  if(k===24)x4-=1;
  s+='<text x="'+x4.toFixed(1)+'" y="'+(H-6)+'" class="xl" text-anchor="'+anc+'">'+(k<24?pad2(k):"24")+'</text>';
});
s+='</svg>';
el.innerHTML=s;
el.querySelectorAll(".hit").forEach(function(r){
  r.addEventListener("pointerdown",function(ev){
    ev.preventDefault();
    var h=hours[+r.getAttribute("data-i")]||{n:0};
    showTip(r,h,{tip:function(x,i){
      var out="<b>"+hourRange(i)+"</b>";
      out+="<br>"+(x.v>50?num(x.v,0)+" В":"напряжение нет");
      if(x.t!==null&&x.t!==undefined)out+=" · "+num(x.t,1)+" °C";
      return out;
    }});
  });
});
}
function showTip(r,h,opt,ev){
var t=gd("tip");
t.innerHTML=opt.tip(h,+r.getAttribute("data-i"));
t.hidden=false;
var b=r.getBoundingClientRect(),tw=t.offsetWidth,th=t.offsetHeight;
var x=b.left+b.width/2-tw/2;
x=Math.max(8,Math.min(window.innerWidth-tw-8,x));
var y;
if(ev&&ev.clientY){
  y=ev.clientY-th-10;
  if(y<8)y=ev.clientY+16;
}else{
  y=b.top-th-8;
  if(y<8)y=b.bottom+8;
}
if(y+th>window.innerHeight-8)y=window.innerHeight-th-8;
t.style.left=x+"px";t.style.top=y+"px";
clearTimeout(S.tipT);S.tipT=setTimeout(function(){t.hidden=true},4000);
}
gd("prev").addEventListener("click",function(){
var i=S.days.indexOf(S.date);
if(i>=0&&i<S.days.length-1)showDay(S.days[i+1]);
});
gd("next").addEventListener("click",function(){
var i=S.days.indexOf(S.date);
if(i>0)showDay(S.days[i-1]);
});
try{S.nums=localStorage.getItem("cydv.nums")==="1"?1:0;S.watt=localStorage.getItem("cydv.unit")==="1"}catch(e){}
function uiBtns(){
var b=gd("numBtn");if(b)b.setAttribute("aria-pressed",S.nums?"true":"false");
var u=gd("unitBtn");if(u){u.textContent=S.watt?"Вт":"кВт·ч";u.setAttribute("aria-pressed",S.watt?"true":"false")}
txt("eCap",S.watt?"высота — средняя мощность за час, Вт; это те же кВт·ч × 1000":"высота — энергия за час, кВт·ч; средняя мощность за тот же час = кВт·ч × 1000, Вт");
}
uiBtns();
gd("numBtn").addEventListener("click",function(){S.nums=S.nums?0:1;try{localStorage.setItem("cydv.nums",S.nums?"1":"0")}catch(e){}uiBtns();if(S.cur)draw()});
gd("unitBtn").addEventListener("click",function(){S.watt=!S.watt;try{localStorage.setItem("cydv.unit",S.watt?"1":"0")}catch(e){}uiBtns();if(S.cur)draw()});
document.addEventListener("pointerdown",function(e){
if(!e.target.classList||!e.target.classList.contains("hit"))gd("tip").hidden=true;
},true);
addEventListener("scroll",function(){gd("tip").hidden=true},{passive:true});
var rt=0;
addEventListener("resize",function(){clearTimeout(rt);rt=setTimeout(draw,150)});
setInterval(function(){
var s=Math.max(0,Math.round((Date.now()-S.last)/1000));
txt("cUpd",(s>20?"нет связи ":"обновлено ")+s+" с");
},1000);
setInterval(paintAges,1000);
setInterval(function(){
if(document.hidden||S.busy||S.date!==S.today||!S.hist[S.today])return;
delete S.hist[S.today];
showDay(S.today,true);
},120000);
setInterval(function(){
if(!document.hidden&&!S.busy)loadEvents();
},120000);
document.addEventListener("visibilitychange",function(){if(!document.hidden)loadData()});
chipNet();
jq("/data").then(apply).catch(function(){S.net=false;chipNet()}).then(function(){
S.poll=setTimeout(loadData,5000);
loadDays();
loadEvents();
});
})();

function scrPaint(d){
  var D=function(i){return document.getElementById(i)};
  var st=D("scrState"),br=D("scrBright"),b=D("scrBtn"),id=D("scrIdle"),ec=D("scrEco");
  if(!d)return;
  var on=(d.disp===1||d.disp==="1");
  if(st)st.textContent=on?"включена":"выключена";
  var bp=(d.bright!=null?Math.round(d.bright/2.55):0); if(br)br.textContent=(d.bright!=null?bp:"—")+" %"; var rg=document.getElementById("scrBrightRange"); if(rg&&document.activeElement!==rg)rg.value=Math.max(5,bp);
  if(b)b.setAttribute("aria-pressed",on?"true":"false");
  if(id&&document.activeElement!==id)id.value=(d.idle!=null?d.idle:0);
  if(ec){
    if(on&&d.idleMs>0){ var s=Math.round(d.idleMs/1000); ec.textContent="гаснет через "+(s>=60?Math.round(s/60)+" мин":s+" с"); }
    else if(!on)ec.textContent="экран погашен";
    else ec.textContent=d.idle>0?"ждёт бездействия":"автогашение выкл";
  }
}
function scrIdleSet(v){
  var n=parseInt(v,10); if(isNaN(n)||n<0||n>60)return;
  var el=document.getElementById("scrIdle");
  fetch("/disp?idle="+n,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    if(j&&j.ok){
      // подтверждаем тем, что вернула плата (значение пишется в NVS)
      if(el&&j.idle!=null)el.value=j.idle;
      var ec=document.getElementById("scrEco");
      if(ec&&!j.on)ec.textContent="экран погашен";
    }
  }).catch(function(){ if(el)el.value=n; });
}
window.scrIdleSet=scrIdleSet;
var scrOn=false;
function scrToggle(){
  // Состояние берём из /data ПЕРЕД переключением: window.scrOn может быть ещё
  // не заполнен, и тогда кнопка всегда посылала on=1 — «не работала».
  var g=function(id){return document.getElementById(id)};
  var b=g("scrBtn"); if(b)b.disabled=true;
  fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
    var on=(d&&(d.disp===1||d.disp==="1"));
    return fetch("/disp?"+(on?"on=0":"on=1"),{cache:"no-store"});
  }).then(function(r){return r.json()}).then(function(j){
    var st=g("scrState"), br=g("scrBright"), b2=g("scrBtn"), ec=g("scrEco");
    if(j&&j.ok){
      window.scrOn=(j.on===1||j.on==="1");
      if(st)st.textContent=window.scrOn?"включена":"выключена";
      if(br)br.textContent=Math.round((j.bright!=null?j.bright:0)/2.55)+" %";
      if(ec)ec.textContent=window.scrOn?"ждёт бездействия":"экран погашен";
      if(b2){b2.setAttribute("aria-pressed",window.scrOn?"true":"false");b2.disabled=false;}
    } else if(b2) b2.disabled=false;
  }).catch(function(){ var b3=g("scrBtn"); if(b3)b3.disabled=false; });
}
function scrSelfTick(){
  fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(scrSelfPaint).catch(function(){});
}
document.addEventListener("DOMContentLoaded",function(){
  scrSelfTick();
  setInterval(scrSelfTick,10000);
});

function scrBrightSet(v){
  var n=parseInt(v,10); if(isNaN(n)||n<5||n>100)return;
  var g=function(id){return document.getElementById(id)};
  fetch("/disp?bright="+n,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var br=g("scrBright"); if(br&&j&&j.bright!=null)br.textContent=Math.round(j.bright/2.55)+" %";
    var rg=g("scrBrightRange"); if(rg&&j&&j.bright!=null)rg.value=Math.max(5,Math.round(j.bright/2.55));
    if(window.scrOn===false){ fetch("/disp?on=1",{cache:"no-store"}); window.scrOn=true; }
  }).catch(function(){});
};
window.scrBrightSet=scrBrightSet;

function gapFill(date){var g=function(i){return document.getElementById(i)};var b=g("gapBtn");if(b){b.disabled=true;b.textContent="тяну…";}fetch("/gapfill?run=1&from="+date+"&to="+date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){  var n0=(j&&j.gaps)||0,t0=Date.now(),last=-1,same=0;  var iv=setInterval(function(){    fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){      if(d.gaps!==last){last=d.gaps;same=0;}else{same++;}      if(b)b.textContent="осталось "+d.gaps+"…";      if((!d.gapAct||same>=2)&&Date.now()-t0>5000){        clearInterval(iv);        if(b){b.textContent=(d.gaps<n0?"готово":"в облаке нет данных");b.disabled=false;}        setTimeout(function(){location.reload()},1800);      }    }).catch(function(){});  },2500);}).catch(function(){if(b){b.disabled=false;b.textContent="ошибка";}});};
window.gapFill=gapFill;
</script>
</body>
</html>
)HTML";
