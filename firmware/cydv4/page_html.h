#pragma once
#include <Arduino.h>

// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ.
static const char PAGE_TMPL[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0a0f1a">
<title>Дом — энергия</title>
<style>
:root{--bg:#0a0f1a;--bg2:#0e1523;--card:#121a2b;--ln:#1e293b;--ink:#e8eefc;--mut:#7f8ea8;
--cyan:#22d3ee;--grn:#34d399;--vio:#a78bfa;--amb:#fbbf24;--pnk:#f472b6;--red:#f87171}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(1100px 560px at 18% -10%,#132038 0%,var(--bg) 55%);color:var(--ink);
font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;min-height:100vh}
.wrap{max-width:1000px;margin:0 auto;padding:20px 16px 44px}
.top{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:14px}
.name{font-size:18px;font-weight:800;letter-spacing:-.02em}
.name i{color:var(--cyan);font-style:normal}
.ver{font-size:10.5px;font-weight:700;color:var(--cyan);background:rgba(34,211,238,.1);
border:1px solid rgba(34,211,238,.25);border-radius:999px;padding:2px 8px}
.chips{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap}
.chip{display:inline-flex;align-items:center;gap:6px;background:rgba(255,255,255,.04);border:1px solid var(--ln);
border-radius:999px;padding:4px 10px;font-size:12px;color:var(--mut)}
.dot{width:7px;height:7px;border-radius:50%;background:#475569}
.dot.ok{background:var(--grn)}
.hero{display:grid;grid-template-columns:290px 1fr;gap:14px;align-items:stretch}
@media(max-width:780px){.hero{grid-template-columns:1fr}}
.card{background:linear-gradient(180deg,rgba(255,255,255,.035),rgba(255,255,255,.012));
border:1px solid var(--ln);border-radius:18px;padding:14px 16px}
.gauge{display:flex;flex-direction:column;align-items:center;justify-content:center;position:relative}
.gauge svg{width:220px;height:220px}
.gauge .num{position:relative;margin-top:-142px;text-align:center;height:70px}
.gauge .num b{font-size:38px;font-weight:800;letter-spacing:-.04em;display:block;line-height:1}
.gauge .num span{font-size:12px;color:var(--mut);display:block;margin-top:2px}
.cap{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--mut);font-weight:700}
.tiles{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
@media(max-width:520px){.tiles{grid-template-columns:repeat(2,minmax(0,1fr))}}
.t{background:linear-gradient(180deg,rgba(255,255,255,.035),rgba(255,255,255,.01));
border:1px solid var(--ln);border-radius:14px;padding:11px 12px}
.t .k{font-size:9.5px;text-transform:uppercase;letter-spacing:.09em;color:var(--mut);font-weight:700}
.t .v{font-size:21px;font-weight:800;letter-spacing:-.03em;margin-top:3px;line-height:1.15}
.t .v small{font-size:11px;color:var(--mut);font-weight:600;margin-left:3px}
.t .s{font-size:10px;color:#5f708a;margin-top:2px}
.cy{color:var(--cyan)}.gr{color:var(--grn)}.vi{color:var(--vio)}.am{color:var(--amb)}.pk{color:var(--pnk)}
.row4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:12px}
@media(max-width:780px){.row4{grid-template-columns:repeat(2,minmax(0,1fr))}}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}
@media(max-width:780px){.two{grid-template-columns:1fr}}
.head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:8px}
.ttl{font-size:10.5px;font-weight:800;text-transform:uppercase;letter-spacing:.09em;color:var(--mut)}
.legend{margin-left:auto;display:flex;gap:12px;font-size:10.5px;color:var(--mut)}
.sw{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:4px}
.kv{display:flex;justify-content:space-between;gap:12px;font-size:13px;padding:7px 0;border-top:1px solid rgba(255,255,255,.05)}
.kv:first-of-type{border-top:0}
svg.chart{display:block;width:100%;height:auto}
.ax{font-size:9px;fill:#4b5b73}
.nav{display:flex;align-items:center;gap:8px;margin-left:auto}
.nav button{background:rgba(255,255,255,.05);border:1px solid var(--ln);color:var(--ink);
border-radius:9px;padding:4px 9px;cursor:pointer;font:inherit;font-size:12px}
.nav button:disabled{opacity:.35;cursor:default}
.date{font-size:12px;color:var(--mut);text-align:center;min-width:110px}
.nbtn{background:rgba(34,211,238,.12);border:1px solid rgba(34,211,238,.3);color:var(--cyan);
border-radius:9px;padding:4px 10px;cursor:pointer;font:inherit;font-size:11.5px}
.gapbox{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--amb);margin-top:6px}
input[type=range]{width:120px;accent-color:var(--cyan)}
input[type=number]{width:56px;background:rgba(255,255,255,.06);border:1px solid var(--ln);color:var(--ink);
border-radius:8px;padding:2px 6px;font:inherit;text-align:right}
.tip{position:fixed;z-index:50;background:#0b1220;color:#e8eefc;border:1px solid var(--ln);border-radius:10px;
padding:8px 10px;font-size:12px;box-shadow:0 8px 24px rgba(0,0,0,.45);pointer-events:none;max-width:260px}
</style>
</head>
<body>
<div class="wrap">
<div class="top">
  <span class="name"><i>◈</i> Дом — энергия</span><span class="ver">CYD-__FW__</span>
  <span class="chips">
    <span class="chip" id="cNet"><i class="dot __NETCLS__"></i><span>__NETTXT__</span></span>
    <span class="chip" id="cRssi">__RSSI__ дБм</span><span class="chip" id="cUpd">обновлено 0 с</span>
  </span>
