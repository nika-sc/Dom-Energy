"""Archive/restore regressions, including 24 synthetic full-resolution months."""
import calendar
import datetime as dt
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pc'))
spec=importlib.util.spec_from_file_location('archive_sd',Path('pc/archive-sd.py'))
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
state={'ok':True,'device':'0123456789abcdef','bootId':1,'sdGeneration':0,'freeBytes':7*1024**3}
data={'energy-2024-01.csv':b'older monthly data\n','energy.csv':b'current data\n','rangecache.bin':bytes(range(256))}
calls=[]
a.storage=lambda url:dict(state)
a.files=lambda url:[{'name':n,'size':len(v)} for n,v in data.items()]
def read(url,name,size):calls.append(name);return data[name]
a.read_file=read
with tempfile.TemporaryDirectory(prefix='dom-archive-tests-') as folder:
    root=Path(folder)/'archive'
    first=a.snapshot(root,'http://fixture')
    first_path=next((root/'snapshots').rglob('*.json'))
    restored=Path(folder)/'restored';assert a.restore(root,first_path,restored)==3
    assert (restored/'rangecache.bin').read_bytes()==bytes(range(256))
    calls.clear();a.snapshot(root,'http://fixture');assert 'energy-2024-01.csv' not in calls
    state['sdGeneration']+=1;data['energy-2024-01.csv']=b'newer monthly data\n'
    calls.clear();second=a.snapshot(root,'http://fixture');assert 'energy-2024-01.csv' in calls
    assert first['files']['energy-2024-01.csv']['sha256']!=second['files']['energy-2024-01.csv']['sha256']
    old=first['files']['energy-2024-01.csv'];assert a.verify_object(a.object_path(root,old['sha256']),old['sha256'],old['size'])
    item=second['files']['energy-2024-01.csv'];blob=a.object_path(root,item['sha256']);blob.write_bytes(b'broken gzip')
    calls.clear();a.snapshot(root,'http://fixture');assert 'energy-2024-01.csv' in calls and any((root/'quarantine').iterdir())
    prior=(root/'latest.json').read_bytes();old_storage=a.storage
    ticks=[0]
    def changing(url):
        ticks[0]+=1;value=dict(state)
        if ticks[0]>1:value['sdGeneration']+=1
        return value
    a.storage=changing
    try:a.snapshot(root,'http://fixture');raise AssertionError('mixed generation accepted')
    except OSError:pass
    assert (root/'latest.json').read_bytes()==prior
    a.storage=old_storage
    assert not a.valid_name('../secrets.h') and not a.valid_name('energy-2024-01/../x.csv')
    (restored/'energy.csv').write_bytes(b'USER FILE')
    try:a.restore(root,first_path,restored);raise AssertionError('user file overwritten')
    except FileExistsError:pass
    assert (restored/'energy.csv').read_bytes()==b'USER FILE'
    print('PASS: binary restore, dedup, same-size rewrite, bit-rot repair/quarantine, generation consistency, path traversal and overwrite protection',flush=True)
    # Process a full two-year inventory with only one month's bytes in the archiver.
    inventory=[]
    sample=b'2024-01-01 00:00:00,230,100,0000034.000,1,0.00,20.0\n'
    for year in (2024,2025):
        for month in range(1,13):
            count=calendar.monthrange(year,month)[1]*2880
            inventory.append({'name':f'energy-{year}-{month:02}.csv','size':count*len(sample)})
    rows=sum(x['size']//len(sample) for x in inventory)
    def monthly(url,name,size):
        year,month=map(int,name[7:14].split('-'));count=calendar.monthrange(year,month)[1]*2880
        body=b''.join((f'{year}-{month:02}-{i//2880+1:02} {i%2880//120:02}:{i%120//2:02}:{i%2*30:02},230,100,0000034.000,1,0.00,20.0\n').encode() for i in range(count))
        assert len(body)==size
        return body
    a.files=lambda url:inventory;a.read_file=monthly;state['device']='fedcba9876543210'
    large=a.snapshot(Path(folder)/'two-years','http://fixture')
    assert len(large['files'])==24
    assert sum(v['size'] for v in large['files'].values())==sum(v['size'] for v in inventory)
    print(f'PASS: 24 full months, {rows:,} rows, {large["downloadedBytes"]:,} raw bytes; complete manifests and gzip checksums',flush=True)
