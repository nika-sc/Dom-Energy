# Как получить ключи Tuya: Device ID и Local Key

Инструкция для своего реле Tuya, которое управляется по локальной сети (протокол 3.5).

## Зачем нужны эти ключи

| Ключ | Что это | Зачем |
|---|---|---|
| **Device ID** | идентификатор устройства в облаке Tuya | по нему облако отдаёт историю |
| **Local Key** | 16-символьный ключ шифрования | им шифруется весь обмен с реле по порту 6668 |
| **Client ID** | логин приложения в облаке Tuya | подпись запросов к облаку |
| **Client Secret** | пароль приложения | HMAC-подпись запросов |

Без `Local Key` реле не ответит — оно проверит подпись `HMAC-SHA256(local_key, nonce)` и оборвёт соединение.

---

## Схема получения

```
1. Учётная запись Tuya IoT Platform  (iot.tuya.com)
        ↓
2. Cloud-проект + Data Center
        ↓
3. Привязка аккаунта мобильного приложения (QR-код)
        ↓
4. Client ID + Client Secret  из проекта
        ↓
5. Device ID  из списка устройств
        ↓
6. GET /v1.0/devices/{device_id}  →  local_key
```

---

## Шаг 1. Регистрация на платформе

1. Открыть **https://iot.tuya.com** → **Sign Up**
2. Зарегистрироваться (почта + пароль)
3. Подтвердить почту
4. Войти в **Cloud** → **Development**

## Шаг 2. Создать Cloud-проект

1. **Create Cloud Project**
2. Заполнить:
   - **Project Name** — любое, например `home-energy`
   - **Industry** — `Smart Home`
   - **Development Method** — `Smart Home`
   - **Data Center** — **важно выбрать правильный!**

### Какой Data Center выбирать

| Data Center | Регион | Когда выбирать |
|---|---|---|
| **Central Europe** | `openapi.tuyaeu.com` | Россия, Европа — **наш случай** |
| Western America | `openapi.tuyaus.com` | США |
| Eastern America | `openapi-use.tuyaus.com` | США (восток) |
| India | `openapi.tuyain.com` | Индия |
| China | `openapi.tuyacn.com` | Китай |

> **Если выбрать не тот Data Center** — устройства не появятся в списке, хотя в приложении они есть. Симптом: список пустой, ошибка `1106` (permission deny).

## Шаг 3. Привязать аккаунт приложения

1. В проекте открыть вкладку **Devices** → **Link Tuya App Account**
2. Нажать **Add App Account**
3. Появится **QR-код**
4. Открыть мобильное приложение **Tuya Smart** или **Smart Life**
5. Профиль → **Scan** (сканировать QR) → подтвердить

После этого все устройства из приложения появятся в проекте.

> **Если QR не сканируется** — у приложения и проекта должны совпадать Data Center. Для России обычно `Central Europe`.

## Шаг 4. Получить Client ID и Client Secret

В проекте: **Overview** → вкладка **Authorization**

```
Access ID / Client ID:      YOUR_CLIENT_ID
Access Secret / Client Secret:  <32 символа>
```

Эти два значения нужны для подписи запросов к облаку.

## Шаг 5. Узнать Device ID

**Devices** → **All Devices** → скопировать **Device ID** нужного устройства.

Он выглядит как `YOUR_DEVICE_ID` — 20 символов, буквы и цифры.

## Шаг 6. Запросить Local Key — главное

**Local Key не показывается в интерфейсе.** Его отдаёт только API:

```
GET https://openapi.tuyaeu.com/v1.0/devices/{device_id}
```

### Пример на Python