</div>

<div class="hero">
  <div class="card gauge">
    <div class="cap">Сейчас</div>
    <svg viewBox="0 0 200 200">
      <circle cx="100" cy="100" r="84" fill="none" stroke="rgba(255,255,255,.06)" stroke-width="13"/>
      <circle id="ring" cx="100" cy="100" r="84" fill="none" stroke="#22d3ee" stroke-width="13"
        stroke-linecap="round" transform="rotate(-90 100 100)" stroke-dasharray="528" stroke-dashoffset="528"/>
      <text id="gcap" x="100" y="158" text-anchor="middle" class="ax">из 600 Вт</text>
    </svg>
    <div class="num"><b id="mp">__MP__</b><span>Вт</span></div>
  </div>
  <div class="tiles">
    <div class="t"><div class="k">Напряжение</div><div class="v cy"><span id="mv">__MV__</span><small>В</small></div><div class="s" id="mvAge">—</div></div>
    <div class="t"><div class="k">Ток</div><div class="v vi"><span id="ma">__MA__</span><small>А</small></div><div class="s"><span id="pcos">—</span></div></div>
    <div class="t"><div class="k">Темп. реле</div><div class="v am"><span id="mt">__MT__</span><small>°C</small></div><div class="s" id="mtAge">—</div></div>
    <div class="t"><div class="k">Реле</div><div class="v" id="rel"><i class="dot __RELCLS__"></i>__RELTXT__</div><div class="s">&nbsp;</div></div>
    <div class="t"><div class="k">Тариф</div><div class="v pk"><span id="ptar">__TAR__</span><small>₽</small></div><div class="s">&nbsp;</div></div>
    <div class="t"><div class="k">Счётчик</div><div class="v gr"><span id="pe">__PE__</span><small>кВт·ч</small></div><div class="s" id="peAge">—</div></div>
  </div>
</div>

