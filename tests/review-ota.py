"""Loader regressions against a loopback device; never writes real firmware."""
import json
import hashlib
import os
import subprocess
import threading
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

BUILD='0123456789abcdef0123'
class Handler(BaseHTTPRequestHandler):
    reads=posts=0
    reply='FAIL'
    installed=BUILD
    keys=[]
    fail_reads=0
    def log_message(self,*args):pass
    def do_GET(self):
        if self.path=='/data' and Handler.fail_reads:
            Handler.fail_reads-=1
            self.send_response(503);self.end_headers();return
        Handler.reads+=1
        body=json.dumps({'up':300 if Handler.reads==1 else 2,'build':Handler.installed,'controlAuth':True}).encode() if self.path=='/data' else b'web'
        self.send_response(200);self.end_headers();self.wfile.write(body)
    def do_POST(self):
        Handler.posts+=1;Handler.keys.append(self.headers.get('X-Dom-Key'))
        self.rfile.read(int(self.headers['Content-Length']))
        self.send_response(200);self.end_headers();self.wfile.write(Handler.reply.encode())

server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory() as folder:
    image=Path(folder)/'image.bin';image.write_bytes(b'\xe9'+b'x'*1023)
    manifest=Path(str(image)+'.manifest.json')
    manifest.write_text(json.dumps({'build':BUILD,'sha256':hashlib.sha256(image.read_bytes()).hexdigest()}))
    base=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(Path('pc/flash-http.ps1').resolve()),'-Ip','127.0.0.1:'+str(server.server_port),'-ReadyTimeoutSec','2']
    env={**os.environ,'DOM_CONTROL_KEY':'b'*64}
    def run(args):return subprocess.run(base+args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=25,env=env)
    result=run([])
    assert result.returncode!=0 and Handler.posts==0,'Unselected TEMP image used'
    result=run(['-Bin',str(image)])
    assert result.returncode!=0 and Handler.reads==1,(Handler.reads,result.returncode,result.stdout,result.stderr)
    Handler.reads=0;Handler.reply='OK';Handler.installed='ffffffffffffffffffff'
    result=run(['-Bin',str(image)])
    assert result.returncode!=0,'Wrong installed build accepted'
    Handler.reads=0;Handler.installed=BUILD
    result=run(['-Bin',str(image)])
    assert result.returncode==0,(result.stdout,result.stderr)
    assert Handler.keys==['b'*64]*3
    assert b'b'*64 not in result.stdout+result.stderr,'Control key leaked into output'
    Handler.reads=Handler.posts=0;Handler.fail_reads=1
    result=run(['-Bin',str(image)])
    assert result.returncode==0 and Handler.posts==1,(result.returncode,Handler.posts,result.stdout,result.stderr)
    Handler.posts=0;image.write_bytes(b'\xe9'+b'y'*1023)
    result=run(['-Bin',str(image)])
    assert result.returncode!=0 and Handler.posts==0,'SHA mismatch was uploaded'
server.shutdown()
print('PASS: explicit image, manifest/hash, FAIL rejection, build/reboot, control header, transient preflight recovery with one upload, secret-free output')
