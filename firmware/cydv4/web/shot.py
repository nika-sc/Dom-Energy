"""Снимок страницы через Chrome DevTools Protocol с точной шириной.

    python shot.py URL ШИРИНА ВЫСОТА ФАЙЛ [ожидание_сек]

Нужен только для проверки вёрстки: ставит viewport ровно 390 px
(обычный headless Chrome не даёт окно уже 500 px).
"""
import base64
import json
import subprocess
import sys
import tempfile
import time
import urllib.request

import websocket

CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'


def main():
    url = sys.argv[1]
    w = int(sys.argv[2])
    h = int(sys.argv[3])
    out = sys.argv[4]
    wait = float(sys.argv[5]) if len(sys.argv) > 5 else 8.0

    prof = tempfile.mkdtemp(prefix='cdp-')
    proc = subprocess.Popen(
        [CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
         '--remote-debugging-port=9333', '--user-data-dir=' + prof,
         '--no-first-run', '--no-default-browser-check', 'about:blank'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ws_url = None
        for _ in range(60):
            try:
                with urllib.request.urlopen('http://127.0.0.1:9333/json', timeout=1) as r:
                    tabs = json.loads(r.read().decode('utf-8'))
                pages = [t for t in tabs if t.get('type') == 'page']
                if pages:
                    ws_url = pages[0]['webSocketDebuggerUrl']
                    break
            except Exception:                       # noqa: BLE001
                time.sleep(0.25)
        if not ws_url:
            sys.exit('Chrome не поднялся')

        ws = websocket.create_connection(ws_url, timeout=120)
        seq = [0]

        def call(method, params=None):
            seq[0] += 1
            ws.send(json.dumps({'id': seq[0], 'method': method, 'params': params or {}}))
            while True:
                msg = json.loads(ws.recv())
                if msg.get('id') == seq[0]:
                    if 'error' in msg:
                        raise RuntimeError(msg['error'])
                    return msg.get('result', {})

        call('Page.enable')
        call('Runtime.enable')
        call('Emulation.setDeviceMetricsOverride',
             {'width': w, 'height': h, 'deviceScaleFactor': 1, 'mobile': w < 600})
        call('Page.navigate', {'url': url})
        time.sleep(wait)
        info = call('Runtime.evaluate', {
            'expression': '[innerWidth, document.documentElement.scrollWidth, document.body.scrollHeight,'
                          ' (document.getElementById("dateLabel")||{}).textContent||"",'
                          ' document.querySelectorAll("svg").length].join("|")',
            'returnByValue': True})
        print('innerWidth|scrollWidth|bodyHeight|dateLabel|svgCount =', info['result']['value'])
        errs = call('Runtime.evaluate', {
            'expression': '(window.__errs||[]).join(" ; ")', 'returnByValue': True})
        print('js errors:', errs['result'].get('value', ''))
        shot = call('Page.captureScreenshot', {'format': 'png', 'captureBeyondViewport': True})
        with open(out, 'wb') as fh:
            fh.write(base64.b64decode(shot['data']))
        print('saved', out)
        ws.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:                           # noqa: BLE001
            proc.kill()


if __name__ == '__main__':
    main()
