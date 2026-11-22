#!/usr/bin/env python3
"""Read-only compact diagnosis of Dom Energy. No external dependencies."""
import argparse
import collections
import json
import os
import pathlib
import re
import sys
import urllib.request

def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.1.51')
    parser.add_argument('--log', default=str(pathlib.Path(os.environ.get('APPDATA', '')) / 'DomMonitor' / 'app.log'))
    args = parser.parse_args()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(args.url.rstrip('/') + '/data', timeout=10) as response:
            data = json.load(response)
        print('ПЛАТА:', json.dumps({k: data.get(k) for k in ('fw','v','p','temp','r','sdRows','rssi','up','heap','loopMaxAll','blk','blkMs','rr')}, ensure_ascii=False))
    except Exception as exc:
        print('ПЛАТА: недоступна:', type(exc).__name__, str(exc)[:180])
    path = pathlib.Path(args.log)
    try:
        with path.open('r', encoding='utf-8-sig', errors='replace') as f:
            lines = collections.deque(f, maxlen=1500)
        errors = [line.strip() for line in lines if re.search(r'ошибка|ошибки|потерян|не отвечает|exception|stack.?overflow|падени', line, re.I)]
        print('ЖУРНАЛ: просмотрено последних строк:', len(lines), '; совпадений:', len(errors))
        for line in errors[-5:]:
            print(' ', re.sub(r'(token|password|secret|ключ)\s*[:=]\s*\S+', r'\1=[скрыто]', line, flags=re.I)[:350])
        if lines: print('ПОСЛЕДНЯЯ СТРОКА:', re.sub(r'(token|password|secret|ключ)\s*[:=]\s*\S+', r'\1=[скрыто]', lines[-1].strip(), flags=re.I)[:350])
    except OSError as exc:
        print('ЖУРНАЛ: недоступен:', str(exc)[:160])
    return 0

if __name__ == '__main__':
    sys.exit(main())
