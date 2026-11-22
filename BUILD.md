# Сборка и прошивка (CYD, ESP32-2432S028)

> Актуально на **4.14** (21.11.2026): образ **1 630 192 Б** = **82,9 %
** от
> слота `min_spiffs` 1 966 080 Б; сборка `6fe4b87d3a861ba37f1f`.
> Страница `/` ~86,3 КБ (86 331 Б отдаётся) Б, `/app.css` 27 437 Б, `/app.js` 56 770 Б,
> `/history.js` 63 122 Б, `/control.js` 5 868 Б; `page_html.h` 86 655 Б
> (лимит `PAGE_CAP` 96 000), TFT_eSPI 2.5.44, ArduinoJson 7.4.3,
> PubSubClient 2.8.

> **Про образ в архиве:** полная сборка с вашими `secrets.h` и `control-secret.h` — 1 630 192 Б (82,9 %). В архиве лежит **демо-образ** 1 628 944 Б, собранный с `secrets.example.h` и без ключа управления: изменяющие маршруты в нём отвечают 503, пока вы не соберёте прошивку со своими секретами.


> Это инструкция для **CYD**: плата ESP32-2432S028, экран 2.8" 240×320,
> тач XPT2046. Старая документация `02-ПРОШИВКА-ESP32.md` — про другую
> плату (ESP32-C3 + OLED). Она **не подходит**.

## 1. Что нужно

### Железо

| Что | Значение |
|---|---|
| Плата | **ESP32-2432S028** (CYD), ESP32-D0WD-V3, 4 МБ flash |
| Экран | ILI9341, 2.8", 240×320 |
| Тач | XPT2046 |
| Карта | microSD, FAT32 (8–32 ГБ) |
| Питание | USB 5 В, 1 А |
| Кабель | USB с передачей данных, не «только зарядка» |

### Софт

| Компонент | Версия | Примечание |
|---|---|---|
| Arduino CLI | **1.5.1** | `C:\Program Files\Arduino CLI\arduino-cli.exe` |
| Ядро `esp32:esp32` | **2.0.9** | на 3.x не собирается |
| Библиотека `TFT_eSPI` | **2.5.44** | обязательно со своим `User_Setup.h` |
| `ArduinoJson` | **7.4.3** | разбор JSON |
| `PubSubClient` | **2.8** | MQTT / Home Assistant |
| Python | 3.9+ | только для `web/build_page.py` |
| Драйвер USB-UART | CH340/CP210x | если COM-порт не появляется |

`WebServer`, `SD`, `SPI`, `Preferences`, `HTTPClient`, `WiFiClientSecure`,
`mbedtls` входят в ядро ESP32. TLS-корни Tuya и OpenWeatherMap лежат в
`trusted_roots.h` — отдельный сертификат ставить не нужно.

## 2. Установка ядра и библиотеки

```powershell
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.9
arduino-cli lib install TFT_eSPI@2.5.44
arduino-cli lib install ArduinoJson@7.4.3 PubSubClient@2.8
```

Проверка:

```powershell
arduino-cli core list
arduino-cli lib list | Select-String TFT_eSPI
```

Должно быть `esp32:esp32 2.0.9` и `TFT_eSPI 2.5.44`.

## 3. `User_Setup.h` — критично, лежит ВНЕ скетча

`TFT_eSPI` берёт пины и драйвер не из папки прошивки, а из своей библиотеки:

```
%USERPROFILE%\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
```

Скопируйте файл из репозитория:

```powershell
Copy-Item firmware\hw\User_Setup.h `
  "$env:USERPROFILE\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h" -Force
