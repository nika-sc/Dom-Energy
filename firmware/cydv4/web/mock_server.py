# -*- coding: utf-8 -*-
"""Локальный стенд для page.html без платы: отдаёт страницу и фикстуры.

    python web/mock_server.py [port]

Страница генерируется из web/page.html при каждом запросе, поэтому после
правки достаточно перезагрузить вкладку. /data и /history берутся из
web/fixtures (сняты с реальной платы 30.10.2026).
"""
import json
import pathlib
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = pathlib.Path(__file__).resolve().parent
FIX = HERE / 'fixtures'
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

REPL = {
    '__FW__': 'CYD-4.3', '__NETCLS__': 'ok', '__NETTXT__': 'сеть ok', '__RSSI__': '-58',
    '__TKWH__': '1.240', '__TCOST__': '9.97', '__MV__': '238', '__MP__': '143',
    '__MA__': '0.88', '__MT__': '22.3', '__RELCLS__': 'ok', '__RELTXT__': 'ВКЛ',
    '__TAR__': '8.04', '__PE__': '18.99', '__YKWH__': '2.120', '__YKOST__': '17.32',
    '__MKWH__': '11.00', '__MCOST__': '88.78', '__TARDAY__': '8.39', '__TARNIGHT__': '4.49',
    '__DATE__': '2026-10-30', '__HOUR__': '17',
}


def page():
    html = (HERE / 'page.html').read_text(encoding='utf-8')
    for k, v in REPL.items():
        html = html.replace(k, v)
    return html.encode('utf-8')


def fixture(name):
    return (FIX / (name + '.json')).read_bytes()


class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, body, ctype='application/json; charset=utf-8', code=200):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == '/':
            return self.send(page(), 'text/html; charset=utf-8')
        if u.path == '/data':
            d = json.loads(fixture('data'))
            d['now'] = '2026-10-30 17:30:00'
            d['up'] = 9000
            d['heap'] = 129000
            return self.send(json.dumps(d).encode())
        if u.path == '/days':
            return self.send(fixture('days'))
        if u.path == '/events':
            return self.send(fixture('events'))
        if u.path == '/history':
            date = (parse_qs(u.query).get('date') or [''])[0]
            name = {'2026-10-30': 'h30', '2026-10-29': 'h29_filled'}.get(date)
            if name:
                return self.send(fixture(name))
            return self.send(b'{"date":"' + date.encode() + b'","hours":[]}')
        return self.send(b'{"error":"not found"}', code=404)


if __name__ == '__main__':
    print('mock: http://127.0.0.1:%d/' % PORT, flush=True)
    ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
