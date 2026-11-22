# -*- coding: utf-8 -*-
"""Стендовая проверка P2.3 (бюджет/прогноз) и P2.4 (экспорт CSV).

    python web/test_p34.py

Поднимает web/mock_server.py (фикстуры /data, /days), а /range, /history и
месячный /data подменяет через page.route. Часы браузера фиксируются на
16.11.2026 12:00, поэтому «текущий месяц» в тесте — ноябрь 2026, и прогноз
считается по 15 полным суткам месяца (1–15 ноября) × 30 дней.
mock_server.py и фикстуры не меняются. Скриншоты: web/shots-p34/.
"""
import datetime
import json
import pathlib
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import time
import urllib.request
from urllib.parse import urlparse, parse_qs

from playwright.sync_api import sync_playwright

HERE = pathlib.Path(__file__).resolve().parent
SHOTS = HERE / 'shots-p34'
SHOTS.mkdir(exist_ok=True)
FIX = HERE / 'fixtures'
RESULTS = []
PORT = 8794
URL = 'http://127.0.0.1:%d/' % PORT
NOW = datetime.datetime(2026, 11, 16, 12, 0, 0)
MONTH_FROM, MONTH_TO = '2026-11-01', '2026-11-16'
HEAD = ('date;timezone;kwh_kWh;cost_RUB;day_kWh;night_kWh;day_RUB;night_RUB;'
        'vmin_V;vmax_V;tmin_C;tmax_C;pmax_W;rows;weather')


def check(name, cond, extra=''):
    RESULTS.append((name, bool(cond), extra))
    print('  %-64s %s %s' % (name, 'OK' if cond else 'FAIL', extra))
    return bool(cond)


