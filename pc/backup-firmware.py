#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backup-firmware.py — архив прошивок проекта «Дом — энергия».

Ничего не теряется: каждая копия кладётся в отдельную папку с датой, временем
и коротким комментарием, рядом — manifest.json с размерами и sha256.

Структура архива:

    <архив>\\cydv4\\2026-10-28_2010_первый-архив\\cydv4.ino
    <архив>\\cydv4\\2026-10-28_2010_первый-архив\\font5x7.h
    <архив>\\cydv4\\2026-10-28_2010_первый-архив\\manifest.json

Команды:

    python pc/backup-firmware.py                     # заархивировать прошивку
    python pc/backup-firmware.py --comment "текст"   # с комментарием
    python pc/backup-firmware.py --only cydv4        # только cydv4
    python pc/backup-firmware.py --list              # список всех версий
    python pc/backup-firmware.py --restore 2026-10-28_2010_первый-архив
    python pc/backup-firmware.py --restore 4.0 --to firmware/cydv4
    python pc/backup-firmware.py --verify            # проверить sha256 всех копий

Куда пишем (по порядку, первый доступный):
    1. диск архива (FW_ARCHIVE)              — сетевой диск NAS
    2. шара по UNC (FW_ARCHIVE_UNC)          — то же место по имени
    3. pc\\firmware-archive                    — локально, если NAS недоступен

Пути можно поменять в файле firmware-archive.json рядом со скриптом
или ключами --archive / --local. secrets.h в архив НЕ попадает.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from datetime import datetime

# ---------------------------------------------------------------- константы
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CONFIG = os.path.join(HERE, 'firmware-archive.json')

NAS_DRIVE = os.environ.get('FW_ARCHIVE', os.path.join(
    os.environ.get('APPDATA', os.path.expanduser('~')), 'DomMonitor', 'firmware-archive'))
NAS_UNC = os.environ.get('FW_ARCHIVE_UNC', r'\\NAS\Backup\firmware-archive')
LOCAL_DIR = os.path.join(HERE, 'firmware-archive')

# что архивируем: имя папки в архиве -> описание источника
SOURCES = {
    'cydv4': {
        'dir': os.path.join(ROOT, 'firmware', 'cydv4'),
        'main': 'cydv4.ino',
        'board': 'ESP32-2432S028 (CYD, большая плата с экраном 2.8")',
        'note': 'текущая прошивка: экран, погода, SD, облако Tuya',
    },
}

# версия прошивки: две формы записи, встречаются обе
VER_PATTERNS = (
    re.compile(r'const\s+char\s*\*\s*FW_VER\s*=\s*"([^"]+)"'),
    re.compile(r'const\s+char\s*\*\s*FW_VER\s*\[\s*\]\s*=\s*"([^"]+)"'),
    re.compile(r'#define\s+FW_VERSION\s+"([^"]+)"'),
    re.compile(r'const\s+char\s*\*\s*FW_VERSION\s*=\s*"([^"]+)"'),
)

# файлы, которые копируем всегда вместе с основным (заголовки, ресурсы)
EXTRA_EXT = ('.h', '.hpp', '.c', '.cpp', '.json')
# старые копии — только по ключу --with-baks
BAK_MASK = re.compile(r'\.bak(\d+|-work|-restore)?$', re.IGNORECASE)


def say(text=''):
    print(text, flush=True)


def die(text, code=1):
    say('ОШИБКА: ' + text)
    raise SystemExit(code)


# ---------------------------------------------------------------- утилиты
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def human(size):
    """Размер по-человечески: 70.3 КБ, 1.4 МБ."""
    if size < 1024:
        return '%d Б' % size
    value = float(size)
    for unit in ('КБ', 'МБ', 'ГБ'):
        value /= 1024.0
        if value < 1024 or unit == 'ГБ':
            return '%.1f %s' % (value, unit)
    return '%.1f ГБ' % value