<div class="row4">
  <div class="t"><div class="k">Сегодня</div><div class="v cy"><span id="tkwh">__TKWH__</span><small>кВт·ч</small></div><div class="s" id="tkwhAge">—</div></div>
  <div class="t"><div class="k">Стоимость</div><div class="v gr"><span id="tcost">__TCOST__</span><small>₽</small></div><div class="s">&nbsp;</div></div>
  <div class="t"><div class="k">Вчера</div><div class="v"><span id="ykwh">__YKWH__</span><small>кВт·ч</small></div><div class="s"><span id="ykost">__YKOST__</span> ₽</div></div>
  <div class="t"><div class="k">Месяц</div><div class="v"><span id="mkwh">__MKWH__</span><small>кВт·ч</small></div><div class="s"><span id="mcost">__MCOST__</span> ₽</div></div>
</div>

<div class="card" style="margin-top:12px">
  <div class="head"><span class="ttl">История</span>
    <span class="nav"><button id="prev" type="button">‹</button><span class="date" id="dateLabel">—</span><button id="next" type="button">›</button></span>
  </div>
  <div class="head"><span class="ttl">Энергия и мощность по часам</span>
    <span class="legend"><span><i class="sw" style="background:#22d3ee"></i>энергия</span><span><i class="sw" style="background:#fbbf24"></i>пик, Вт</span></span>
  </div>
  <svg class="chart" id="chE" viewBox="0 0 900 190" preserveAspectRatio="none"></svg>
  <div class="head" style="margin-top:10px"><span class="ttl">Напряжение и температура</span>
    <span class="legend"><span><i class="sw" style="background:#60a5fa"></i>В</span><span><i class="sw" style="background:#f472b6"></i>°C</span></span>
  </div>
  <svg class="chart" id="chVT" viewBox="0 0 900 140" preserveAspectRatio="none"></svg>
  <div class="gapbox" id="gapBox" style="display:none"><span id="gapTxt"></span>
    <button class="nbtn" id="gapBtn" type="button">подтянуть</button></div>
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
  <div class="head"><span class="ttl">Экран платы</span><span class="nav"><button class="nbtn" id="scrBtn" type="button">Экран</button></span></div>
  <div class="kv"><span>Подсветка</span><b id="scrState">—</b></div>
  <div class="kv"><span>Яркость</span><b><input id="scrBrightRange" type="range" min="5" max="100" step="5" value="100"> <span id="scrBright">—</span></b></div>
  <div class="kv"><span>Гасить через</span><b><input id="scrIdle" type="number" min="0" max="60" step="1" value="2"> мин</b></div>
  <div class="kv"><span>Экономия</span><b id="scrEco">—</b></div>
