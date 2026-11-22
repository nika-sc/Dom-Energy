# -*- coding: utf-8 -*-
"""Стендовая проверка P1.3/P1.5: события, диагностика, обезличенный отчёт.

    python web/test_p13.py

Поднимает web/mock_server.py с фикстурами и проверяет страницу в headless
Chrome через Playwright (channel='chrome', отдельный браузер не нужен):
  1) блоки «События» и «Диагностика» читают /vlog, /boots, /clog, /gapfill/status;
  2) опрос /vlog,/boots,/clog — не чаще 10 с, /gapfill/status — 20 с;
  3) отчёт не содержит IP/MAC/device id/токенов/единиц измерений, копируется;
  4) при 503 на /boots,/clog,/gapfill и пустом /vlog страница не падает.
Скриншоты: web/shots-p13/.
"""
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright

HERE = pathlib.Path(__file__).resolve().parent
SHOTS = HERE / 'shots-p13'
SHOTS.mkdir(exist_ok=True)
RESULTS = []


def check(name, cond, extra=''):
    RESULTS.append((name, bool(cond), extra))
    print('  %-58s %s %s' % (name, 'OK' if cond else 'FAIL', extra))
    return bool(cond)


def start_server(port, extra=()):
    proc = subprocess.Popen([sys.executable, str(HERE / 'mock_server.py'), str(port)] + list(extra),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for _ in range(80):
        try:
            urllib.request.urlopen('http://127.0.0.1:%d/data' % port, timeout=1).read()
            return proc
        except Exception:
            time.sleep(0.15)
    proc.kill()
    raise SystemExit('mock server не поднялся')


def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=8)
    except Exception:
        proc.kill()


SPY = """
window.__p13reqs = [];
(function(){
  var of = window.fetch;
  window.fetch = function(u, o){
    try{
      var s = (typeof u === 'string') ? u : u.url;
      if (/^\\/(vlog|boots|clog|gapfill)/.test(s)) window.__p13reqs.push([s, Date.now()]);
    }catch(e){}
    return of.apply(this, arguments);
  };
})();
"""


def new_page(browser, url):
    ctx = browser.new_context(viewport={'width': 1280, 'height': 1000})
    try:
        ctx.grant_permissions(['clipboard-read', 'clipboard-write'])
    except Exception:
        pass
    ctx.add_init_script(SPY)
    page = ctx.new_page()
    errs = []
    page.on('pageerror', lambda e: errs.append('pageerror: %s' % e))
    page.on('console', lambda m: errs.append('console: %s' % m.text)
            if m.type == 'error' and 'Failed to load resource' not in m.text else None)
    page.goto(url, wait_until='domcontentloaded', timeout=30000)
    return ctx, page, errs


def wait_ready(page, marker='/vlog n=', timeout=20000):
    page.wait_for_function(
        "m => { var e = document.getElementById('evLive'); return e && e.textContent.indexOf(m) >= 0 }",
        arg=marker, timeout=timeout)


def text(page, sel):
    return page.eval_on_selector(sel, 'e => e.textContent')


