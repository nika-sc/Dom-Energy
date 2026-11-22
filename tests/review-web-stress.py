"""P0.3 hardening: устойчивость веб-сервера к медленным и параллельным клиентам.

Проверяет:
1) 120 последовательных /data — без отказов;
2) 8 «медленных» клиентов, которые запрашивают /measure и не читают ответ:
   /data всё равно отвечает быстро (ограниченная отправка закрывает их сама);
3) 160 параллельных коротких запросов /measure из 8 потоков — сервер не зависает,
   после нагрузки /data и / отвечают.
Только чтение; ?reset=1 не используется.
"""
import json
import socket
import statistics
import threading
import time
import urllib.parse
import urllib.request

import os
BASE = os.environ.get("BOARD_URL", "http://192.168.1.51").rstrip("/")
HOST = urllib.parse.urlparse(BASE).hostname
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def get(path, timeout=20):
    t0 = time.perf_counter()
    try:
        b = OPENER.open(BASE + path, timeout=timeout).read()
        return (time.perf_counter() - t0) * 1000, len(b), True
    except Exception:  # noqa: BLE001
        return (time.perf_counter() - t0) * 1000, 0, False


def stats(values):
    values = sorted(values)
    return dict(n=len(values), p50=round(values[len(values) // 2], 1),
                p95=round(values[max(0, int(len(values) * 0.95) - 1)], 1),
                max=round(values[-1], 1))


def main():
    out = {}

    # 1) последовательные запросы
    vals, fails = [], 0
    for _ in range(120):
        ms, _n, ok = get("/data")
        if ok:
            vals.append(ms)
        else:
            fails += 1
    out["sequential"] = dict(stats=stats(vals), fails=fails)
    print("1) 120 последовательных /data:", out["sequential"])

    # 2) медленные клиенты: запросили большой файл и не читают
    slow = []
    try:
        for _ in range(8):
            s = socket.socket()
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512)
            s.settimeout(5)
            s.connect((HOST, 80))
            s.sendall(b"GET /measure HTTP/1.1\r\nHost: dev\r\nConnection: close\r\n\r\n")
            slow.append(s)
        time.sleep(0.2)
        ms, _n, ok = get("/data", timeout=10)
        out["withSlowClients"] = dict(dataMs=round(ms, 1), ok=ok, slow=len(slow))
        print("2) /data при 8 медленных клиентах:", out["withSlowClients"])
    finally:
        for s in slow:
            try:
                s.close()
            except OSError:
                pass

    # 3) параллельные короткие запросы
    results = []
    lock = threading.Lock()

    def worker():
        for _ in range(20):
            ms, _n, ok = get("/measure", timeout=15)
            with lock:
                results.append((ms, ok))

    threads = [threading.Thread(target=worker) for _ in range(8)]
    t0 = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = round(time.time() - t0, 1)
    ok_n = sum(1 for _ms, ok in results if ok)
    vals3 = [ms for ms, ok in results if ok]
    out["parallelMeasure"] = dict(total=len(results), ok=ok_n, seconds=elapsed, stats=stats(vals3) if vals3 else {})
    print("3) 160 параллельных /measure:", out["parallelMeasure"])

    # после нагрузки
    ms, n, ok = get("/data", timeout=15)
    ms2, n2, ok2 = get("/", timeout=20)
    out["after"] = dict(dataMs=round(ms, 1), dataOk=ok, pageMs=round(ms2, 1), pageBytes=n2, pageOk=ok2)
    print("после нагрузки:", out["after"])

    problems = []
    if out["sequential"]["fails"]:
        problems.append("последовательные отказы: %d" % out["sequential"]["fails"])
    if out["sequential"]["stats"]["p95"] > 800:
        problems.append("p95 /data %s > 800 мс" % out["sequential"]["stats"]["p95"])
    if not out["withSlowClients"]["ok"] or out["withSlowClients"]["dataMs"] > 5000:
        problems.append("медленные клиенты блокируют сервер: %r" % (out["withSlowClients"],))
    if ok_n < len(results) * 0.9:
        problems.append("параллельные /measure: успешно %d из %d" % (ok_n, len(results)))
    if not (out["after"]["dataOk"] and out["after"]["pageOk"]):
        problems.append("после нагрузки сервер не отвечает: %r" % (out["after"],))

    print(json.dumps(out, ensure_ascii=False, indent=2))
    if problems:
        print("\nПРОВАЛ: " + "; ".join(problems))
        raise SystemExit(1)
    print("\nОК: веб-сервер устойчив к медленным и параллельным клиентам")


if __name__ == "__main__":
    main()
