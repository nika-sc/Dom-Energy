"""Live regressions. Read-only except empty OTA rejected before Update.begin."""
import json
import time
import os
import urllib.request
import urllib.error
from pathlib import Path

BASE=os.environ.get('BOARD_URL','http://192.168.1.51').rstrip('/')
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def get(path):
    for attempt in range(4):
        try:
            with op.open(BASE+path,timeout=20) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError((error.code,error.read().decode('utf-8','replace'))) from error
        except (OSError, ValueError):
            if attempt==3:raise
            time.sleep(.7*(attempt+1))
def complete(path):
    until=time.monotonic()+120
    while True:
        data=get(path)
        if not data.get('pending'):
            assert not data.get('trunc'),data
            return data
        assert time.monotonic()<until,'Scan never completed'
        time.sleep(.7)

report=[]
before=get('/data')
for date in ['2026-11-01','2026-11-07','2026-11-12']:
    started=time.monotonic()
    history=complete('/history?date='+date)
    period=complete('/range?from='+date+'&to='+date)
    rows=sum(x.get('n',0) for x in history['hours'])
    kwh=sum(x.get('kwh',0) for x in history['hours'])
    row=period['rows'][0]
    assert rows==row['rows'],(date,rows,row['rows'])
    assert abs(kwh-row['kwh'])<.025,(date,kwh,row['kwh'])
    report.append({'date':date,'rows':rows,'historyKwh':round(kwh,3),'rangeKwh':row['kwh'],'seconds':round(time.monotonic()-started,2)})
key_file=Path(os.environ['APPDATA'])/'DomMonitor/control.key'
key=key_file.read_text().strip()
for path,method,body,headers,expected in [
    ('/update','POST',b'',{},401),
    ('/tap','GET',None,{},401),
    ('/update','POST',b'',{'X-Dom-Key':key},400),
]:
    request=urllib.request.Request(BASE+path,data=body,headers=headers,method=method)
    try:
        op.open(request,timeout=10)
        raise AssertionError('Unauthorised or empty upload accepted')
    except urllib.error.HTTPError as error:
        assert error.code==expected,(path,error.code,expected)
        report.append({'path':path,'status':error.code})
time.sleep(1)
after=get('/data')
assert after['up']>=before['up'],'Rejected upload rebooted board'
assert after['r']==before['r'],'Test changed relay state'
print(json.dumps({'build':after.get('build'),'checks':report,'heap':after['heap']},ensure_ascii=False,indent=2))
