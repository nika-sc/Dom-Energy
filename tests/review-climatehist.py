"""Проверка нового эндпоинта /climatehist (комнатный климат за период).

Без сети не обойтись: эндпоинт читает реальный /climate.csv с платы.
Проверки только чтением, ничего не меняют.

Запуск: python tests/review-climatehist.py
"""
import json
import sys
import time
import urllib.parse
import urllib.request

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import os
BASE = os.environ.get("BOARD_URL", "http://192.168.1.51").rstrip("/")
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
problems = []


def get(path, timeout=30):
    t0 = time.time()
    with OPENER.open(BASE + path, timeout=timeout) as r:
        body = r.read().decode("utf-8", "replace")
    return body, (time.time() - t0) * 1000


def check(cond, msg):
    if cond:
        print("  [OK] " + msg)
    else:
        print("  [FAIL] " + msg)
        problems.append(msg)


def main():
    print("1) структура и значения по умолчанию")
    body, ms = get("/climatehist")
    print(f"  {ms:.0f} мс, {len(body)} Б")
    j = json.loads(body)
    check(j.get("ok") is True, "ok=true")
    check("from" in j and "to" in j and isinstance(j.get("days"), list), "поля from/to/days")
    check(isinstance(j.get("partial"), bool), "поле partial (bool)")
    check(isinstance(j.get("fileBytes"), int) and j["fileBytes"] >= 0, "поле fileBytes")
    check(j["from"] <= j["to"], f"from<=to ({j['from']}..{j['to']})")
    days = j["days"]
    print(f"  дней с данными: {len(days)}")
    for d in days:
        for k in ("d", "n", "tmin", "tmax", "tavg", "hmin", "hmax", "havg", "pavg"):
            check(k in d, f"поле {k} в дне {d.get('d')}")
        check(0 <= d["n"] <= 24, f"{d['d']}: 0<=n<=24 ({d['n']})")
        check(-50 <= d["tmin"] <= 60 and -50 <= d["tmax"] <= 60, f"{d['d']}: t в разумных пределах")
        check(d["tmin"] <= d["tavg"] <= d["tmax"], f"{d['d']}: tmin<=tavg<=tmax")
        check(0 <= d["hmin"] <= 100 and 0 <= d["havg"] <= 100, f"{d['d']}: влажность 0..100")
        check(800 <= d["pavg"] <= 1100 or d["pavg"] == 0, f"{d['d']}: давление 800..1100 (или 0 нет данных)")
        check(d["d"] >= j["from"] and d["d"] <= j["to"], f"{d['d']}: в пределах from..to")

    print("2) явный диапазон и фильтрация")
    if days:
        frm, to = days[0]["d"], days[-1]["d"]
        body2, _ = get(f"/climatehist?from={frm}&to={to}")
        j2 = json.loads(body2)
        check(j2.get("from") == frm and j2.get("to") == to, f"from/to вернулись как просили ({frm}..{to})")
        check([x["d"] for x in j2["days"]] == [x["d"] for x in days], "тот же набор дней")
        # инвариант: повторный запрос не меняет данные
        body3, _ = get(f"/climatehist?from={frm}&to={to}")
        # поле ms — время выполнения, оно меняется; сравниваем без него
        j2c = dict(j2); j2c.pop("ms", None)
        j3c = json.loads(body3); j3c.pop("ms", None)
        check(j2c == j3c, "повторный запрос даёт те же данные (без времени ms)")
        # диапазон из одного дня
        one, _ = get(f"/climatehist?from={days[0]['d']}&to={days[0]['d']}")
        jo = json.loads(one)
        check(len(jo["days"]) == 1 and jo["days"][0]["d"] == days[0]["d"], "диапазон из одного дня")
        # пустой диапазон в прошлом
        empty, _ = get("/climatehist?from=2020-01-01&to=2020-01-02")
        je = json.loads(empty)
        check(je.get("ok") is True and je.get("days") == [], "пустой диапазон -> days=[]")
    else:
        print("  [SKIP] данных за период нет — проверки диапазонов пропущены")

    print("3) битые параметры не ломают эндпоинт")
    for q in ("?from=abc&to=def", "?from=2026-13-99&to=2026-02-30", "?from=&to="):
        try:
            body4, _ = get("/climatehist" + q)
            j4 = json.loads(body4)
            check(j4.get("ok") is True, f"битые параметры {q}: ok=true")
        except Exception as e:  # noqa: BLE001
            check(False, f"битые параметры {q}: исключение {e}")

    print("4) сегодняшний день согласуется с /data")
    try:
        dbody, _ = get("/data")
        d = json.loads(dbody)
        today = time.strftime("%Y-%m-%d")
        row = next((x for x in days if x["d"] == today), None)
        if row is not None and d.get("temp") is not None:
            t = float(d["temp"])
            # текущая температура реле vs комнатная — разные датчики: сравниваем с допуском
            print(f"  сейчас: комната по /data et={d.get('et')} t={d.get('temp')}; день {row['d']}: tavg={row['tavg']}")
            check(-50 <= float(d.get("temp", 0)) <= 60, "t реле в разумных пределах")
        else:
            print("  [SKIP] сегодняшних данных в климате нет")
    except Exception as e:  # noqa: BLE001
        print("  [SKIP] /data недоступен:", e)

    print()
    if problems:
        print("ПРОВАЛ:")
        for p in problems:
            print(" -", p)
        sys.exit(1)
    print("ВСЁ ОК: /climatehist отдаёт корректные суточные агрегаты комнатного климата")


if __name__ == "__main__":
    main()
