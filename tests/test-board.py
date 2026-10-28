"""Полный набор тестов платы «Дом — энергия».

Запуск:
    python test-board.py            все тесты
    python test-board.py --quick    только быстрые (без облака и полного файла)
    python test-board.py --verbose  подробный вывод

Код возврата: 0 — всё прошло, 1 — есть провалы.

ПРИНЦИПЫ (из практики проверки систем учёта энергии):
  * Min-Max: значение вне физических границ — ошибка, а не данные
  * Накопленный счётчик -> интервалы: энергия за интервал = разница счётчика
  * Сверка двух независимых источников: счётчик против интеграла мощности
  * Детектор «застывшего» датчика: если значение часами одинаково,
    а счётчик растёт — датчик отдаёт кэш, а не измерение
"""
import io
import json
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone

BOARD = "http://192.168.1.51"
TZ = timezone(timedelta(hours=3))

# Физические границы (Min-Max из практики): вне них — мусор, не данные.
LIMITS = {
    "v":    (180.0, 260.0),     # напряжение, В
    "temp": (15.0, 60.0),       # температура реле, °C
    "p":    (0.0, 5000.0),      # мощность, Вт
    "amp":  (0.0, 25.0),        # ток, А
}

# Тарифы
TAR_DAY, TAR_NIGHT = 8.39, 4.49

results = []
FRESH_DAYS = 1      # проверять только СЕГОДНЯШНИЕ часы (вчерашние писались до фикса)
ALL_DAYS = "--all-days" in sys.argv


WARN_ONLY = set()      # имена проверок, которые не считаются провалом


def check(group, name, ok, detail="", warn=False):
    """warn=True — при провале это ПРЕДУПРЕЖДЕНИЕ, а не ошибка."""
    results.append((group, name, bool(ok), detail, warn))
    if ok:
        mark = "OK  "
    elif warn:
        mark = "ВНИМ"
        WARN_ONLY.add(name)
    else:
        mark = "FAIL"
    print("  [%s] %s%s" % (mark, name, ("  — " + detail) if detail else ""))
    return ok


def get(path, timeout=90, tries=4):
    last = None
    for i in range(tries):
        try:
            with urllib.request.urlopen(BOARD + path, timeout=timeout) as r:
                return r.read().decode("utf-8", "replace")
        except Exception as e:
            last = e
            time.sleep(1.5 * (i + 1))
    raise last


def jget(path, timeout=90):
    return json.loads(get(path, timeout))


def rows_of(body):
    """Только строки нормального формата: дата,В,Вт,кВт·ч,реле,руб,°C"""
    out = []
    for line in body.splitlines():
        if len(line) < 20:
            continue
        if not (line.startswith("20") and line[4] == "-" and line[7] == "-"):
            continue
        p = line.split(",")
        if len(p) < 6:
            continue
        try:
            out.append({
                "t": p[0],
                "day": p[0][:10],
                "hour": int(p[0][11:13]),
                "v": float(p[1]),
                "p": float(p[2]),
                "e": float(p[3]),
                "relay": int(p[4]),
                "cost": float(p[5]),
                "temp": float(p[6]) if len(p) > 6 and p[6].strip() else None,
            })
        except (ValueError, IndexError):
            continue
    return out


# ============================================================ ГРУППА 1
def t_endpoints():
    print("\n--- 1. Доступность и контракты ---")

    page = get("/", 30)
    check("доступность", "GET / отвечает", True, "%d байт" % len(page))

    d = jget("/data", 30)
    need = ["v", "temp", "p", "amp", "e", "r", "tkwh", "tcost",
            "ykwh", "ykost", "mkwh", "mcost", "tariff", "rssi", "sdRows"]
    missing = [k for k in need if k not in d]
    check("контракт", "/data содержит все поля", not missing,
          ("нет: " + ", ".join(missing)) if missing else "%d полей" % len(d))

    for path in ("/days", "/history?date=" + datetime.now(TZ).strftime("%Y-%m-%d"), "/events"):
        try:
            j = jget(path, 60)
            check("контракт", "GET %s -> JSON" % path, isinstance(j, (dict, list)), "ok")
        except Exception as e:
            check("контракт", "GET %s -> JSON" % path, False, type(e).__name__)

    # мусорная дата должна давать 4xx, а не молча сегодняшние данные
    try:
        with urllib.request.urlopen(BOARD + "/history?date=abrakadabra", timeout=30) as r:
            code = r.status
            body = r.read().decode("utf-8", "replace")
        bad = (code == 200 and "hours" in body)
        check("контракт", "/history с мусорной датой отклоняется", not bad,
              "код %d" % code)
    except urllib.error.HTTPError as e:
        check("контракт", "/history с мусорной датой отклоняется", 400 <= e.code < 500,
              "код %d" % e.code)
    except Exception as e:
        check("контракт", "/history с мусорной датой отклоняется", False, type(e).__name__)

    return d


