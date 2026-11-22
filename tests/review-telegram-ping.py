"""Оффлайн-тест пинг-бота Telegram (DomTelegramPing.ps1) — без сети.

Поднимает два mock-сервера:
• mock Telegram API (getUpdates/sendMessage, включая 429 с Retry-After);
• mock платы (/data,/storage,/measure,/gapfill/status,/boots) с фикстурами.

Проверки:
1) «пинг» из личного чата -> ответ со всеми значениями платы;
2) авто-привязка первого личного чата (tgChatId не задан) -> tg-ping-config.json;
3) чужой чат при заданном tgChatId — игнор;
4) битый tgChatId — fail-closed (exit 2, никакой привязки);
5) группа — игнор;
6) «помощь» -> справка;
7) 429 + Retry-After от Telegram -> повтор и успешная доставка;
8) повторный проход без updates не шлёт ответ (offset);
9) нет токена -> скрипт не падает и пишет пояснение в лог.

Запуск: python tests/review-telegram-ping.py
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PS = os.environ.get("PS_EXE", r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
SCRIPT = r"C:\esp32\tools\DomTelegramPing.ps1"
TOKEN = "123456789:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
BOARD = {
    "/data": {
        "build": "test-build-id", "fw": "4.14", "v": 237, "temp": 21.4, "p": 18.0, "amp": 0.15,
        "e": 38.31, "r": True, "tkwh": 1.42, "tcost": 11.48, "ykwh": 0.442, "ykost": 3.18,
        "mkwh": 16.8, "mcost": 133.27, "tariff": 8.09, "tday": 8.39, "tnight": 4.49,
        "rssi": -52, "sdRows": 46636, "vAge": 8, "tAge": 13, "net": True, "heap": 105900,
        "up": 7173, "stale": False, "gaps": 0, "gapMin": 0, "gapFillOk": 0,
        "tok": 1, "cHold": 1, "cOk": 1, "cTry": 1, "cFail": 0,
    },
    "/storage": {"ok": True, "freeBytes": 125793468416, "writeOk": 8, "writeFail": 0, "lastWriteAgeSec": 11},
    "/measure": {"voltage": {"source": "local", "ageSec": 7}},
    "/gapfill/status": {"commitStage": "idle", "gaps": 0},
    "/boots": "2026-11-16 19:23:00 rr=OTA(11) rrn=3 up=1841\n",
}

STATE = {"updates": [], "sent": [], "fail429": 0}


class TgHandler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path.endswith("/getMe"):
            self._json({"ok": True, "result": {"id": 1}})
            return
        if path.endswith("/getUpdates"):
            self._json({"ok": True, "result": STATE["updates"].pop(0) if STATE["updates"] else []})
            return
        self._json({"ok": False}, 404)

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        data = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode("utf-8", "replace")))
        if not path.endswith("/sendMessage"):
            self._json({"ok": False}, 404)
            return
        if STATE["fail429"] > 0:
            STATE["fail429"] -= 1
            body = json.dumps({"ok": False, "error_code": 429, "description": "Too Many Requests",
                               "parameters": {"retry_after": 1}}).encode()
            self.send_response(429)
            self.send_header("Content-Type", "application/json")
            self.send_header("Retry-After", "1")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        STATE["sent"].append(data)
        self._json({"ok": True, "result": {"message_id": len(STATE["sent"])}})


class BoardHandler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path not in BOARD:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        val = BOARD[path]
        body = val.encode() if isinstance(val, str) else json.dumps(val, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def make_update(update_id, chat_id, text, chat_type="private"):
    return {
        "update_id": update_id,
        "message": {
            "message_id": update_id,
            "from": {"id": chat_id, "is_bot": False, "first_name": "Owner"},
            "chat": {"id": chat_id, "type": chat_type},
            "date": int(time.time()),
            "text": text,
        },
    }


def main():
    tg_port, board_port = 8791, 8792
    tg_srv = ThreadingHTTPServer(("127.0.0.1", tg_port), TgHandler)
    board_srv = ThreadingHTTPServer(("127.0.0.1", board_port), BoardHandler)
    for s in (tg_srv, board_srv):
        threading.Thread(target=s.serve_forever, daemon=True).start()
    problems = []
    tmp = tempfile.mkdtemp(prefix="tgping-")
    base_settings = {"tgBotToken": TOKEN, "tgChatId": 777}
    own_cfg = os.path.join(tmp, "tg-ping-config.json")

    def write_json(path, d):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False)

    def clear_files(names=("tg-ping-chat.json", "tg-ping-offset.json", "tg-ping-config.json")):
        for n in names:
            fp = os.path.join(tmp, n)
            if os.path.exists(fp):
                os.remove(fp)

    def run(label, settings=None, extra=(), expected=(0,)):
        STATE["sent"] = []
        if settings is not None:
            write_json(os.path.join(tmp, "settings.json"), settings)
        cmd = [PS, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", SCRIPT,
               "-Once", "-ApiBase", f"http://127.0.0.1:{tg_port}",
               "-BoardUrl", f"http://127.0.0.1:{board_port}",
               "-SettingsPath", os.path.join(tmp, "settings.json"),
               "-StateDir", tmp, "-NoTokenSleepSec", "1",
               "-MutexName", "Global\\DomTelegramPing_Test_" + str(int(time.time() * 1000) % 1000000 + len(label))] + list(extra)
        t0 = time.time()
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=240)
        ok = proc.returncode in expected
        print(f"  [{'OK' if ok else 'FAIL'}] {label} · exit={proc.returncode} · {time.time()-t0:.1f} с")
        if not ok:
            problems.append(f"{label}: exit={proc.returncode} stderr={proc.stderr[-300:]}")
            print("    stderr:", proc.stderr[-300:])
        return proc

    print("1) пинг из личного чата, tgChatId=777")
    write_json(os.path.join(tmp, "settings.json"), base_settings)
    clear_files(); clear_files(("settings.json.tg-bak",))
    STATE["updates"] = [[make_update(100, 777, "пинг")]]
    run("пинг -> ответ")
    sent = STATE["sent"]
    if len(sent) != 1:
        problems.append(f"ожидался 1 ответ, получено {len(sent)}")
    else:
        text = sent[0].get("text", "")
        must = ["ДОМ — ЭНЕРГИЯ", "Плата: сборка test-build-id", "напряжение", "кВт·ч", "Карта", "LAN", "Пропуски"]
        missing = [m for m in must if m not in text]
        if missing:
            problems.append(f"в ответе нет полей: {missing}")
        print(f"  ответ {len(text)} симв., начало: {text[:80]!r}")
        if str(sent[0].get("chat_id")) != "777":
            problems.append("ответ ушёл не в тот чат")
    if not os.path.exists(os.path.join(tmp, "tg-ping-offset.json")):
        problems.append("offset не сохранён")

    print("2) авто-привязка первого личного чата (tgChatId не задан)")
    write_json(os.path.join(tmp, "settings.json"), {"tgBotToken": TOKEN})
    clear_files()
    STATE["updates"] = [[make_update(200, 4242, "пинг")]]
    run("авто-привязка")
    if len(STATE["sent"]) != 1:
        problems.append(f"авто-привязка: ожидался 1 ответ, получено {len(STATE['sent'])}")
    if os.path.exists(own_cfg):
        try:
            with open(own_cfg, encoding="utf-8") as f:
                oc = json.load(f)
            if int(oc.get("tgChatId", 0) or 0) != 4242:
                problems.append(f"авто-привязка: tgChatId не записан в tg-ping-config.json ({oc.get('tgChatId')})")
        except Exception as e:  # noqa: BLE001
            problems.append(f"авто-привязка: tg-ping-config.json не читается: {e}")
    else:
        problems.append("авто-привязка: не создан tg-ping-config.json")

    print("3) чужой чат при заданном tgChatId — игнор")
    write_json(os.path.join(tmp, "settings.json"), base_settings)
    clear_files()
    STATE["updates"] = [[make_update(300, 999, "пинг")]]
    run("чужой чат")
    if STATE["sent"]:
        problems.append("чужой чат получил ответ")

    print("4) битый tgChatId «abc» — fail-closed (exit 2, без привязки)")
    clear_files()
    STATE["updates"] = [[make_update(350, 5555, "пинг")]]
    proc = run("битый tgChatId", settings={"tgBotToken": TOKEN, "tgChatId": "abc"}, expected=(2,))
    if proc.returncode != 2:
        problems.append(f"битый tgChatId: ожидался exit 2, получен {proc.returncode}")
    if STATE["sent"]:
        problems.append("битый tgChatId: бот всё равно ответил")
    if os.path.exists(own_cfg):
        with open(own_cfg, encoding="utf-8") as f:
            oc = json.load(f)
        if oc.get("tgChatId"):
            problems.append("битый tgChatId: всё-таки привязал чат")

    print("5) группа — игнор")
    write_json(os.path.join(tmp, "settings.json"), base_settings)
    clear_files()
    STATE["updates"] = [[make_update(400, -100123, "пинг", chat_type="group")]]
    run("группа")
    if STATE["sent"]:
        problems.append("группа получила ответ")

    print("6) помощь")
    STATE["updates"] = [[make_update(450, 777, "помощь")]]
    run("помощь")
    if not STATE["sent"] or "Команды" not in STATE["sent"][0].get("text", ""):
        problems.append("помощь: нет справки")

    print("7) 429 + Retry-After -> повтор и доставка")
    STATE["updates"] = [[make_update(500, 777, "пинг")]]
    STATE["fail429"] = 1
    run("429 retry")
    if len(STATE["sent"]) != 1:
        problems.append(f"429: ответ не доставлен после повтора (sent={len(STATE['sent'])})")

    print("8) повторный проход без updates не шлёт ответ (offset)")
    STATE["updates"] = []
    run("пустые updates")
    if STATE["sent"]:
        problems.append("повторный проход отправил ответ без новых сообщений")

    print("9) нет токена: скрипт не падает")
    write_json(os.path.join(tmp, "settings.json"), {"tgChatId": 777})
    clear_files()
    run("нет токена")
    log_path = os.path.join(tmp, "tg-ping.log")
    if not os.path.exists(log_path):
        problems.append("нет токена: не создан лог")
    else:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            if "tgBotToken" not in f.read():
                problems.append("нет токена: в логе нет пояснения")

    tg_srv.shutdown(); board_srv.shutdown()
    print()
    if problems:
        print("ПРОВАЛ:")
        for p in problems:
            print(" -", p)
        sys.exit(1)
    print("ВСЁ ОК: 9 проверок пинг-бота прошли (оффлайн, mock Telegram + mock плата)")


if __name__ == "__main__":
    main()