def safe_name(text):
    """Комментарий -> кусок имени папки (без запрещённых символов)."""
    text = (text or '').strip()
    text = re.sub(r'[\\/:*?"<>|\r\n\t]+', '-', text)
    text = re.sub(r'\s+', '-', text)
    text = re.sub(r'-{2,}', '-', text).strip('-. ')
    return text[:48] or 'без-комментария'


def find_version(path):
    """Достаём версию из строки FW_VER / FW_VERSION. Возвращает (версия, строка)."""
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as fh:
            for line in fh:
                for pat in VER_PATTERNS:
                    m = pat.search(line)
                    if m:
                        return m.group(1).strip(), line.strip()
    except OSError:
        pass
    return '', ''


# ---------------------------------------------------------------- конфиг
def load_config():
    cfg = {'archive': NAS_DRIVE, 'local': LOCAL_DIR, 'mirror_local': True}
    if os.path.exists(CONFIG):
        try:
            with open(CONFIG, 'r', encoding='utf-8') as fh:
                cfg.update(json.load(fh))
        except (OSError, ValueError):
            pass
    return cfg


def save_config(cfg):
    try:
        with open(CONFIG, 'w', encoding='utf-8') as fh:
            json.dump(cfg, fh, ensure_ascii=False, indent=2)
    except OSError:
        pass


def is_writable_dir(path):
    """Проверяем каталог на запись настоящей записью, а не только Test-Path."""
    try:
        os.makedirs(path, exist_ok=True)
        probe = os.path.join(path, '.write-test.tmp')
        with open(probe, 'w', encoding='utf-8') as fh:
            fh.write('ok')
        os.remove(probe)
        return True
    except OSError:
        return False


def pick_roots(cfg):
    """
    Возвращает (основной_корень, локальный_корень_или_None, примечание).
    Основной — NAS, если он доступен; иначе локальный.
    """
    nas_candidates = [cfg.get('archive', NAS_DRIVE), NAS_UNC]
    for cand in nas_candidates:
        if not cand:
            continue
        if is_writable_dir(cand):
            local = cfg.get('local') or LOCAL_DIR
            return cand, (local if os.path.normcase(cand) != os.path.normcase(local) else None), ''
    local = cfg.get('local') or LOCAL_DIR
    if is_writable_dir(local):
        return local, None, 'NAS недоступен — архив только локально'
    die('нет ни NAS, ни локальной папки для архива')


def all_roots(cfg):
    """
    Все корни, где может лежать архив — для --list, --verify и --restore.
    буква диска и UNC на одну и ту же шару — это одно и то же место, второй
    путь добавляем только если буквы диска нет.
    """
    roots = []
    seen = set()

    def add(path):
        if not path or not os.path.isdir(path):
            return
        key = os.path.normcase(os.path.normpath(path))
        if key in seen:
            return
        seen.add(key)
        roots.append(path)

    add(cfg.get('archive') or NAS_DRIVE)
    if not roots:
        add(NAS_UNC)
    add(cfg.get('local') or LOCAL_DIR)
    return roots


# ---------------------------------------------------------------- снимок
def collect_files(src_key, with_baks=False):
    """Список файлов прошивки: основной + заголовки (+ старые копии по флагу)."""
    src = SOURCES[src_key]
    folder = src['dir']
    if not os.path.isdir(folder):
        return []
    main = src['main']
    picked = []

    def add(name):
        p = os.path.join(folder, name)
        if os.path.isfile(p) and name not in [os.path.basename(x) for x in picked]:
            picked.append(p)

    add(main)
    for name in sorted(os.listdir(folder)):
        p = os.path.join(folder, name)
        if not os.path.isfile(p):
            continue
        low = name.lower()
        # секреты в архив не копируем ни при каких условиях
        if low in ('secrets.h', 'secret-telegram.txt') or low.endswith(('.credentials', '.env')):
            continue
        if BAK_MASK.search(low):
            if with_baks:
                add(name)
            continue
        if low.endswith(EXTRA_EXT):
            add(name)
    return picked