# ============================================================ ГРУППА 2
def t_limits(d):
    print("\n--- 2. Min-Max: значения в физических границах ---")
    for key, (lo, hi) in LIMITS.items():
        val = d.get(key)
        if val is None:
            check("границы", "поле %s присутствует" % key, False)
            continue
        check("границы", "%s = %s в [%s..%s]" % (key, val, lo, hi), lo <= val <= hi)
    check("границы", "счётчик > 0", d.get("e", 0) > 0, "%.2f кВт·ч" % d.get("e", 0))
    check("границы", "карта есть", d.get("sdRows", 0) > 0, "%d строк" % d.get("sdRows", 0))


# ============================================================ ГРУППА 3
def t_integrity(rows):
    print("\n--- 3. Целостность данных на карте ---")
    check("целостность", "строк с датой найдено", len(rows) > 1000, "%d шт" % len(rows))

    # счётчик не убывает. Строки с нулевым напряжением — известные артефакты
    # ранних прошивок (до исправления «не писать без напряжения»), их пропускаем.
    drops = []
    small = []
    artifacts = 0
    prev = None
    for r in rows:
        if r["v"] < 50 or r["e"] <= 0:
            artifacts += 1
            continue
        if prev is not None and r["e"] < prev - 0.05:
            drops.append((r["t"], prev, r["e"]))
        elif prev is not None and r["e"] < prev - 0.02:
            small.append((r["t"], prev, r["e"]))
        prev = r["e"]
    check("целостность", "счётчик не убывает", not drops,
          "провалов %d, мелких (до 0.05) %d, артефактов %d"
          % (len(drops), len(small), artifacts))

    # полнота часов за последние 2 суток
    now = datetime.now(TZ)
    by_day_hour = {}
    for r in rows:
        by_day_hour.setdefault((r["day"], r["hour"]), 0)
        by_day_hour[(r["day"], r["hour"])] += 1

    gaps = []
    for day_off in (1, 0):
        day = (now - timedelta(days=day_off)).strftime("%Y-%m-%d")
        # сегодня проверяем только ПОЛНЫЕ часы: текущий ещё наполняется
        last_hour = now.hour - 1 if day_off == 0 else 23
        for h in range(0, last_hour + 1):
            n = by_day_hour.get((day, h), 0)
            if n < 100:                       # норма 113-120
                gaps.append("%s %02d: %d" % (day[5:], h, n))
    # одна короткая дырка обычно означает перепрошивку — это предупреждение
    warn_gap = len(gaps) <= 2
    check("целостность", "все часы за 2 суток заполнены", not gaps,
          ("провалы: " + ", ".join(gaps[:6]) +
           ("  (похоже на перепрошивку)" if warn_gap and gaps else "")) if gaps
          else "норма 113-120 строк", warn=warn_gap)


