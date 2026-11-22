"""Read-only live GUI checks in an isolated headless browser, no user profile."""
import json
import time
import urllib.request
from pathlib import Path
from playwright.sync_api import sync_playwright
base=os.environ.get('BOARD_URL','http://192.168.1.51').rstrip('/')
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def complete(path):
    until=time.monotonic()+180
    while True:
        with op.open(base+path,timeout=20) as response:j=json.load(response)
        if not j.get('pending'):return j
        if time.monotonic()>until:raise TimeoutError(path)
        time.sleep(.7)
print('LIVE BUILD:',complete('/data').get('build'),flush=True)
print('Loading reference history',flush=True)
history=complete('/history?date=2026-11-12')
records={x['h']:x for x in history['hours']}
with sync_playwright() as p:
    browser=p.chromium.launch(executable_path=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',headless=True,args=['--no-sandbox'])
    page=browser.new_page(viewport={'width':1280,'height':900})
    errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
    configs={}
    def response_seen(response):
        name=response.url.split('?',1)[0].rsplit('/',1)[-1]
        if name in ('protect','sched','sound'):configs[name]=response.status
    page.on('response',response_seen)
    page.on('requestfailed',lambda r:print('NETWORK:',r.url,r.failure,flush=True))
    def groups(count):
        try:
            page.wait_for_function('(n)=>document.querySelectorAll("#historyWorkspace .history-combined svg g[data-index]").length===n || document.querySelector("#historySummary").textContent.startsWith("Не удалось")',arg=count,timeout=240000,polling=200)
            assert page.locator('#historyWorkspace .history-combined svg g[data-index]').count()==count,page.locator('#historySummary').inner_text()
        except Exception:
            print('FAILED state:',page.locator('#historySummary').inner_text(),flush=True)
            page.locator('#historyWorkspace').scroll_into_view_if_needed()
            page.screenshot(path='pc/firmware-archive/recovery-validation-20261114/live-web-failed.png')
            raise
    print('Opening live page',flush=True)
    dialogs=[]
    page.on('dialog',lambda d:(dialogs.append(d.message),d.dismiss()))
    page.goto(base,wait_until='domcontentloaded',timeout=60000)
    assert page.locator('#historyWorkspace .history-report #rngCard').is_visible()
    assert page.locator('#historyWorkspace details.history-report').count()==0
    page.locator('#historyDate').fill('2026-11-12');page.locator('#historyReload').click()
    page.wait_for_function("document.querySelectorAll('#historyWorkspace .history-combined svg g[data-index]').length===24 && document.querySelector('#historyDate').value==='2026-11-12'",timeout=180000)
    print('Checking real hourly values',flush=True)
    for hour in (0,14,23):
        page.locator(f'#historyWorkspace .history-combined svg g[data-index="{hour}"] rect').last.click()
        assert f'{hour:02}:00' in page.locator('#historySummary').inner_text()
        if records[hour]['n']>0:assert f'{records[hour]["kwh"]:.3f}' in page.locator('#historyMetrics').inner_text()
    print('Checking seven daily groups',flush=True)
    page.locator('[data-mode="days"]').click()
    groups(7)
    assert page.locator('.history-legend button').count()==6
    print('Checking natural double click',flush=True)
    page.locator('#historyWorkspace .history-combined svg g[data-index="4"] rect').last.dblclick()
    groups(24)
    assert page.locator('#historyDate').input_value()=='2026-11-10'
    print('Checking four weekly groups',flush=True)
    page.locator('[data-mode="weeks"]').click()
    groups(4)
    print('Checking month groups',flush=True)
    page.locator('[data-mode="month"]').click()
    groups(10)
    print('SVG groups: visible history=',page.locator('#historyWorkspace .history-combined svg g[data-index]').count(),'all including hidden report=',page.locator('#historyWorkspace g[data-index]').count(),flush=True)
    page.locator('#historyWorkspace').scroll_into_view_if_needed()
    page.screenshot(path='pc/firmware-archive/recovery-validation-20261114/live-web.png',full_page=False)
    assert not errors,errors
    assert not dialogs,dialogs
    assert all(configs.get(name)==200 for name in ('protect','sched','sound')),configs
    print('Configuration reads:',configs,flush=True)
    browser.close()
print('PASS: live hour values match API; days, natural double click, weeks, month and legend; no JS errors')
