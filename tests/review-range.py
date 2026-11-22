"""Live bounded-period regression: deliberately include today for a cold scan."""
import datetime as dt
import json
import time
import urllib.request
from pathlib import Path

op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
base=os.environ.get('BOARD_URL','http://192.168.1.51').rstrip('/')
def get(path):
    for attempt in range(4):
        try:
            with op.open(base+path,timeout=15) as response:return json.load(response)
        except (OSError,ValueError):
            if attempt==3:raise
            time.sleep(.7)
before=get('/data');today=before['now'][:10]
path='/range?from='+today[:8]+'01&to='+today
started=time.monotonic();pending=0;latency=[]
while True:
    data=get(path)
    if not data.get('pending'):break
    pending+=1
    check=time.monotonic();live=get('/data');latency.append(time.monotonic()-check)
    assert live['up']>=before['up'],'Board rebooted'
    assert live['r']==before['r'],'Relay state changed'
    assert time.monotonic()-started<240,'Period never finished'
    time.sleep(.7)
assert pending,'Expected a cold scan including today'
assert max(latency)<8,latency
expect={'2026-11-01':(2828,1.06),'2026-11-07':(2889,.44),'2026-11-12':(2839,2.35)}
rows={x['d']:x for x in data['rows']}
compared=0
for date,(n,kwh) in expect.items():
    if date in rows:
        compared+=1
        assert rows[date]['rows']==n,(date,rows[date]['rows'],n)
        assert abs(rows[date]['kwh']-kwh)<.01,(date,rows[date]['kwh'],kwh)
print(json.dumps({'status':'PASS','build':before['build'],'pendingResponses':pending,'seconds':round(time.monotonic()-started,2),'maxDataSeconds':round(max(latency),3),'days':len(rows),'knownDaysCompared':compared,'historicalCountsAndEnergyMatch':True if compared else None}))
