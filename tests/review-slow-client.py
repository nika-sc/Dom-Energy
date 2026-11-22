"""Slow-reader regression; only resets diagnostic timing counters."""
import json
import os
import socket
import time
import urllib.request
from pathlib import Path
base=os.environ.get('BOARD_URL','http://192.168.1.51')
host=urllib.parse.urlparse(base).hostname
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
key=(Path(os.environ['APPDATA'])/'DomMonitor/control.key').read_text().strip()
with op.open(urllib.request.Request(base+'/data?reset=1',headers={'X-Dom-Key':key}),timeout=15) as response:
    before=json.load(response)
slow=socket.socket();slow.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,512);slow.settimeout(5)
try:
    slow.connect((host,80))
    slow.sendall(b'GET /app.js HTTP/1.1\r\nHost: device\r\nConnection: close\r\n\r\n')
    time.sleep(.1)
    started=time.monotonic()
    with op.open(base+'/data',timeout=8) as response: after=json.load(response)
    elapsed=time.monotonic()-started
    assert elapsed<4,elapsed
    assert after['up']>=before['up']
    print(json.dumps({'seconds':round(elapsed,3),'maxLoopMs':after['loopMaxAll'],'block':after['blk'],'blockMs':after['blkMs'],'build':after['build']}))
finally:slow.close()
