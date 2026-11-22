"""Temporarily disable board Internet APIs, observe LAN polling, restore NETON."""
import datetime
import argparse
import json
from pathlib import Path
import time
import urllib.request
import serial

op = urllib.request.build_opener(urllib.request.ProxyHandler({}))
keys = ['up', 'v', 'p', 'temp', 'vAge', 'pAge', 'tAge', 'eAge', 'stale',
        'net', 'rssi', 'ty', 'phN', 'phR', 'cTry', 'cOk', 'sdRows', 'msTuya','e','build','r']
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--seconds',type=int,default=205)
parser.add_argument('--out',default='pc/firmware-archive/local-fallback-check-20261113.jsonl')
args=parser.parse_args()
if not 60<=args.seconds<=3600:raise ValueError('Duration must be 60..3600 seconds')
out = Path(args.out)
out.parent.mkdir(parents=True,exist_ok=True)
def snapshot(stage):
    for attempt in range(5):
        try:
            d = json.load(op.open('http://192.168.1.51/data', timeout=15))
            break
        except (OSError, ValueError):
            if attempt == 4:
                raise
            time.sleep(2)
    row = {'time': datetime.datetime.now().isoformat(), 'stage': stage,
           **{k: d.get(k) for k in keys}}
    with out.open('a', encoding='utf-8') as f:
        f.write(json.dumps(row, ensure_ascii=False) + '\n')
    print(json.dumps(row, ensure_ascii=False), flush=True)
    return d

before=snapshot('before')
offline=[]
s = serial.Serial(port=None, baudrate=115200, timeout=.1)
s.dtr = False
s.rts = False
s.port = 'COM5'
s.open()
try:
    s.write(b'NETOFF\n')
    s.flush()
    started = time.monotonic()
    next_check = started + 3
    while time.monotonic() - started < args.seconds:
        s.readline()
        if time.monotonic() >= next_check:
            offline.append(snapshot('internet_apis_disabled'))
            next_check = time.monotonic() + 25
    assert offline[-1]['phR']>offline[0]['phR'],'No local phase updates'
    assert offline[-1]['sdRows']>before['sdRows'],'SD did not continue recording'
    assert all(not x['stale'] and x['vAge']<90 and x['pAge']<90 and x['eAge']<90 for x in offline),'Stale local measurements'
    assert all(x['cTry']==offline[0]['cTry'] for x in offline),'Cloud API was called during NETOFF'
    assert offline[-1]['up']>before['up']+args.seconds*.7,'Board rebooted'
    assert all(x['r']==before['r'] for x in offline),'Relay state changed'
    print('PASS: local V/P/E, SD growth, no cloud calls, no reboot or relay switching',flush=True)
finally:
    s.write(b'NETON\n')
    s.flush()
    time.sleep(1)
    s.close()
    print('NETON sent; Internet APIs restored', flush=True)
for i in range(4):
    time.sleep(10)
    snapshot('restoring')