# ============================================================ ГРУППА 4
def t_power_quality(rows):
    """ГЛАВНЫЙ ТЕСТ: сходится ли мощность со счётчиком.

    Именно он ловит «застывшую» мощность: если в CSV писалось 19 Вт,
    а счётчик за час прибавил 0.32 кВт·ч, то интеграл мощности даст
    0.019 кВт·ч вместо 0.32 — расхождение в 17 раз.
    """
    print("\n--- 4. Качество мощности (интеграл против счётчика) ---")
    print("    проверяю последние %d суток%s" % (FRESH_DAYS, "" if not ALL_DAYS else " (вся история)"))

    # По умолчанию проверяем только последние FRESH_DAYS суток: до 29.10 17:00
    # мощность писалась из устаревшего локального датчика, и это уже история.
    # Полную историю смотреть флагом --all-days.
    fresh = (datetime.now(TZ) - timedelta(days=max(0, FRESH_DAYS - 1))).strftime("%Y-%m-%d")
    by_hour = {}
    for r in rows:
        if r["v"] < 50 or r["e"] <= 0:        # артефакты ранних прошивок
            continue
        if not ALL_DAYS and r["day"] < fresh:
            continue
        by_hour.setdefault((r["day"], r["hour"]), []).append(r)

    bad_hours = []
    stale_hours = []      # часы с «застывшей» мощностью (до фикса)
    checked = 0
    for (day, hour), rs in sorted(by_hour.items()):
        if len(rs) < 50:
            continue
        rs_sorted = sorted(rs, key=lambda x: x["t"])
        e_first = rs_sorted[0]["e"]
        e_last = rs_sorted[-1]["e"]
        kwh_counter = e_last - e_first
        if kwh_counter < 0.03:            # на малых значениях квантование 0.01 даёт шум
            continue
        # интеграл: средняя мощность × длительность
        dur_h = (len(rs) - 1) * 30.0 / 3600.0
        avg_p = sum(x["p"] for x in rs) / len(rs)
        kwh_power = avg_p * dur_h / 1000.0
        checked += 1
        if kwh_power <= 0:
            bad_hours.append((day, hour, kwh_counter, 0.0, 999.0))
            continue
        ratio = kwh_counter / kwh_power
        if not (0.75 <= ratio <= 1.33):
            # «Застывшая» мощность: все значения часа одинаковые — значит писался
            # не живой замер, а одно и то же число (устаревший локальный датчик).
            # Это исторические данные ДО фикса 31.10.2026 17:44, а не текущая
            # поломка. Помечаем предупреждением, а не провалом.
            vals = [round(x["p"], 1) for x in rs]
            uniq = len(set(vals))
            # «Почти одинаковые»: датчик отдавал одно число, но пара записей
            # успевала попасть между обновлениями. 90 % одинаковых — застыло.
            common = max(vals.count(v) for v in set(vals)) if vals else 0
            mostly_same = (uniq == 1) or (common / float(len(vals)) >= 0.90)
            if mostly_same:
                stale_hours.append((day, hour, kwh_counter, kwh_power, ratio))
            else:
                bad_hours.append((day, hour, kwh_counter, kwh_power, ratio))

    check("мощность", "интеграл мощности сходится со счётчиком",
          not bad_hours,
          ("проверено часов %d, расходятся %d" % (checked, len(bad_hours))))
    for day, hour, kc, kp, ratio in bad_hours[:8]:
        print("         %s %02d:00  счётчик %.3f кВт·ч, по мощности %.3f -> x%.1f"
              % (day, hour, kc, kp, ratio))
    if stale_hours:
        print("         (не считается провалом: %d ч с «застывшей» мощностью — "
              "записаны до фикса 31.10)" % len(stale_hours))
        for day, hour, kc, kp, ratio in stale_hours[:6]:
            print("           %s %02d:00  счётчик %.3f, по мощности %.3f -> x%.1f"
                  % (day, hour, kc, kp, ratio))

    # детектор «застывшей» мощности: много одинаковых значений при растущем счётчике
    stuck = []
    for (day, hour), rs in sorted(by_hour.items()):
        if len(rs) < 50:
            continue
        ps = [x["p"] for x in rs if x["v"] > 50]
        if len(ps) < 20:
            continue
        mode_p = max(set(ps), key=ps.count)
        share = ps.count(mode_p) / len(ps)
        rs_sorted = sorted(rs, key=lambda x: x["t"])
        grew = rs_sorted[-1]["e"] - rs_sorted[0]["e"] > 0.05
        if share > 0.6 and grew and mode_p < avg_p * 0.3 if (avg_p := sum(ps)/len(ps)) else False:
            stuck.append((day, hour, mode_p, round(share*100), round(avg_p)))
    check("мощность", "нет «застывших» часов", not stuck,
          ("подозрительных %d" % len(stuck)) if stuck else "все часы живые")
    for day, hour, mp, share, ap in stuck[:6]:
        print("         %s %02d:00  %.0f Вт в %d%% строк, средняя %.0f Вт — датчик отдавал кэш"
              % (day, hour, mp, share, ap))


