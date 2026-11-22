/* История показателей — вариант 2: один блок, один ряд периодов.
   Правило простое: период задаёт пресет (Сегодня…С начала года) или свой
   диапазон, а вид графика подбирается сам:
     1 день  → столбики по часам (видно скачки за сутки);
     до 62 дней → столбики по дням;
     больше → столбики по неделям.
   Кнопками «Часы · Дни · Недели» вид можно переопределить вручную.
   Легенда (Расход, Пик мощности, …) отключает ряды в любом виде. */
(function(){
  var hourly=[['kwh','Энергия','кВт·ч',3],['p','Средняя мощность','Вт',0],['pmax','Пиковая мощность','Вт',0],['v','Напряжение','В',1],['t','Температура реле','°C',1]];
  var period=[['kwh','Расход','кВт·ч',3],['pmax','Пик мощности','Вт',0],['vmin','Напряжение мин','В',1],['vmax','Напряжение макс','В',1],['tmin','Температура мин','°C',1],['tmax','Температура макс','°C',1]];
  var colors={kwh:'#24c7e8',p:'#f3ad33',pmax:'#ed6b54',v:'#80aaff',vmin:'#6495ed',vmax:'#80c8ff',t:'#ef81bd',tmin:'#c873d9',tmax:'#ef81bd'}, hidden={};
  var MON=['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'];

  var mode='hours', date='', rangeFrom='', rangeMode=false;
  var selected=-1, points=[], request=0, loading=false, controller=null;
  var everLoaded=false, loadTries=0, loadedFrom='', loadedTo='';

  var host=G('historyWorkspace'), plot=host.querySelector('.history-combined svg');
  var root=G('historyMetrics'), legend=host.querySelector('.history-legend');

  function add(d,n){var x=new Date(d+'T12:00:00');x.setDate(x.getDate()+n);return iso(x)}
  function dayDiff(a,b){return Math.round((new Date(b+'T12:00:00')-new Date(a+'T12:00:00'))/86400000)}
  function isDate(s){return /^\d{4}-\d{2}-\d{2}$/.test(s)}
  function pair(a,b,unit){return a==null||b==null?'—':num(a,1)+'–'+num(b,1)+' '+unit}
  function delay(ms){return new Promise(function(r){setTimeout(r,ms)})}
  function status(t){G('historySummary').textContent=t}
  function hint(t){var el=G('historyHint');if(el)el.textContent=t}
  function dShort(s){var p=s.split('-');return (+p[2])+' '+MON[+p[1]-1]}
  function periodText(from,to){
    if(!isDate(from))return dShort(to);
    if(from===to)return dShort(to);
    var a=from.split('-'),b=to.split('-');
    if(a[0]===b[0]&&a[1]===b[1])return (+a[2])+'–'+(+b[2])+' '+MON[+b[1]-1];
    return dShort(from)+' – '+dShort(to);
  }
  function autoMode(from,to){var n=dayDiff(from,to);return n<=0?'hours':n<=62?'days':'weeks'}
  function modeTitle(){return mode==='hours'?'Показатели по часам':mode==='weeks'?'Показатели по неделям':'Показатели по дням'}
  function progressText(j){var kb=j&&+j.read>0?Math.round(+j.read/1024):0;return kb?'прочитано '+kb+' КБ · считаю …':'считаю …'}
  /* Честная математика: null/undefined — это «неизвестно», а не 0.
     Суммируем только известные значения, остальное помечаем «неполные данные». */
  function isNum(x){return x!=null&&x!==''&&isFinite(+x)}
  function addNum(a,x){return isNum(x)?(a==null?0:a)+(+x):a}
  function dm(s){return s.slice(8)+'.'+s.slice(5,7)}
  function todayIso(){var t=S&&S.today;if(isDate(t||''))return t;return iso(new Date())}
  function dayMeasured(x){return !!x&&(x.rows!=null?+x.rows>0:(isNum(x.kwh)||x.vvalid===true||+x.pmax>0))}
  function hourRange(item){return item.label+':00–'+('0'+(item.h+1)).slice(-2)+':00'}
  function escAttr(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
  function pointsForHours(d,j){
    var map={};(j&&j.hours||[]).forEach(function(r){if(r&&r.h!=null)map[+r.h]=r});
    var out=[];
    for(var i=0;i<24;i++){var r=map[i];out.push({label:('0'+i).slice(-2),date:d,h:i,valid:!!r&&(+r.n>0||(r.n==null&&r.kwh!=null)),values:r||{}})}
    return out;
  }
  /* Дни/недели: суммируем только известные kwh/cost, частичный бакет помечаем.
     Для недель конец бакета — min(start+6, to), будущие дни не считаются измеренными. */
  function pointsForPeriod(from,to,md,rows){
    var byDay={};
    (rows||[]).forEach(function(r){if(r&&isDate(r.d||''))byDay[r.d]=r});
    var step=(md==='weeks')?7:1,count=dayDiff(from,to)+1,today=todayIso(),out=[];
    for(var start=0;start<count;start+=step){
      var first=add(from,start);
      var last=step===1?first:(add(first,step-1)>to?to:add(first,step-1));
      var n=dayDiff(first,last)+1,list=[],k=0;
      for(k=0;k<n;k++){var day=byDay[add(first,k)];if(dayMeasured(day))list.push(day)}
      var val={kwh:null,cost:null,pmax:null,vmin:null,vmax:null,tmin:null,tmax:null},kwhKnown=0;
      list.forEach(function(x){
        if(isNum(x.kwh)){val.kwh=addNum(val.kwh,x.kwh);kwhKnown++}
        if(isNum(x.cost))val.cost=addNum(val.cost,x.cost);
        if(isNum(x.pmax))val.pmax=val.pmax==null?+x.pmax:Math.max(val.pmax,+x.pmax);
        var vv=x.vvalid==null?+x.vmax>0:!!x.vvalid,tv=x.tvalid==null?(+x.tmin!==0||+x.tmax!==0):!!x.tvalid;
        if(vv){
          if(isNum(x.vmin)&&+x.vmin>0)val.vmin=val.vmin==null?+x.vmin:Math.min(val.vmin,+x.vmin);
          if(isNum(x.vmax)&&+x.vmax>0)val.vmax=val.vmax==null?+x.vmax:Math.max(val.vmax,+x.vmax);
        }
        if(tv){
          if(isNum(x.tmin))val.tmin=val.tmin==null?+x.tmin:Math.min(val.tmin,+x.tmin);
          if(isNum(x.tmax))val.tmax=val.tmax==null?+x.tmax:Math.max(val.tmax,+x.tmax);
        }
      });
      var missing=n-list.length,unknownKwh=list.length-kwhKnown,live=first<=today&&last>=today;
      out.push({label:n===1?dm(first):dm(first)+'–'+dm(last),date:first,end:last,
                valid:list.length>0,measured:list.length,totalDays:n,missing:missing,kwhKnown:kwhKnown,unknownKwh:unknownKwh,
                partial:missing>0||unknownKwh>0||live,live:live,values:val});
    }
    return out;
  }
  function partialReason(p){
    var why=[];
    if(p.missing)why.push('нет измерений за '+p.missing+' из '+p.totalDays+' сут');
    if(p.unknownKwh)why.push('у '+p.unknownKwh+' сут нет суммы кВт·ч');
    if(p.live)why.push('текущие сутки ещё не закончились');
    return why.join('; ');
  }

  /* /range отвечает 202 {pending:true,retryMs,read} — ждём, показывая прогресс. */
  function rangeRequest(url,seq,onWait){
    var until=Date.now()+180000,fails=0;
    function attempt(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      return fetch(url,{cache:'no-store',signal:controller.signal,timeoutMs:15000}).then(function(r){
        return r.json().catch(function(){return {}}).then(function(j){return {status:r.status,j:j||{}}});
      }).then(function(x){
        if(seq!==request)throw Error('отменено');
        var j=x.j;
        if(x.status===202||j.pending){
          onWait(j);
          if(Date.now()>=until)throw Error('плата слишком долго считает период');
          return delay(Math.max(700,Math.min(3000,+j.retryMs||900))).then(attempt);
        }
        if(x.status>=500){var e=Error(j.err||('HTTP '+x.status));e.retryable=true;throw e}
        if(j.trunc)throw Error('плата вернула неполную историю — повторите загрузку');
        if(j.ok===false)throw Error(j.err||('HTTP '+x.status));
        return j;
      }).catch(function(error){
        if(seq!==request)throw error;
        var retry=error.retryable||error.name==='TypeError'||error.name==='AbortError'||error.name==='Read timeout';
        if(Date.now()<until&&retry&&fails<6){fails++;return delay(600*fails).then(attempt)}
        throw error;
      });
    }
    return attempt();
  }
  /* /history тоже асинхронный: отдаёт {pending:true,read,total}, пока читает CSV.
     Показываем прогресс и опрашиваем, пока день не будет готов. */
  function historyRequest(d,seq){
    var until=Date.now()+180000,fails=0;
    function attempt(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      return fetch('/history?date='+d,{cache:'no-store',signal:controller.signal,timeoutMs:15000}).then(function(r){
        return r.json().catch(function(){return {}}).then(function(j){return {status:r.status,j:j||{}}});
      }).then(function(x){
        if(seq!==request)throw Error('отменено');
        if(x.status===202||x.j.pending){
          var kb=Math.round((+x.j.read||0)/1024),tb=Math.round((+x.j.total||0)/1024);
          status('читаю день: '+kb+' КБ'+(tb?' из '+tb:''));
          if(Date.now()>=until)throw Error('плата слишком долго читает день');
          return delay(700).then(attempt);
        }
        if(x.status>=500){var e=Error(x.j.err||('HTTP '+x.status));e.retryable=true;throw e}
        if(x.j.ok===false)throw Error(x.j.err||('HTTP '+x.status));
        return x.j;
      }).catch(function(error){
        if(seq!==request)throw error;
        var retry=error.retryable||error.name==='TypeError'||error.name==='AbortError';
        if(Date.now()<until&&retry&&fails<6){fails++;return delay(600*fails).then(attempt)}
        throw error;
      });
    }
    return attempt();
  }

  /* Плата отдаёт максимум 31 день за запрос — длинный диапазон режем на части. */
  function rangeRows(from,to,seq){
    var chunks=[],cur=from;
    while(cur<=to){var end=add(cur,30);if(end>to)end=to;chunks.push([cur,end]);cur=add(end,1)}
    var all=[],i=0;
    function step(){
      if(seq!==request)return Promise.reject(Error('отменено'));
      if(i>=chunks.length)return Promise.resolve(all);
      var c=chunks[i],n=++i;
      return rangeRequest('/range?from='+c[0]+'&to='+c[1],seq,function(j){
        status(progressText(j)+(chunks.length>1?' · часть '+n+' из '+chunks.length:''));
      }).then(function(j){all=all.concat(j.rows||[]);return step()});
    }
    return step();
  }

  /* Загрузка: d — конец периода (обычно выбранная дата), fromDate — начало
     для режима диапазона (пресеты и «свой диапазон»). */
  function load(d,newMode,fromDate){
    if(!isDate(d))return;
    everLoaded=true;
    var prevDate=date,prevMode=mode,prevRange=rangeFrom,prevRangeMode=rangeMode,prevSel=selected;
    if(controller)controller.abort();controller=new AbortController();
    rangeMode=(newMode==='range');
    var from;
    if(rangeMode){
      if(isDate(fromDate||''))rangeFrom=fromDate;
      if(!isDate(rangeFrom)||rangeFrom>d)rangeFrom=d;
      from=rangeFrom;
      mode=autoMode(from,d);
    }else{
      mode=newMode||mode;
      rangeFrom='';
      from=mode==='hours'?d:(mode==='weeks'?add(d,-27):add(d,-6));
    }
    var keepSel=(prevDate===d&&prevMode===mode&&prevRangeMode===rangeMode&&(!rangeMode||prevRange===rangeFrom))?prevSel:-1;
    date=d;selected=-1;points=[];loading=true;var seq=++request;
    plot.innerHTML='';plot.style.display='none';plot.setAttribute('aria-busy','true');
    root.innerHTML='';G('historyDrill').hidden=true;
    G('historyDate').value=d;
    host.querySelectorAll('[data-mode]').forEach(function(b){var on=b.dataset.mode===mode;b.classList.toggle('on',on);b.setAttribute('aria-pressed',on?'true':'false')});
    /* «свой диапазон» всегда показывает текущий период: в часовом виде — дату */
    if(mode==='hours'){var f0=G('rngFrom'),t0=G('rngTo');if(f0)f0.value=d;if(t0)t0.value=d}
    else{var fIn=G('rngFrom'),tIn=G('rngTo');if(fIn)fIn.value=from;if(tIn)tIn.value=d}
    hint('загружаю…');
    G('historyChartTitle').textContent=modeTitle()+' · '+(mode==='hours'?dShort(d):periodText(from,d));
    var jobs;
    if(mode==='hours'){
      status('читаю историю дня…');
      jobs=historyRequest(d,seq);
    }else{
      status('считаю '+(mode==='weeks'?'по неделям':'по дням')+'…');
      jobs=rangeRows(from,d,seq);
    }
    jobs.then(function(j){
      if(seq!==request)return;
      loading=false;plot.setAttribute('aria-busy','false');plot.style.display='block';
      loadedFrom=mode==='hours'?'':from;loadedTo=d;loadTries=0;
      if(mode==='hours'){
        S.date=d;S.hist=j;
        if(window.drawNav)drawNav();if(window.renderDetails)renderDetails(j);
        points=pointsForHours(d,j);
      }else{
        /* …ниже, после сборки точек, выбираем последний день автоматически */
        var rows=j||[];
        points=pointsForPeriod(from,d,mode,rows);
        /* Итоги и таблица периода — в app.js (rngFill), без второго /range. */
        if(window.rngFill)window.rngFill(from,d,rows);
      }
      /* Повторное обновление той же даты/вида не сбрасывает выбор (даже если
         выбрана пустая ячейка). Для нового периода — последний час/день с данными. */
      if(keepSel>=0&&keepSel<points.length)selected=keepSel;
      if(selected<0){var last=-1;for(var q=0;q<points.length;q++)if(points[q].valid)last=q;selected=last}
      render();
    }).catch(function(error){
      if(seq!==request)return;
      loading=false;plot.style.display='none';plot.setAttribute('aria-busy','false');
      status('Не удалось загрузить данные. '+(error&&error.message?error.message:''));
      hint('ошибка загрузки');
      plot.innerHTML='';root.innerHTML='';G('historyDrill').hidden=true;
      everLoaded=false;
    });
  }

  /* Итоги выбранного дня — из 24 часов, чтобы в часовом виде было видно
     весь диапазон, а не только выбранный час. Только известные значения:
     null не превращается в 0, а средняя мощность взвешивается по n. */
  function dayTotalsFor(pts){
    if(!pts||!pts.length)return '';
    var kwh=null,kwhN=0,cnt=0,pmax=null,vmin=null,vmax=null,tmin=null,tmax=null;
    var pW=0,pWN=0,pPlain=0,pPlainN=0;
    pts.forEach(function(pt){
      var v=(pt&&pt.values)||{};
      if(!pt||!pt.valid)return;
      cnt++;
      if(isNum(v.kwh)){kwh=addNum(kwh,v.kwh);kwhN++}
      if(isNum(v.pmax)&&(pmax==null||+v.pmax>pmax))pmax=+v.pmax;
      if(isNum(v.p)){
        var n=+v.n;
        if(n>0){pW+=+v.p*n;pWN+=n}else{pPlain+=+v.p;pPlainN++}
      }
      if(isNum(v.v)&&+v.v>50){vmin=vmin==null?+v.v:Math.min(vmin,+v.v);vmax=vmax==null?+v.v:Math.max(vmax,+v.v)}
      if(isNum(v.t)&&+v.t>-60){tmin=tmin==null?+v.t:Math.min(tmin,+v.t);tmax=tmax==null?+v.t:Math.max(tmax,+v.t)}
    });
    if(!cnt)return 'за день: нет измерений';
    var s='за день: '+(kwh==null?'—':(kwhN<cnt?'≥ ':'')+num(kwh,3)+' кВт·ч');
    if(pWN)s+=' · средняя мощность '+num(pW/pWN,0)+' Вт (взвешено по n часов)';
    else if(pPlainN)s+=' · среднее по доступным часам '+num(pPlain/pPlainN,0)+' Вт';
    if(pmax!=null)s+=' · пик '+num(pmax,0)+' Вт';
    if(vmin!=null)s+=' · U '+pair(vmin,vmax,'В')+' (средние за час, '+cnt+' ч)';
    if(tmin!=null)s+=' · t '+pair(tmin,tmax,'°C')+' (средние за час, '+cnt+' ч)';
    s+=' · часов с измерениями '+cnt+' из 24'+(cnt<24?' — день неполный':'');
    if(pWN&&pPlainN)s+=' · '+pPlainN+' ч без n в среднюю не вошли';
    return s;
  }
  function dayTotalsText(){
    if(mode!=='hours'||!points.length)return '';
    return dayTotalsFor(points);
  }

  /* Осмысленная подпись для каждой ячейки графика: дата, диапазон, значения. */
  function describeItem(item){
    var specs=mode==='hours'?hourly:period,parts=[];
    if(item.valid){
      specs.forEach(function(m){var val=item.values?item.values[m[0]]:null;if(isNum(val))parts.push(m[1]+' '+num(val,m[3])+' '+m[2])});
      if(!parts.length)parts.push('показатели пусты');
      if(item.partial){var why=partialReason(item);parts.push('неполные данные'+(why?': '+why:''))}
    }else parts.push('нет измерений');
    var head=mode==='hours'?(dShort(item.date)+', '+hourRange(item)):periodText(item.date,item.end||item.date);
    return head+': '+parts.join(', ');
  }
  /* Ровно один элемент в tab-порядке (roving tabindex); остальные — стрелками. */
  function paintSelection(){
    plot.querySelectorAll('[data-selection]').forEach(function(r){r.setAttribute('opacity',+r.dataset.selection===selected?'.12':'0')});
    plot.querySelectorAll('[data-index]').forEach(function(g){
      var i=+g.dataset.index,on=i===selected;
      g.setAttribute('aria-selected',on?'true':'false');
      g.setAttribute('tabindex',(on||(selected<0&&i===0))?'0':'-1');
    });
  }

  function render(selectionOnly){
    if(loading)return;
    /* История может обновиться извне (app.js отдаёт /history опросом) — если
       выбор не задан, показываем последний час/день с данными. */
    if(selected<0){for(var q=points.length-1;q>=0;q--)if(points[q]&&points[q].valid){selected=q;break}}
    var specs=mode==='hours'?hourly:period,p=points[selected],v=p?p.values:{};
    var partial=!!(p&&p.valid&&p.partial),why=partial?partialReason(p):'';
    host.classList.toggle('hours-mode',mode==='hours');
    var dtEl=G('historyDayTotals');
    if(dtEl)dtEl.textContent=progressFreeDayTotals();
    hint(modeTitle().replace('Показатели ','')+' · '+(mode==='hours'?(p?p.date:date):(loadedFrom?loadedFrom+' — '+loadedTo:loadedTo||date)));
    G('historyDrill').hidden=mode==='hours'||!p;G('historyDrill').textContent=mode==='weeks'?'Открыть дни недели':'Открыть часы дня';
    plot.setAttribute('aria-label',mode==='hours'?'Показатели по часам, 24 ячейки: стрелки ← → переключают час, Enter выбирает':'Показатели по '+(mode==='weeks'?'неделям':'дням')+': стрелки ← → переключают, Enter выбирает');
    var where,state;
    if(mode==='hours'){where=p?p.date+' · '+hourRange(p):date;state=!p?'выберите час':(p.valid?'данные есть':'нет измерений')}
    else{where=p?p.date+(p.end!==p.date?' — '+p.end:''):'выберите '+(mode==='weeks'?'неделю':'день');state=p?(!p.valid?'нет измерений':(partial?'неполные данные':'данные есть')):''}
    G('historySummary').textContent=where+(state?' · '+state:'')+(partial&&why?' ('+why+')':'');
    legend.innerHTML=specs.map(function(m){
      return '<button type="button" data-series="'+m[0]+'" aria-pressed="'+(!hidden[m[0]])+'"><i style="background:'+colors[m[0]]+'"></i>'+m[1]+' ('+m[2]+')</button>'
    }).join('');
    /* Неполные суммы показываем как «≥», а не как точный итог: null/пропуски
       не должны выглядеть нулём. 0 остаётся настоящим нулём. */
    var pre=partial?'≥ ':'',cards;
    if(mode==='hours'){
      cards=hourly.map(function(m){
        var val=p&&p.valid?v[m[0]]:null;
        return {t:m[1],v:isNum(val)?num(val,m[3])+' '+m[2]:'—'};
      });
    }else{
      cards=[
        {t:'Расход',v:p&&p.valid&&isNum(v.kwh)?pre+num(v.kwh,3)+' кВт·ч':'—',bound:!!partial},
        {t:'Стоимость',v:p&&p.valid&&isNum(v.cost)?pre+num(v.cost,2)+' ₽':'—',bound:!!partial},
        {t:'Пик мощности',v:p&&p.valid&&isNum(v.pmax)?pre+num(v.pmax,0)+' Вт':'—',bound:!!partial},
        {t:'Напряжение мин–макс',v:p&&p.valid?pair(v.vmin,v.vmax,'В'):'—'},
        {t:'Температура реле мин–макс',v:p&&p.valid?pair(v.tmin,v.tmax,'°C'):'—'}
      ];
    }
    root.innerHTML=cards.map(function(c){return '<article class="history-metric'+(c.bound?' partial':'')+'"><div class="head"><strong>'+c.t+'</strong></div><div class="history-value">'+c.v+'</div></article>'}).join('');
    if(partial&&mode!=='hours')root.innerHTML+='<p class="hist-note" role="note">неполные данные ('+why+'): суммы и пик — минимальные (≥), напряжение и температура — только по доступным суткам.</p>';
    if(selectionOnly){paintSelection();return}
    var hadFocus=plot.contains(document.activeElement);
    var mx=specs.map(function(m){
      var values=points.filter(function(x){return x.valid&&isNum(x.values[m[0]])}).map(function(x){return Math.abs(+x.values[m[0]])});
      return Math.max.apply(null,values)||1;
    });
    var left=40,slot=920/Math.max(1,points.length),baseY=218,s='';
    points.forEach(function(item,index){
      var x=left+index*slot,w=Math.min(7,(slot-6)/specs.length),sel=selected===index;
      s+='<g data-index="'+index+'" role="option" aria-selected="'+(sel?'true':'false')+'" tabindex="'+(sel||(selected<0&&index===0)?'0':'-1')+'" aria-label="'+escAttr(describeItem(item))+'">';
      s+='<rect data-selection="'+index+'" x="'+x+'" y="12" width="'+slot+'" height="208" fill="var(--ink)" opacity="'+(sel?'.12':'0')+'"/>';
      specs.forEach(function(m,i){
        if(hidden[m[0]]||!item.valid||!isNum(item.values[m[0]]))return;
        var height=Math.abs(+item.values[m[0]])/mx[i]*188;
        s+='<rect x="'+(x+slot/2+(i-specs.length/2)*w)+'" y="'+(baseY-height)+'" width="'+Math.max(2,w-1)+'" height="'+Math.max(1,height)+'" fill="'+colors[m[0]]+'"><title>'+m[1]+': '+num(item.values[m[0]],m[3])+' '+m[2]+'</title></rect>';
      });
      var vals=item.valid?specs.filter(function(m){return isNum(item.values[m[0]])}).map(function(m){return m[1]+' '+num(item.values[m[0]],m[3])+' '+m[2]}):[];
      var tip=item.label+': '+(item.valid?(vals.length?vals.join(' · '):'показатели пусты')+(item.partial?' · неполные данные':''):'нет измерений');
      s+='<rect x="'+x+'" y="10" width="'+slot+'" height="210" fill="transparent"><title>'+tip+'</title></rect>';
      if(points.length<=7||index%3===0)s+='<text x="'+(x+slot/2)+'" y="244" text-anchor="middle" fill="var(--mut)" font-size="12">'+item.label+'</text>';
      s+='</g>';
    });
    plot.innerHTML=s;
    paintSelection();
    /* После перерисовки возвращаем фокус выбранной ячейке, чтобы клавиатура
       продолжала работать, а выбор не «уезжал» на первый столбик. */
    if(hadFocus&&selected>=0){var fg=plot.querySelector('[data-index="'+selected+'"]');if(fg&&fg.focus){try{fg.focus()}catch(e){}}}
  }

  function progressFreeDayTotals(){try{return dayTotalsText()}catch(e){return ''}}

  function refresh(){
    if(!everLoaded)return;
    load(date||G('historyDate').value||S.date||S.today,mode,rangeMode?rangeFrom:undefined);
  }
  function ensureLoaded(){
    if(everLoaded)return false;
    var d=date||G('historyDate').value||S.date||S.today||BOOT_DATE||'';
    if(!isDate(d)){
      status('жду список дней от платы…');
      if(++loadTries<=120)setTimeout(ensureLoaded,500);   // до ~60 с: первый /data после OTA бывает медленным
      return false;
    }
    load(d,'hours');
    return true;
  }

  window.historyRefresh=refresh;
  window.historyEnsureLoaded=ensureLoaded;
  window.historyLoadRange=function(from,to){
    if(!isDate(from)||!isDate(to))return;
    if(from>to){var x=from;from=to;to=x}
    G('historyDate').value=to;
    load(to,'range',from);
  };
  window.historyAccept=function(j){
    if(!j||!j.date)return;
    if(!everLoaded||mode!=='hours'||rangeMode)return;
    date=j.date;G('historyDate').value=j.date;loadedFrom='';loadedTo=j.date;
    points=pointsForHours(j.date,j);   /* выбор (даже пустого часа) сохраняем */
    render();
  };
  window.openMetricHistory=function(key,d){
    load(d||S.date||S.today,'hours');
    host.scrollIntoView({block:'start'});
  };

  function groupIndex(ev){
    var t=ev.target;if(!t||!t.closest)return -1;
    var g=t.closest('[data-index]');return g?+g.dataset.index:-1;
  }
  function selectAt(i,focus){
    if(i<0||i>=points.length)return;
    selected=i;render(true);
    if(focus){var el=plot.querySelector('[data-index="'+i+'"]');if(el&&el.focus){try{el.focus()}catch(e){}}}
  }
  /* Любая ячейка (в том числе пустая) выбирается кликом и с клавиатуры:
     ←/→ (и ↑/↓) ходят по 24 часам, Home/End — крайние, Enter/пробел — выбор. */
  function select(ev){
    if(loading)return;
    var gi=groupIndex(ev),base=gi>=0?gi:(selected>=0?selected:0),key=ev.type==='keydown'?ev.key:'';
    if(ev.type==='keydown'){
      if(key==='ArrowLeft'||key==='ArrowUp'){ev.preventDefault();selectAt(base-1,true);return}
      if(key==='ArrowRight'||key==='ArrowDown'){ev.preventDefault();selectAt(base+1,true);return}
      if(key==='Home'){ev.preventDefault();selectAt(0,true);return}
      if(key==='End'){ev.preventDefault();selectAt(points.length-1,true);return}
      if(key!=='Enter'&&key!==' '&&key!=='Spacebar')return;
      if(gi<0)return;
      ev.preventDefault();selectAt(gi,true);return;
    }
    if(gi<0)return;
    ev.preventDefault();selectAt(gi,false);
  }
  plot.addEventListener('click',select);
  plot.addEventListener('keydown',select);
  plot.addEventListener('dblclick',function(ev){
    if(loading)return;
    var gi=groupIndex(ev);if(gi<0)return;
    var p=points[gi];if(!p)return;
    if(mode==='weeks')load(p.end,'days');else load(p.date,'hours');
  });
  legend.addEventListener('click',function(ev){
    var b=ev.target.closest('[data-series]');if(!b)return;
    var key=b.dataset.series;hidden[key]=!hidden[key];render();
    legend.querySelector('[data-series="'+key+'"]').focus();
  });
  /* Ручной вид: Часы / Дни / Недели — для текущего периода или выбранной даты */
  G('historyModes').addEventListener('click',function(ev){
    var b=ev.target.closest('[data-mode]');if(!b)return;
    var want=b.dataset.mode;
    var d=G('historyDate').value||date||S.date||S.today;
    if(want===mode)return;
    if(rangeMode){
      if(want==='hours')load(d,'hours');else load(d,want,rangeFrom);
    }else{
      load(d,want);
    }
  });
  G('historyDrill').onclick=function(){
    var p=points[selected];if(!p)return;
    if(mode==='weeks')load(p.end,'days');else load(p.date,'hours');
  };
  G('historyReload').onclick=function(){
    load(G('historyDate').value||date,mode,rangeMode?rangeFrom:'');
  };
  /* Блок открыт всегда — грузим сразу, без ожидания клика по summary */
  host.addEventListener('toggle',function(){if(host.open)ensureLoaded()});
  if(host.open)ensureLoaded();
  [['dVbig','v'],['dTbig','t'],['mp','p'],['tkwh','kwh'],['pe','kwh']].forEach(function(pair){
    var el=G(pair[0]);if(!el)return;
    var card=el.closest('.card,.t');card.style.cursor='pointer';card.tabIndex=0;card.setAttribute('role','button');
    card.addEventListener('click',function(){window.openMetricHistory(pair[1])});
    card.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();window.openMetricHistory(pair[1])}});
  });
  var initial=S.date||S.today||BOOT_DATE||'';
  if(isDate(initial)){date=initial;G('historyDate').value=initial}
  hint('сегодня · по часам');
})();