```

Что внутри:

```cpp
#define ILI9341_2_DRIVER
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
```

Без этого файла экран, скорее всего, будет чёрным: библиотека соберётся
с «generic» настройками. После замены **удалите старую сборку** и соберите
заново — иначе останется старый объектный файл:

```powershell
Remove-Item build -Recurse -Force -ErrorAction SilentlyContinue
```

Не редактируйте `User_Setup_Select.h` «на всякий случай»: по умолчанию он
подключает именно `User_Setup.h`.

## 4. `secrets.h`

```powershell
cd firmware\cydv4
Copy-Item secrets.example.h secrets.h
notepad secrets.h
```

Заполните:

```cpp
#define WIFI_SSID      "ваша_сеть_2.4ghz"
#define WIFI_PASS      "пароль"
#define TUYA_DEVICE_ID "..."
#define TUYA_LOCAL_KEY "..."
#define TUYA_CLIENT_ID "..."
#define TUYA_SECRET    "..."
#define RELAY_IP       "192.168.1.25"
#define PC_API_URL     "http://192.168.1.20:8090"
#define WEATHER_LAT    "YOUR_LAT"
#define WEATHER_LON    "YOUR_LON"
#define WEATHER_CITY   "ВАШ ГОРОД"
```

- `secrets.h` уже в `.gitignore`, в репозиторий не попадёт.
- Wi-Fi только **2,4 ГГц**.
- Если отдельного PC-сервера нет, поставьте `PC_API_URL ""` — плата будет
  пропускать этот шаг.
- `WEATHER_LAT/LON` — координаты вашего города; можно оставить пустыми,
  тогда погода будет «НЕТ ДАННЫХ».

## 5. Сборка веб-страницы

Страница и её ресурсы лежат в `firmware/cydv4/web/` и собираются в два
заголовка, которые подключает прошивка:

| Исходник | Куда | Что |
|---|---|---|
| `web/page.html` | `page_html.h` (`PAGE_TMPL`) | разметка страницы, лимит `PAGE_CAP` 96 000 Б |
| `web/app.css` | `assets.h` (`APP_CSS`) | `/app.css` |
| `web/app.js` | `assets.h` (`APP_JS`) | `/app.js` |
| `web/history.js` | `assets.h` (`HISTORY_JS`) | `/history.js` |
| `web/control.js` | `assets.h` (`CONTROL_JS`) | `/control.js` |

```powershell
cd firmware\cydv4
python web\build_page.py
```

Скрипт:

- проверяет, что в HTML нет `)HTML"` (иначе сломается raw-строка);
- проверяет **19 плейсхолдеров** (`__FW__`, `__MV__`, …);
- не даёт превысить лимиты: каждый ресурс < `ASSET_LIMIT` 64 512 Б,
  `page_html.h` < `PAGE_CAP` 96 000 Б — при превышении падает **до** записи
  файлов, чтобы не оставить полусобранную прошивку.

После изменения страницы **всегда** запускайте сборку и коммитьте и исходники, и
сгенерированные `page_html.h` / `assets.h`.

`build_identity.h` (идентификатор сборки) генерирует загрузчик при сборке;
он попадает в `/data.build` и в `*.manifest.json` рядом с образом, но
**секретов не содержит**.

## 6. Компиляция

Из корня репозитория:

```powershell
$cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"

& $cli compile `
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
  --libraries "$env:USERPROFILE\Documents\Arduino\libraries" `
  --build-path "build" `
  "firmware\cydv4"
```

Почему `PartitionScheme=min_spiffs`:

- образ 4.14 занимает **1 630 192 Б** (около 1,55 МБ);
- в `min_spiffs` слот приложения — **1 966 080 Б** (1,875 МиБ), занято **82,9 %
**;
- в стандартном `default` раздел 1,3 МБ — образ туда уже не влезает, да и
  второго OTA-слота для обновления по Wi-Fi там нет.

Ожидаемый результат (Arduino CLI 1.5.1): строки о размере кода и глобальных
данных, без `Sketch too big`. Ориентир на 4.14: код ~1,63 МБ, глобальные
данные ~120 КБ, `heap` после загрузки 80–120 КБ. Точные цифры зависят от
версий библиотек.

## 7. Прошивка

Плата — в COM5 (проверьте в диспетчере устройств; если другой — замените).

```powershell
& $cli upload `
  -p COM5 `
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
  --upload-property "upload.speed=921600" `
  --input-dir "build" `
  "firmware\cydv4"
```

Скорость **921600**, не 115200: на 115200 бывают срывы. Если порт не
открывается:

1. Проверьте кабель (не «только зарядка»).
2. Поставьте драйвер CH340/CP210x.
3. Зажмите **BOOT**, коротко нажмите **RST**, отпустите BOOT — и повторите.
4. Убедитесь, что монитор порта не держит COM5.

## 8. Проверка после прошивки

Монитор порта на **115200**:

```
=== ДОМ 4.14 ===
SD: ok, строк 61087
веб: http://192.168.1.51/
```

Затем:

```powershell
# страница
start http://192.168.1.51/

