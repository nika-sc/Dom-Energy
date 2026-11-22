"""Regression checks for the reviewed web bugs; real Edge, deterministic API."""
from pathlib import Path
import json
from web_fixtures import *
base_respond = respond
pending_reads = 0
pending_ranges = 0
failed_ranges = 0
def respond(route):
    global pending_reads,pending_ranges,failed_ranges
    path = urlparse(route.request.url).path
    if path == '/control.js':
        return route.fulfill(status=200, content_type='application/javascript', body=(web/'control.js').read_bytes())
    if path == '/history' and pending_reads < 2:
        pending_reads += 1
        return route.fulfill(status=202, content_type='application/json', body='{"pending":true,"retryMs":200}')
    if path == '/range' and failed_ranges < 2:
        failed_ranges += 1
        return route.abort('connectionreset')
    if path == '/range' and pending_ranges < 2:
        pending_ranges += 1
        return route.fulfill(status=202, content_type='application/json', body='{"pending":true,"retryMs":200}')
    return base_respond(route)

with sync_playwright() as p:
    browser=p.chromium.launch(executable_path=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',headless=True,args=['--no-sandbox'])
    page=browser.new_page(viewport={'width':1280,'height':900})
    errors=[]
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.route('**/*',respond)
    page.goto('http://example.test/#control='+'a'*64,wait_until='domcontentloaded')
    page.locator('#historyWorkspace .history-combined svg g[data-index="14"]').wait_for()
    assert pending_reads == 2
    assert page.locator('#historyWorkspace .history-report #rngCard').is_visible()
    assert page.locator('#historyWorkspace details.history-report').count()==0
    for hour in range(24):
        page.locator(f'#historyWorkspace .history-combined svg g[data-index="{hour}"] rect').last.click()
        assert f'{hour:02}:00' in page.locator('#historySummary').inner_text()
    page.locator('#historyWorkspace .history-combined svg g[data-index="5"] rect').last.click()
    values=page.locator('#historyMetrics .history-value').all_text_contents()
    assert values[1]=='— Вт' and values[3]=='— В' and values[4]=='0.0 °C',values
    assert page.evaluate('location.hash') == ''
    page.evaluate('apply({v:0,p:0,temp:0,amp:0,vSeen:0,pSeen:0,tSeen:0,stale:true});renderDetails({hours:[]})')
    assert page.locator('#dVbig').inner_text() == '—'
    assert page.locator('#dTbig').inner_text() == '—'
    assert page.locator('#wxTom').inner_text()=='ожидание прогноза'
    page.locator('[data-mode="days"]').click()
    page.wait_for_function("document.querySelectorAll('#historyWorkspace .history-combined svg g[data-index]').length===7")
    assert pending_ranges == 2
    assert failed_ranges == 2
    page.locator('#historyWorkspace .history-combined svg g[data-index="4"] rect').last.click()
    assert '0.0–0.0 °C' in page.locator('#historyMetrics').inner_text()
    page.locator('#historyWorkspace .history-combined svg g[data-index="4"] rect').last.dblclick()
    page.wait_for_function("document.querySelectorAll('#historyWorkspace .history-combined svg g[data-index]').length===24")
    assert page.locator('#historyDate').input_value() == '2026-11-11'
    pending=[]
    page.route('**/range?**',lambda route:pending.append(route))
    page.locator('[data-mode="month"]').click()
    page.wait_for_function("document.querySelector('#historySummary').textContent==='Загрузка…'")
    assert page.locator('#historyWorkspace .history-combined svg g[data-index]').count() == 0
    page.locator('#historyWorkspace').click()
    assert page.locator('#historySummary').inner_text() == 'Загрузка…'
    for route in pending: respond(route)
    page.wait_for_function("document.querySelectorAll('#historyWorkspace .history-combined svg g[data-index]').length>0")
    queued=[]
    for route_name in ('sched','protect','sound'):
        page.route('**/'+route_name,lambda route:queued.append(route))
    page.evaluate("window.readBatchDone=false;Promise.all(['/sched','/protect','/sound'].map(u=>fetch(u).then(r=>r.json()))).then(()=>window.readBatchDone=true);true")
    page.wait_for_timeout(100)
    assert len(queued)==2,('Read concurrency was not bounded',len(queued))
    queued[0].fulfill(status=200,content_type='application/json',body='{}')
    page.wait_for_timeout(100)
    assert len(queued)==3,'Queued configuration read did not resume'
    for route in queued[1:]:route.fulfill(status=200,content_type='application/json',body='{}')
    page.wait_for_function('window.readBatchDone===true')
    taps=[]
    def rejected_tap(route):
        taps.append(route.request.url)
        route.abort('connectionreset')
    page.route('**/tap',rejected_tap)
    assert page.evaluate("fetch('/tap').then(()=>false,()=>true)")
    assert len(taps)==1,'A control command must never be retried automatically'
    seen=[]
    def auth_route(route):
        seen.append(route.request.headers.get('x-dom-key'))
        route.fulfill(status=200,content_type='application/json',body='{}')
    page.route('**/test-control',auth_route)
    page.evaluate("fetch('/test-control').then(r=>r.json())")
    assert seen == ['a'*64]
    dialogs=[]
    page.on('dialog',lambda dialog:(dialogs.append(dialog.message),dialog.dismiss()))
    denied=[]
    def expired_session(route):
        denied.append(route.request.method)
        route.fulfill(status=401 if len(denied)==1 else 200,content_type='application/json',body='{}')
    page.route('**/denied-control',expired_session)
    assert page.evaluate("fetch('/denied-control',{method:'POST'}).then(r=>r.ok)")
    assert denied==['POST','POST'] and not dialogs,denied
    # Two 401 responses must stop; no endless control retries.
    rejected=[]
    def always_denied(route):
        rejected.append(route.request.method)
        route.fulfill(status=401,content_type='application/json',body='{}')
    page.route('**/always-denied',always_denied)
    assert page.evaluate("fetch('/always-denied',{method:'POST'}).then(r=>r.status)")==401
    assert rejected==['POST','POST'] and not dialogs,rejected
    held=[]
    page.route('**/never',lambda route:held.append(route))
    result=page.evaluate("fetch('/never',{timeoutMs:50}).then(r=>r.json()).then(()=>false,e=>e.name==='AbortError')")
    assert result
    for route in held:route.abort()
    assert page.evaluate("rangeTempValid({rows:2,tmin:0,tmax:0,tvalid:true}) && !rangeTempValid({rows:2,tmin:0,tmax:0,tvalid:false})")
    page.evaluate("S.date='2024-01-01';loadDays()")
    page.wait_for_function("S.hist && S.hist.date==='2024-01-01'")
    assert page.evaluate('S.date')=='2024-01-01'
    for skin in range(6):
        page.evaluate('setSkin('+str(skin)+')')
        assert page.locator('html').get_attribute('data-skin')==str(skin)
    for width in (360,768,1280,1920):
        page.set_viewport_size({'width':width,'height':900})
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth+1'),f'overflow at {width}px'
    assert not errors,errors
    page.unroute_all(behavior='ignoreErrors')
    browser.close()
print('PASS: pending history, missing/zero values, navigation, control key, timeout, reset recovery, two-reader queue and no automatic control retries')
