"""Проверка живой страницы платы через Chrome DevTools Protocol.

    python test_page.py [URL]

Проверяет: цифры совпадают с /data, стрелки ‹ › листают дни,
подсказка на столбике появляется, ошибок JS нет.
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
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://192.168.1.51/'

prof = tempfile.mkdtemp(prefix='cdp-')
proc = subprocess.Popen(
    [CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
     '--remote-debugging-port=9334', '--user-data-dir=' + prof,
     '--no-first-run', '--no-default-browser-check', 'about:blank'],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    ws_url = None
    for _ in range(60):
        try:
            with urllib.request.urlopen('http://127.0.0.1:9334/json', timeout=1) as r:
                pages = [t for t in json.loads(r.read().decode('utf-8')) if t.get('type') == 'page']
            if pages:
                ws_url = pages[0]['webSocketDebuggerUrl']
                break
        except Exception:                            # noqa: BLE001
            time.sleep(0.25)
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

    def js(expr):
        r = call('Runtime.evaluate', {'expression': expr, 'returnByValue': True, 'awaitPromise': True})
        return r.get('result', {}).get('value')

    call('Page.enable')
    call('Runtime.enable')
    call('Emulation.setDeviceMetricsOverride', {'width': 390, 'height': 1600,
                                                 'deviceScaleFactor': 1, 'mobile': True})
    call('Page.addScriptToEvaluateOnNewDocument', {
        'source': 'window.__errs=[];window.addEventListener("error",function(e){window.__errs.push(e.message)});'
                  'window.addEventListener("unhandledrejection",function(e){window.__errs.push("rej:"+e.reason)});'})
    call('Page.navigate', {'url': URL})
    time.sleep(12)

    out = {}
    out['label0'] = js('document.getElementById("dateLabel").textContent')
    out['tkwh_page'] = js('document.getElementById("tkwh").textContent')
    out['tcost_page'] = js('document.getElementById("tcost").textContent')
    out['barsE'] = js('document.querySelectorAll("#chE rect.bar, #chE rect.bar0").length')
    out['barsP'] = js('document.querySelectorAll("#chP rect.bar").length')
    out['errs'] = js('(window.__errs||[]).join(" | ")')
    out['upd'] = js('document.getElementById("cUpd").textContent')
    out['events'] = js('document.getElementById("events").textContent')

    # цифры с /data для сверки
    with urllib.request.urlopen(URL.rstrip('/') + '/data', timeout=10) as r:
        d = json.loads(r.read().decode('utf-8'))
    out['tkwh_data'] = round(float(d['tkwh']), 2)
    out['tcost_data'] = round(float(d['tcost']), 2)

    # листаем назад: 30 -> 29 -> 28
    for step in (1, 2):
        js('document.getElementById("prev").click()')
        time.sleep(5)
        out['label%d' % step] = js('document.getElementById("dateLabel").textContent')
        out['barsE%d' % step] = js('document.querySelectorAll("#chE rect.bar, #chE rect.bar0").length')
        out['loading%d' % step] = js('document.querySelectorAll("#chE .ph").length')

    # подсказка на столбике
    js('''(function(){var r=document.querySelector('#chE rect.hit[data-i="5"]');
           r.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,cancelable:true}));})()''')
    time.sleep(0.5)
    out['tip'] = js('(function(){var t=document.getElementById("tip");return t.hidden?"hidden":t.textContent})()')

    # вперёд до сегодня
    js('document.getElementById("next").click()')
    time.sleep(4)
    out['label_back'] = js('document.getElementById("dateLabel").textContent')
    out['errs_end'] = js('(window.__errs||[]).join(" | ")')
    print(json.dumps(out, ensure_ascii=False, indent=1))
    ws.close()
finally:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except Exception:                                # noqa: BLE001
        proc.kill()
