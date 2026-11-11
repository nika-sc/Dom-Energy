"""Собирает прошивочные .h из исходников страницы.

Запуск:  python web/build_page.py

Читает:
    web/page.html  ->  ../page_html.h  (PAGE_TMPL, плейсхолдеры не подставлены)
    web/app.css    ->  ../assets.h     (APP_CSS)
    web/app.js     ->  ../assets.h     (APP_JS)

CSS и JS вынесены из HTML в отдельные файлы: страница отдаётся с платы
двумя дополнительными запросами /app.css и /app.js, поэтому ответ на "/"
не упирается в лимит 64 КБ. Шаблон страницы тоже лежит в отдельном .h,
поэтому cydv4.ino этот скрипт не трогает.
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
SRC_HTML = HERE / 'page.html'
SRC_CSS = HERE / 'app.css'
SRC_JS = HERE / 'app.js'
OUT_HTML = ROOT / 'page_html.h'
OUT_ASSETS = ROOT / 'assets.h'

# Каждый из app.css / app.js должен быть меньше 40 КБ: держим запас
# до лимита ответа и не раздуваем PROGMEM.
ASSET_LIMIT = 48 * 1024

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

    # --- проверки до записи, чтобы не оставить битые .h ---
    if ')HTML"' in html:
        fail('page.html содержит )HTML" — сломает raw-строку PAGE_TMPL')
    if ')CSS"' in css:
        fail('app.css содержит )CSS" — сломает raw-строку APP_CSS')
    if ')JS"' in js:
        fail('app.js содержит )JS" — сломает raw-строку APP_JS')

    for name, text in (('app.css', css), ('app.js', js)):
        size = nbytes(text)
        if size >= ASSET_LIMIT:
            fail('%s: %d байт, а нужно меньше %d (40 КБ)'
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

    # --- assets.h: две raw-строки для /app.css и /app.js ---
    assets_h = ('#pragma once\n'
                '#include <Arduino.h>\n\n'
                '// Сгенерировано из web/app.css и web/app.js - НЕ ПРАВИТЬ ВРУЧНУЮ\n'
                '// (python web/build_page.py).\n'
                'static const char APP_CSS[] PROGMEM = R"CSS(\n'
                + css +
                '\n)CSS";\n\n'
                'static const char APP_JS[] PROGMEM = R"JS(\n'
                + js +
                '\n)JS";\n')

    OUT_HTML.write_text(page_h, encoding='utf-8', newline='\n')
    OUT_ASSETS.write_text(assets_h, encoding='utf-8', newline='\n')

    print('  web/page.html   : %6d байт (%d символов)'
          % (nbytes(html), len(html)))
    print('  web/app.css     : %6d байт (%d символов)'
          % (nbytes(css), len(css)))
    print('  web/app.js      : %6d байт (%d символов)'
          % (nbytes(js), len(js)))
    print('  page_html.h     : %6d байт' % nbytes(page_h))
    print('  assets.h        : %6d байт' % nbytes(assets_h))
    print('  плейсхолдеры    : %d/%d на месте'
          % (len(PLACEHOLDERS) - len(missing), len(PLACEHOLDERS)))


if __name__ == '__main__':
    main()
