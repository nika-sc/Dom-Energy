"""Daily, lossless SD snapshots. Content-addressed gzip objects; no expiry/deletion.
Restore only writes to a local directory, never to the board. Standard Python only.
"""
import argparse
import contextlib
import datetime as dt
import gzip
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import zlib
import urllib.request
from board_http import files, read_file

LOG=logging.getLogger('dom-archive')
NAME=re.compile(r'(energy(?:-\d{4}-\d{2}(?:\.rev\d{4})?)?\.(?:csv|bak|peak)|climate\.csv|weather\.csv|rangecache\.bin|boots(?:\.\d+|-\d{4}-\d{2}-\d{4})?\.log)\Z')

def valid_name(name):
    return isinstance(name,str) and bool(NAME.fullmatch(name))

def atomic_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
    try:
        with os.fdopen(fd,'w',encoding='utf-8',newline='\n') as stream:
            json.dump(value,stream,ensure_ascii=False,indent=2);stream.flush();os.fsync(stream.fileno())
        os.replace(temp,path)
    finally:
        if os.path.exists(temp):os.unlink(temp)

def object_path(root,digest):
    if not re.fullmatch('[0-9a-f]{64}',digest):raise ValueError('Invalid object SHA256')
    return root/'objects'/digest[:2]/(digest+'.gz')

def verify_object(path,digest,size):
    try:
        sha=hashlib.sha256();count=0
        with gzip.open(path,'rb') as stream:
            for block in iter(lambda:stream.read(65536),b''):
                sha.update(block);count+=len(block)
        return count==size and sha.hexdigest()==digest
    except (OSError,EOFError,zlib.error):return False

def save_object(root,data):
    digest=hashlib.sha256(data).hexdigest();path=object_path(root,digest)
    if verify_object(path,digest,len(data)):return digest
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():
        quarantine=root/'quarantine';quarantine.mkdir(exist_ok=True)
        path.rename(quarantine/(path.name+'.'+dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%f')))
    if shutil.disk_usage(root).free<max(64*1024*1024,len(data)*2):raise OSError('Archive disk has less than required free space')
    fd,temp=tempfile.mkstemp(suffix='.gz.tmp',dir=path.parent);os.close(fd)
    try:
        with gzip.open(temp,'wb',compresslevel=6) as stream:stream.write(data)
        with open(temp,'r+b') as stream:os.fsync(stream.fileno())
        if not verify_object(Path(temp),digest,len(data)):raise OSError('Gzip verification failed')
        os.replace(temp,path)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return digest

def storage(url):
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(url.rstrip('/')+'/storage',timeout=20) as response:state=json.load(response)
    if not state.get('ok') or not re.fullmatch('[0-9a-fA-F]{16}',str(state.get('device',''))):
        raise ValueError('Storage diagnostics unavailable or device identity missing')
    return state

def snapshot(root,url):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    before=storage(url);previous={}
    latest=root/'latest.json'
    if latest.exists():previous=json.loads(latest.read_text(encoding='utf-8'))
    if previous and previous['storage']['device']!=before['device']:
        raise ValueError('Different board: choose a separate archive directory')
    same_generation=all(previous.get('storage',{}).get(k)==before[k] for k in ('bootId','sdGeneration'))
    prior=previous.get('files',{});saved={};downloaded=0
    for item in files(url):
        name=item['name'];size=int(item['size'])
        if not valid_name(name):raise ValueError('Unexpected SD filename: '+repr(name))
        if size<0 or size>128*1024*1024:raise ValueError('SD file exceeds bounded monthly file policy')
        old=prior.get(name)
        immutable=name.startswith('energy-') or name.startswith('boots-')
        if immutable and same_generation and old and old['size']==size and verify_object(object_path(root,old['sha256']),old['sha256'],size):
            saved[name]=old
        else:
            data=read_file(url,name,size)
            if len(data)!=size:raise OSError('Incomplete download: '+name)
            saved[name]={'size':size,'sha256':save_object(root,data)};downloaded+=size
        LOG.info('%s: %d bytes',name,size)
    after=storage(url)
    if any(after[k]!=before[k] for k in ('device','bootId','sdGeneration')):
        raise OSError('SD rewritten or board rebooted during download; previous complete snapshot preserved; retry')
    now=dt.datetime.now(dt.timezone.utc)
    report={'format':1,'createdUtc':now.isoformat(),'url':url,'storage':after,'downloadedBytes':downloaded,'files':saved}
    name=now.strftime('%Y-%m/%Y%m%dT%H%M%S%fZ.json')
    atomic_json(root/'snapshots'/name,report)
    atomic_json(latest,report)
    LOG.info('COMPLETE: %d files, %d new downloaded bytes',len(saved),downloaded)
    if after.get('lowSpace') or after.get('archiveError'):LOG.warning('SD storage warning: %s',after)
    return report

def restore(root,manifest,destination):
    root=Path(root);destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    report=json.loads(Path(manifest).read_text(encoding='utf-8'))
    for name,item in report['files'].items():
        if not valid_name(name):raise ValueError('Unsafe restore filename')
        blob=object_path(root,item['sha256'])
        if not verify_object(blob,item['sha256'],item['size']):raise OSError('Damaged archive object: '+name)
        target=destination/name
        with gzip.open(blob,'rb') as stream:data=stream.read()
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest()!=item['sha256']:raise FileExistsError('Restore would overwrite different file: '+name)
        else:
            fd,temp=tempfile.mkstemp(prefix=name+'.',suffix='.restore.tmp',dir=destination)
            try:
                with os.fdopen(fd,'wb') as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
                os.link(temp,target) # atomic create; never overwrite an existing user file
            finally:
                if os.path.exists(temp):os.unlink(temp)
    return len(report['files'])

@contextlib.contextmanager
def exclusive(root):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    with (root/'.archive.lock').open('a+b') as lock:
        if lock.tell()==0:lock.write(b'0');lock.flush()
        lock.seek(0)
        if os.name=='nt':
            import msvcrt
            msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        else:
            import fcntl
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        yield

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://192.168.1.51')
    parser.add_argument('--dest',default=r'D:\DomEnergy\DomEnergy-Archive')
    parser.add_argument('--restore',metavar='MANIFEST')
    parser.add_argument('--restore-dir')
    args=parser.parse_args();root=Path(args.dest);root.mkdir(parents=True,exist_ok=True)
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',handlers=[logging.FileHandler(root/'archive.log',encoding='utf-8'),logging.StreamHandler()])
    try:
        with exclusive(root):
            if args.restore:
                if not args.restore_dir:raise ValueError('--restore-dir is required')
                LOG.info('Restored %d files to local directory only',restore(root,args.restore,args.restore_dir))
            else:snapshot(root,args.url)
    except Exception:
        LOG.exception('FAILED: previous complete snapshots preserved')
        return 1
    return 0

if __name__=='__main__':sys.exit(main())