def make_snapshot(root, src_key, comment, with_baks=False, extra=None):
    """Создаёт папку снимка с manifest.json. Возвращает путь или None."""
    files = collect_files(src_key, with_baks)
    if not files:
        say('  %-14s ПРОПУСК: нет файлов в %s' % (src_key, SOURCES[src_key]['dir']))
        return None

    now = datetime.now().astimezone()
    stamp = now.strftime('%Y-%m-%d_%H%M')
    slug = safe_name(comment)
    base = '%s_%s' % (stamp, slug)

    src_root = os.path.join(root, src_key)
    os.makedirs(src_root, exist_ok=True)
    target = os.path.join(src_root, base)
    n = 2
    while os.path.exists(target):
        target = os.path.join(src_root, '%s_%d' % (base, n))
        n += 1
    os.makedirs(target)

    entries = []
    for path in files:
        name = os.path.basename(path)
        dst = os.path.join(target, name)
        shutil.copy2(path, dst)
        entries.append({
            'name': name,
            'size': os.path.getsize(dst),
            'sha256': sha256_file(dst),
            'mtime': datetime.fromtimestamp(os.path.getmtime(path)).strftime('%Y-%m-%d %H:%M:%S'),
        })

    version, ver_line = find_version(os.path.join(target, SOURCES[src_key]['main']))
    total = sum(e['size'] for e in entries)
    joined = hashlib.sha256(''.join(e['sha256'] for e in entries).encode('ascii')).hexdigest()

    manifest = {
        'id': base,
        'source': src_key,
        'source_dir': SOURCES[src_key]['dir'],
        'date': now.strftime('%Y-%m-%d %H:%M:%S'),
        'date_iso': now.isoformat(timespec='seconds'),
        'size': total,
        'sha256': joined,
        'comment': comment,
        'version': version,
        'version_line': ver_line,
        'board': SOURCES[src_key]['board'],
        'note': SOURCES[src_key]['note'],
        'files': entries,
        'archive_root': root,
    }
    if extra:
        manifest.update(extra)

    with open(os.path.join(target, 'manifest.json'), 'w', encoding='utf-8') as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2)
        fh.write('\n')

    say('  %-14s %s  версия %-6s %s  sha256 %s' % (
        src_key, base, version or '—', human(total), joined[:12]))
    return target


# ---------------------------------------------------------------- поиск
def read_manifest(snapshot_dir):
    path = os.path.join(snapshot_dir, 'manifest.json')
    try:
        with open(path, 'r', encoding='utf-8') as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def scan(roots):
    """Все снимки: список словарей с manifest + где лежит."""
    found = {}
    for root in roots:
        if not os.path.isdir(root):
            continue
        for src_key in sorted(os.listdir(root)):
            src_dir = os.path.join(root, src_key)
            if not os.path.isdir(src_dir):
                continue
            for name in sorted(os.listdir(src_dir)):
                snap = os.path.join(src_dir, name)
                if not os.path.isdir(snap):
                    continue
                man = read_manifest(snap)
                if not man:
                    continue
                key = (man.get('source', src_key), man.get('id', name))
                item = found.setdefault(key, {'manifest': man, 'paths': []})
                item['paths'].append(snap)
    items = list(found.values())
    items.sort(key=lambda it: (it['manifest'].get('date_iso') or '', it['manifest'].get('source') or ''))
    return items


def cmd_list(cfg):
    roots = all_roots(cfg)
    if not roots:
        say('архив пуст: папок пока нет')
        say('запусти: python backup-firmware.py')
        return 0

    items = scan(roots)
    say('АРХИВ ПРОШИВОК')
    for root in roots:
        say('  каталог: ' + root)
    say('')
    if not items:
        say('снимков пока нет — запусти: python backup-firmware.py')
        return 0

    say('%-17s %-34s %-7s %-9s %-13s %s' % (
        'ДАТА', 'ПАПКА', 'ВЕРСИЯ', 'РАЗМЕР', 'SHA256', 'КОММЕНТАРИЙ'))
    say('-' * 118)
    for it in items:
        m = it['manifest']
        say('%-17s %-34s %-7s %-9s %-13s %s' % (
            m.get('date', '')[:16],
            m.get('id', ''),
            m.get('version') or '—',
            human(m.get('size', 0)),
            (m.get('sha256') or '')[:12],
            m.get('comment', ''),
        ))
    say('-' * 118)
    say('всего снимков: %d, прошивок: %d' % (
        len(items), len({it['manifest'].get('source') for it in items})))
    say('')
    say('восстановить: python backup-firmware.py --restore <папка | версия | sha256>')
    return 0