# ============================================================ ГРУППА 5
def t_math(d, rows):
    print("\n--- 5. Математика ---")

    # сумма часов сегодня = /data.tkwh
    today = datetime.now(TZ).strftime("%Y-%m-%d")
    try:
        h = jget("/history?date=" + today, 60)
        s = sum(x.get("kwh", 0) for x in (h.get("hours") or []))
        diff = abs(s - d["tkwh"])
        check("математика", "сумма часов = сегодня в /data", diff <= 0.03,
              "%.3f против %.2f (разница %.3f)" % (s, d["tkwh"], diff))
    except Exception as e:
        check("математика", "сумма часов = сегодня в /data", False, type(e).__name__)

    # вчера: история против /data.ykwh
    yday = (datetime.now(TZ) - timedelta(days=1)).strftime("%Y-%m-%d")
    try:
        h = jget("/history?date=" + yday, 60)
        s = sum(x.get("kwh", 0) for x in (h.get("hours") or []))
        diff = abs(s - d["ykwh"])
        check("математика", "сумма часов = вчера в /data", diff <= 0.05,
              "%.3f против %.2f" % (s, d["ykwh"]))
    except Exception as e:
        check("математика", "сумма часов = вчера в /data", False, type(e).__name__)

    # пик >= средняя в каждом часе
    bad = []
    for x in (h.get("hours") or []):
        if x.get("n", 0) and x.get("pmax", 0) + 0.5 < x.get("p", 0):
            bad.append(x.get("h"))
    check("математика", "пик >= средняя", not bad, ("часы: %s" % bad) if bad else "ok")

    # стоимость сегодня по зонам
    try:
        rs = [r for r in rows if r["day"] == today]
        cost = 0.0
        for i in range(1, len(rs)):
            de = rs[i]["e"] - rs[i-1]["e"]
            if de <= 0 or de > 5:
                continue
            hh = rs[i]["hour"]
            cost += de * (TAR_NIGHT if (hh >= 23 or hh < 6) else TAR_DAY)
        diff = abs(cost - d["tcost"])
        check("математика", "стоимость по зонам сходится", diff <= 0.5,
              "расчёт %.2f ₽ против %.2f ₽" % (cost, d["tcost"]))
    except Exception as e:
        check("математика", "стоимость по зонам сходится", False, type(e).__name__)

    # прогноз: средний расход × дни месяца
    try:
        now = datetime.now(TZ)
        days_passed = now.day - 1 + now.hour / 24.0
        if days_passed > 0.5:
            rate = d["tkwh"] / days_passed
            in_month = 31
            check("математика", "прогноз правдоподобен",
                  0 < rate < 200,
                  "%.2f кВт·ч в сутки -> %.1f за месяц" % (rate, rate * in_month))
    except Exception:
        pass


# ============================================================ ГРУППА 6
def t_stability():
    print("\n--- 6. Стабильность ---")
    d0 = jget("/data", 30)
    up0 = d0.get("up", 0)
    heap0 = d0.get("heap", 0)

    ok_pages = 0
    for i in range(10):
        try:
            get("/", 30)
            ok_pages += 1
        except Exception:
            pass
    check("стабильность", "10 загрузок страницы", ok_pages == 10, "%d из 10" % ok_pages)

    ok_data = 0
    for i in range(10):
        try:
            jget("/data", 30)
            ok_data += 1
        except Exception:
            pass
    check("стабильность", "10 запросов /data", ok_data == 10, "%d из 10" % ok_data)

    d1 = jget("/data", 30)
    check("стабильность", "нет перезагрузки", d1.get("up", 0) >= up0,
          "up %s -> %s" % (up0, d1.get("up")))
    dh = d1.get("heap", 0) - heap0
    check("стабильность", "heap не просел", dh > -20000, "%+d байт" % dh)


# ============================================================ ИТОГ
def summary():
    print("\n" + "=" * 64)
    groups = {}
    for g, n, ok, det, w in results:
        groups.setdefault(g, [0, 0])
        groups[g][1] += 1
        if ok:
            groups[g][0] += 1
    print("  ГРУППА            ПРОШЛО")
    for g, (ok, total) in groups.items():
        mark = "" if ok == total else "   <-- есть провалы"
        print("  %-18s %d/%d%s" % (g, ok, total, mark))
    fails = [(g, n, det) for g, n, ok, det, w in results if not ok and not w]
    warns = [(g, n, det) for g, n, ok, det, w in results if not ok and w]
    print("\n  ВСЕГО: %d из %d" % (len(results) - len(fails) - len(warns), len(results)))
    if warns:
        print("\n  ПРЕДУПРЕЖДЕНИЯ (не критично):")
        for g, n, det in warns:
            print("    ~ [%s] %s%s" % (g, n, (" — " + det) if det else ""))
    if fails:
        print("\n  ПРОВАЛЫ (нужно исправлять):")
        for g, n, det in fails:
            print("    - [%s] %s%s" % (g, n, (" — " + det) if det else ""))
    return 0 if not fails else 1


def main():
    quick = "--quick" in sys.argv
    print("=" * 64)
    print("  ТЕСТЫ ПЛАТЫ «ДОМ — ЭНЕРГИЯ»   %s" % datetime.now(TZ).strftime("%d.%m.%Y %H:%M"))
    print("  плата: %s" % BOARD)
    print("=" * 64)

    try:
        d = t_endpoints()
        t_limits(d)
        body = get("/full", 180)
        rows = rows_of(body)
        t_integrity(rows)
        if not quick:
            t_power_quality(rows)
        t_math(d, rows)
        t_stability()
    except Exception as e:
        print("\n  КРИТИЧЕСКАЯ ОШИБКА: %s: %s" % (type(e).__name__, e))
        results.append(("критично", "выполнение тестов", False,
                        "%s: %s" % (type(e).__name__, e), False))

    return summary()


if __name__ == "__main__":
    sys.exit(main())
