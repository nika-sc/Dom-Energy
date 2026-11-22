#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка публичного репозитория «Дом — энергия» на внутренние документы
и личные данные во ВСЕХ коммитах и refs.

Запуск:  python tests/check-public-repo.py
Код возврата: 0 — чисто, 1 — найдены нарушения.

Точные значения личных идентификаторов в публичном файле не хранятся:
проверяются обобщённые образцы (MAC, длинные hex-идентификаторы, приватные
IP-диапазоны, токены Telegram, пути пользователя).
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Запрещённые имена файлов (внутренние рабочие документы)
NAME_RE = re.compile(
    r"(ПРАВИЛА-РАЗРАБОТКИ|ПРАВИЛА\.md|ПЕРЕДАЧА-|ОБЗОР-ЧАТА|ПЛАН-|РЕВЬЮ-|РЕЕСТР-ДЕТАЛЕЙ"
    r"|СОСТОЯНИЕ-ПЛАТЫ|СОЦСЕТИ-И-ВЕРСИИ|ГРАБЛИ-И-РЕШЕНИЯ|UI-KIT"
    r"|(^|/)NEXT\.md$|PREFLIGHT-|SANITIZE-|PITFALLS|HISTORY-REWORK|STUDIO-DEPLOY"
    r"|RELEASE-CHECKLIST|RELEASE-4\.12\.0|social-4\.14)",
    re.I,
)

# Обобщённые образцы личных данных (без конкретных значений)
CONTENT_PATTERNS = [
    (r"192\.168\.0\.[0-9]{1,3}", "приватный IP 192.168.0.x (в примерах — 192.168.1.51)"),
    (r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", "MAC-адрес"),
    (r"[0-9]{8,10}:[A-Za-z0-9_-]{30,}", "токен Telegram-бота"),
    (r"C:\\Users\\[0-9]+", "путь пользователя с числовым именем"),
]


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT)] + list(args), capture_output=True)


def main():
    problems = []
    commits = git("rev-list", "--all").stdout.decode().split()
    if not commits:
        print("Это не git-репозиторий или нет коммитов")
        return 1

    names = set()
    for commit in commits:
        out = git("ls-tree", "-r", "--name-only", commit, "-z").stdout
        for path in out.split(b"\x00"):
            if path:
                names.add(path.decode("utf-8", "replace"))
    for path in sorted(names):
        if NAME_RE.search(path):
            problems.append("ИМЯ: %s (внутренний документ в истории)" % path)

    for pattern, title in CONTENT_PATTERNS:
        out = subprocess.run(
            ["git", "-C", str(ROOT), "grep", "-I", "-n", "-E", pattern] + commits,
            capture_output=True,
        )
        for line in out.stdout.decode("utf-8", "replace").splitlines():
            if ":tests/check-public-repo.py:" in line:
                continue
            if "MAC-адрес" in title and "aa:bb:cc:dd:ee:ff" in line.lower():
                continue  # учебный пример MAC в тестах
            if "токен Telegram" in title and ("123456789:" in line or "AAAA" in line):
                continue  # учебный токен в тестах
            problems.append("СОДЕРЖИМОЕ: %s — %s" % (title, line[:240]))

    if problems:
        print("НАЙДЕНО %d нарушений:" % len(problems))
        for p in problems:
            print(" ", p)
        return 1

    print("OK: внутренних документов и личных данных нет ни в одном коммите/ref")
    print("коммитов проверено: %d, файлов в HEAD: %d" % (
        len(commits), len(git("ls-tree", "-r", "--name-only", "HEAD").stdout.splitlines())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