```python
import hashlib, hmac, time, json, urllib.request

CID    = "ваш_client_id"
SECRET = "ваш_client_secret"
DEVICE = "ваш_device_id"
HOST   = "https://openapi.tuyaeu.com"


def headers(method, sign_path, body, token):
    """Заголовки с подписью. Формат строки — строго по Tuya v1.0:

        метод + "\n" + sha256(тело) + "\n\n" + путь_с_query

    Два перевода строки подряд — потому что между хэшем тела и путём
    в протоколе стоят пустые заголовки. Если сделать один "\n" —
    придёт 1004 «sign invalid».
    """
    ts = str(int(time.time() * 1000))          # МИЛЛИСЕКУНДЫ, не секунды
    content = hashlib.sha256(body.encode("utf-8")).hexdigest()
    str_to_sign = "%s\n%s\n\n%s" % (method, content, sign_path)
    sign = hmac.new(SECRET.encode(), str_to_sign.encode(),
                    hashlib.sha256).hexdigest().upper()
    h = {
        "client_id": CID,
        "sign": sign,
        "t": ts,
        "sign_method": "HMAC-SHA256",
        "Content-Type": "application/json",
    }
    if token:
        h["access_token"] = token
    return h


def call(method, path, body="", token=None):
    req = urllib.request.Request(HOST + path, method=method,
                                 data=body.encode() if body else None,
                                 headers=headers(method, path, body, token))
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read())


# 1) токен
tok = call("GET", "/v1.0/token?grant_type=1")["result"]["access_token"]
time.sleep(1.5)          # пауза больше 0.8 с, иначе 40000309

# 2) устройство — local_key внутри result
res = call("GET", "/v1.0/devices/%s" % DEVICE, token=tok)["result"]

print("Device ID :", res["id"])
print("Local Key :", res["local_key"])     # ← вот он
print("Название  :", res["name"])
print("Категория :", res["category"])
```

> **Внимание:** поля лежат внутри `result`, а не в корне ответа. Если обратиться
> к `dev["id"]` напрямую — будет `KeyError`.

### Что вернёт API

Внутри `result` — 22 поля, среди них:
### Что вернёт API

22 поля, среди них:

```
id          = YOUR_DEVICE_ID
local_key   = YOUR_LOCAL_KEY     ← 16 символов, это он
name        = Breaker
category    = dlq
online      = True
ip          = 203.0.113.17
product_name= Breaker
```

---

## Грабли, на которые мы наступили

### 1. Время в миллисекундах, а не секундах

```python
ts = str(int(time.time() * 1000))    # правильно
ts = str(int(time.time()))           # ошибка 1004 «sign invalid»
```

Подпись считается по строке времени — с секундами вместо миллисекунд она не сойдётся.

### 2. Параметры в подписи — по алфавиту и БЕЗ URL-кодирования

```python
# правильно
path = "/v1.0/devices/%s/report-logs?codes=phase_a&start_time=1&end_time=2"
# неправильно — %2C вместо запятой в codes
path = "/v1.0/devices/%s/report-logs?codes=phase_a%2Ctemp_current&..."
```

Если в подписи закодировать запятую, а в запросе — нет (или наоборот), придёт `1004`.

### 3. Пауза между запросами больше 0.8 секунды

```
40000309  «request too frequent»
```

Ставить `time.sleep(1.3)` между запросами.

### 4. `size` не больше 100

```
size=100  — максимум
size=200  — ошибка
```

### 5. `report-logs` отдаёт записи от НОВЫХ к СТАРЫМ

```
size=100 покрывает ~27 минут (100 × 16 секунд)
```

Чтобы получить весь час — три окна по 20 минут. Мы на этом потеряли половину данных: пик показывался 1558 Вт вместо 1833.

### 6. Local Key меняется при перепривязке

Если устройство удалить из приложения и добавить заново — **Local Key будет другим**. Прошивку придётся обновить.

---

## Куда вписать ключи

В файл `secrets.h` рядом с прошивкой (он в `.gitignore`):

```cpp
#define WIFI_SSID     "ваша_сеть"
#define WIFI_PASS     "ваш_пароль"

#define TUYA_DEVICE_ID "YOUR_DEVICE_ID"
#define TUYA_LOCAL_KEY "YOUR_LOCAL_KEY"

#define TUYA_CLIENT_ID "YOUR_CLIENT_ID"
#define TUYA_SECRET    "0123456789abcdef0123456789abcdef"

#define RELAY_IP  "192.168.1.25"
```

Шаблон — `secrets.example.h`.

---

## Проверка, что ключ верный

```python
import socket, binascii

s = socket.create_connection(("192.168.1.25", 6668), timeout=5)
s.send(binascii.unhexlify("000055aa000000000000000a00000000"))
resp = s.recv(1024)
# 000055aa — префикс ответа = соединение установлено
```

Если реле ответило `000055aa` — порт открыт. Дальше идёт рукопожатие с `local_key`; если ключ неверный, соединение оборвётся после второго пакета.

---

## Ссылки

- Tuya IoT Platform — https://iot.tuya.com
- Документация API — https://developer.tuya.com/en/docs/cloud/
- Наш протокол в деталях — [АРХИТЕКТУРА.md](АРХИТЕКТУРА.md), раздел 7 «Tuya local: протокол 3.5»