</div>
</div>
<div id="tip" class="tip" hidden></div>
<script>
"use strict";
var BOOT_DATE="__DATE__",BOOT_HOUR=__HOUR__;
var S={date:"",days:[],hist:null,hour:0,last:0};
function G(i){return document.getElementById(i)}
function txt(i,t){var e=G(i);if(e)e.textContent=t}
function num(v,n){return (v==null||isNaN(v))?"—":(+v).toFixed(n==null?0:n)}
function hhmm(ts){if(!ts)return "—";var d=new Date(ts*1000);return("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)}
function fmtDate(s){if(!s)return "—";var p=s.split("-");var m=["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"];return (+p[2])+" "+m[(+p[1])-1]+" "+p[0]}

/* ---- живое состояние ---- */
function apply(d){
  if(!d)return;
  S.last=Date.now();
  txt("mv",num(d.v)); txt("mp",num(d.p)); txt("ma",num(d.amp,2)); txt("mt",num(d.temp,1));
  txt("ptar",num(d.tariff,2)); txt("pe",num(d.e,2));
  txt("tkwh",num(d.tkwh,2)); txt("tcost",num(d.tcost,2));
  txt("ykwh",num(d.ykwh,2)); txt("ykost",num(d.ykost,2));
  txt("mkwh",num(d.mkwh,2)); txt("mcost",num(d.mcost,2));
  txt("cRssi",num(d.rssi)+" дБм");
  txt("mvAge","получено "+hhmm(d.vAt)); txt("mtAge","получено "+hhmm(d.tAt));
  txt("peAge","получено "+hhmm(d.eAt)); txt("tkwhAge","получено "+hhmm(d.eAt));
  var rel=G("rel");
  if(rel){rel.innerHTML='<i class="dot '+(d.r?"ok":"")+'"></i>'+(d.r?"ВКЛ":"ВЫКЛ");rel.style.color=d.r?"var(--grn)":"var(--red)"}
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
  /* кольцо */
  var pw=+d.p||0, mx=Math.max(600,Math.ceil(pw*1.4/100)*100), f=Math.max(0.02,Math.min(1,pw/mx));
  var ring=G("ring"); if(ring){ring.setAttribute("stroke-dashoffset",(528*(1-f)).toFixed(1));
    ring.setAttribute("stroke",pw>2000?"#f87171":(pw>1000?"#fbbf24":"#22d3ee"))}
  txt("gcap","из "+mx+" Вт");
  if(S.hist&&S.date===S.today)drawE(S.hist);
}
function poll(){fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(apply).catch(function(){txt("cNet","нет связи")})}
setInterval(function(){var s=Math.round((Date.now()-S.last)/1000);txt("cUpd",(s>20?"нет связи ":"обновлено ")+s+" с")},1000);

/* ---- история ---- */
function loadDays(){
  fetch("/days",{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    S.days=j.days||[]; S.today=(j.today||S.days[0]||""); S.date=S.days[0]||S.today;
    loadHist();
  }).catch(function(){});
}
function loadHist(){
  if(!S.date)return;
  fetch("/history?date="+S.date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    S.hist=j; drawNav(); drawE(j); drawVT(j);
  }).catch(function(){});
}
function drawNav(){
  var i=S.days.indexOf(S.date);
  G("prev").disabled=!(i>=0&&i<S.days.length-1); G("next").disabled=!(i>0);
  txt("dateLabel",fmtDate(S.date)+(S.date===S.today?" · сегодня":""));
}
function drawE(j){
  var h=j.hours||[],W=900,H=190,pl=12,pr=10,pt=8,pb=16,slot=(W-pl-pr)/24,base=H-pb,hh=base-pt,am=0,pm=0,i;
  for(i=0;i<24;i++){var a=h[i]||{};if(+a.kwh>am)am=+a.kwh;if(+a.pmax>pm)pm=+a.pmax}
  var s='<defs><linearGradient id="gb" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#22d3ee" stop-opacity=".95"/><stop offset="100%" stop-color="#22d3ee" stop-opacity=".12"/></linearGradient></defs>';
  s+='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="#1e293b"/>';
  for(i=0;i<24;i++){var b=h[i]||{},v=+b.kwh||0,x=pl+i*slot+slot*0.2,w=slot*0.6;
    if(v>0){var bh=Math.max(2,v/(am||1)*hh*0.72);s+='<rect x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="url(#gb)"/>'}
    if(+b.pmax>0){var ph=Math.max(2,(+b.pmax)/(pm||1)*hh*0.22);s+='<rect x="'+x.toFixed(1)+'" y="'+(base-ph).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+ph.toFixed(1)+'" rx="2" fill="#fbbf24" opacity=".85"/>'}
  }
  for(i=0;i<=3;i++){var xx=pl+(W-pl-pr)*i/3;s+='<text class="ax" x="'+xx.toFixed(1)+'" y="'+(H-3)+'" text-anchor="middle">'+(i*8)+'</text>'}
  G("chE").innerHTML=s;
}
function drawVT(j){
  var h=j.hours||[],W=900,H=140,pl=12,pr=10,pt=8,pb=14,slot=(W-pl-pr)/24,i;
  var vs=[],ts=[];
  for(i=0;i<24;i++){var a=h[i]||{};vs.push(+a.v||0);ts.push(a.t==null?null:+a.t)}
  var vmin=999,vmax=0,tmin=99,tmax=-99;
  for(i=0;i<24;i++){if(vs[i]>50){vmin=Math.min(vmin,vs[i]);vmax=Math.max(vmax,vs[i])}if(ts[i]!=null){tmin=Math.min(tmin,ts[i]);tmax=Math.max(tmax,ts[i])}}
  if(vmax<=0)vmax=250;if(vmin>vmax)vmin=vmax-10;if(tmax<tmin){tmin=20;tmax=25}
  function vy(v){return pt+(1-(v-vmin)/((vmax-vmin)||1))*(H-pt-pb)}
  function ty(t){return pt+(1-(t-tmin)/((tmax-tmin)||1))*(H-pt-pb)}
  var sv='',st='',pv=false,ptt=false;
  for(i=0;i<24;i++){var x=pl+slot*(i+0.5);
    if(vs[i]>50){sv+=(pv?'L':'M')+x.toFixed(1)+' '+vy(vs[i]).toFixed(1)+' ';pv=true}
    if(ts[i]!=null){st+=(ptt?'L':'M')+x.toFixed(1)+' '+ty(ts[i]).toFixed(1)+' ';ptt=true}}
  var s='<line x1="'+pl+'" y1="'+(H-pb)+'" x2="'+(W-pr)+'" y2="'+(H-pb)+'" stroke="#1e293b"/>';
  if(sv)s+='<path d="'+sv+'" fill="none" stroke="#60a5fa" stroke-width="2"/>';
  if(st)s+='<path d="'+st+'" fill="none" stroke="#f472b6" stroke-width="2"/>';
  s+='<text class="ax" x="'+pl+'" y="'+(H-2)+'">'+vmin.toFixed(0)+'–'+vmax.toFixed(0)+' В · '+tmin.toFixed(1)+'–'+tmax.toFixed(1)+' °C</text>';
  G("chVT").innerHTML=s;
}

/* ---- экран платы ---- */
function scrToggle(){
  fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
    var on=(d.disp===1||d.disp==="1");
    return fetch("/disp?"+(on?"on=0":"on=1"),{cache:"no-store"});
  }).then(function(){setTimeout(poll,700)}).catch(function(){});
}
G("scrBtn").addEventListener("click",scrToggle);
G("scrBrightRange").addEventListener("input",function(){
  var v=+this.value; txt("scrBright",v+" %");
  fetch("/disp?bright="+v,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("scrIdle").addEventListener("change",function(){
  var n=+this.value; if(isNaN(n)||n<0||n>60)return;
  fetch("/disp?idle="+n,{cache:"no-store"}).then(function(){setTimeout(poll,700)}).catch(function(){});
});
G("prev").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>=0&&i<S.days.length-1){S.date=S.days[i+1];loadHist()}});
G("next").addEventListener("click",function(){var i=S.days.indexOf(S.date);if(i>0){S.date=S.days[i-1];loadHist()}});

/* ---- подтянуть пропуски ---- */
function gapFill(){
  var b=G("gapBtn"); if(b){b.disabled=true;b.textContent="тяну…"}
  fetch("/gapfill?run=1&from="+S.date+"&to="+S.date,{cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
    var n0=(j&&j.gaps)||0,t0=Date.now(),last=-1,same=0;
    var iv=setInterval(function(){
      fetch("/data",{cache:"no-store"}).then(function(r){return r.json()}).then(function(d){
        if(d.gaps!==last){last=d.gaps;same=0}else same++;
        if(b)b.textContent="осталось "+d.gaps+"…";
        if((!d.gapAct||same>=2)&&Date.now()-t0>5000){
          clearInterval(iv);
          if(b){b.textContent=(d.gaps<n0?"готово":"в облаке нет данных");b.disabled=false}
          loadHist();
        }
      }).catch(function(){});
    },2500);
  }).catch(function(){if(b){b.disabled=false;b.textContent="ошибка"}});
}
G("gapBtn").addEventListener("click",gapFill);

loadDays(); poll(); setInterval(poll,5000);
</script>
</body>
</html>
)HTML";