def resolve_snapshot(items, query):
    """
    Ищем снимок по имени папки, версии прошивки, sha256 или имени прошивки.

    Важно: имя папки снимка одинаковое у всех прошивок (дата_время_коммент),
    поэтому «--restore 2026-10-28_2014_первый-архив» подходит сразу к четырём.
    В таком случае выбор не угадываем, а просим уточнить: «cydv4/<папка>».
    """
    q = (query or '').strip().replace('\\', '/')
    if not q:
        return None, []

    # форма «прошивка/папка»
    if '/' in q:
        src, _, rest = q.partition('/')
        pool = [it for it in items if it['manifest'].get('source') == src]
        if not pool:
            return None, []
        found, matches = resolve_snapshot(pool, rest)
        if found is None and not matches:
            return None, []
        return found, matches

    exact = [it for it in items if it['manifest'].get('id') == q]
    if len(exact) == 1:
        return exact[0], exact
    if len(exact) > 1:
        return None, exact

    by_prefix = [it for it in items if it['manifest'].get('id', '').startswith(q)]
    if len(by_prefix) == 1:
        return by_prefix[0], by_prefix
    if len(by_prefix) > 1:
        return None, by_prefix

    by_ver = [it for it in items if (it['manifest'].get('version') or '') == q]
    if len(by_ver) == 1:
        return by_ver[0], by_ver
    if len(by_ver) > 1:
        return None, by_ver

    # имя прошивки (cydv4, dom-monitor, ...) — берём самый свежий её снимок
    by_src = [it for it in items if it['manifest'].get('source') == q]
    if by_src:
        return by_src[-1], by_src

    by_hash = [it for it in items if (it['manifest'].get('sha256') or '').startswith(q)]
    if len(by_hash) == 1:
        return by_hash[0], by_hash
    if len(by_hash) > 1:
        return None, by_hash

    return None, []