# данные
curl http://192.168.1.51/data
```

Ожидаемо: JSON с полями `v`, `temp`, `p`, `amp`, `e`, `r`, `tkwh`, `tcost`,
`ykwh`, `ykost`, `mkwh`, `mcost`, `tariff`, `rssi`, `sdRows`, `fw`/`build`,
`vSeen`/`pSeen`/`tSeen`, `stale`. Отдельно проверьте `/measure`
(источник и возраст), `/storage` (запись на карту) и `/climatehist`
(климат за период).

Если экран показывает «НЕТ ДАННЫХ»:

- `NET` в serial — есть ли Wi-Fi/IP;
- `DIAG` — `gSdOk`, `gTuyaOk`, `gRssi`;
- `ping <IP реле>`, порт 6668;
- `WCLR` — сбросить погоду и проверить заново.

## 9. Частые ошибки

| Ошибка | Причина | Решение |
|---|---|---|
| `secrets.h: No such file` | не скопирован шаблон | `Copy-Item secrets.example.h secrets.h` |
| `Sketch too big` | собрано без `min_spiffs` | добавить `PartitionScheme=min_spiffs` |
| `TFT_eSPI` ошибки `_spi_cmd` | не тот `User_Setup.h` | скопировать из `firmware/hw/` |
| Экран чёрный, но serial работает | `User_Setup.h` / подсветка | проверить пины, `TFT_BL 21`, clean build |
| `Failed to connect` | скорость/порт/BOOT | 921600, COM5, BOOT+RST |
| Плата подключается к старой сети | старые `ssid/pass` в NVS | `esptool erase_region 0x9000 0x5000` |
| `WebServer` не компилируется | ядро 3.x | `esp32:esp32@2.0.9` |
| Страница пустая после изменения | забыли `build_page.py` | пересобрать и перепрошить |

## 10. Чего НЕ делать

- Не собирать с `esp32c3` и не искать OLED — это другая плата.
- Не ставить `PartitionScheme=default`.
- Не копировать `User_Setup.h` в папку скетча: `TFT_eSPI` его там не ищет.
- Не коммитить `secrets.h`, `control-secret.h`, `build/`, `*.bin`,
  `*.manifest.json` и `settings.json`.
- Не прошивать 3.x ядром «на удачу».
- Не публиковать порт 80 платы в интернет: чтение данных в LAN открыто,
  управление закрыто ключом `X-Dom-Key`, но HTTP внутри LAN не шифруется.

## 11. Размер и запас

| Параметр | Значение |
|---|---|
| Размер образа 4.14 | 1 630 192 Б (~1,55 МБ) |
| Слот `min_spiffs` | 1 966 080 Б (1,875 МиБ) |
| Занято | **82,9 %
** |
| Свободно в слоте | ~336 КБ |
| Страница `/` | ~86,3 КБ (86 331 Б отдаётся) Б (`page_html.h` 86 655 Б, лимит 96 000) |
| `/app.css` | 27 437 Б |
| `/app.js` | 56 770 Б |
| `/history.js` | 63 122 Б (лимит ресурса 64 512) |
| `/control.js` | 5 868 Б |

Запас в слоте нужен для строковых ресурсов, шрифтов и будущих изменений.
`history.js` уже у лимита: новый код кладём в `app.js`/`page.html`,
а не в историю.

## 11.1. Файлы прошивки

| Файл | Что |
|---|---|
| `cydv4.ino` | основной скетч (сеть, Tuya, SD, экран, веб-маршруты) |
| `page_html.h` | страница `/` (генерируется) |
| `assets.h` | `.css` и `.js`-ресурсы (генерируется) |
| `build_identity.h` | идентификатор сборки (генерируется) |
| `meas_sources.h` | источник/возраст/срок годности V/P/I/T, `/measure` |
| `bounded_web_server.h` | дедлайны отправки, сроки без прогресса, ключ |
| `range_reader.h` | асинхронное чтение `/range` порциями (202/pending) |
| `archive_transaction.h` | ротация месячных CSV без потери истории |
| `runtime_time.h` | единый runtime-взгляд на время (часы/NTP) |
| `trusted_roots.h` | CA для Tuya и OpenWeatherMap |
| `font5x7.h`, `secrets.example.h`, `web/` | шрифт, шаблон ключей, исходники страницы |

## Страница платы: `page_html.h`

Веб-страница лежит отдельно — `firmware/cydv4/web/page.html`. Из неё собирается
заголовок `page_html.h`, который подключает прошивка:

```powershell
cd firmware\cydv4\web
python build_page.py          # перезапишет ../page_html.h
```

В репозитории `page_html.h` уже собран — **если страницу не меняли, этот шаг
не нужен**. После изменения `page.html` файл надо пересобрать, иначе плата
отдаст старую страницу (проверка: откройте `http://<IP платы>/` и поищите
своё изменение).