def main():
    port = 8791
    url = 'http://127.0.0.1:%d/' % port
    srv = start_server(port)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(channel='chrome')
            print('--- сценарий 1: нормальные данные ---')
            ctx, page, errs = new_page(browser, url)
            wait_ready(page)
            # 12 с не раскрываем блоки: проверяем частоту опроса
            page.wait_for_timeout(12000)
            reqs = page.evaluate('window.__p13reqs')
            for name, path in (('vlog', '/vlog'), ('boots', '/boots'), ('clog', '/clog'), ('gapfill', '/gapfill/status')):
                ts = [t for (s, t) in reqs if s == path]
                gaps = [round(ts[i + 1] - ts[i]) for i in range(len(ts) - 1)]
                # у /vlog два стартовых запроса: старый блок app.js (раз в 20 с) и новый;
                # паузы короче 3 с — это старт, дальше проверяем «не чаще 10 с»
                limit = 3 if name == 'vlog' else (2 if name != 'gapfill' else 1)
                steady = [g for g in gaps if g >= 3000]
                check('опрос %s (%d запросов, паузы %s с)' % (path, len(ts), gaps),
                      len(ts) <= limit and all(g >= 9500 for g in steady),
                      'не чаще 10 с' if name != 'gapfill' else 'не чаще 20 с')
            check('данные наблюдаются с …', 'данные наблюдаются с' in text(page, '#evObs'), text(page, '#evObs'))
            page.screenshot(path=str(SHOTS / 'p13-collapsed-1280.png'), full_page=True)
            page.evaluate("document.getElementById('evBox').open = true;"
                          "document.getElementById('diagBox').open = true;")
            page.wait_for_timeout(2500)

            ev = text(page, '#evHistList')
            check('события: провал и всплеск из /vlog', 'провал' in ev and 'всплеск' in ev, ev[:120].replace('\n', ' | '))
            check('события: перезагрузки из /boots', 'перезагрузка' in ev and 'включение питания' in ev
                  and 'сторожевой таймер задачи' in ev)
            check('события: старт сессии из /data', 'старт текущей сессии' in text(page, '#evState'))
            check('события: cWhy — последняя ошибка облака', 'последняя ошибка облака: нет интернета'
                  in text(page, '#evState'), text(page, '#evState'))
            check('событий не больше 20', len(page.query_selector_all('#evHistList .ev')) <= 20,
                  str(len(page.query_selector_all('#evHistList .ev'))))
            check('сводка: /vlog n=3', '/vlog n=3' in text(page, '#evLive'), text(page, '#evLive'))

            check('диагностика: версия', text(page, '#dgVer') == 'CYD-4.14', text(page, '#dgVer'))
            check('диагностика: build', '6acd775a4f217e710854' in text(page, '#dgBuild'), text(page, '#dgBuild'))
            check('диагностика: uptime 2 ч 30 мин', '2 ч 30 мин' in text(page, '#dgUp'), text(page, '#dgUp'))
            check('диагностика: V из /measure — локальное реле',
                  'локальное реле' in text(page, '#dgV'), text(page, '#dgV'))
            check('диагностика: T из /measure — облако', 'облако' in text(page, '#dgT'), text(page, '#dgT'))
            check('диагностика: LAN/WAN переиспользуют /data',
                  'плата отвечает' in text(page, '#dgLan') and 'облако 1200 из 1234' in text(page, '#dgWan'),
                  text(page, '#dgWan')[:90])
            check('диагностика: SD writeFail/lastWriteAgeSec/archiveError',
                  'ошибок 0' in text(page, '#dgSdFail') and 'последняя запись' in text(page, '#dgSdAge')
                  and 'нет ошибки' in text(page, '#dgSdArch'))
            check('диагностика: последний gapfill', text(page, '#dgGapStage').startswith('завершено')
                  and text(page, '#dgGapCode') == 'успешно' and text(page, '#dgGapNo').startswith('нет'),
                  '%s / %s / %s' % (text(page, '#dgGapStage'), text(page, '#dgGapCode'), text(page, '#dgGapNo')))
            clog = text(page, '#dgClog')
            check('диагностика: /clog 3 строки с расшифровкой',
                  'этап: успешно' in clog and 'причина: нет интернета' in clog
                  and 'HTTP 200' in clog and 'cTry=1234' in clog, clog.replace('\n', ' | ')[:150])

            page.click('#repBtn')
            page.wait_for_timeout(300)
            prev_visible = page.eval_on_selector('#repPrev', 'e => !e.hidden')
            rep = text(page, '#repPre')
            check('отчёт: предпросмотр виден', prev_visible and len(rep) > 300, '%d символов' % len(rep))
            for need in ('обезличенный отчёт', 'Версия: CYD-4.14', 'сборка 6acd775a4f217e710854',
                         'Uptime платы', 'LAN:', 'WAN:', 'Показатели', 'SD:', 'GAPFILL:',
                         'Перезагрузки (/boots)', 'События напряжения (/vlog)', 'Облако (/clog)',
                         'Счётчики: cTry=1234', 'Обезличено'):
                check('отчёт: есть «%s»' % need, need in rep)
            checks = {
                'нет IPv4': not re.search(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', rep),
                'нет MAC': not re.search(r'\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b', rep, re.I),
                'нет device id из /storage': '1ca10f05d79ca6b2' not in rep,
                'нет токена/ключа': 'token' not in rep.lower() and 'key=' not in rep.lower(),
                'нет значений V/P/I/T': not re.search(r'(238|236,1|143|0[.,]88|22[.,]3)\s*(В|Вт|А|°C)', rep),
                'нет SSID': not re.search(r'SSID\s*[:=]\s*\S', rep),
            }
            for k, v in checks.items():
                check('отчёт: ' + k, v)
            m = page.evaluate("window.domP13.sanitize('ip 192.168.1.51 mac aa:bb:cc:dd:ee:ff dev 1ca10f05d79ca6b2')")
            check('sanitize: IP/MAC/идентификатор маскируются',
                  '[IP скрыт]' in m and '[MAC скрыт]' in m and '[идентификатор скрыт]' in m, m)

            page.click('#repCopy')
            page.wait_for_timeout(700)
            msg = text(page, '#repCopyMsg')
            check('копирование: показан результат', msg.strip() != '', msg)
            try:
                clip = page.evaluate('navigator.clipboard.readText()')
            except Exception as e:
                clip = ''
                print('    (буфер не прочитан: %s)' % e)
            if clip:
                same = clip.strip().replace('\r\n', '\n') == rep.strip()
                check('копирование: в буфере тот же обезличенный текст', same,
                      'совпало' if same else 'не совпало')
            else:
                check('копирование: буфер недоступен в headless — проверен только отклик', True)
            page.screenshot(path=str(SHOTS / 'p13-1280.png'), full_page=True)

            # 390 px: нет горизонтальной прокрутки, блоки читаемы
            page.set_viewport_size({'width': 390, 'height': 900})
            page.wait_for_timeout(500)
            check('390 px: нет горизонтального скролла',
                  page.evaluate('document.documentElement.scrollWidth <= innerWidth + 1'),
                  'scrollWidth %s' % page.evaluate('document.documentElement.scrollWidth'))
            page.screenshot(path=str(SHOTS / 'p13-390.png'), full_page=True)
            check('JS-ошибок нет (сценарий 1)', not errs, '; '.join(errs[:3]))
            ctx.close()

            print('--- сценарий 2: пустой /vlog и 503 на /boots,/clog,/gapfill ---')
            stop_server(srv)
            srv = start_server(port, ['--vlog-empty', '--calm', '--fail', 'boots,clog,gapfill'])
            ctx, page, errs = new_page(browser, url)
            wait_ready(page, '/vlog n=0')
            page.evaluate("document.getElementById('evBox').open = true;"
                          "document.getElementById('diagBox').open = true;")
            page.wait_for_timeout(4000)
            ev = text(page, '#evHistList')
            check('пустой /vlog: «событий нет»', 'событий нет' in ev, ev[:100])
            live = text(page, '#evLive')
            check('ошибки: /boots нет ответа в сводке', '/boots: нет ответа' in live, live)
            check('ошибки: /clog — нет ответа', 'нет ответа /clog' in text(page, '#dgClog'),
                  text(page, '#dgClog'))
            check('ошибки: /gapfill/status — нет ответа',
                  'нет ответа /gapfill/status' in text(page, '#dgGapStage'), text(page, '#dgGapStage'))
            check('ошибки: версия всё равно показана', text(page, '#dgVer') == 'CYD-4.14')
            check('JS-ошибок нет (сценарий 2)', not errs, '; '.join(errs[:3]))
            page.screenshot(path=str(SHOTS / 'p13-errors.png'), full_page=True)
            ctx.close()
            browser.close()
    finally:
        stop_server(srv)

    fails = [r for r in RESULTS if not r[1]]
    print('\nИТОГО: %d проверок, %d OK, %d FAIL' % (len(RESULTS), len(RESULTS) - len(fails), len(fails)))
    for name, ok, extra in fails:
        print('  FAIL:', name, extra)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
