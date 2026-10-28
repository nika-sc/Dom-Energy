"""Бэкап истории с SD-карты платы по Wi-Fi.

Безопасность: только GET-запросы к плате. Карту стереть невозможно —
все файловые эндпоинты прошивки открывают файл в режиме FILE_READ.

Запуск:
    python pc/backup-sd.py               снимок с датой в имени
    python pc/backup-sd.py --latest      переписать energy-latest.csv (для ежечаса)
    python pc/backup-sd.py --list        список копий
    python pc/backup-sd.py --merge       слить всё в один файл без дублей
    python pc/backup-sd.py --all         снимок + latest + merge (полный цикл)

Адрес платы: переменная окружения BOARD_URL (например,
http://192.168.1.50). Куда писать: BACKUP_DIR (по умолчанию — папка
backup-sd рядом со скриптом), необязательная копия на NAS — BACKUP_NAS.
"""
import io
import json
import os
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime

BOARD = os.environ.get('BOARD_URL', 'http://192.168.1.50').rstrip('/')
LOCAL = os.environ.get(
    'BACKUP_DIR',
    os.path.join(os.path.dirname(os.path.abspath(__file__)), 'backup-sd'))
NAS = os.environ.get('BACKUP_NAS', '')
TRIES = 3


def targets():
    dirs = [LOCAL]
    if NAS and os.path.isdir(os.path.dirname(NAS) or NAS):
        dirs.append(NAS)
    return dirs


def ensure_dirs():
    for d in targets():
        os.makedirs(d, exist_ok=True)
        for sub in ('daily', 'monthly'):
            os.makedirs(os.path.join(d, sub), exist_ok=True)


def get(path, timeout=120):
    """GET с тремя попытками: веб-сервер платы иногда рвёт соединение."""
    last = None
    for i in range(TRIES):
        try:
            with urllib.request.urlopen(BOARD + path, timeout=timeout) as r:
                return r.read().decode('utf-8', 'replace')
        except Exception as exc:
            last = exc
            time.sleep(1.5 * (i + 1))
    raise last


def write_all(name, text):
    paths = []
    for d in targets():
        path = os.path.join(d, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with io.open(path, 'w', encoding='utf-8', newline='') as fh:
            fh.write(text)
        paths.append(path)
    return paths


def valid_rows(body):
    """Только строки нового формата: дата,вольты,ватты,кВт·ч,реле,руб,темп"""
    out = []
    for line in body.splitlines():
        line = line.strip()
        if len(line) < 20:
            continue
        if not (line.startswith('20') and line[4] == '-' and line[7] == '-'):
            continue
        if line.count(',') < 5:
            continue
        out.append(line)
    return out


def history_files():
    """Рабочий файл + месячные архивы (после ротации на плате)."""
    try:
        data = json.loads(get('/files', timeout=20))
        names = [str(f.get('name')) for f in data.get('files', []) if f.get('name')]
        if names:
            return sorted(set(names))
    except Exception as exc:                            # noqa: BLE001
        print('  /files недоступен (%s) — читаю только energy.csv' % exc)
    return ['energy.csv']


def read_all_history():
    """Все файлы истории, строки без дублей, по возрастанию даты."""
    seen = set()
    for name in history_files():
        try:
            body = get('/full?file=' + urllib.parse.quote(name), timeout=180)
        except Exception as exc:                        # noqa: BLE001
            print('  %s: ошибка чтения (%s)' % (name, exc))
            continue
        rows = valid_rows(body)
        print('  %s: %d строк' % (name, len(rows)))
        seen.update(rows)
    return sorted(seen)


def backup(latest=False, daily=False):
    ensure_dirs()
    print('  читаю историю с платы...')
    rows = read_all_history()
    if len(rows) < 3:
        print('  на плате нет данных (%d строк) — пропускаю' % len(rows))
        return False

    clean = 'datetime,voltage,power,energy_kwh,relay,cost,temp\n' + '\n'.join(rows) + '\n'
    now = datetime.now()
    names = []
    if latest:
        names.append('energy-latest.csv')
    else:
        names.append('energy-%s.csv' % now.strftime('%Y-%m-%d_%H%M'))
    if daily:
        names.append(os.path.join('daily', 'energy-%s.csv' % now.strftime('%Y-%m-%d')))

    for name in names:
        paths = write_all(name, clean)
        print('  записано: %s (%d строк)' % (paths[0], len(rows)))

    summary = [
        'время: %s' % now.strftime('%Y-%m-%d %H:%M:%S'),
        'строк: %d' % len(rows),
        'первая: %s' % rows[0],
        'последняя: %s' % rows[-1],
        'по часам: %s' % get('/day', 30).strip(),
        'данные: %s' % get('/data', 30).strip(),
    ]
    write_all('energy-latest.txt', '\n'.join(summary) + '\n')
    print('  сводка: %s' % summary[4][:130])
    return True


def list_backups():
    ensure_dirs()
    for folder, title in ((LOCAL, 'локально'), (os.path.join(LOCAL, 'daily'), 'дневные')):
        if not os.path.isdir(folder):
            continue
        files = sorted(f for f in os.listdir(folder) if f.endswith('.csv'))
        print('  %s: %d файлов' % (title, len(files)))
        for f in files[-8:]:
            p = os.path.join(folder, f)
            with io.open(p, encoding='utf-8') as fh:
                n = sum(1 for _ in fh)
            print('    %-26s %6.1f КБ  %5d строк' % (f, os.path.getsize(p) / 1024, n))


def merge():
    """Слияние всех копий. Источники НЕ изменяются, результат — только energy-all.csv."""
    ensure_dirs()
    files = []
    for folder in (LOCAL, os.path.join(LOCAL, 'daily'), os.path.join(LOCAL, 'monthly')):
        if os.path.isdir(folder):
            files += [os.path.join(folder, f) for f in sorted(os.listdir(folder))
                      if f.endswith('.csv') and f != 'energy-all.csv']

    seen = set()
    for path in files:
        with io.open(path, encoding='utf-8') as fh:
            for line in fh:
                line = line.strip()
                # строго формат ДАТЫ: 2026-10-29 08:15:00,...
                if len(line) < 20:
                    continue
                if not (line.startswith('20') and line[4] == '-' and line[7] == '-'):
                    continue          # старый формат с uptime — пропускаем
                if line.count(',') < 5:
                    continue
                seen.add(line)

    rows = sorted(seen)
    text = 'datetime,voltage,power,energy_kwh,relay,cost,temp\n' + '\n'.join(rows) + '\n'
    for p in write_all('energy-all.csv', text):
        print('  слито %d строк → %s' % (len(rows), p))


if __name__ == '__main__':
    if '--help' in sys.argv or '-h' in sys.argv:
        print(__doc__)
        sys.exit(0)
    try:
        if '--list' in sys.argv:
            list_backups()
        elif '--merge' in sys.argv:
            merge()
        elif '--all' in sys.argv:
            backup(latest=True, daily=True)
            merge()
        elif '--latest' in sys.argv:
            backup(latest=True)
        else:
            backup(latest=False, daily=True)
    except Exception as exc:
        print('  ОШИБКА: %s' % exc)
        sys.exit(1)
