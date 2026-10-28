"""Тестовый сервер для page.html на компьютере (без платы).

Поднимает те же эндпоинты, что и прошивка: /, /data, /days, /history,
считая историю из скачанного /full (energy.csv) по тем же правилам.

    python serve_test.py [--csv путь] [--port 8099]

Данные /data берутся с живой платы (http://192.168.1.51/data), если она
доступна, иначе — заглушка.
"""
import argparse
import json
import pathlib
import re
import urllib.request
from collections import defaultdict
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = pathlib.Path(__file__).resolve().parent
BOARD = 'http://192.168.1.51'

MOCK = {
    'v': 236, 'temp': 21.9, 'p': 18.0, 'amp': 0.14, 'e': 17.91, 'r': True,
    'tkwh': 0.160, 'tcost': 0.91, 'ykwh': 2.120, 'ykost': 17.32,
    'mkwh': 9.94, 'mcost': 79.88, 'tariff': 7.25, 'rssi': -56, 'net': True,
}


def live_data():
    try:
        with urllib.request.urlopen(BOARD + '/data', timeout=2.5) as r:
            d = json.loads(r.read().decode('utf-8', 'replace'))
        if isinstance(d, dict) and d.get('v') is not None:
            if not d.get('now'):
                d['now'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            return d
    except Exception as exc:                       # noqa: BLE001
        print('нет платы, заглушка:', exc)
    return dict(MOCK)


def load_rows(csv_path):
    rows = defaultdict(list)
    seq = []
    with open(csv_path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            line = line.rstrip('\r\n')
            m = re.match(r'^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)(?:,([^,]*))?$', line)
            if not m:
                continue
            d, hh, mm, ss, v, p, e, relay, cost, temp = m.groups()
            row = {
                'tod': int(hh) * 10000 + int(mm) * 100 + int(ss),
                'h': int(hh), 'v': float(v or 0), 'p': float(p or 0),
                'e': float(e or 0), 't': (float(temp) if temp not in (None, '') else None),
            }
            rows[d].append(row)
            if row['v'] > 50 and row['e'] > 0:
                seq.append({'t': '%s %s:%s:%s' % (d, hh, mm, ss),
                            'r': 1 if float(relay or 0) > 0.5 else 0,
                            'v': row['v'], 'p': row['p']})
    return rows, seq


def events(seq, limit=10):
    """Как в прошивке: последние 3 дня, строка с v>50, порядок по времени."""
    if not seq:
        return []
    cut = (datetime.now() - timedelta(days=3)).strftime('%Y-%m-%d')
    rows = sorted((r for r in seq if r['t'][:10] >= cut), key=lambda r: r['t'])
    out = []
    prev = None
    for row in rows:
        if prev is not None and row['r'] != prev:
            out.append(row)
        prev = row['r']
    return out[-limit:][::-1]


def history(rows, date):
    hours = []
    for h in range(24):
        rr = [r for r in rows.get(date, []) if r['h'] == h]
        n = len(rr)
        vs = [r['v'] for r in rr if r['v'] > 50]
        ts = [r['t'] for r in rr if r['t'] is not None and -40 < r['t'] < 100]
        es = [r for r in rr if r['e'] > 0]
        kwh = 0.0
        if es:
            first = min(es, key=lambda r: r['tod'])
            last = max(es, key=lambda r: r['tod'])
            if last['e'] >= first['e']:
                kwh = round(last['e'] - first['e'], 2)
        hours.append({
            'h': h, 'kwh': kwh,
            'p': round(sum(r['p'] for r in rr if r['v'] > 50) / len(vs), 0) if vs else 0,
            'pmax': max([r['p'] for r in rr if r['v'] > 50] or [0]),
            'v': round(sum(vs) / len(vs)) if vs else 0,
            't': round(sum(ts) / len(ts), 1) if ts else None,
            'n': n,
        })
    return {'date': date, 'ms': 0, 'trunc': False, 'hours': hours}


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    rows = {}
    seq = []

    def log_message(self, fmt, *args):
        pass

    def _send(self, body, ctype='text/html; charset=utf-8', code=200):
        if isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        from urllib.parse import urlparse, parse_qs
        u = urlparse(self.path)
        qs = parse_qs(u.query)
        if u.path == '/':
            d = live_data()
            now = datetime.now()
            reps = {
                '__FW__': '4.3',
                '__NETCLS__': 'ok' if d.get('net', True) else 'bad',
                '__NETTXT__': 'сеть ok' if d.get('net', True) else 'нет сети',
                '__RSSI__': str(d.get('rssi', -56)),
                '__TKWH__': '{:.2f}'.format(d.get('tkwh', 0)),
                '__TCOST__': '{:.2f}'.format(d.get('tcost', 0)),
                '__MV__': '{:.0f}'.format(d.get('v', 0)),
                '__MP__': '{:.0f}'.format(d.get('p', 0)),
                '__MA__': '{:.2f}'.format(d.get('amp', 0)),
                '__MT__': '{:.1f}'.format(d.get('temp', 0)),
                '__RELCLS__': 'ok' if d.get('r') else 'bad',
                '__RELTXT__': 'ВКЛ' if d.get('r') else 'ВЫКЛ',
                '__TAR__': '{:.2f}'.format(d.get('tariff', 0)),
                '__PE__': '{:.2f}'.format(d.get('e', 0)),
                '__YKWH__': '{:.2f}'.format(d.get('ykwh', 0)),
                '__YKOST__': '{:.2f}'.format(d.get('ykost', 0)),
                '__MKWH__': '{:.2f}'.format(d.get('mkwh', 0)),
                '__MCOST__': '{:.2f}'.format(d.get('mcost', 0)),
                '__TARDAY__': '8.39',
                '__TARNIGHT__': '4.49',
                '__DATE__': now.strftime('%Y-%m-%d'),
                '__HOUR__': str(now.hour),
            }
            html = (HERE / 'page.html').read_text(encoding='utf-8')
            for k, v in reps.items():
                html = html.replace(k, v)
            if qs.get('debug'):
                probe = (
                    '<script>window.addEventListener("load",function(){setTimeout(function(){'
                    'var bad=[];document.querySelectorAll("*").forEach(function(e){'
                    'var r=e.getBoundingClientRect();'
                    'if(r.right>innerWidth+1)bad.push(e.tagName+"."+(e.className.baseVal||e.className||"")+":"+Math.round(r.right));'
                    '});'
                    'document.title="SW="+document.documentElement.scrollWidth+" IW="+innerWidth+" BAD="+bad.slice(0,10).join("|");'
                    '},600);});</script>')
                html = html.replace('</body>', probe + '</body>')
            self._send(html)
            return
        if u.path == '/boardframe':
            w = qs.get('w', ['390'])[0]
            h = qs.get('h', ['1800'])[0]
            html = ('<!doctype html><html><head><meta charset="utf-8">'
                    '<style>html,body{margin:0;padding:0;background:#fff}'
                    'iframe{border:0;width:%spx;height:%spx;display:block}</style></head>'
                    '<body><iframe src="http://192.168.1.51/"></iframe></body></html>') % (w, h)
            self._send(html)
            return
        if u.path == '/frame':
            w = qs.get('w', ['390'])[0]
            h = qs.get('h', ['1800'])[0]
            html = ('<!doctype html><html><head><meta charset="utf-8">'
                    '<style>html,body{margin:0;padding:0;background:#fff}'
                    'iframe{border:0;width:%spx;height:%spx;display:block}</style></head>'
                    '<body><iframe src="/"></iframe></body></html>') % (w, h)
            self._send(html)
            return
        if u.path == '/data':
            self._send(json.dumps(live_data()), 'application/json; charset=utf-8')
            return
        if u.path == '/days':
            days = sorted(self.rows.keys(), reverse=True)[:30]
            self._send(json.dumps({'days': days}, ensure_ascii=False), 'application/json; charset=utf-8')
            return
        if u.path == '/events':
            self._send(json.dumps({'events': events(self.seq)}, ensure_ascii=False),
                       'application/json; charset=utf-8')
            return
        if u.path == '/history':
            date = qs.get('date', [''])[0]
            if not re.match(r'^\d{4}-\d\d-\d\d$', date):
                self._send('{"error":"date"}', 'application/json; charset=utf-8', 400)
                return
            self._send(json.dumps(history(self.rows, date), ensure_ascii=False),
                       'application/json; charset=utf-8')
            return
        self._send('{"error":"not found"}', 'application/json; charset=utf-8', 404)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default=str(pathlib.Path.home() / 'AppData/Local/Temp/energy.csv'))
    ap.add_argument('--port', type=int, default=8099)
    args = ap.parse_args()
    Handler.rows, Handler.seq = load_rows(args.csv)
    print('дней:', len(Handler.rows), 'строк:', sum(len(v) for v in Handler.rows.values()),
          'событий:', len(events(Handler.seq)))
    srv = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print('http://127.0.0.1:%d/' % args.port)
    srv.serve_forever()


if __name__ == '__main__':
    main()
