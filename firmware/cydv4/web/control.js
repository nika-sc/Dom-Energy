/* Shared transport: bounded requests, explicit incomplete-history handling and control access. */
(function(){
  'use strict';
  var original=window.fetch.bind(window), key='';
  var activeReads=0,readQueue=[];
  function queuedRead(action){
    return new Promise(function(resolve,reject){
      function start(){
        activeReads++;var released=false;
        function release(){if(released)return;released=true;activeReads--;if(readQueue.length)readQueue.shift()()}
        Promise.resolve().then(function(){return action(release)}).then(resolve,function(error){release();reject(error)});
      }
      if(activeReads<2)start();else readQueue.push(start);
    });
  }
  try {
    key=sessionStorage.getItem('dom.control')||'';
    var params=new URLSearchParams(location.hash.slice(1)), supplied=params.get('control');
    if(supplied&&/^[a-f0-9]{64}$/i.test(supplied)){
      key=supplied;sessionStorage.setItem('dom.control',key);
      history.replaceState(null,'',location.pathname+location.search);
    }
  }catch(e){}
  window.fetch=function(input,options){
    var url=new URL(typeof input==='string'?input:input.url,location.href);
    var local=url.origin===location.origin;
    var method=((options&&options.method)||(input&&input.method)||'GET').toUpperCase();
    var commands=['/tap','/reboot','/rotate','/restorebak','/beep','/update','/wifiset'];
    var readOnly=local&&method==='GET'&&((!url.search&&commands.indexOf(url.pathname)<0)||['/history','/range','/days','/files','/full'].indexOf(url.pathname)>=0);
    var readUntil=Date.now()+((options&&options.timeoutMs)||10000),releaseSlot=function(){};
    function send(retry){
      if(readOnly&&Date.now()>=readUntil)return Promise.reject(new DOMException('Read timeout','AbortError'));
      var init=Object.assign({},options||{}), headers=new Headers(init.headers||(input&&input.headers)||{});
      if(local&&key)headers.set('X-Dom-Key',key);
      var controller=new AbortController(), outer=init.signal;
      if(outer){if(outer.aborted)controller.abort();else outer.addEventListener('abort',function(){controller.abort()},{once:true})}
      var timeout=setTimeout(function(){controller.abort();releaseSlot()},readOnly?Math.max(1,readUntil-Date.now()):(init.timeoutMs||10000));
      delete init.timeoutMs;init.headers=headers;init.signal=controller.signal;if(local)init.redirect='error';
      return original(input,init).then(function(response){
        if(local&&response.status===401&&!retry){
          clearTimeout(timeout);
          var entered=window.prompt('Введите ключ управления устройством (доступен в приложении Windows).');
          if(!entered||!/^[a-f0-9]{64}$/i.test(entered.trim()))throw Error('Доступ к управлению не предоставлен');
          key=entered.trim();try{sessionStorage.setItem('dom.control',key)}catch(e){}
          return send(true);
        }
        // Ошибочный ответ (4xx/5xx) часто не читают, а слот чтения держится до
        // чтения тела или таймаута: два таких подряд залипали на 10 с. Слот
        // освобождаем сразу — release() идемпотентен, повторное чтение работает.
        if(local && !response.ok && releaseSlot) releaseSlot();
        ['json','text','blob','arrayBuffer'].forEach(function(name){
          var method=response[name].bind(response);
          response[name]=function(){return method().then(function(body){clearTimeout(timeout);releaseSlot();return body},function(error){clearTimeout(timeout);releaseSlot();throw error})};
        });
        return response;
      },function(error){clearTimeout(timeout);throw error});
    }
    if(!readOnly)return send(false);
    return queuedRead(function(release){
      releaseSlot=release;
      function attempt(count){return send(false).catch(function(error){
        if(options&&options.signal&&options.signal.aborted)throw error;
        if(error.name==='TypeError'&&count<2&&Date.now()+250<readUntil)return new Promise(function(resolve){setTimeout(resolve,250)}).then(function(){return attempt(count+1)});
        throw error;
      })}
      return attempt(0);
    });
  };
  window.domJson=function(url,deadlineMs,signal){
    var until=Date.now()+(deadlineMs||240000),failures=0;
    function read(){
      if(signal&&signal.aborted)return Promise.reject(Error('Загрузка отменена'));
      if(Date.now()>=until)return Promise.reject(Error('Время загрузки истории истекло'));
      return fetch(url,{cache:'no-store',signal:signal,timeoutMs:Math.min(10000,Math.max(1,until-Date.now()))}).then(function(r){
        if(!r.ok){var error=Error('HTTP '+r.status);error.retryable=[408,429,500,502,503,504].indexOf(r.status)>=0;throw error}
        return r.json().catch(function(error){error.retryable=true;throw error});
      }).catch(function(error){
        if(signal&&signal.aborted)throw error;
        if(Date.now()<until&&(error.retryable||error.name==='TypeError'||error.name==='AbortError')){
          return new Promise(function(resolve){setTimeout(resolve,Math.min(until-Date.now(),2000,400*++failures))}).then(read);
        }
        throw error;
      }).then(function(j){
        if(j.pending){if(Date.now()>=until)throw Error('Время загрузки истории истекло');return new Promise(function(resolve){setTimeout(resolve,Math.max(200,Math.min(1500,+j.retryMs||700)))}).then(read)}
        if(j.trunc)throw Error('Плата вернула неполную историю. Повторите загрузку.');
        if(j.ok===false)throw Error(j.err||'Ошибка устройства');
        return j;
      });
    }
    return read();
  };
})();
