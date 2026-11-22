"""Собирает прошивочные .h из исходников страницы.

Запуск:  python web/build_page.py

Читает:
    web/page.html  ->  ../page_html.h  (PAGE_TMPL, плейсхолдеры не подставлены)
    web/app.css    ->  ../assets.h     (APP_CSS)
    web/app.js     ->  ../assets.h     (APP_JS)
    web/history.js ->  ../assets.h     (HISTORY_JS)
    web/control.js ->  ../assets.h     (CONTROL_JS)

CSS и JS вынесены из HTML в отдельные файлы: страница отдаётся с платы
запросами /app.css, /app.js, /history.js и /control.js, поэтому ответ на "/"
не упирается в лимит 64 КБ. Шаблон страницы тоже лежит в отдельном .h,
поэтому cydv4.ino этот скрипт не трогает.
"""
import pathlib
import hashlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
SRC_HTML = HERE / 'page.html'
SRC_CSS = HERE / 'app.css'
SRC_JS = HERE / 'app.js'
OUT_HTML = ROOT / 'page_html.h'
OUT_ASSETS = ROOT / 'assets.h'

# Каждый ресурс должен быть меньше 63 КБ: держим запас
# до лимита ответа и не раздуваем PROGMEM.
ASSET_LIMIT = 63 * 1024
# Шаблон страницы (page_html.h) отдаётся на "/" — держим его под 96 000 байт:
# столько остаётся под ответ платы с запасом на подстановки.
PAGE_CAP = 96000

PLACEHOLDERS = [
    '__FW__', '__NETCLS__', '__NETTXT__', '__RSSI__',
    '__TKWH__', '__TCOST__', '__MP__',
    '__RELCLS__', '__RELTXT__', '__TAR__', '__PE__',
    '__YKWH__', '__YKOST__', '__MKWH__', '__MCOST__',
    '__TARDAY__', '__TARNIGHT__',
    '__DATE__', '__HOUR__',
]


def fail(msg):
    sys.exit('build_page: ' + msg)


def read_source(path):
    if not path.is_file():
        fail('нет файла %s' % path)
    return path.read_text(encoding='utf-8').replace('\r\n', '\n')


def nbytes(text):
    return len(text.encode('utf-8'))


def main():
    html = read_source(SRC_HTML)
    css = read_source(SRC_CSS)
    js = read_source(SRC_JS)
    history = read_source(HERE / 'history.js')
    control = read_source(HERE / 'control.js')

    # --- проверки до записи, чтобы не оставить битые .h ---
    if ')HTML"' in html:
        fail('page.html содержит )HTML" — сломает raw-строку PAGE_TMPL')
    if ')CSS"' in css:
        fail('app.css содержит )CSS" — сломает raw-строку APP_CSS')
    if ')JS"' in js:
        fail('app.js содержит )JS" — сломает raw-строку APP_JS')
    if ')HJS"' in history:
        fail('history.js содержит )HJS" — сломает raw-строку HISTORY_JS')
    if ')CTL"' in control:
        fail('control.js contains a raw-string terminator')

    for name, text in (('app.css', css), ('app.js', js), ('history.js', history), ('control.js', control)):
        size = nbytes(text)
        if size >= ASSET_LIMIT:
            fail('%s: %d байт, а нужно меньше %d'
                 % (name, size, ASSET_LIMIT))

    found = set(re.findall(r'__[A-Z_]+__', html))
    missing = [p for p in PLACEHOLDERS if p not in html]
    extra = sorted(found - set(PLACEHOLDERS))
    if missing:
        fail('в page.html нет плейсхолдеров: %s' % ', '.join(missing))
    if extra:
        fail('неизвестные плейсхолдеры: %s' % ', '.join(extra))

    # --- page_html.h: только разметка, без CSS/JS ---
    page_h = ('#pragma once\n'
              '#include <Arduino.h>\n\n'
              '// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ\n'
              '// (python web/build_page.py). CSS и JS — в assets.h.\n'
              'static const char PAGE_TMPL[] PROGMEM = R"HTML(\n'
              + html +
              ')HTML";\n')
    if nbytes(page_h) >= PAGE_CAP:
        fail('page_html.h: %d байт, а нужно меньше %d (page.html %d байт)'
             % (nbytes(page_h), PAGE_CAP, nbytes(html)))

    # --- assets.h: отдельные raw-строки для каждого ресурса ---
    assets_h = ('#pragma once\n'
                '#include <Arduino.h>\n\n'
                '// Сгенерировано из web/app.css, web/app.js и web/history.js - НЕ ПРАВИТЬ ВРУЧНУЮ\n'
                '// (python web/build_page.py).\n'
                'static const char APP_CSS[] PROGMEM = R"CSS(\n'
                + css +
                '\n)CSS";\n\n'
                'static const char APP_JS[] PROGMEM = R"JS(\n'
                + js +
                '\n)JS";\n\n'
                'static const char HISTORY_JS[] PROGMEM = R"HJS(\n'
                + history +
                '\n)HJS";\n\n'
                'static const char CONTROL_JS[] PROGMEM = R"CTL(\n'
                + control + '\n)CTL";\n')

    OUT_HTML.write_text(page_h, encoding='utf-8', newline='\n')
    OUT_ASSETS.write_text(assets_h, encoding='utf-8', newline='\n')
    identity = hashlib.sha256()
    sources = [ROOT / 'cydv4.ino'] + sorted(ROOT.glob('*.h')) + sorted(HERE.glob('*.js')) + sorted(HERE.glob('*.css')) + [SRC_HTML]
    for path in sources:
        if path.name in ('assets.h','page_html.h','build_identity.h') or 'secret' in path.name.lower():
            continue
        identity.update(path.relative_to(ROOT).as_posix().encode())
        identity.update(path.read_bytes().replace(b'\r\n', b'\n'))
    build_id = identity.hexdigest()[:20]
    (ROOT / 'build_identity.h').write_text('#pragma once\n#define DOM_BUILD_ID "' + build_id + '"\n', encoding='utf-8')
    print('  build ID        : ' + build_id)

    print('  web/page.html   : %6d байт (%d символов)'
          % (nbytes(html), len(html)))
    print('  web/app.css     : %6d байт (%d символов)'
          % (nbytes(css), len(css)))
    print('  web/app.js      : %6d байт (%d символов)'
          % (nbytes(js), len(js)))
    print('  web/history.js  : %6d байт (%d символов)'
          % (nbytes(history), len(history)))
    print('  page_html.h     : %6d байт (лимит %d)' % (nbytes(page_h), PAGE_CAP))
    print('  assets.h        : %6d байт' % nbytes(assets_h))
    print('  плейсхолдеры    : %d/%d на месте'
          % (len(PLACEHOLDERS) - len(missing), len(PLACEHOLDERS)))


if __name__ == '__main__':
    main()