def cmd_restore(cfg, query, to_dir=None, no_backup=False, force=False):
    roots = all_roots(cfg)
    items = scan(roots)
    if not items:
        die('архив пуст — восстанавливать нечего')

    item, matches = resolve_snapshot(items, query)
    if item is None:
        if matches:
            say('под запрос «%s» подходит несколько версий:' % query)
            for m in matches:
                say('  %-14s %-34s версия %-6s %s' % (
                    m['manifest'].get('source', '?'),
                    m['manifest'].get('id', ''),
                    m['manifest'].get('version') or '—',
                    m['manifest'].get('comment', '')))
            die('уточни прошивку: --restore <прошивка>/<папка>, например '
                'cydv4/2026-10-28_2014_первый-архив')
        die('версия «%s» не найдена. Список: python backup-firmware.py --list' % query)

    man = item['manifest']
    snap_dir = item['paths'][0]
    target = to_dir or man.get('source_dir')
    if not target:
        die('в manifest нет source_dir — укажи --to')

    say('восстанавливаю: %s  (прошивка %s, версия %s, %s)' % (
        man.get('id'), man.get('source', '?'), man.get('version') or '—', man.get('date')))
    say('  из: ' + snap_dir)
    say('  в : ' + target)

    # Защита от восстановления чужого скетча в папку другой прошивки:
    # в папке скетча не должно быть посторонних .ino — Arduino их скомпилирует.
    if os.path.isdir(target):
        own = {f['name'] for f in man.get('files', [])}
        foreign = [n for n in os.listdir(target)
                   if n.lower().endswith('.ino') and n not in own]
        if foreign and not force:
            say('  в папке уже лежат другие скетчи: ' + ', '.join(foreign))
            die('это папка другой прошивки — остановился, ничего не тронул.\n'
                '       если так и задумано, повтори с ключом --force')

    # ПРАВИЛО БЭКАПОВ: перед откатом сохраняем текущее состояние в архив
    if os.path.isdir(target) and not no_backup and man.get('source') in SOURCES:
        live = [os.path.join(target, f['name']) for f in man.get('files', [])]
        if any(os.path.exists(p) for p in live):
            say('  сначала сохраняю текущее состояние в архив...')
            try:
                root_for_backup = roots[0]
                for root in roots:
                    if os.path.normcase(root) == os.path.normcase(man.get('archive_root') or ''):
                        root_for_backup = root
                        break
                make_snapshot(root_for_backup, man['source'], 'до-восстановления')
            except OSError as exc:
                say('  (предварительный снимок не сделан: %s)' % exc)

    os.makedirs(target, exist_ok=True)
    ok = 0
    for entry in man.get('files', []):
        src = os.path.join(snap_dir, entry['name'])
        if not os.path.isfile(src):
            say('  ПРОПУСК: в архиве нет файла ' + entry['name'])
            continue
        dst = os.path.join(target, entry['name'])
        if os.path.exists(dst):
            # старую версию не затираем молча — рядом кладём .bak с датой
            stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
            shutil.copy2(dst, dst + '.bak-' + stamp)
            say('  прежний %s -> %s.bak-%s' % (entry['name'], entry['name'], stamp))
        shutil.copy2(src, dst)
        got = sha256_file(dst)
        mark = 'ok' if got == entry['sha256'] else 'НЕ СОВПАЛ sha256!'
        if got == entry['sha256']:
            ok += 1
        say('  %-18s %s  %s' % (entry['name'], human(entry['size']), mark))

    say('')
    say('готово: файлов %d из %d, папка %s' % (ok, len(man.get('files', [])), target))
    if ok != len(man.get('files', [])):
        die('часть файлов восстановлена с ошибкой — проверь вручную', 2)
    say('проверка: собери прошивку и прошей плату, затем смотри {"t":"status"} в мониторе порта')
    return 0


def cmd_verify(cfg, query=None):
    roots = all_roots(cfg)
    items = scan(roots)
    if query:
        item, matches = resolve_snapshot(items, query)
        if item is None:
            die('версия «%s» не найдена' % query)
        items = [item]

    bad = 0
    checked = 0
    for it in items:
        m = it['manifest']
        for snap in it['paths']:
            for entry in m.get('files', []):
                p = os.path.join(snap, entry['name'])
                checked += 1
                if not os.path.isfile(p):
                    say('НЕТ ФАЙЛА  %s\\%s' % (snap, entry['name']))
                    bad += 1
                    continue
                if sha256_file(p) != entry['sha256']:
                    say('ИЗМЕНЁН    %s' % p)
                    bad += 1
    say('проверено файлов: %d, проблем: %d' % (checked, bad))
    return 0 if bad == 0 else 2