def start_server():
    proc = subprocess.Popen([sys.executable, str(HERE / 'mock_server.py'), str(PORT)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for _ in range(80):
        try:
            urllib.request.urlopen(URL + 'data', timeout=1).read()
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
window.__req = [];
(function(){
  var of = window.fetch;
  window.fetch = function(u, o){
    try{
      var s=(typeof u === 'string') ? u : (u && u.url) || '';
      window.__req.push({u:s, m:(o && o.method) || 'GET'});
    }catch(e){}
    return of.apply(this, arguments);
  };
  var ox = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function(m, u){
    try{ window.__req.push({u:String(u), m:String(m||'GET')}); }catch(e){}
    return ox.apply(this, arguments);
  };
})();
"""


def day_rows(frm, tto, fn):
    """Суточные строки /range: fn(day_number) -> dict-набор полей или None."""
    out, d = [], datetime.date.fromisoformat(frm)
    while d <= datetime.date.fromisoformat(tto):
        r = {'d': d.isoformat()}
        r.update(fn(d.day) or {})
        out.append(r)
        d += datetime.timedelta(days=1)
    return out


def measured(kwh, cost=None, rows=100, day_frac=0.6, **kw):
    r = {'kwh': kwh,
         'cost': round(kwh * 8.04, 2) if cost is None and kwh is not None else cost,
         'dayKwh': None if kwh is None else round(kwh * day_frac, 3),
         'nightKwh': None if kwh is None else round(kwh * (1 - day_frac), 3),
         'vmin': 230, 'vmax': 240, 'vvalid': True,
         'tmin': 20.0, 'tmax': 22.0, 'tvalid': True,
         'pmax': 500, 'rows': rows, 'w': 'ясно'}
    r.update(kw)
    return r


def empty_day(kwh=0, cost=0, rows=0):
    return {'kwh': kwh, 'cost': cost, 'dayKwh': kwh, 'nightKwh': 0, 'vmin': 0, 'vmax': 0,
            'vvalid': False, 'tmin': None, 'tmax': None, 'tvalid': False,
            'pmax': 0, 'rows': rows, 'w': None}


# полный месяц: 1–15 ноября по 0.1*d кВт·ч (сумма 12.0), 16-е — неполные сутки
def full_month(day):
    if day == 16:
        return measured(0.3, rows=40)
    return measured(round(0.1 * day, 3))


class Routes:
    """page.route('**/*') для /data, /days, /history и /range."""

    def __init__(self, range_fn=full_month, data=None, days=None):
        self.range_fn = range_fn
        self.data = data or {'mkwh': 12.3, 'mcost': 98.89, 'tariff': 8.04,
                             'now': '2026-11-16 12:00:00'}
        self.days = days or ['2026-11-16', '2026-11-15', '2026-11-14', '2026-11-13',
                             '2026-11-12', '2026-11-11', '2026-11-10']

    def __call__(self, route):
        u = urlparse(route.request.url)
        if u.path == '/data':
            resp = route.fetch()
            d = resp.json()
            d.update(self.data)
            return route.fulfill(status=200, content_type='application/json',
                                 body=json.dumps(d))
        if u.path == '/days':
            return route.fulfill(status=200, content_type='application/json',
                                 body=json.dumps({'days': self.days, 'ms': 900}))
        if u.path == '/history':
            d = (parse_qs(u.query).get('date') or [''])[0]
            j = json.loads((FIX / 'h30.json').read_text(encoding='utf-8'))
            j['date'] = d
            return route.fulfill(status=200, content_type='application/json',
                                 body=json.dumps(j))
        if u.path == '/range':
            q = parse_qs(u.query)
            frm, tto = q.get('from', [''])[0], q.get('to', [''])[0]
            rows = []
            if self.range_fn is not None and frm and tto:
                rows = day_rows(frm, tto, self.range_fn)
            return route.fulfill(status=200, content_type='application/json',
                                 body=json.dumps({'from': frm, 'to': tto,
                                                  'days': len(rows), 'rows': rows}))
        return route.continue_()


def new_page(browser, routes=None):
    ctx = browser.new_context(viewport={'width': 1280, 'height': 1000})
    ctx.add_init_script(SPY)
    page = ctx.new_page()
    errs = []
    page.on('pageerror', lambda e: errs.append('pageerror: %s' % e))
    page.on('console', lambda m: errs.append('console: %s' % m.text)
            if m.type == 'error' and 'Failed to load resource' not in m.text else None)
    if routes:
        page.route('**/*', routes)
    page.clock.install(time=NOW)
    page.goto(URL, wait_until='domcontentloaded', timeout=30000)
    return ctx, page, errs


def text(page, sel):
    el = page.query_selector(sel)
    return el.evaluate('e => e.textContent') if el else ''


def wait_ready(page):
    page.wait_for_function(
        "() => { var e=document.querySelector('#budNow');"
        " return e && e.textContent.indexOf('Факт за') >= 0; }", timeout=20000)


def load_month(page):
    page.click('[data-p="month"]')
    page.wait_for_function(
        "() => { var e=document.querySelector('#expInfo');"
        " return e && e.textContent.indexOf('Диапазон') >= 0"
        " && e.textContent.indexOf('не выбран') < 0; }", timeout=30000)
    page.wait_for_timeout(400)


def save_limits(page, kwh, rub):
    page.evaluate("document.getElementById('budgetBox').open = true")
    page.wait_for_timeout(150)
    if kwh is not None:
        page.fill('#budKwh', str(kwh))
    if rub is not None:
        page.fill('#budRub', str(rub))
    page.click('#budSave')
    page.wait_for_timeout(300)


def download_csv(page):
    page.evaluate("document.getElementById('exportBox').open = true")
    page.wait_for_timeout(150)
    with page.expect_download(timeout=15000) as dl:
        page.click('#expDl')
    d = dl.value
    path = pathlib.Path(d.path())
    return d.suggested_filename, path.read_bytes()


def main():
    srv = start_server()
    totals = 0
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(channel='chrome')

            # ---------------------------------------------------------------- 1
            print('--- 1: нет лимита, загружен текущий месяц (1–16.11) ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            load_month(page)
            check('блоки «Бюджет и прогноз» и «Экспорт и перенос» внутри historyWorkspace',
                  page.eval_on_selector('#historyWorkspace',
                                        'e => !!e.querySelector("#budgetBox") && !!e.querySelector("#exportBox")'))
            check('оба блока свёрнуты по умолчанию',
                  page.eval_on_selector('#budgetBox', 'e => !e.open')
                  and page.eval_on_selector('#exportBox', 'e => !e.open'))
            bud = text(page, '#budNow')
            check('факт месяца по /data: 12.30 кВт·ч · 98.89 ₽',
                  '12.30 кВт·ч' in bud and '98.89 ₽' in bud, bud[:120])
            check('источник факта указан: /data', 'по данным платы /data' in bud)
            check('лимит не задан — без выдуманных процентов',
                  'не задан' in bud and 'Лимит 0' not in bud)
            check('прогноз: 24.00 кВт·ч · 192.96 ₽ (среднее 0.80 × 30 дней)',
                  '24.00 кВт·ч' in bud and '192.96 ₽' in bud, bud[:260])
            check('метод прогноза указан явно',
                  'среднее по 15 полным суткам с данными × 30 дней' in bud)
            check('покрытие прогноза: 15 из 15 полных суток',
                  '15 из 15 полных суток' in bud)
            note = text(page, '#budNote')
            check('пометка «оценка» и отказ от счёта поставщика',
                  'а не счёт поставщика' in note and 'оценка' in note, note[:160])
            check('приглашение задать лимит при отсутствии лимита',
                  'лимит не задан — задайте добровольный месячный лимит' in note)
            check('тариф прогноза помечен как текущий',
                  'по текущему тарифу платы 8.04' in note)
            check('текущие сутки не включены в среднее прогноза',
                  'сегодняшние сутки ещё идут' in note)
            check('в сводке блока «лимит не задан»', text(page, '#budSub') == 'лимит не задан',
                  text(page, '#budSub'))
            exp = text(page, '#expInfo')
            check('экспорт: диапазон 1 ноября 2026 — 16 ноября 2026',
                  '1 ноября 2026 — 16 ноября 2026' in exp, exp[:120])
            check('экспорт: 16 сут · с данными 16',
                  '16 сут · с данными 16' in exp, exp[:200])
            check('экспорт: имя файла содержит диапазон',
                  'energy-2026-11-01_2026-11-16.csv' in exp)
            check('экспорт: кнопки включены',
                  page.eval_on_selector('#expDl', 'e => !e.disabled')
                  and page.eval_on_selector('#expCopy', 'e => !e.disabled'))
            page.wait_for_timeout(1500)  # дать догрузиться сравнению с прошлым периодом (P2.1)

            def rh():
                return len([r for r in page.evaluate('window.__req')
                            if '/range' in r['u'] or '/history' in r['u']])
            n0 = rh()
            page.click('#budgetBox > summary')
            page.click('#exportBox > summary')
            page.wait_for_timeout(400)
            page.click('#budgetBox > summary')
            page.click('#budgetBox > summary')
            page.wait_for_timeout(700)
            n1 = rh()
            check('открытие/закрытие блоков не запрашивает /range и /history',
                  n1 == n0, 'до %d, после %d' % (n0, n1))
            page.screenshot(path=str(SHOTS / 'p34-budget-full-1280.png'), full_page=True)
            check('JS-ошибок нет (сценарий 1)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 2
            print('--- 2: нет лимита при отсутствии диапазона (прогноз не выдумывается) ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            page.wait_for_timeout(500)
            bud = text(page, '#budNow')
            check('без периода: прогноз не построен', 'оценка не построена' in bud, bud[:180])
            check('без периода: подсказка загрузить «Месяц»/«30 дней»',
                  'нажмите «Месяц» или «30 дней»' in text(page, '#budNote'),
                  text(page, '#budNote')[:180])
            check('без периода: экспорт не выбран',
                  'не выбран' in text(page, '#expInfo'))
            check('без периода: кнопки экспорта выключены',
                  page.eval_on_selector('#expDl', 'e => e.disabled')
                  and page.eval_on_selector('#expCopy', 'e => e.disabled'))
            check('JS-ошибок нет (сценарий 2)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 3
            print('--- 3: лимит 0 — не делим на ноль, не пишем Infinity/NaN ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            load_month(page)
            save_limits(page, 0, None)
            bud = text(page, '#budNow')
            check('лимит 0 кВт·ч: «0 кВт·ч · любой расход выше лимита»',
                  '0 кВт·ч · любой расход выше лимита' in bud, bud[:200])
            check('лимит 0: факт уже больше нуля', 'факт уже больше нуля' in bud)
            check('лимит 0: полоса превышения',
                  page.eval_on_selector_all('#budNow .p34bar.over', 'a => a.length') == 1)
            check('лимит 0: в сводке без процентов', text(page, '#budSub') == 'лимит 0 — любой расход выше',
                  text(page, '#budSub'))
            alltxt = bud + text(page, '#budSub') + text(page, '#budNote')
            check('лимит 0: нет NaN/Infinity/undefined',
                  all('bad' not in alltxt for bad in ('NaN', 'Infinity', 'undefined')))
            check('лимит 0: прогноз всё равно считается',
                  '24.00 кВт·ч' in bud)
            ls = page.evaluate("() => localStorage.getItem('domEnergy.budget.v1')")
            check('лимит 0 сохранён в localStorage', ls and json.loads(ls) == {'kwh': 0, 'rub': None}, str(ls))
            # второй лимит (₽) нулевой тоже
            save_limits(page, None, 0)
            bud = text(page, '#budNow')
            check('лимит 0 ₽: «0 ₽ · любой расход выше лимита»',
                  '0 ₽ · любой расход выше лимита' in bud)
            check('два нулевых лимита: две полосы превышения',
                  page.eval_on_selector_all('#budNow .p34bar.over', 'a => a.length') == 2)
            page.screenshot(path=str(SHOTS / 'p34-limit-zero-1280.png'), full_page=True)
            check('JS-ошибок нет (сценарий 3)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 4
            print('--- 4: неполный месяц (сначала «Месяц», потом «7 дней») ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            load_month(page)
            check('смена периода: на «Месяце» прогноз 24.00 кВт·ч',
                  '24.00 кВт·ч' in text(page, '#budNow'), text(page, '#budNow')[:200])
            page.click('[data-p="d7"]')
            page.wait_for_function(
                "() => { var e=document.querySelector('#expInfo');"
                " return e && e.textContent.indexOf('2026-11-10') >= 0; }", timeout=30000)
            page.wait_for_timeout(400)
            bud = text(page, '#budNow')
            # полные сутки 1–15, из них загружены 10–15: 6 суток, среднее 1.25, прогноз 37.50
            check('смена периода: на «7 днях» прогноз пересчитан на 37.50 кВт·ч',
                  '37.50 кВт·ч' in bud, bud[:280])
            check('неполный месяц: покрытие «6 из 15 полных суток»',
                  '6 из 15 полных суток' in bud)
            check('неполный месяц: указано, что в периоде 6 из 15',
                  'в загруженном периоде 6 из 15' in bud)
            note = text(page, '#budNote')
            check('неполный месяц: предупреждение о смещении оценки',
                  'прогноз по неполному покрытию: суток с данными 6 из 15 полных' in note, note[:200])
            check('неполный месяц: экспорт показывает 7 суток',
                  '7 сут · с данными 7' in text(page, '#expInfo'), text(page, '#expInfo')[:120])
            check('JS-ошибок нет (сценарий 4)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 5
            print('--- 5: 0 кВт·ч за месяц — это ноль, а не «нет данных» ---')
            ctx, page, errs = new_page(browser, Routes(
                range_fn=lambda day: measured(0, cost=0, pmax=0) if day else None,
                data={'mkwh': 0, 'mcost': 0, 'tariff': 8.04, 'now': '2026-11-16 12:00:00'}))
            wait_ready(page)
            load_month(page)
            save_limits(page, 5, None)
            bud = text(page, '#budNow')
            check('0 кВт·ч: факт 0.00, прогноз 0.00', '0.00 кВт·ч' in bud and bud.count('0.00 кВт·ч') >= 2, bud[:240])
            check('0 кВт·ч: прогресс 0.00 / 5.00 · 0 %',
                  '0.00 / 5.00 кВт·ч · 0 %' in bud, bud[:240])
            check('0 кВт·ч: оценка 0 помечена как ноль, а не пропуск',
                  'показали 0 кВт·ч — оценка 0, это не «нет данных»' in text(page, '#budNote'),
                  text(page, '#budNote')[:200])
            check('0 кВт·ч: в среднее 0, деления на ноль нет', 'NaN' not in bud + text(page, '#budSub'))
            check('JS-ошибок нет (сценарий 5)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 6
            print('--- 6: пустой диапазон — экспорт не делает фиктивный файл ---')
            ctx, page, errs = new_page(browser, Routes(range_fn=None))
            wait_ready(page)
            page.click('[data-p="month"]')
            page.wait_for_function(
                "() => { var e=document.querySelector('#expInfo');"
                " return e && e.textContent.indexOf('суток с данными нет') >= 0; }", timeout=20000)
            exp = text(page, '#expInfo')
            check('пустой диапазон: «суток с данными нет — файл был бы пустым»',
                  'суток с данными нет — файл был бы пустым' in exp, exp[:160])
            check('пустой диапазон: кнопки выключены',
                  page.eval_on_selector('#expDl', 'e => e.disabled')
                  and page.eval_on_selector('#expCopy', 'e => e.disabled'))
            dialogs = []
            page.on('dialog', lambda d: (dialogs.append(d.message), d.dismiss()))
            got_dl = []
            page.on('download', lambda d: got_dl.append(d.suggested_filename))
            page.evaluate("document.getElementById('exportBox').open = true")
            page.wait_for_timeout(150)
            page.evaluate("document.getElementById('expDl').click()")
            page.wait_for_timeout(500)
            check('пустой диапазон: клик по выключенной кнопке не качает файл', not got_dl)
            # старая кнопка «CSV за период» — честное сообщение вместо пустого файла
            page.click('#rngCsv')
            page.wait_for_timeout(500)
            check('пустой диапазон: старая кнопка CSV пишет «нет данных за период»',
                  dialogs and 'нет данных за период' in dialogs[0], str(dialogs)[:100])
            check('пустой диапазон: файл не скачан и со старой кнопки', not got_dl)
            check('JS-ошибок нет (сценарий 6)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 7
            print('--- 7: CSV — BOM, единицы, пустые пропуски, null-поля, имя файла ---')

            def csv_month(day):
                if day == 10:  # измерено, но все значения — null
                    return measured(None, cost=None, rows=120, dayKwh=None, nightKwh=None,
                                    dayCost=None, nightCost=None, vmin=None, vmax=None,
                                    tmin=None, tmax=None, pmax=None, w=None)
                if day == 11:  # пустые сутки: rows 0 и нули не должны стать нулями
                    return empty_day()
                if day == 12:  # невалидные V/T и «;» в погоде
                    return measured(1.5, vmin=220, vmax=260, vvalid=False,
                                    tmin=10, tmax=30, tvalid=False, w='ясно; дождь')
                if day == 16:
                    return measured(0.3, rows=40)
                return measured(round(0.1 * day, 3))

            ctx, page, errs = new_page(browser, Routes(range_fn=csv_month))
            wait_ready(page)
            load_month(page)
            name, data = download_csv(page)
            check('CSV: имя файла содержит диапазон',
                  name == 'energy-2026-11-01_2026-11-16.csv', name)
            check('CSV: BOM UTF-8 в начале', data.startswith(b'\xef\xbb\xbf'))
            txt = data.decode('utf-8-sig')
            lines = txt.rstrip('\r\n').split('\r\n')
            check('CSV: заголовок с единицами', lines[0] == HEAD, lines[0][:120])
            check('CSV: 16 суток + заголовок', len(lines) == 17, '%d строк' % len(lines))
            check('CSV: в каждой строке 15 колонок',
                  all(l.count(';') == 14 for l in lines))
            by_day = {l.split(';')[0]: l.split(';') for l in lines[1:]}
            check('CSV: строка 10.11 — null-поля пустые, не 0 и не null',
                  by_day['2026-11-10'][2:13] == [''] * 11
                  and by_day['2026-11-10'][13] == '120')
            check('CSV: пустые сутки 11.11 — все значения пустые, rows=0',
                  by_day['2026-11-11'][2:13] == [''] * 11
                  and by_day['2026-11-11'][13] == '0')
            check('CSV: vvalid=false → vmin/vmax пустые',
                  by_day['2026-11-12'][8] == '' and by_day['2026-11-12'][9] == '')
            check('CSV: tvalid=false → tmin/tmax пустые',
                  by_day['2026-11-12'][10] == '' and by_day['2026-11-12'][11] == '')
            check('CSV: «;» в погоде не ломает колонки',
                  by_day['2026-11-12'][14] == 'ясно дождь'
                  and len(by_day['2026-11-12']) == 15)
            check('CSV: обычные сутки — числовые поля с точкой',
                  by_day['2026-11-05'][2] == '0.5' and by_day['2026-11-05'][3] == '4.02')
            check('CSV: временная зона device-local в каждой строке',
                  all(l.split(';')[1] == 'device-local' for l in lines[1:]))
            check('CSV: нет null/undefined/NaN в файле',
                  all(bad not in txt for bad in ('null', 'undefined', 'NaN')))
            name2, data2 = download_csv(page)
            check('CSV: повторный экспорт даёт тот же файл (идемпотентность)',
                  name2 == name and data2 == data)
            reqs = page.evaluate('window.__req')
            methods = sorted({r['m'] for r in reqs})
            check('CSV: только чтение — ни одного POST/PUT/DELETE',
                  all(m in ('GET', 'HEAD') for m in methods), str(methods))
            page.screenshot(path=str(SHOTS / 'p34-export-1280.png'), full_page=True)
            check('JS-ошибок нет (сценарий 7)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 8
            print('--- 8: лимит переживает перезагрузку страницы (localStorage) ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            load_month(page)
            save_limits(page, 4, 100)
            check('после сохранения: процент и превышение по кВт·ч',
                  '12.30 / 4.00 кВт·ч · 308 %' in text(page, '#budNow')
                  and 'превышен на 8.30 кВт·ч' in text(page, '#budNow'),
                  text(page, '#budNow')[:200])
            check('после сохранения: сводка «факт 12.30 из 4.00 кВт·ч · 308 %»',
                  text(page, '#budSub') == 'факт 12.30 из 4.00 кВт·ч · 308 %',
                  text(page, '#budSub'))
            page.reload(wait_until='domcontentloaded')
            wait_ready(page)
            page.wait_for_timeout(600)
            check('после перезагрузки: поля лимита на месте',
                  page.eval_on_selector('#budKwh', 'e => e.value') == '4'
                  and page.eval_on_selector('#budRub', 'e => e.value') == '100')
            check('после перезагрузки: прогресс считается из localStorage',
                  '12.30 / 4.00 кВт·ч · 308 %' in text(page, '#budNow'))
            save_limits(page, 4, 100)  # открыть заново после reload
            page.click('#budReset')
            page.wait_for_timeout(300)
            check('«Убрать» очищает лимит и поля',
                  page.eval_on_selector('#budKwh', 'e => e.value') == ''
                  and 'не задан' in text(page, '#budNow')
                  and text(page, '#budSub') == 'лимит не задан')
            check('«Убрать» очищает localStorage',
                  page.evaluate("() => localStorage.getItem('domEnergy.budget.v1')") is None)
            check('JS-ошибок нет (сценарий 8)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # ---------------------------------------------------------------- 9
            print('--- 9: печать — диапазон и легенда не теряются ---')
            ctx, page, errs = new_page(browser, Routes())
            wait_ready(page)
            load_month(page)
            page.emulate_media(media='print')
            page.evaluate("window.dispatchEvent(new Event('beforeprint'))")
            page.wait_for_timeout(300)
            meta = text(page, '#printMeta')
            check('печать: в шапке отчёта выбранный диапазон',
                  '2026-11-01 — 2026-11-16' in meta, meta[:160])
            check('печать: в шапке тариф и время формирования',
                  'тариф:' in meta and 'сформировано' in meta)
            disp = page.evaluate("""() => {
              function d(s){var e=document.querySelector(s);return e?getComputedStyle(e).display:'none'}
              return {legend:d('#historyWorkspace .history-legend'),
                      chart:d('#historyWorkspace .history-combined'),
                      table:d('#historyWorkspace .rt'),
                      controls:d('#rngControls'),
                      p2boxes:d('#historyWorkspace .p2box')};
            }""")
            check('печать: легенда серий не скрыта', disp['legend'] != 'none', str(disp))
            check('печать: график и таблица периода видны',
                  disp['chart'] != 'none' and disp['table'] != 'none', str(disp))
            check('печать: кнопки периода скрыты', disp['controls'] == 'none')
            check('печать: экранные P2-блоки (без фона нечитаемы) скрыты', disp['p2boxes'] == 'none',
                  str(disp['p2boxes']))
            page.pdf(path=str(SHOTS / 'p34-print-period.pdf'))
            check('печать: PDF сформирован',
                  (SHOTS / 'p34-print-period.pdf').stat().st_size > 2000)
            page.emulate_media(media='screen')
            check('JS-ошибок нет (сценарий 9)', not errs, '; '.join(errs)[:200])
            ctx.close()

            # --------------------------------------------------------------- 10
            print('--- 10: мобильные 390 px ---')
            ctx = browser.new_context(viewport={'width': 390, 'height': 844})
            ctx.add_init_script(SPY)
            page = ctx.new_page()
            page.route('**/*', Routes())
            page.clock.install(time=NOW)
            page.goto(URL, wait_until='domcontentloaded', timeout=30000)
            wait_ready(page)
            page.click('[data-p="month"]')
            page.wait_for_timeout(2500)
            sw0 = page.evaluate('document.documentElement.scrollWidth')
            page.evaluate("document.getElementById('budgetBox').open = true;"
                          " document.getElementById('exportBox').open = true;")
            page.wait_for_timeout(600)
            sw = page.evaluate('document.documentElement.scrollWidth')
            wide = page.evaluate("""() => {
              var W = document.documentElement.clientWidth, out = [];
              document.querySelectorAll('body *').forEach(function(e){
                var r = e.getBoundingClientRect();
                if(r.right > W + 2) out.push([e.tagName, e.id || e.className,
                    Math.round(r.left), Math.round(r.right),
                    (e.textContent || '').slice(0, 40)]);
              });
              return out.slice(0, 8);
            }""")
            check('390 px: нет горизонтального скролла', sw <= 390,
                  'scrollWidth %s; вылезает: %s' % (sw, wide))
            page.screenshot(path=str(SHOTS / 'p34-mobile-390.png'), full_page=True)
            ctx.close()

            browser.close()
    finally:
        stop_server(srv)

    ok = sum(1 for _, c, _ in RESULTS if c)
    totals = len(RESULTS)
    print('\nИТОГО: %d проверок, %d OK, %d FAIL' % (totals, ok, totals - ok))
    return 0 if ok == totals else 1


if __name__ == '__main__':
    sys.exit(main())