## Секреты и TFT

1. `copy firmware\cydv4\secrets.example.h firmware\cydv4\secrets.h` и заполнить.
2. `firmware\hw\User_Setup.h` скопировать в библиотеку `TFT_eSPI`
   (иначе экран будет белым: не тот драйвер и не та распиновка).

## ВАЖНО: TFT_eSPI — какой экран активен

Библиотека `TFT_eSPI` одна на всю машину и переключается файлом
`libraries/TFT_eSPI/User_Setup_Select.h`. Если там активен чужой Setup
(например, `User_Setup_C3_ST7735.h` от ESP32-C3), прошивка **соберётся, но
зависнет на `tft.init()`**: плата печатает баннер и уходит в перезагрузку по
сторожу (`INT_WDT`), Wi-Fi не поднимается.

Проверка (должна быть активна строка с `User_Setup.h`, а чужие — закомментированы):

```powershell
Select-String -Path "$env:USERPROFILE\Documents\Arduino\libraries\TFT_eSPI\User_Setup_Select.h" -Pattern '^#include <User_Setup'
```

Симптом в serial: `dbg: loadWifi ok` и дальше тишина — значит завис `tft.init()`.

## Обновление прошивки по Wi-Fi (OTA)

Плата умеет принимать прошивку по воздуху — USB-кабель нужен только для
первой прошивки.

По правилам проекта OTA запускают из DomMonitor: вкладка **«Прошивка»**, выбранная папка скетча, FQBN `esp32:esp32:esp32:PartitionScheme=min_spiffs`, кнопка **«Прошить по Wi-Fi»**.

Кнопка передаёт `-Build`, конкретный `-Sketch`, `-Cli` и `-Fqbn` в `flash-http.ps1`. Генератор собирает веб и `build_identity.h`; компиляция создаёт конкретный образ и соседний `*.manifest.json` с SHA-256 и идентификатором сборки. Поиск «последнего образа в TEMP» удалён. Без выбранного образа/сборки загрузчик отказывает. Он проверяет ответ `OK`, сброс uptime, `/data.build` и доступность веба. Процесс работает в фоне, прогресс и журнал доступны в приложении.

OTA и управляющие HTTP-запросы требуют `X-Dom-Key`. Ключ хранится только локально: `%APPDATA%\DomMonitor\control.key` и `firmware\cydv4\control-secret.h` (`DOM_CONTROL_TOKEN`). Его значения должны совпадать; не публиковать их. При отсутствии прошивочного ключа управление закрыто с HTTP 503. `ControlAuth.ps1` устанавливается рядом с приложением. Открытие веба из меню приложения передаёт ключ через URL-фрагмент; браузер переносит его в `sessionStorage` и убирает фрагмент из адреса. При прямом открытии веба управление запрашивает ключ, чтение данных открыто в LAN. HTTP внутри LAN не шифрует трафик: порт платы не публиковать в интернет.

`POST /update` использует multipart-поле `firmware`; пустая/отменённая загрузка возвращает отказ без перезагрузки. `GET /reboot` также требует ключ. Генератор не включает секреты в идентификатор исходников. CA для Tuya/OpenWeatherMap находятся в `trusted_roots.h`; при смене цепочки серверов обновлять доверенные корни, не возвращать `setInsecure()`.