def cmd_backup(cfg, comment, only=None, with_baks=False):
    root, mirror, note = pick_roots(cfg)
    say('АРХИВ ПРОШИВОК — «Дом — энергия»')
    say('  дата:      ' + datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    say('  каталог:   ' + root + ('   (NAS)' if 'NAS' in root.upper() or root.upper().startswith('Y:') else '   (локально)'))
    if mirror:
        say('  копия:     ' + mirror)
    if note:
        say('  ВНИМАНИЕ:  ' + note)
    say('  комментарий: ' + comment)
    say('')

    keys = [only] if only else list(SOURCES.keys())
    for key in keys:
        if key not in SOURCES:
            die('неизвестная прошивка «%s». Есть: %s' % (key, ', '.join(SOURCES)))
        path = make_snapshot(root, key, comment, with_baks)

        # второй экземпляр: NAS + локально — так ничего не теряется
        if path and mirror:
            try:
                rel = os.path.relpath(path, root)
                dst = os.path.join(mirror, rel)
                os.makedirs(dst, exist_ok=True)
                for name in os.listdir(path):
                    shutil.copy2(os.path.join(path, name), os.path.join(dst, name))
                man_path = os.path.join(dst, 'manifest.json')
                man = read_manifest(dst)
                if man is not None:
                    man['archive_root'] = mirror
                    man['mirror_of'] = path
                    with open(man_path, 'w', encoding='utf-8') as fh:
                        json.dump(man, fh, ensure_ascii=False, indent=2)
                        fh.write('\n')
            except OSError as exc:
                say('  (копию сделать не удалось: %s)' % exc)

    say('')
    say('готово. список версий: python backup-firmware.py --list')
    return 0


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass

    cfg = load_config()

    ap = argparse.ArgumentParser(
        description='Архив прошивок «Дом — энергия»: копии с датой, версией и sha256.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='примеры:\n'
               '  python backup-firmware.py --comment "перед правкой экрана"\n'
               '  python backup-firmware.py --list\n'
               '  python backup-firmware.py --restore cydv4\n'
               '  python backup-firmware.py --restore cydv4/2026-10-28_2010_первый-архив\n')
    ap.add_argument('--comment', '-c', default='авто-бэкап',
                    help='короткий комментарий к версии (идёт в имя папки)')
    ap.add_argument('--only', help='только одна прошивка: ' + ', '.join(SOURCES))
    ap.add_argument('--with-baks', action='store_true',
                    help='включить в архив старые копии .bak/.bak2/... (много места)')
    ap.add_argument('--list', '-l', action='store_true', help='показать все версии')
    ap.add_argument('--restore', metavar='ВЕРСИЯ',
                    help='восстановить: прошивка, прошивка/папка, имя папки, версия или sha256')
    ap.add_argument('--to', help='куда восстанавливать (по умолчанию — исходная папка)')
    ap.add_argument('--force', action='store_true',
                    help='восстанавливать, даже если в папке лежит другой скетч')
    ap.add_argument('--no-backup', action='store_true',
                    help='при восстановлении не архивировать текущее состояние')
    ap.add_argument('--verify', nargs='?', const='', metavar='ВЕРСИЯ',
                    help='проверить sha256 файлов (всех или одной версии)')
    ap.add_argument('--archive', help='папка архива (по умолчанию ' + NAS_DRIVE + ')')
    ap.add_argument('--local', help='локальная папка-копия (по умолчанию ' + LOCAL_DIR + ')')
    ap.add_argument('--no-mirror', action='store_true', help='не делать локальную копию')
    ap.add_argument('--init', action='store_true', help='проверить доступность NAS и показать пути')
    args = ap.parse_args(argv)

    if args.archive:
        cfg['archive'] = args.archive
        save_config(cfg)
    if args.local:
        cfg['local'] = args.local
        save_config(cfg)
    if args.no_mirror:
        cfg['mirror_local'] = False

    # файл настроек создаём при первом запуске, чтобы пути можно было править руками
    if not os.path.exists(CONFIG):
        save_config(cfg)

    if args.init:
        root, mirror, note = pick_roots(cfg)
        say('основной архив: ' + root)
        say('локальная копия: ' + (mirror or 'выключена'))
        if note:
            say('примечание: ' + note)
        return 0

    if args.list:
        return cmd_list(cfg)
    if args.restore:
        return cmd_restore(cfg, args.restore, args.to, args.no_backup, args.force)
    if args.verify is not None:
        return cmd_verify(cfg, args.verify or None)

    comment = args.comment
    if not comment or comment == 'авто-бэкап':
        comment = 'авто-бэкап ' + datetime.now().strftime('%H:%M')
    return cmd_backup(cfg, comment, args.only, args.with_baks)


if __name__ == '__main__':
    sys.exit(main())
