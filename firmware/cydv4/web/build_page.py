"""Вставляет web/page.html в cydv4.ino между маркерами PAGE_TMPL.

Запуск:  python web/build_page.py

Шаблон держим внутри cydv4.ino, чтобы архив прошивки (один файл) был полным.
Править страницу удобнее в web/page.html, потом прогнать этот скрипт.
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / 'page.html'
INO = HERE.parent / 'cydv4.ino'
START = '// >>> PAGE_TMPL'
END = '// <<< PAGE_TMPL'

PLACEHOLDERS = [
    '__FW__', '__NETCLS__', '__NETTXT__', '__RSSI__',
    '__TKWH__', '__TCOST__', '__MV__', '__MP__', '__MA__', '__MT__',
    '__RELCLS__', '__RELTXT__', '__TAR__', '__PE__',
    '__YKWH__', '__YKOST__', '__MKWH__', '__MCOST__',
    '__TARDAY__', '__TARNIGHT__',
    '__DATE__', '__HOUR__',
]


def main():
    html = SRC.read_text(encoding='utf-8').replace('\r\n', '\n')
    # Шаблон пишем в отдельный .h: Arduino-препроцессор ломает генерацию
    # прототипов, когда видит HTML внутри raw-строки в .ino.
    hdr = HERE.parent / 'page_html.h'
    hdr.write_text('#pragma once\n#include <Arduino.h>\n\n'
                   '// Сгенерировано из web/page.html - НЕ ПРАВИТЬ ВРУЧНУЮ.\n'
                   'static const char PAGE_TMPL[] PROGMEM = R"HTML(\n'
                   + html + ')HTML";\n', encoding='utf-8')
    print('  page_html.h обновлён')
    if ')HTML"' in html:
        sys.exit('page.html содержит )HTML" — сломает raw-строку')
    found = set(re.findall(r'__[A-Z_]+__', html))
    missing = [p for p in PLACEHOLDERS if p not in html]
    extra = sorted(found - set(PLACEHOLDERS))
    if missing:
        sys.exit('в page.html нет плейсхолдеров: %s' % ', '.join(missing))
    if extra:
        sys.exit('неизвестные плейсхолдеры: %s' % ', '.join(extra))

    block = (START + ' — генерируется из web/page.html (python web/build_page.py)\n'
             '// Шаблон держим в этом же файле, чтобы бэкап прошивки был полным.\n'
             'static const char PAGE_TMPL[] PROGMEM = R"HTML(\n'
             + html +
             ')HTML";\n' + END)

    src = INO.read_text(encoding='utf-8')
    i = src.find(START)
    j = src.find(END)
    if i < 0 or j < 0 or j < i:
        sys.exit('в cydv4.ino нет маркеров PAGE_TMPL')
    src = src[:i] + block + src[j + len(END):]
    INO.write_text(src, encoding='utf-8', newline='\n')
    print('cydv4.ino: шаблон обновлён, html %d байт (utf-8 %d), плейсхолдеров %d'
          % (len(html), len(html.encode('utf-8')), len(PLACEHOLDERS)))


if __name__ == '__main__':
    main()