## Уведомления и Telegram через реле GitHub

Пороги задаются в `%APPDATA%\DomMonitor\settings.json`:

| поле | по умолчанию | смысл |
|---|---|---|
| `alertOn` | `true` | уведомления включены |
| `alertVMin` / `alertVMax` | `200` / `250` | порог по напряжению, В |
| `alertTMax` | `55` | перегрев реле, °C |
| `alertOffMin` | `3` | «плата молчит», минуты |
| `alertSpikeW` / `alertSpikeX` | `500` / `2.0` | скачок мощности: Вт и во сколько раз выше среднего |
| `tgRelay` | `false` | слать ли в Telegram |
| `githubToken` | — | **личный** PAT, только локально, в репозиторий не попадает |
| `tgRepo` | `nika-sc/Dom-Energy` | куда отправлять `repository_dispatch` |

Куда приходят уведомления: тост в трее Windows, строка в журнале приложения
(`app.log`), и — если включено — Telegram через реле.

### Как устроено реле

Telegram API из домашней сети недоступен, поэтому сообщение идёт так:

```
DomMonitor -> tg-relay.ps1 -> GitHub API (repository_dispatch)
           -> .github/workflows/telegram-relay.yml -> api.telegram.org
```

В репозитории должны быть **секреты** (Settings -> Secrets and variables -> Actions):

- `TELEGRAM_BOT_TOKEN` — токен бота от @BotFather
- `TELEGRAM_CHAT_ID` — id чата

Токен бота и chat_id в коде и в репозитории не хранятся.
Токен GitHub (PAT) вводится только локально в `settings.json`.

Проверка реле вручную: Actions -> **telegram-relay** -> Run workflow.

## Прошивка: схема разделов и OTA

Используется **`PartitionScheme=min_spiffs`** — два слота приложения
по 1.875 МиБ (1 966 080 байт). Схемы с одним большим приложением без второго OTA-слота для обновления по Wi-Fi не подходят.

```bash
# первая прошивка (USB, один раз)
arduino-cli upload -p COM5 \
  --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs firmware/cydv4

# дальше — кнопка «Прошить по Wi-Fi» в приложении Windows
```

Проверка: `POST /update` отвечает `OK`, в `/boots.log` появляется `rr=OTA(11)`.

## Защита: что где лежит

| Порог | DP автомата Tuya | Наша плата (второй слой) |
|---|---|---|
| Перенапряжение | `alarm_set_2`, код 3, В | `gProtVMax` (NVS `prot/vmax`) |
| Просадка | `alarm_set_2`, код 4, В | `gProtVMin` |
| Перегруз | `alarm_set_2`, код 1, А | `gProtPMax` = А × 230 |
| Перегрев | `alarm_set_1`, код 5, °C | `gProtTMax` |

Запись: `GET /protect?apply=1&vmax=250&vmin=200&tmax=60&imax=25` — плата отправляет
один облачный `POST /v1.0/devices/{id}/commands` с двумя DP и дублирует значения локально.
Ответ содержит `{"apply":{"a1":1,"a2":1}}` при успехе.

Формат `alarm_set_*`: base64, каждая запись 4 байта — `код`, `включено`, `значение` (big-endian).

## Планировщик

`GET /sched` — чтение; `GET /sched?set=07:30,on,127,1;23:30,off,127,1` — запись
(`ЧЧ:ММ,действие,маска_дней,включено`); `GET /sched?allow=1` — разрешить выключение реле.

Правила хранятся в NVS (`prot/sched`), проверяются раз в 20 секунд; выключение
выполняется только при `allowOff=1`, иначе в MQTT уходит `schedule_off_blocked`.

## Как читать quality-эндпоинты

- `/gost` → `n`, `vmin`, `vmax`, `vavg`, `bad`, `badPct`, `events`
- `/vlog` → `{"n":N,"items":[{"t":epoch,"v":В,"s":секунд,"k":0|1}]}` (0 — провал, 1 — всплеск)
- `/tuya` → `a1`, `a2` (base64), температура, состояние реле

Частота сети не измеряется: у реле Tuya нет такого DP, на плате нет датчика напряжения.
