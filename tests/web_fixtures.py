import datetime as dt
import json
from pathlib import Path
from urllib.parse import parse_qs, urlparse
from playwright.sync_api import sync_playwright

web = Path('firmware/cydv4/web')
html = (web / 'page.html').read_text(encoding='utf-8')
replacements = {'__FW__': '4.14', '__NETCLS__': 'ok', '__NETTXT__': 'ok',
                '__DATE__': '2026-11-13', '__HOUR__': '14'}
import re
html = re.sub(r'__[A-Z_]+__', lambda m: replacements.get(m[0], '0'), html)
hours = [{'h': h, 'n': 1, 'kwh': h / 100, 'p': h * 10, 'pmax': h * 20,
          'v': 230 + h / 10, 't': 18 + h / 10} for h in range(24) if h != 23]
hours[5].update({'v':None,'p':None,'pmax':None,'t':0})

def respond(route):
    u = urlparse(route.request.url)
    if u.path == '/':
        return route.fulfill(status=200, content_type='text/html; charset=utf-8', body=html)
    if u.path in ('/app.js', '/history.js', '/app.css'):
        return route.fulfill(status=200, content_type='text/css' if u.path.endswith('.css') else 'application/javascript', body=(web / u.path[1:]).read_bytes())
    q = parse_qs(u.query)
    if u.path == '/history':
        result = {'date': q.get('date', ['2026-11-13'])[0], 'hours': hours}
    elif u.path == '/range':
        start = dt.date.fromisoformat(q['from'][0]); end = dt.date.fromisoformat(q['to'][0])
        rows = []
        while start <= end:
            zero = start.isoformat() == '2026-11-11'
            rows.append({'d': start.isoformat(), 'rows': 5, 'kwh': 1.2, 'cost': 10,
                         'pmax': 200, 'vmin': 225, 'vmax': 242, 'tmin': 0 if zero else 17,
                         'tmax': 0 if zero else 19, 'tvalid': True, 'vvalid': True})
            start += dt.timedelta(days=1)
        result = {'rows': rows}
    elif u.path == '/days': result = {'days': ['2026-11-13']}
    elif u.path == '/data': result = {'vSeen': 1, 'v': 238, 'p': 140, 'amp': .59, 'temp': 19, 'net': True}
    else: result = {}
    return route.fulfill(status=200, content_type='application/json', body=json.dumps(result))

