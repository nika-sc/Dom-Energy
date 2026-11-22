"use strict";
var BOOT_DATE=document.documentElement.dataset.bootDate||"",BOOT_HOUR=+document.documentElement.dataset.bootHour||0;
var S={date:"",days:[],hist:null,today:"",watt:false,nums:true,last:0,dayMax:0,sel:null};
function G(i){return document.getElementById(i)}
function txt(i,t){var e=G(i);if(e)e.textContent=t}
function num(v,n){return (v==null||isNaN(v))?"—":(+v).toFixed(n==null?0:n)}
function hhmm(ts){if(!ts)return "—";var d=new Date(ts*1000);return("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)}
function fmtDate(s){if(!s)return "—";var p=s.split("-");var m=["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"];return (+p[2])+" "+m[(+p[1])-1]+" "+p[0]}
function niceMax(v){var e=Math.pow(10,Math.floor(Math.log10(v||1))),f=(v||1)/e,c=[1,1.2,1.5,2,2.5,3,4,5,6,8,10];for(var i=0;i<c.length;i++)if(f<=c[i]+1e-9)return c[i]*e;return 10*e}

/* ---------- скины (те же шесть, что на плате) ---------- */
var SKIN_NAMES=["Светлая","Тёмная","ЧБ","Розовая","Красная","Радужная"];
var SKIN_ICON=["☀","🌙","◐","🌸","🔥","🌈"];
function setSkin(n,noBoard){
  n=((n%6)+6)%6;
  document.documentElement.setAttribute("data-skin",String(n));
  try{localStorage.setItem("dom.skin",String(n))}catch(e){}
  var mt=document.querySelector('meta[name="theme-color"]');
  if(mt){ var BG=["#f8faff","#0b111b","#000000","#ffe0ec","#2e0808","#0c0a18"]; mt.setAttribute("content",BG[n]); }
  var b=G("themeBtn");
  if(b)b.textContent=SKIN_ICON[n]+" "+SKIN_NAMES[n];
  // тема браузера и тема платы независимы: плату не трогаем
}
function skinNext(){ var c=+(document.documentElement.getAttribute("data-skin")||1); setSkin(c+1); }
// ?skin=3 — посмотреть тему без сохранения (удобно для скриншотов и ссылок)
var FIXED_SKIN=null;
try{
  var q=new URLSearchParams(location.search).get("skin");
  if(q!=null && q!==""){ FIXED_SKIN=+q; setSkin(FIXED_SKIN,true); }
  else{
    var sk=localStorage.getItem("dom.skin");
    if(sk==null) sk=document.documentElement.getAttribute("data-skin");
    setSkin(sk==null?1:+sk,true);
  }
}catch(e){ setSkin(1,true); }

/* ---------- тема (совместимость) ---------- */
function setTheme(t){
  document.documentElement.setAttribute("data-theme",t);
  G("themeBtn").textContent = (t==="dark" ? "☀ Светлая" : "🌙 Тёмная");
  try{localStorage.setItem("dom.theme",t)}catch(e){}
}
G("themeBtn").addEventListener("click",function(){ skinNext() });
if(G("retryBtn")) G("retryBtn").addEventListener("click",function(){ poll() });
try{var th=localStorage.getItem("dom.theme"); if(th) document.documentElement.setAttribute("data-theme",th);}catch(e){}

/* ---------- живое состояние ---------- */
function applyEnv(d){
  try{
    var et=document.getElementById('envT');
    if(!et) return;
    if(d.eok){
      et.textContent=num(d.et,1);
      document.getElementById('envTS').textContent='датчик подключён';
      document.getElementById('envH').textContent=num(d.eh,0);
      document.getElementById('envP').textContent=num((d.ep||0)*0.75006,0);
      document.getElementById('envPS').textContent=num(d.ep,0)+' гПа';
    } else {
      et.textContent='—';
      document.getElementById('envTS').textContent='датчик не найден';
      document.getElementById('envH').textContent='—';
      document.getElementById('envP').textContent='—';
      document.getElementById('envPS').textContent='проверь /i2c';
    }
  }catch(e){}
}
function apply(d){
  if(!d)return;
  S.data=d; S.last=Date.now();
  var phaseKnown=d.vSeen==null?+d.v>50:!!d.vSeen, powerKnown=d.pSeen==null?d.p!=null&&phaseKnown:!!d.pSeen, tempKnown=d.tSeen==null?d.temp!=null&&!!d.tAt:!!d.tSeen, phaseStale=!!d.stale;
  txt("mv",num(phaseKnown?d.v:null)); txt("mp",num(powerKnown?d.p:null)); txt("ma",num(phaseKnown?d.amp:null,2)); txt("mt",num(tempKnown?d.temp:null,1));
  txt("ptar",num(d.tariff,2)); txt("pe",num(d.e,2));
  txt("tkwh",num(d.tkwh,2)); txt("tcost",num(d.tcost,2));
  applyEnv(d);
  txt("ykwh",num(d.ykwh,2)); txt("ykost",num(d.ykost,2));
  txt("mkwh",num(d.mkwh,2)); txt("mcost",num(d.mcost,2));
  txt("cRssi",num(d.rssi)+" дБм");
  txt("dVbig",num(phaseKnown?d.v:null)); txt("dAbig",num(phaseKnown?d.amp:null,2)); txt("dTbig",num(tempKnown?d.temp:null,1));
  txt("dVnow",!phaseKnown?"ожидание данных":(phaseStale?"устарело · ":"получено ")+hhmm(d.vAt)); txt("dAnow",!powerKnown?"ожидание данных":(+d.pAge>180?"устарело · ":"получено ")+hhmm(d.pAt)); txt("dTnow",!tempKnown?"ожидание данных":(+d.tAge>180?"устарело · ":"получено ")+hhmm(d.tAt));
  txt("mvAge","получено "+hhmm(d.vAt)); txt("mtAge","получено "+hhmm(d.tAt));
  txt("peAge","получено "+hhmm(d.eAt)); txt("tkwhAge","получено "+hhmm(d.eAt));
  var rel=G("rel");
  if(rel){rel.innerHTML='<i class="dot '+(d.r?"ok":"")+'"></i>'+(d.r?"ВКЛ":"ВЫКЛ");rel.style.color=d.r?"var(--c2)":"var(--red)"}
  txt("pcos","cos φ "+num((phaseKnown&&powerKnown&&d.amp>0.001)?d.p/(d.v*d.amp):null,2));
  var on=(d.disp===1||d.disp==="1");
  txt("scrState",on?"включена":"выключена");
  var bp=d.bright!=null?Math.round(d.bright/2.55):0;
  txt("scrBright",bp+" %");
  var rg=G("scrBrightRange"); if(rg&&document.activeElement!==rg)rg.value=Math.max(5,bp);
  var id=G("scrIdle"); if(id&&document.activeElement!==id)id.value=(d.idle!=null?d.idle:0);
  txt("scrEco",!on?"экран погашен":(d.idle>0?"гаснет через "+Math.round((d.idleMs||0)/60000)+" мин":"автогашение выкл"));
  txt("wxNow",(d.wDesc||"—")+" · "+num(d.wT,1)+" °C · дождь "+num(d.wPop)+" %");
  txt("wxTom",+d.wCodeT>0?num(d.wTminT)+"…"+num(d.wTmaxT)+" °C · дождь "+num(d.wPopT)+" %":"ожидание прогноза");
  txt("fc",num(d.mkwh/new Date().getDate()*30,1)+" кВт·ч");
  ring(d);
  if(S.hist&&S.date===S.today)drawE(S.hist);
  if(typeof d.gaps!=="undefined") gapShow(d.gaps, d.gapMin);
  if(window.p2OnData)window.p2OnData();   /* P2.1/P2.2: /data уже получен */
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
function netbar(on){
  var b=G("netbar");
  if(b) b.className = on ? "netbar on" : "netbar";
}
var POLL_BUSY=false, POLL_FAILS=0;
function poll(){
  if(POLL_BUSY)return;
  POLL_BUSY=true;
  fetch("/data",{cache:"no-store",timeoutMs:8000}).then(function(r){
    if(!r.ok) throw 0;
    return r.json();
  }).then(function(d){
    POLL_FAILS=0;
    document.documentElement.setAttribute("data-loading","0");
    netbar(false);
    txt("cNet",d.net?"интернет доступен":"локальная сеть · нет интернета");
    // тема браузера своя: скин платы не подхватываем
    apply(d);
    // P1.2: источник/свежесть (/measure) и качество данных (/storage).
    // Работает в этом же цикле poll; своего таймера нет.
    if(window.domStatusTick) window.domStatusTick(d);
  }).catch(function(){
    // Один сбой — не повод писать «нет связи»: плата бывает занята чтением
    // истории 5-15 с и отвечает с задержкой. Плашку показываем после 2 сбоев
    // подряд и сразу пробуем снова, чтобы связь восстановилась без F5.
    POLL_FAILS++;
    txt("cNet","нет связи");
    txt("liveAge","нет связи");
    if(POLL_FAILS>=2) netbar(true);
    document.documentElement.setAttribute("data-loading","0");
    if(window.domStatusTick) window.domStatusTick(null);
    setTimeout(function(){ if(!POLL_BUSY) poll() },2500);
  }).then(function(){POLL_BUSY=false})
}
try{document.documentElement.setAttribute("data-loading","1")}catch(e){}
setInterval(function(){if(!S.last)return;var s=Math.round((Date.now()-S.last)/1000);
  // 1-20 с — нормальная задержка; >20 с — данные устарели, но это НЕ значит,
  // что связи нет: плата может читать историю. «Нет связи» пишет только poll
  // после двух сбоев подряд (POLL_FAILS).
  var line=(s>20?"данные устарели ":"обновлено ")+s+" с";
  txt("cUpd",line);txt("liveAge",s>20?"устарели "+s+" с":s+" с")},1000);

/* ---------- история ---------- */
function loadDays(cb){
  domJson("/days").then(function(j){
    S.days=j.days||[]; S.today=S.days[0]||""; if(!S.date) S.date=S.today;
    loadHist(cb);
  }).catch(function(){txt('historySummary','Не удалось загрузить список дней. Повторите обновление.');if(cb)cb()});
}

function loadHist(cb){
  if(!S.date){if(cb)cb();return}
  var requestedDate=S.date;
  domJson("/history?date="+requestedDate).then(function(j){
    if(S.date!==requestedDate)return;
    S.hist=j;
    var mx=0,k;for(k=0;k<24;k++){var a=(j.hours||[])[k]||{};if(+a.pmax>mx)mx=+a.pmax}
    S.dayMax=mx;
    drawNav(); drawE(j); drawVT(j); renderDetails(j); if(window.historyAccept)window.historyAccept(j); if(window.p2OnHist)window.p2OnHist(j); if(cb)cb();
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
  var area=d;
  for(i=23;i>=0;i--){var v2=arr[i];if(v2==null)continue;area+="L"+(pl+slot*(i+0.5)).toFixed(1)+" "+(H-pb).toFixed(1)+" "}
  var s='<path d="'+area+'Z" fill="var(--c1)" opacity=".10"/>';
  s+='<path d="'+d+'" fill="none" stroke="var(--c1)" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"/>';
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
  for(i=0;i<24;i++){var a=h[i]||{};vs.push(+a.n>0?(+a.v||0):0);ts.push(+a.n>0&&a.t!=null?+a.t:null)}
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
G("gapDayBtn").addEventListener("click",function(){
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
  var sums=new Array(days.length), loaded=0;
  days.forEach(function(d,idx){
    fetch("/history?date="+d, {cache:"no-store"}).then(function(r){return r.json()}).then(function(j){
      /* Сумма за сутки только по известным kwh; null — пробел, а не 0.
         known < total → сумма неполная и помечается «≥». */
      var h=(j&&j.hours)||[], s=null, known=0, total=0;
      for(var i=0;i<h.length;i++){
        var r=h[i];
        if(!r||!(+r.n>0))continue;
        total++;
        if(r.kwh!=null&&isFinite(+r.kwh)){s=(s==null?0:s)+(+r.kwh);known++}
      }
      sums[idx]={kwh:s,known:known,total:total};
    }).catch(function(){ sums[idx]={kwh:null,known:0,total:0} }).then(function(){
      loaded++;
      if(loaded===days.length) renderWeek(days.slice().reverse(), sums.slice().reverse());
    });
  });
}

function renderWeek(days, sums){
  var W=900,H=150,pl=14,pr=10,pt=16,pb=22,i;
  var slot=(W-pl-pr)/Math.max(1,days.length), base=H-pb, mx=0;
  for(i=0;i<sums.length;i++){var v=sums[i]&&sums[i].kwh;if(v!=null&&v>mx)mx=v}
  var s='<line x1="'+pl+'" y1="'+base+'" x2="'+(W-pr)+'" y2="'+base+'" stroke="var(--grid)"/>';
  for(i=0;i<days.length;i++){
    var e=sums[i]||{kwh:null,known:0,total:0}, val=e.kwh, x=pl+i*slot+slot*0.18, w=slot*0.64;
    var bh = val==null ? 0 : (val===0 ? 2 : (mx>0 ? Math.max(2,val/mx*(base-pt)*0.8) : 2));
    var tail = e.known<e.total ? '≥ ' : '';
    var tip = days[i]+': '+(val==null?'нет измерений':tail+val.toFixed(2)+' кВт·ч')+
              (e.total?' · часов с данными '+e.known+' из '+e.total:'');
    if(bh>0) s+='<rect x="'+x.toFixed(1)+'" y="'+(base-bh).toFixed(1)+'" width="'+w.toFixed(1)+'" height="'+bh.toFixed(1)+'" rx="2" fill="var(--bar1)"><title>'+tip+'</title></rect>';
    else s+='<rect x="'+x.toFixed(1)+'" y="'+(base-2).toFixed(1)+'" width="'+w.toFixed(1)+'" height="2" rx="1" fill="var(--grid)"><title>'+tip+'</title></rect>';
    s+='<text class="ax" x="'+(x+w/2).toFixed(1)+'" y="'+(base-bh-4).toFixed(1)+'" text-anchor="middle">'+(val==null?'—':tail+val.toFixed(2))+'</text>';
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
  for(var i=0;i<bs.length;i++){
    var on=bs[i].getAttribute("data-p")===p;
    bs[i].className=on?"on":"";
    bs[i].setAttribute("aria-pressed",on?"true":"false");
  }
}

/* ---------- графики периода: расход и температура по дням ---------- */

function drawBars(el, seq, hourly){
  var W=900,H=170,pl=34,pr=10,pt=18,pb=26;
  var mx=0; for(var i=0;i<seq.length;i++){ if(+seq[i].kwh>mx) mx=+seq[i].kwh }
  if(!(mx>0)) mx=1;
  var g2=niceMax(mx);
  var s='';
  for(var g=0;g<=2;g++){ var v=g2*g/2, yy=pt+(1-v/g2)*(H-pt-pb);
    s+='<line x1="'+pl+'" y1="'+yy.toFixed(1)+'" x2="'+(W-pr)+'" y2="'+yy.toFixed(1)+'" stroke="var(--grid)"/>';
    s+='<text class="bl" x="'+(pl-6)+'" y="'+(yy+3).toFixed(1)+'" text-anchor="end">'+num(v,g2<0.1?3:g2<1?2:g2<10?1:0)+'</text>';
  }
  var slot=(W-pl-pr)/seq.length, bw=Math.max(3,Math.min(38,slot*0.62));
  for(var i=0;i<seq.length;i++){
    var r=seq[i], x=pl+slot*i+(slot-bw)/2, v=+r.kwh||0;
    var yy=pt+(1-v/g2)*(H-pt-pb), hgt=H-pb-yy;
    var hot = g2>0 && v>=g2*0.85;
    var label = hourly ? '' : r.d;
    s+='<g class="bclick" data-day="'+(label||'')+'" data-index="'+i+'" style="cursor:pointer">';
    s+='<rect class="bar-hit" x="'+(pl+slot*i).toFixed(1)+'" y="'+pt+'" width="'+slot.toFixed(1)+'" height="'+(H-pt-pb)+'" fill="transparent"/>';
    s+='<rect class="bar-value" x="'+x.toFixed(1)+'" y="'+yy.toFixed(1)+'" width="'+bw.toFixed(1)+'" height="'+Math.max(1,hgt).toFixed(1)+
       '" rx="2" fill="'+(hot?'var(--c4)':'var(--c1)')+'" opacity="'+(hourly&&!r.ok?0.25:0.92)+'"/>';
    if(seq.length<=24 && v>0)
      s+='<text class="bv" x="'+(x+bw/2).toFixed(1)+'" y="'+(yy-4).toFixed(1)+'" text-anchor="middle">'+
         (hourly ? num(v,v<10?2:1) : (v<10?num(v,2):num(v,1)))+'</text>';
    var lab = hourly ? (r.hour+'') : (r.d.slice(8)+'.'+r.d.slice(5,7));
    if(!hourly && seq.length>16){
      var step = seq.length>25 ? 5 : Math.ceil(seq.length/12);
      if(i%step!==0) lab='';
    }
    if(hourly && i%3!==0) lab='';
    if(lab) s+='<text class="bl" x="'+(x+bw/2).toFixed(1)+'" y="'+(H-8)+'" text-anchor="middle">'+lab+'</text>';
    if(label) s+='<title>'+r.d+' · '+num(v,2)+' кВт·ч — нажми, чтобы открыть этот день</title>';
    s+='</g>';
  }
  el.innerHTML='<svg viewBox="0 0 '+W+' '+H+'" style="pointer-events:auto">'+s+'</svg>';
  el.__barRows=seq; el.__barHourly=hourly;
  // делегирование на контейнер: обработчик живёт дольше, чем SVG (его перерисовывают)
  if(!el.__barClick){
    el.__barClick = function(ev){
      var tgt = ev.target, g = null;
      while (tgt && tgt !== el) {
        if (tgt.getAttribute && tgt.getAttribute('class') === 'bclick') { g = tgt; break }
        tgt = tgt.parentNode;
      }
      if(!g) return;
      var idx=+g.getAttribute('data-index'), row=el.__barRows[idx];
      if(!row) return;
      if(el.__barHourly){
        if(ev.type==='click' && Date.now()-(el.__hourDownAt||0)<700) return;
        if(ev.type==='pointerdown') el.__hourDownAt=Date.now();
        ev.preventDefault();
        el.querySelectorAll('.bclick.selected').forEach(function(x){x.classList.remove('selected')});
        g.classList.add('selected');
        var detail=el.nextElementSibling;
        if(!detail || !detail.classList.contains('bar-detail')){detail=document.createElement('div');detail.className='bar-detail';el.parentNode.insertBefore(detail,el.nextSibling)}
        var h=+row.hour, data=row.data;
        detail.textContent=('0'+h).slice(-2)+':00–'+('0'+(h+1)).slice(-2)+':00 · '+(data && +data.n>0 ? 'энергия '+num(data.kwh,3)+' кВт·ч · средняя мощность '+num(data.p,0)+' Вт · пик '+num(data.pmax,0)+' Вт · напряжение '+num(data.v,1)+' В · температура '+num(data.t,1)+' °C' : 'нет данных');
        return;
      }
      var d = g.getAttribute('data-day'); if(!d) return;
      // pointerdown и click приходят парой: один выбор дня = один запрос.
      var now=Date.now();
      if(ev.type==='click' && el.__barDownDay===d && now-el.__barDownAt<700) return;
      if(ev.type==='pointerdown'){ el.__barDownDay=d; el.__barDownAt=now; }
      ev.preventDefault();
      openRangeDay(d);
    };
    el.addEventListener('pointerdown', el.__barClick, false);
    el.addEventListener('click', el.__barClick, false);
  }
}

/* ---------- раскрытие суток: профиль по часам прямо в карточке периода ---------- */
function openRangeDay(day){
  if(!/^\d{4}-\d{2}-\d{2}$/.test(day)) return;
  if(window.openMetricHistory){window.openMetricHistory('kwh',day);return}
  var from=G('rngFrom'), to=G('rngTo');
  if(from) from.value=day;
  if(to) to.value=day;
  rngMark('');
  rngLoad(day,day);
}

function rangeTempValid(r){return r.tvalid!==false && +r.rows>0 && r.tmin!=null && r.tmax!=null}
function escapeText(value){return String(value).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
/* Сутки «измерены», если плата отдала хотя бы одну строку. Пустые сутки /range
   присылает с rows:0 и kwh:0 — это не ноль расхода, а отсутствие измерений. */
function rngDayMeasured(r){return !!r&&(r.rows!=null?+r.rows>0:(r.kwh!=null||r.vvalid===true||+r.pmax>0))}
function rngRender(j){
  RNG.rows=j.rows||[];
  var t=j.totals||{};
  var days=+t.days||0,total=+t.daysTotal>0?+t.daysTotal:RNG.rows.length,missing=+t.missing||0,unknown=+t.unknownKwh||0;
  var kwhDays=+t.kwhDays||0;
  var live=!!(j.to&&S&&S.today&&j.to>=S.today);
  var partial=days>0&&(missing>0||unknown>0||live);
  var pre=partial?"≥ ":"";
  txt("rKwh",t.kwh==null?"—":pre+num(t.kwh,2));
  var kwhS=days?(days+(days==1?" день":" дней")+" с данными"+(missing?" · без измерений "+missing+(total?" из "+total:""):"")+(unknown?" · у "+unknown+" сут нет суммы кВт·ч":"")+(live?" · сегодняшние сутки ещё не закончились":"")):"нет измерений за период";
  txt("rKwhS",kwhS);
  txt("rCost",t.cost==null?"—":pre+num(t.cost,2)); txt("rCostS","средний тариф "+(t.kwh>0?(t.cost/t.kwh).toFixed(2):"—")+" ₽");
  txt("rDN",(t.dayKwh==null?"—":num(t.dayKwh,2))+" / "+(t.nightKwh==null?"—":num(t.nightKwh,2)));
  txt("rDNS",(t.dayCost==null?"—":num(t.dayCost,2))+" ₽ / "+(t.nightCost==null?"—":num(t.nightCost,2))+" ₽");
  txt("rAvg",t.avgDayKwh==null?"—":num(t.avgDayKwh,2));
  var avgS=[];
  if(t.vmin!=null&&t.vmax!=null)avgS.push("U "+t.vmin+"–"+t.vmax+" В");
  if(t.pmax!=null)avgS.push("пик "+t.pmax+" Вт");
  avgS.push(days?(missing||unknown?"среднее по доступным суткам ("+kwhDays+" из "+total+")":"среднее по "+days+" суткам"):"—");
  txt("rAvgS",avgS.join(" · "));
  txt("rngNote",(j.from||"")+" — "+(j.to||""));
  var tb=G("rngTbl").getElementsByTagName("tbody")[0], h="";
  for(var i=0;i<RNG.rows.length;i++){
    var r=RNG.rows[i],ok=rngDayMeasured(r);
    var cell=function(v,fmt){return ok&&v!=null&&isFinite(+v)?fmt(v):"—"};
    h+="<tr"+(ok?"":" class='mut'")+"><td>"+fmtDate(r.d)+(ok?"":" · нет измерений")+"</td><td>"+cell(r.kwh,function(v){return num(v,2)})+"</td><td>"+cell(r.cost,function(v){return num(v,2)})+"</td>"
      +"<td class='mut'>"+cell(r.dayKwh,function(v){return num(v,2)})+"</td><td class='mut'>"+cell(r.nightKwh,function(v){return num(v,2)})+"</td>"
      +"<td>"+(ok&&!(r.vvalid===false)&&+(r.vmax)>0?num(r.vmin,0)+"–"+num(r.vmax,0):"—")+"</td>"
      +"<td>"+(ok&&rangeTempValid(r)?num(r.tmin,1)+"–"+num(r.tmax,1):"—")+"</td>"
      +"<td class='mut'>"+cell(r.pmax,function(v){return num(v,0)})+"</td>"
      +"<td class='mut'>"+escapeText(r.w||"—")+"</td>"
      +"<td class='mut'>"+(ok&&r.wvalid!==false && r.wtmin!=null && r.wtmax!=null && (r.wvalid || r.w)? num(r.wtmin,1)+"–"+num(r.wtmax,1)+"°" : "—")+"</td></tr>";
  }
  if(!h) h="<tr><td colspan='10' class='mut'>нет данных за период</td></tr>";
  tb.innerHTML=h;
}
/* «Заполнить пропуски»: запуск + опрос /gapfill/status каждые 2 с.
   Показываем «собрано X из Y окон · вставлено N строк», по завершении — итог
   (msg) или причину (sdErr/sdMsg) и обновляем таблицу периода и график. */
var GAP_BUSY=false, GAP_TIMER=null, GAP_POLLS=0, GAP_DONE=0;
var GAP_PHASE=['простой','поиск пропусков','запрос облака','запись на карту'];
function gapPhase(p){ return GAP_PHASE[+p]||'работа' }
function gapShow(g,m){
  // Итог заполнения держим на экране минуту, фоновый /data его не затирает.
  if(GAP_BUSY||Date.now()-GAP_DONE<60000) return;
  txt("gapInfo",(g>0)?("пропусков "+g+", "+(m||0)+" мин"):"пропусков нет");
}
function gapFinish(text, ok){
  if(GAP_TIMER){ clearInterval(GAP_TIMER); GAP_TIMER=null }
  GAP_BUSY=false; GAP_DONE=Date.now();
  txt("gapInfo", text);
  var b=G("gapBtn"); if(b){ b.disabled=false; b.textContent="Заполнить пропуски" }
  // Плата заново пишет /energy.csv — просим новые данные, когда скан уже закончен.
  if(ok!==false) setTimeout(function(){
    try{ if(window.historyRefresh) window.historyRefresh(); }catch(e){}
    try{ loadDays(); }catch(e){}
  }, 900);
}
function gapProgress(s){
  var parts=[];
  var x=+s.gap||0, y=+s.gaps||0;
  parts.push('собрано '+Math.min(x,y)+' из '+y+' окон');
  parts.push('вставлено '+(s.ins!=null?s.ins:(s.insTotal||0))+' строк');
  if(s.phase!=null) parts.push(gapPhase(s.phase));
  return parts.join(' · ');
}
function gapFillRun(){
  if(GAP_BUSY) return;
  var f=RNG.from||(G("rngFrom")||{}).value||S.date||S.today;
  var t2=RNG.to||(G("rngTo")||{}).value||f;
  if(!f||!t2){ txt("gapInfo","сначала выбери период"); return }
  GAP_BUSY=true; GAP_POLLS=0;
  var b=G("gapBtn"); if(b){ b.disabled=true; b.textContent="заполняю…" }
  txt("gapInfo","запускаю…");
  fetch("/gapfill?run=1&from="+f+"&to="+t2,{cache:"no-store"}).then(function(r){ return r.json() }).then(function(j){
    if(!j||j.ok===false){ var er=j&&j.err?String(j.err):""; gapFinish("не запустилось"+(er?": "+(er==="sd"?"нет карты SD":er==="recovery busy"?"идёт восстановление карты":er):""),false); return }
    GAP_TIMER=setInterval(function(){
      if(++GAP_POLLS>450){ gapFinish("не дождался завершения за 15 минут",false); return }
      fetch("/gapfill/status",{cache:"no-store"}).then(function(r){ return r.json() }).then(function(s){
        if(s&&s.active){ txt("gapInfo",gapProgress(s)+' · '+(GAP_POLLS*2)+' с'); return }
        if(GAP_TIMER){ clearInterval(GAP_TIMER); GAP_TIMER=null }
        if(!s){ gapFinish("плата не ответила",false); return }
        var msg=(s.msg&&String(s.msg).trim)?String(s.msg).trim():"";
        if(+s.sdErr){ gapFinish("ошибка карты SD ("+s.sdErr+")"+(s.sdMsg?": "+s.sdMsg:msg?": "+msg:""),false); return }
        var ins=(s.insLast!=null?s.insLast:(s.ins!=null?s.ins:s.insTotal||0));
        var left=+s.gaps||0;
        gapFinish(msg?msg:("готово: вставлено "+ins+" строк"+(left?" · пропусков осталось "+left:"")),true);
      }).catch(function(){
        // одна неудача опроса — не повод бросать: плата занята записью на карту
        if(!GAP_TIMER) txt("gapInfo","нет ответа платы · продолжаю ждать…");
      });
    },2000);
  }).catch(function(){ gapFinish("ошибка запроса запуска",false) });
}
document.getElementById("gapBtn").addEventListener("click", gapFillRun);

function rngClear(){
  ["rKwh","rCost","rDN","rAvg","rKwhS","rCostS","rDNS","rAvgS"].forEach(function(i){ txt(i,"—"); });
  RNG.rows=[];
  G("rngTbl").getElementsByTagName("tbody")[0].innerHTML="<tr><td colspan='10' class='mut'>нет данных за период</td></tr>";
}
// период длиннее 31 дня плата считает порциями: 31 день за запрос
var RNG_PRINT_NOTE="";
function printStamp(){
  var el=G("rngNote"); if(!el) return;
  RNG_PRINT_NOTE=el.textContent;
  var n=new Date();
  var d=("0"+n.getDate()).slice(-2)+"."+("0"+(n.getMonth()+1)).slice(-2)+"."+n.getFullYear();
  var tm=("0"+n.getHours()).slice(-2)+":"+("0"+n.getMinutes()).slice(-2);
  el.textContent=el.textContent+" · сформировано "+d+" "+tm;
}
window.addEventListener("beforeprint", printStamp);
window.addEventListener("afterprint", function(){ if(RNG_PRINT_NOTE) G("rngNote").textContent=RNG_PRINT_NOTE; });

var RNG_SEQUENCE=0;
/* Единственный источник /range — history.js (там прогресс 202 и нарезка по 31 дню).
   Здесь только просьба загрузить диапазон, чтобы не было двух параллельных загрузок. */
function rngLoad(from,to){
  RNG_SEQUENCE++;
  if(window.p2Reset)window.p2Reset();   /* P2.1: новый период — прошлый итог не показываем */
  if(window.historyLoadRange){ window.historyLoadRange(from,to); return }
  txt("rngNote","история периода недоступна");
}
/* Итоги и таблица периода из уже полученных строк /range (без второго запроса).
   Суммируем только измеренные сутки и только известные значения: null — это
   «неизвестно», а не 0; пустые сутки (rows:0) в итог и среднее не входят. */
function rngFill(from,to,rows){
  rows=rows||[];
  RNG.from=from||""; RNG.to=to||""; RNG.rows=rows;
  var tot={kwh:null,cost:null,dayKwh:null,nightKwh:null,dayCost:null,nightCost:null,vmin:null,vmax:null,pmax:null};
  var measured=0,missing=0,kwhDays=0;
  for(var i=0;i<rows.length;i++){
    var r=rows[i];if(!r)continue;
    if(!rngDayMeasured(r)){missing++;continue}
    measured++;
    if(r.kwh!=null&&isFinite(+r.kwh)){tot.kwh=(tot.kwh==null?0:tot.kwh)+(+r.kwh);kwhDays++}
    if(r.cost!=null&&isFinite(+r.cost))tot.cost=(tot.cost==null?0:tot.cost)+(+r.cost);
    if(r.dayKwh!=null&&isFinite(+r.dayKwh))tot.dayKwh=(tot.dayKwh==null?0:tot.dayKwh)+(+r.dayKwh);
    if(r.nightKwh!=null&&isFinite(+r.nightKwh))tot.nightKwh=(tot.nightKwh==null?0:tot.nightKwh)+(+r.nightKwh);
    if(r.dayCost!=null&&isFinite(+r.dayCost))tot.dayCost=(tot.dayCost==null?0:tot.dayCost)+(+r.dayCost);
    if(r.nightCost!=null&&isFinite(+r.nightCost))tot.nightCost=(tot.nightCost==null?0:tot.nightCost)+(+r.nightCost);
    var vmn=+r.vmin,vmx=+r.vmax;
    if(r.vvalid!==false&&vmn>0&&(tot.vmin==null||vmn<tot.vmin))tot.vmin=vmn;
    if(r.vvalid!==false&&vmx>0&&(tot.vmax==null||vmx>tot.vmax))tot.vmax=vmx;
    if(r.pmax!=null&&isFinite(+r.pmax)&&(tot.pmax==null||+r.pmax>tot.pmax))tot.pmax=+r.pmax;
  }
  tot.days=measured; tot.daysTotal=rows.length; tot.missing=missing;
  tot.unknownKwh=measured-kwhDays; tot.kwhDays=kwhDays;
  tot.avgDayKwh=kwhDays?tot.kwh/kwhDays:null;
  rngRender({from:RNG.from,to:RNG.to,days:measured,daysTotal:rows.length,missing:missing,totals:tot,rows:rows});
  if(window.p2OnRange)window.p2OnRange(from,to,rows);   /* P2.1: сравнение с прошлым периодом */
}
window.rngFill=rngFill;
function rngApply(p){
  rngMark(p);
  var r=rngPeriod(p);
  G("rngFrom").value=r.from; G("rngTo").value=r.to;
  rngLoad(r.from,r.to);
}
function rngCsv(){
  // P2.4: экспорт с заголовком единиц, временной зоной и пустыми пропусками
  // живёт в page.html (там же блок «Экспорт и перенос»), чтобы не растить app.js.
  if(window.p34Csv){ var c=window.p34Csv(); if(c&&c.err){ alert(c.err); return } if(window.p34Download){ window.p34Download(); return } }
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
rngMark("");   /* начальное состояние aria-pressed у пресетов периода */
G("rngGo").addEventListener("click",function(){
  var a=G("rngFrom").value,b=G("rngTo").value;
  if(!a||!b){ alert("укажи даты"); return; }
  if(a>b){ var x=a;a=b;b=x; }
  rngMark(""); rngLoad(a,b);
});
G("rngCsv").addEventListener("click",rngCsv);
G("rngPrint").addEventListener("click", function(){ window.print(); });


// ---------------------------------------------------------------- панель дня сверху
function daySync(){
  var dl=G("dateLabel"), l=G("dayLbl"), n=G("dayNote");
  var txt2 = dl ? dl.textContent : "—";
  if(l) l.textContent = txt2;
  if(n){
    var isToday = (S.date && S.date===S.today);
    n.textContent = isToday ? "текущие показания и история за сегодня"
                            : "текущие показания — сейчас; история — за " + txt2;
    n.style.color = isToday ? "" : "var(--c4)";
  }
}
function dayStep(dir){
  var p=G("prev"), nx=G("next");
  if(dir<0 && p) p.click(); else if(dir>0 && nx) nx.click();
  setTimeout(daySync, 350);
}
function dayTodayGo(){
  if(S.days && S.days.length){ S.date = S.days[0]; loadHist(); }
  setTimeout(daySync, 400);
}
(function(){
  var bp=G("dayPrev"), bn=G("dayNext"), bt=G("dayToday");
  /* role=button на <span> должен работать и с клавиатуры: Enter/пробел. */
  function dayKey(el,fn){
    if(!el) return;
    el.addEventListener("keydown",function(e){
      if(e.key==="Enter"||e.key===" "||e.key==="Spacebar"){e.preventDefault();fn()}
    });
  }
  if(bp){ bp.addEventListener("click", function(){ dayStep(-1); }); dayKey(bp,function(){ dayStep(-1) }); }
  if(bn){ bn.addEventListener("click", function(){ dayStep(1); }); dayKey(bn,function(){ dayStep(1) }); }
  if(bt){ bt.addEventListener("click", dayTodayGo); dayKey(bt,dayTodayGo); }
  setInterval(daySync, 1500);
  // старый переключатель дня внутри карточки «История» больше не нужен — он теперь сверху
  var pv=G("prev");
  if(pv && pv.parentNode) pv.parentNode.style.display="none";
})();
window.addEventListener("beforeprint", function(){
  // Вся статистика лежит в одном <details>: перед печатью раскрываем и, если
  // данные ещё не грузились, просим history.js загрузить их.
  var rep=G("historyWorkspace");
  if(rep){
    rep.open=true;
    if(window.historyEnsureLoaded){ try{ window.historyEnsureLoaded() }catch(e){} }
  }
  var m=G("printMeta"); if(!m) return;
  var fw=(document.querySelector(".ver")||{}).textContent||"";
  var td=document.getElementById("tarDay"), tn=document.getElementById("tarNight");
  var now=new Date();
  var d=("0"+now.getDate()).slice(-2)+"."+("0"+(now.getMonth()+1)).slice(-2)+"."+now.getFullYear();
  var tm=("0"+now.getHours()).slice(-2)+":"+("0"+now.getMinutes()).slice(-2);
  m.innerHTML = "Дом — энергия · отчёт за период · " + (RNG.from||"") + " — " + (RNG.to||"") +
    "<br>тариф: день " + (td?td.textContent:"—") + " · ночь " + (tn?tn.textContent:"—") +
    " · " + fw.trim() + " · сформировано " + d + " " + tm;
});

loadDays(); poll(); setInterval(function(){if(!document.hidden)poll()},3000); (function(){
  // Ленивая загрузка: при открытии страницы считаем только «Часы · сегодня».
  // /range (7/30 дней, месяц) запускается только по кнопке или по ссылке ?p=…
  var q=(location.search.match(/[?&]p=([a-z0-9]+)/)||[])[1];
  var ok=["today","yest","d2","d7","d30","week","month","quarter","half","prev","year","ytd"];
  if(q&&ok.indexOf(q)>=0&&q!=="today") setTimeout(function(){ rngApply(q) }, 0);
})(); initQuality(); initSched(); tuyaUpdate(); setInterval(tuyaUpdate, 60000);
/* ---------- звуки включения и перезагрузки ---------- */
var SND_BOOT=null;
function sndLoad(){
  fetch('/sound',{cache:'no-store'}).then(function(r){return r.json()}).then(function(s){
    SND_BOOT=s;
    ['boot','reboot'].forEach(function(key){
      var sel=G('snd'+(key==='boot'?'Boot':'Reboot'));
      if(!sel) return;
      var ev=(s.events||[]).filter(function(e){return e.key.indexOf(key)===0})[0];
      var cur=ev?+ev.preset:0;
      sel.innerHTML=(s.presets||[]).map(function(p){
        return '<option value="'+p.id+'"'+(p.id===cur?' selected':'')+'>'+p.name+'</option>';
      }).join('');
      sel.addEventListener('change',function(){
        fetch('/sound?'+key+'='+sel.value).then(function(){/* сохранено */});
      });
    });
  }).catch(function(){});
}
function sndPlay(key){
  fetch('/beep?ev='+key).then(function(){},function(){});
}
(function(){
  if(!G('sndBoot')) return;
  sndLoad();
  G('sndBootPlay').addEventListener('click',function(){ sndPlay('boot') });
  G('sndRebootPlay').addEventListener('click',function(){ sndPlay('reboot') });
})();

/* тариф и перезагрузка */
(function(){var b=G('tarSave');if(b)b.addEventListener('click',function(){var d=+G('tarDayIn').value,n=+G('tarNightIn').value,f=+G('tarFromIn').value,t=+G('tarToIn').value;if(!(d>0)||!(n>0)){txt('tarMsg','цена > 0');return}fetch('/tariff?set='+d+','+n+','+t+','+f).then(function(r){return r.text()}).then(function(){txt('tarMsg','сохранено');loadDays();poll()}).catch(function(){txt('tarMsg','ошибка')})});var rb=G('rebootBtn');if(rb)rb.addEventListener('click',function(){if(!confirm('Перезагрузить плату?'))return;txt('rebootMsg','…');fetch('/reboot').then(function(){txt('rebootMsg','отправлено')}).catch(function(){txt('rebootMsg','нет связи')})})})();

document.addEventListener('visibilitychange',function(){if(!document.hidden)poll()});

/* подпись «месяц»: с 1-го по сегодня */
(function(){
  var e=G('mkwhS'); if(!e) return;
  var d=new Date(), m=['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'];
  e.textContent='с 1 по '+d.getDate()+' '+m[d.getMonth()];
})();