/* ---------- P1.2: источник и свежесть показателей, качество данных ----------
   /measure отдаёт по КАЖДОМУ показателю отдельно локальное реле и облако:
   {value,unit,source:'local|cloud|none',ageSec,valid,local:{value,ageSec},
    cloud:{...},ttlSec}. /storage добавляет здоровье записи на карту.
   Здесь только отображение; опрос идёт из существующего цикла poll
   (window.domStatusTick вызывает app.js) — своего таймера нет.
   /measure — не чаще 5 с, /storage — раз в минуту (и 20 с после ошибки).
   При 503/таймауте значения не обнуляются: остаются последние известные,
   а в строке состояния честно пишется «нет ответа /measure». */
(function(){
  'use strict';
  var MEAS={data:null,at:0,tryAt:0,tried:false,err:0};
  var SD={data:null,at:0,tryAt:0,failAt:0,tried:false,err:0};
  var DLAST=null,DAT=0,DOk=false;
  var MEAS_EVERY=5000,MEAS_STALE=15000,SD_EVERY=30000,SD_RETRY=15000;
  /* [префикс, ключ /measure, подпись, единица, знаков после запятой] */
  var METRICS=[['V','voltage','Напряжение','В',0],['P','power','Мощность','Вт',0],
               ['I','current','Ток','А',2],['T','temperature','Темп. реле','°C',1]];
  /* [ключ /measure, id «получено…», id большого числа, знаков, единица] */
  var CARDS=[['voltage','dVnow','dVbig',0,'В'],['current','dAnow','dAbig',2,'А'],
             ['temperature','dTnow','dTbig',1,'°C']];
  var WHY={0:'ок',1:'нет Wi-Fi',2:'нет интернета',3:'нет токена облака',
           4:'ошибка запроса к облаку',5:'облако ответило отказом',
           6:'в ответе облака нет напряжения'};
  function now(){return Date.now()}
  function ageTxt(sec){sec=Math.max(0,Math.round(+sec||0));return sec<90?sec+' с':Math.round(sec/60)+' мин'}
  function drift(){return MEAS.data?Math.max(0,(now()-MEAS.at)/1000):0}
  function candAge(m){var a=[];
    if(m.local&&m.local.value!=null)a.push(+m.local.ageSec||0);
    if(m.cloud&&m.cloud.value!=null)a.push(+m.cloud.ageSec||0);
    return a.length?Math.min.apply(null,a):null}
  /* возраст последнего известного значения, даже если оно уже вне TTL */
  function effAge(m){if(!m)return null;var a=m.source==='none'?candAge(m):(+m.ageSec||0);return a==null?null:a+drift()}
  /* источник мог истечь, пока не было ответа /measure — проверяем TTL сами */
  function effSrc(m){if(!m||m.source==='none')return 'none';
    var a=(+m.ageSec||0)+drift(),ttl=+m.ttlSec||0;return ttl>0&&a>ttl?'none':m.source}
  function numOr(v,n){return v==null||isNaN(+v)?'—':num(+v,n)}
  function gb(b){if(b==null||isNaN(+b))return '—';var g=+b/1073741824;
    return g>=1?g.toFixed(1).replace('.',',')+' ГБ':Math.round(+b/1048576)+' МБ'}
  /* freeBytes может быть неверным (старые сборки отдавали дубль ключа и
     браузер брал 1,2 ГБ вместо 117). Если total и used известны — сверяем. */
  function freeBytesSafe(st){
    if(!st)return null;
    var total=+st.totalBytes,used=+st.usedBytes,free=+st.freeBytes;
    var calc=(total>0&&used>=0)?(total-used):null;
    if(calc!=null&&(st.freeBytes==null||isNaN(free)||Math.abs(free-calc)>1048576))return calc;
    return isNaN(free)?calc:free;
  }
  function setRow(id,t,cls){var el=G(id);if(!el)return;el.textContent=t;el.className=cls||''}

  /* карточки «Сейчас»: источник и возраст, при source=none — не ноль */
  function renderCards(){
    var md=MEAS.data;if(!md)return;
    for(var i=0;i<CARDS.length;i++){
      var c=CARDS[i],m=md[c[0]];if(!m)continue;
      var big=G(c[2]),el=G(c[1]);if(!big||!el)continue;
      var s=effSrc(m),a=effAge(m),label;
      if(s==='local'||s==='cloud'){
        big.textContent=numOr(m.value,c[3]);big.className='';
        label=(s==='local'?'локальное реле':'облако')+' · '+ageTxt(a);
      }else{
        big.textContent='—';big.className='none';
        label=a==null?'нет измерений':'нет свежих данных · устарело '+ageTxt(a);
      }
      el.textContent=label;
      var lv=m.local&&m.local.value!=null?numOr(m.local.value,c[3])+' '+c[4]+' ('+ageTxt((+m.local.ageSec||0)+drift())+')':'нет';
      var cv=m.cloud&&m.cloud.value!=null?numOr(m.cloud.value,c[3])+' '+c[4]+' ('+ageTxt((+m.cloud.ageSec||0)+drift())+')':'нет';
      big.title='локально: '+lv+'; облако: '+cv;
      el.title=big.title;
    }
  }
  /* компактная строка: группируем показатели по источнику */
  function summary(){
    var md=MEAS.data;if(!md)return '';
    var groups={},order=[];
    for(var i=0;i<METRICS.length;i++){
      var m=md[METRICS[i][1]];if(!m)continue;
      var s=effSrc(m);
      if(!groups[s]){groups[s]={names:[],age:-1};order.push(s)}
      groups[s].names.push(METRICS[i][0]);
      var a=effAge(m);if(a!=null&&a>=0&&a>groups[s].age)groups[s].age=a;
    }
    var out=[];
    for(var j=0;j<order.length;j++){
      var k=order[j],g=groups[k],pre=g.names.join('/')+' — ';
      if(k==='none')out.push(pre+(g.age<0?'нет измерений':'устарело · '+ageTxt(g.age)));
      else out.push(pre+(k==='local'?'локальное реле':'облако')+' · '+ageTxt(Math.max(0,g.age)));
    }
    return out.join(' · ');
  }
  /* LAN и WAN раздельно: плата отвечает сама, облако — через интернет.
     Плата может ответить и только на /measure (если /data занят) — это честно. */
  function measFresh(){return !!MEAS.data&&now()-MEAS.at<MEAS_STALE}
  function lanAlive(){return (DOk&&DAT&&now()-DAT<MEAS_STALE)||measFresh()}
  function boardRow(){
    // ВАЖНО: подпись строки уже «Плата отвечает», поэтому значение не должно
    // начинаться с «отвечает» — иначе получается «Плата отвечает отвечает …».
    if(DOk&&DAT&&now()-DAT<MEAS_STALE)return {t:'да · /data '+ageTxt((now()-DAT)/1000)+' назад',cls:''};
    if(measFresh())return {t:'только /measure'+(DAT?' · /data '+ageTxt((now()-DAT)/1000)+' назад':''),cls:'bad'};
    if(DAT)return {t:'нет · последний ответ '+ageTxt((now()-DAT)/1000)+' назад',cls:'err'};
    return {t:'ожидание',cls:''};
  }
  function linkTxt(){
    var s='плата: '+boardRow().t;
    var d=DLAST;
    if(!lanAlive())return s+' · облако: нет свежих данных платы';
    if(!d)return s+' · облако: нет данных';
    var ca=(+d.cAge||0)+Math.max(0,(now()-DAT)/1000),wan;
    if(!d.net)wan='нет интернета';
    else if(+d.cFail>0)wan='ошибка · '+(WHY[+d.cWhy]||('код '+d.cWhy));
    else if(+d.cOk>0)wan='отвечает · '+ageTxt(ca);
    else wan='нет успешных ответов';
    return s+' · облако: '+wan;
  }
  function renderPanel(){
    var t=now(),md=MEAS.data,d=DLAST,lan=G('sbLan'),br=boardRow();
    if(lan)lan.textContent=lanAlive()?('LAN · плата отвечает'+(!DOk||t-DAT>=MEAS_STALE?' по /measure':' · /data '+ageTxt((t-DAT)/1000)+' назад'))
      :('LAN · плата не отвечает'+(DAT?' · последние данные '+ageTxt((t-DAT)/1000)+' назад':''));
    setRow('sbBoard',br.t,br.cls);
    setRow('sbRssi',d&&d.rssi!=null?d.rssi+' дБм':'—',d&&d.rssi!=null&&+d.rssi<=-75?'bad':'');
    setRow('sbRelay',d?(d.r?'ВКЛ':'ВЫКЛ'):'—','');
    setRow('sbClock',md?(md.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно (/measure)',md&&!md.clockOk?'bad':'');
    /* SD: /storage отдаёт ok, freeBytes, writeFail, writeFailRun, lastWriteAgeSec, archiveError */
    if(!SD.tried){setRow('sbSd','ожидание /storage','');setRow('sbSdFree','—','');setRow('sbSdWrite','—','')}
    else if(SD.data){
      var as=(t-SD.at)/1000,wf=+SD.data.writeFail||0,wr=+SD.data.writeFailRun||0,
          failed=SD.failAt>SD.at;
      setRow('sbSd',failed?('нет ответа /storage · данные '+ageTxt(as)+' назад')
        :('в порядке'+(as>120?' · проверено '+ageTxt(as)+' назад':'')),failed?'err':'');
      var free=freeBytesSafe(SD.data);
      setRow('sbSdFree',gb(free)+' свободно',free===null?'err':'');
      var ws=wf===0?'ошибок нет':'ошибок '+wf+' всего'+(wr?' · подряд '+wr:'');
      if(+SD.data.lastWriteAgeSec>0)ws+=' · последняя запись '+ageTxt(+SD.data.lastWriteAgeSec+as)+' назад';
      if(SD.data.archiveError)ws+=' · ошибка архива';
      if(SD.data.lowSpace)ws+=' · мало места';
      setRow('sbSdWrite',ws,(wf>0||SD.data.archiveError||SD.data.lowSpace)?'bad':'');
    }else{
      setRow('sbSd',SD.err===503?'карта не готова (503)':'нет ответа /storage','err');
      setRow('sbSdFree','—','');setRow('sbSdWrite','—','');
    }
    if(!d){setRow('sbNet','—','');setRow('sbCOk','—','');setRow('sbCFail','—','');setRow('sbCAge','—','');setRow('sbCWhy','—','')}
    else{
      setRow('sbNet',d.net?'проверка прошла':'проверка не прошла',d.net?'':'bad');
      setRow('sbCOk',(+d.cOk||0)+' из '+(+d.cTry||0)+' попыток','');
      setRow('sbCFail',(+d.cFail||0)+' подряд',(+d.cFail>0)?'bad':'');
      setRow('sbCAge','попытка '+ageTxt((+d.cAge||0)+Math.max(0,(t-DAT)/1000))+' назад','');
      // cHold: 1 — облако на паузе (напряжение свежее от локального реле),
      // 8 — кончился сетевой бюджет. Без этого панель показывала старую
      // причину «нет токена» и выглядело, будто токен пропал навсегда.
      var w=+d.cWhy||0,why=(WHY[w]||('код '+w)),whyCls=(w?'bad':'');
      if(+d.cHold===1){why='пауза: напряжение свежее (локальное реле)';whyCls=''}
      else if(+d.cHold===8){why='пауза: сетевой бюджет на этот проход';whyCls='bad'}
      if(+d.tok===0&&!w&&+d.cHold!==1&&+d.cHold!==8)why='токена ещё нет';
      setRow('sbCWhy',why,whyCls);
    }
    var sm=G('sbMeasure');
    if(sm){
      if(md)sm.textContent=(t-MEAS.at<MEAS_STALE?'ответ ':'нет ответа · последний ')+ageTxt((t-MEAS.at)/1000)+' назад';
      else sm.textContent=MEAS.tried?'нет ответа':'ожидание';
    }
  }
  function candCell(c,unit,dec){
    return !c||c.value==null?'—':numOr(c.value,dec)+' '+unit+' · '+ageTxt((+c.ageSec||0)+drift());
  }
  function renderFresh(){
    var tb=G('sbFresh');if(!tb)return;
    var md=MEAS.data,htm='';
    if(!md){
      var tries=MEAS.tried?(now()-MEAS.tryAt)/1000:0;
      htm='<tr><td colspan="7" class="mut">'+(MEAS.tried?'нет ответа /measure · '+(tries<5?'последняя попытка только что':'попытка '+ageTxt(tries)+' назад'):'ожидание ответа /measure')+'</td></tr>';
    }else{
      for(var i=0;i<METRICS.length;i++){
        var it=METRICS[i],m=md[it[1]];if(!m)continue;
        var s=effSrc(m),a=effAge(m),age;
        if(s==='none')age=a==null?'нет измерений':'устарело · '+ageTxt(a);else age=ageTxt(a);
        var st=s==='local'?'локальное реле':s==='cloud'?'облако':'не определён';
        htm+='<tr><td>'+it[2]+'</td><td'+(s==='none'?' class="src-bad"':'')+'>'+st+
             '</td><td>'+age+'</td><td>'+(s==='none'?'—':numOr(m.value,it[4])+' '+it[3])+
             '</td><td>'+candCell(m.local,it[3],it[4])+'</td><td>'+candCell(m.cloud,it[3],it[4])+
             '</td><td>'+(m.ttlSec?m.ttlSec+' с':'—')+'</td></tr>';
      }
    }
    tb.innerHTML=htm;
  }
  function renderAll(){
    var live=G('statLive'),link=G('statLink');
    if(live){
      var s=summary();
      live.textContent='источник и свежесть: '+(s||(MEAS.tried?'нет ответа /measure':'ожидание /measure'))+
        (!MEAS.data||now()-MEAS.at<MEAS_STALE?'':' · нет ответа /measure');
    }
    if(link)link.textContent=linkTxt();
    renderCards();renderPanel();renderFresh();
  }
  function fetchMeasure(){
    var t=now();if(t-MEAS.tryAt<MEAS_EVERY)return;
    MEAS.tryAt=t;MEAS.tried=true;
    fetch('/measure',{cache:'no-store',timeoutMs:4000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return r.json();
    }).then(function(j){
      if(!j||typeof j!=='object'||!j.voltage)throw Error('bad /measure');
      MEAS.data=j;MEAS.at=now();MEAS.err=0;renderAll();
    }).catch(function(e){
      MEAS.err=(e&&e.code)||0;renderAll();   /* значения не трогаем — «не ноль» */
    });
  }
  function fetchStorage(){
    var t=now(),wait=(SD.data&&SD.failAt<=SD.at)?SD_EVERY:SD_RETRY;
    if(t-SD.tryAt<wait)return;
    SD.tryAt=t;SD.tried=true;
    fetch('/storage',{cache:'no-store',timeoutMs:5000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return r.json();
    }).then(function(j){
      SD.data=j&&j.ok?j:null;SD.at=now();SD.err=0;renderAll();
    }).catch(function(e){
      SD.err=(e&&e.code)||0;SD.failAt=now();renderAll();
    });
  }
  /* вызывается из цикла poll в app.js: d — свежий /data или null при ошибке */
  window.domP12={snapshot:function(){return {meas:MEAS.data,measAt:MEAS.at,measTried:MEAS.tried,measErr:MEAS.err,sd:SD.data,sdAt:SD.at,sdTried:SD.tried,sdErr:SD.err,data:DLAST,dataAt:DAT,dataOk:DOk}},
    age:ageTxt,gb:gb,src:effSrc,ageOf:effAge};
  window.domStatusTick=function(d){
    if(d){DLAST=d;DAT=now();DOk=true}else{DOk=false}
    // Первый успешный /data даёт точную дату: если авто-загрузка истории
    // не успела (блок открыт), грузим сейчас — без клика по summary.
    // ensureLoaded — из первого IIFE, наружу отдан как window.historyEnsureLoaded.
    if(d&&window.historyEnsureLoaded&&G('historyWorkspace')&&G('historyWorkspace').open)window.historyEnsureLoaded();
    fetchMeasure();fetchStorage();renderAll();
  };
})();
/* ---------- P1.3/P1.5: события, диагностика, обезличенный отчёт ----------
   Источники: /vlog, /boots, /clog, /gapfill/status, /data; V/P/T/I и LAN/WAN —
   из панели P1.2 (window.domP12). Опрос из цикла poll: /vlog,/boots,/clog —
   не чаще 10 с, /gapfill/status — 20 с. Отчёт только в <pre> и в буфер по
   кнопке, без IP, MAC, SSID, токенов, device id и значений измерений. */
(function(){
  'use strict';
  var EV_EVERY=10000,GAP_EVERY=20000,OBS=Date.now();
  var NEXT={vlog:0,boots:0,clog:0,gap:0};
  var EV={vlog:null,vlogErr:0,boots:null,bootsErr:0,clog:null,clogErr:0,gap:null,gapErr:0};
  var LASTD=null;
  function mp(s){var o={},a=s.split(';');for(var i=0;i<a.length;i++){var p=a[i].split('=');o[p[0]]=p[1]}return o}
  var STAGE=mp('ok=успешно;clock=нет времени;connect=соединение;print=отправка запроса;hdr=чтение заголовков;http=ответ не HTTP;short=тело короче;empty=пустое тело;budget=бюджет сети;dns=DNS');
  var WHY=mp('0=нет ошибки;1=нет Wi-Fi;2=нет интернета;3=нет токена облака;4=запрос к облаку не выполнен;5=облако ответило отказом;6=в ответе нет напряжения');
  var RR=mp('POWERON=включение питания;EXT_PIN=внешний сигнал;SW_RESET=программный сброс;PANIC=паника;INT_WDT=сторожевой таймер;TASK_WDT=сторожевой таймер задачи;WDT=сторожевой таймер;DEEPSLEEP=глубокий сон;BROWNOUT=просадка питания;SDIO=SDIO;OTA=обновление по Wi-Fi;UNKNOWN=неизвестная причина');
  var GAPST=mp('idle=ожидание;check_rows=проверка строк;prepare=подготовка;check_space=проверка места;backup_rename=резервная копия;rewrite=перезапись файла;check_insert_count=сверка вставок;activate=активация;verify=проверка размера;complete=завершено');
  var GAPCODE=mp('none=нет;running=выполняется;no_cloud_data=в облаке нет записей;sd_unavailable=карта недоступна;recover_old_failed=не вернулся .old;cleanup_old_failed=не убрать .old;cleanup_tmp_failed=не убрать .tmp;target_open_failed=не открылся файл;target_too_small=файл слишком мал;insufficient_space=не хватает места;backup_cleanup_failed=не удалилась .bak;backup_rename_failed=не сделать .bak;rewrite_failed=ошибка перезаписи;insert_count_mismatch=вставки не совпали;activate_failed=не активировать;final_size_mismatch=размер не совпал;success=успешно');
  var MET=['voltage','power','current','temperature'],MKEY=['V','P','I','T'];
  function n(){return Date.now()}
  function p2(v){return (v<10?'0':'')+v}
  function clock(ts){var d=new Date(ts);return p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds())}
  function dt(ts){var d=new Date(ts),t=new Date();
    return d.toDateString()===t.toDateString()?clock(ts):p2(d.getDate())+'.'+p2(d.getMonth()+1)+' '+p2(d.getHours())+':'+p2(d.getMinutes())}
  function plur(k,a,b,c){k=Math.abs(+k||0)%100;var d=k%10;return k>10&&k<20?c:(d>1&&d<5?b:(d===1?a:c))}
  function dur(s){s=Math.max(0,Math.round(+s||0));if(s<90)return s+' с';if(s<5400)return Math.round(s/60)+' мин';
    var h=Math.floor(s/3600),m=Math.round((s%3600)/60);return h+' ч'+(m?' '+m+' мин':'')}
  /* /boots: первая строка — текущая загрузка, далее «дата причина rrn up(прошлая сессия)» */
  function bootsRows(){
    if(!EV.boots)return [];
    var o=[],ls=String(EV.boots).replace(/\r/g,'').split('\n'),i,m;
    for(i=0;i<ls.length;i++){
      m=/^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) rr=([A-Z_a-z0-9]+)\((\d+)\) rrn=(\d+) up=(\d+)/.exec(ls[i].trim());
      if(m)o.push({ts:new Date(+m[1],+m[2]-1,+m[3],+m[4],+m[5],+m[6]).getTime(),name:m[7],up:+m[10]});
    }
    return o;
  }
  /* /clog: строки «uptime_ms stage why http len» + счётчики */
  function clogRows(){
    var o=[],ls=String(EV.clog||'').replace(/\r/g,'').split('\n'),i,m;
    for(i=1;i<ls.length;i++){
      m=/^(\d+)\s+(\S+)\s+(\d+)\s+(-?\d+)\s+(-?\d+)\s*$/.exec(ls[i].trim());
      if(m)o.push({at:+m[1],stage:m[2],why:+m[3],http:+m[4],len:+m[5]});
    }
    return o;
  }
  function clogCounters(){
    var m=/cTry=(\d+) cOk=(\d+) cFail=(\d+) cAge=(\d+)/.exec(String(EV.clog||''));
    return m?{try:+m[1],ok:+m[2],fail:+m[3]}:null;
  }
  function clogLine(x,d){
    var w=d&&+d.up?'за '+dur(Math.max(0,(+d.up||0)-x.at/1000))+' до снимка':'uptime '+Math.round(x.at/1000)+' с';
    return w+' · этап: '+(STAGE[x.stage]||x.stage)+' · причина: '+(WHY[x.why]||('код '+x.why))+' · HTTP '+x.http+' · '+x.len+' Б';
  }
  function gapText(){
    if(!EV.gap)return null;
    var g=EV.gap;
    return {stage:GAPST[g.commitStage]||(g.commitStage||'—'),code:GAPCODE[g.commitCode]||(g.commitCode||'—'),
            noData:g.noData?'да':'нет',msg:g.msg||'',rows:+g.rows||0,active:+g.active||0};
  }
  function measText(key){
    var P=window.domP12;
    if(!P||!P.snapshot)return {t:'нет данных /measure',age:'',cls:''};
    var s=P.snapshot(),m=s.meas&&s.meas[key];
    if(!m)return {t:'нет данных /measure',age:'',cls:''};
    var so=P.src(m),a=P.ageOf(m);
    if(so==='none')return {t:a==null?'нет измерений':'нет свежих данных',age:a==null?'':P.age(a),cls:'bad'};
    return {t:so==='local'?'локальное реле':'облако',age:P.age(a),cls:''};
  }
  function sdText(){
    var P=window.domP12;
    if(!P||!P.snapshot)return null;
    var s=P.snapshot();if(!s.sd)return null;
    var d=s.sd,as=Math.max(0,(n()-s.sdAt)/1000);
    return {fail:+d.writeFail||0,run:+d.writeFailRun||0,age:(+d.lastWriteAgeSec||0)+as,
            arch:!!d.archiveError,low:!!d.lowSpace,free:d.freeBytes};
  }
  /* «События»: только реальные сообщения платы; пусто — «событий нет» */
  function renderEvents(){
    var d=LASTD,rows=[],i,bl=bootsRows(),e;
    if(EV.vlog&&EV.vlog.items){e=EV.vlog.items;
      for(i=0;i<e.length&&i<20;i++)rows.push({ts:(+e[i].t||0)*1000,tag:'напряж.',cls:e[i].k?'swl':'sag',
        text:(e[i].k?'всплеск':'провал')+' '+num(e[i].v)+' В · '+num(e[i].s)+' с'})}
    for(i=0;i<bl.length&&i<20;i++)rows.push({ts:bl[i].ts,tag:'ребут',cls:'reb',
      text:'перезагрузка · '+(RR[bl[i].name]||bl[i].name)+' · прошлая сессия '+dur(bl[i].up)});
    if(d&&+d.cWhy>0)rows.push({ts:n()-(+d.cAge||0)*1000,tag:'облако',cls:'bad',
      text:'облако: '+(WHY[+d.cWhy]||('код '+d.cWhy))+(+d.cFail>1?' · подряд '+d.cFail:'')});
    rows.sort(function(a,b){return b.ts-a.ts});if(rows.length>20)rows.length=20;
    var list=G('evHistList');
    if(list){
      if(!rows.length)list.innerHTML='<div class="ev"><span>'+((!EV.vlog&&!EV.boots&&!d)?'ожидание /vlog, /boots, /data':(EV.vlog?'событий нет':'/vlog: нет ответа'))+'</span></div>';
      else{var h='';for(i=0;i<rows.length;i++)h+='<div class="ev"><span class="t">'+dt(rows[i].ts)+'</span><span class="ev-tag '+rows[i].cls+'">'+rows[i].tag+'</span><span>'+rows[i].text+'</span></div>';list.innerHTML=h}
    }
    var st=G('evState');
    if(st)st.innerHTML=d?('<span><b>старт текущей сессии:</b> '+dt(n()-(+d.up||0)*1000)+' · <i>по uptime /data</i></span><span><b>последняя ошибка облака:</b> '+(+d.cWhy>0?((WHY[+d.cWhy]||('код '+d.cWhy))+(+d.cFail?' · подряд '+d.cFail:'')):'нет')+'</span>'):'<span>ожидание /data</span>';
    var nv=(EV.vlog&&+EV.vlog.n)||0,er=[];
    if(EV.vlogErr)er.push('/vlog: нет ответа');
    if(EV.bootsErr)er.push('/boots: нет ответа');
    txt('evLive','события: '+(rows.length?rows.length+' '+plur(rows.length,'запись','записи','записей'):'пока пусто')+' · /vlog n='+nv+' · загрузок в журнале '+bl.length+(er.length?' · '+er.join(' · '):''));
    txt('evObs','данные наблюдаются с '+dt(OBS));
    txt('evFoot',(er.length?'Не получено: '+er.join(', ')+'. ':'')+'Пока страница закрыта, события напряжения не пишутся; перезагрузки — из /boots.log на карте.');
  }
  /* «Диагностика»: версия/uptime, V/P/T/I, LAN/WAN из /data, SD, gapfill, /clog */
  function renderDiag(){
    var d=LASTD,P=window.domP12,snap=P&&P.snapshot?P.snapshot():null,i,sd,g;
    txt('dgVer',d&&d.fw?d.fw:'—');txt('dgBuild',d&&d.build?d.build:'—');txt('dgUp',d?dur(+d.up||0):'—');
    txt('dgClock',snap&&snap.meas?(snap.meas.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно (/measure)');
    if(!d){txt('dgLan','ожидание /data');txt('dgWan','ожидание /data')}
    else{
      txt('dgLan','плата '+(snap&&snap.dataOk?'отвечает':'не отвечает')+' · Wi-Fi '+(d.rssi!=null?'есть':'нет данных')+' · реле '+(d.r?'ВКЛ':'ВЫКЛ'));
      var cw=+d.cWhy||0,cadd=snap?Math.max(0,(n()-snap.dataAt)/1000):0;
      txt('dgWan','интернет '+(d.net?'есть':'нет')+' · облако '+(+d.cOk||0)+' из '+(+d.cTry||0)+' · подряд ошибок '+(+d.cFail||0)+' · попытка '+dur((+d.cAge||0)+cadd)+' назад'+(cw?' · причина: '+(WHY[cw]||('код '+cw)):''));
    }
    for(i=0;i<MET.length;i++){var r=measText(MET[i]);var el=G('dg'+MKEY[i]);if(el){el.textContent=r.t+(r.age?' · '+r.age:'');el.className=r.cls||''}}
    sd=sdText();
    if(!snap||!snap.sdTried){txt('dgSdFail','ожидание /storage');txt('dgSdAge','—');txt('dgSdArch','—');txt('dgSdFree','—')}
    else if(!sd){txt('dgSdFail',snap.sdErr===503?'карта не готова (503)':'нет ответа /storage');txt('dgSdAge','—');txt('dgSdArch','—');txt('dgSdFree','—')}
    else{txt('dgSdFail','ошибок '+sd.fail+' всего'+(sd.run?' · подряд '+sd.run:''));
      txt('dgSdAge',sd.age>0?'последняя запись '+dur(sd.age)+' назад':'нет данных о записи');
      txt('dgSdArch',sd.arch?'ошибка архива':(sd.low?'мало места':'нет ошибки'));
      txt('dgSdFree',window.domP12.gb(sd.free)+' свободно')}
    g=gapText();
    if(!g){txt('dgGapStage',EV.gapErr?'нет ответа /gapfill/status':'ожидание /gapfill/status');txt('dgGapCode','—');txt('dgGapNo','—');txt('dgGapMsg','—')}
    else{txt('dgGapStage',g.stage+(g.active?' · идёт':''));txt('dgGapCode',g.code);txt('dgGapNo',g.noData+(g.rows?' · строк '+g.rows:''));txt('dgGapMsg',g.msg||'—')}
    var cl=G('dgClog');
    if(cl){
      if(EV.clogErr)cl.textContent='нет ответа /clog';
      else if(!EV.clog)cl.textContent='ожидание /clog';
      else{var rr=clogRows(),cc=clogCounters(),out=[];
        for(i=rr.length-1;i>=0&&out.length<3;i--)out.push(clogLine(rr[i],d));
        if(cc)out.push('всего: cTry='+cc.try+' · cOk='+cc.ok+' · cFail='+cc.fail);
        cl.textContent=out.length?out.join('\n'):'строк нет'}
    }
    txt('dgLive','диагностика: '+(d?'CYD '+(d.fw||'?')+' · uptime '+dur(+d.up||0):'ожидание /data'));
  }
  function renderAll(){renderEvents();renderDiag()}
  /* опрос с ограничением частоты — только из общего цикла poll */
  function load(key,url,isJson){
    var t=n();if(t<NEXT[key])return;NEXT[key]=t+(key==='gap'?GAP_EVERY:EV_EVERY);
    fetch(url,{cache:'no-store',timeoutMs:5000}).then(function(r){
      if(!r.ok)return r.text().then(function(){var e=Error('HTTP '+r.status);e.code=r.status;throw e});
      return isJson?r.json():r.text();
    }).then(function(j){EV[key]=j;EV[key+'Err']=0;renderAll()})
      .catch(function(e){EV[key+'Err']=(e&&e.code)||1;renderAll()});
  }
  function fetchAll(){NEXT.vlog=NEXT.boots=NEXT.clog=NEXT.gap=0;load('vlog','/vlog',true);load('boots','/boots',false);load('clog','/clog',false);load('gap','/gapfill/status',true)}
  function tick(d){
    if(d)LASTD=d;
    load('vlog','/vlog',true);load('boots','/boots',false);load('clog','/clog',false);load('gap','/gapfill/status',true);
    renderAll();
  }
  /* обезличенный отчёт: состояния, коды, возраст, счётчики — без значений */
  function reportLines(){
    var d=LASTD,P=window.domP12,snap=P&&P.snapshot?P.snapshot():null,i,L=[];
    L.push('Дом — энергия · обезличенный отчёт');
    L.push('Сформирован: '+dt(n())+' (время браузера)');
    L.push('Версия: '+(d&&d.fw?d.fw:'—')+' · сборка '+(d&&d.build?d.build:'—'));
    L.push('Uptime платы: '+(d?dur(+d.up||0):'—'));
    L.push('Время платы: '+(snap&&snap.meas?(snap.meas.clockOk?'синхронизировано':'не синхронизировано'):'неизвестно'));
    if(d){
      L.push('Текущая сессия: '+dt(n()-(+d.up||0)*1000)+' (по uptime /data)');
      L.push('LAN: плата отвечает · Wi-Fi '+(d.rssi!=null?'есть':'нет данных')+' · реле '+(d.r?'ВКЛ':'ВЫКЛ'));
      var cw=+d.cWhy||0,ca=(+d.cAge||0)+(snap?Math.max(0,(n()-snap.dataAt)/1000):0);
      L.push('WAN: интернет '+(d.net?'есть':'нет')+' · облако '+(+d.cOk||0)+' из '+(+d.cTry||0)+' попыток · подряд ошибок '+(+d.cFail||0)+' · последняя попытка '+dur(ca)+' назад · причина: '+(cw?(WHY[cw]||('код '+cw)):'нет'));
    }else L.push('Плата: нет свежих /data');
    L.push('Показатели (только источник и возраст, без значений):');
    for(i=0;i<MET.length;i++){var r=measText(MET[i]);L.push('  '+MKEY[i]+' — '+r.t+(r.age&&r.age!=='—'?' · '+r.age:''))}
    var sd=sdText();
    if(sd)L.push('SD: ошибок записи '+sd.fail+(sd.run?' (подряд '+sd.run+')':'')+' · последняя запись '+dur(sd.age)+' назад · архив: '+(sd.arch?'ошибка':'без ошибки')+(sd.low?' · мало места':'')+' · свободно '+window.domP12.gb(sd.free));
    else L.push('SD: нет ответа /storage');
    var g=gapText();
    L.push('GAPFILL: '+(g?('стадия '+g.stage+' · код '+g.code+' · в облаке данных нет: '+g.noData+' · сообщение: '+(g.msg||'—')):'нет ответа /gapfill/status'));
    var bl=bootsRows();
    L.push('Перезагрузки (/boots): '+(bl.length?bl.length+' '+plur(bl.length,'запись','записи','записей')+' · последняя причина: '+(RR[bl[bl.length-1].name]||bl[bl.length-1].name)+' · прошлая сессия '+dur(bl[bl.length-1].up):'журнал пуст или недоступен'));
    var vn=(EV.vlog&&+EV.vlog.n)||0;
    L.push('События напряжения (/vlog): '+vn+(vn?' '+plur(vn,'запись','записи','записей'):' — событий нет'));
    var cr=clogRows(),cc=clogCounters();
    L.push('Облако (/clog), последние '+Math.min(3,cr.length)+':');
    if(!cr.length)L.push('  нет строк / нет ответа');
    for(i=cr.length-1;i>=0&&i>=cr.length-3;i--)L.push('  '+clogLine(cr[i],d));
    if(cc)L.push('Счётчики: cTry='+cc.try+' cOk='+cc.ok+' cFail='+cc.fail);
    L.push('Обезличено: IP, MAC, SSID, токены, device id и значения измерений не включены; отчёт никуда не отправляется.');
    return L.join('\n');
  }
  /* маскировка на случай утечки; build (разрешён) скрываем на время замены */
  function sanitize(s){
    var K='\u0001',build=(LASTD&&LASTD.build)?String(LASTD.build):'';
    if(build)s=s.split(build).join(K+'B'+K);
    s=String(s).replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g,'[IP скрыт]')
      .replace(/\b(?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}\b/g,'[MAC скрыт]')
      .replace(/\b[0-9a-fA-F]{16,}\b/g,'[идентификатор скрыт]')
      .replace(/\b(?:[0-9a-fA-F]{4}:){7}[0-9a-fA-F]{4}\b/g,'[IPv6 скрыт]');
    return s.split(K+'B'+K).join(build);
  }
  function openReport(){
    var pre=G('repPre');if(!pre)return;
    pre.textContent=sanitize(reportLines());
    var pv=G('repPrev');if(pv)pv.hidden=false;
    txt('repMsg','предпросмотр: проверьте текст и нажмите «Скопировать»');txt('repCopyMsg','');
  }
  function fallbackCopy(t){
    try{
      var ta=document.createElement('textarea');ta.value=t;ta.setAttribute('readonly','');
      ta.style.position='fixed';ta.style.top='-1000px';ta.style.left='-1000px';
      document.body.appendChild(ta);ta.focus();ta.select();
      var ok=false;try{ok=document.execCommand('copy')}catch(e){}
      document.body.removeChild(ta);return !!ok;
    }catch(e){return false}
  }
  function copyReport(){
    var pre=G('repPre');if(!pre)return;
    var t=pre.textContent||'';
    function done(ok){txt('repCopyMsg',ok?'скопировано в буфер':'не удалось — выделите текст вручную')}
    if(navigator.clipboard&&navigator.clipboard.writeText)
      navigator.clipboard.writeText(t).then(function(){done(true)}).catch(function(){done(fallbackCopy(t))});
    else done(fallbackCopy(t));
  }
  function init(){
    if(!G('evBox')&&!G('diagBox'))return;
    var eb=G('evBox'),db=G('diagBox');
    if(eb)eb.addEventListener('toggle',function(){if(eb.open)fetchAll()});
    if(db)db.addEventListener('toggle',function(){if(db.open)fetchAll()});
    var rb=G('evReload');if(rb)rb.addEventListener('click',fetchAll);
    var ab=G('repBtn');if(ab)ab.addEventListener('click',openReport);
    var cb=G('repCopy');if(cb)cb.addEventListener('click',copyReport);
    var xb=G('repClose');if(xb)xb.addEventListener('click',function(){
      var pv=G('repPrev');if(pv)pv.hidden=true;
      txt('repMsg','предпросмотр закрыт · без IP, MAC, SSID, токенов, device id и значений')});
    window.domP13={report:reportLines,sanitize:sanitize,fallback:fallbackCopy};
    var prev=window.domStatusTick||function(){};
    window.domStatusTick=function(d){try{prev(d)}catch(e){}try{tick(d)}catch(e){}};
    renderAll();
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();
