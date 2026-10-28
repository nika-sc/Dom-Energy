# Сборка и прошивка (CYD, ESP32-2432S028)

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
| Python | 3.9+ | только для `web/build_page.py` |
| Драйвер USB-UART | CH340/CP210x | если COM-порт не появляется |

`ArduinoJson` **не нужен**: JSON разбирается вручную. `WebServer`, `SD`,
`SPI`, `Preferences`, `HTTPClient`, `WiFiClientSecure`, `mbedtls` входят
в ядро ESP32.

## 2. Установка ядра и библиотеки

```powershell
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.9
arduino-cli lib install TFT_eSPI@2.5.43
```

Проверка:

```powershell
arduino-cli core list
arduino-cli lib list | Select-String TFT_eSPI
```

Должно быть `esp32:esp32 2.0.9` и `TFT_eSPI 2.5.43`.

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

Исходник страницы — `firmware/cydv4/web/page.html`. Он вшивается в `.ino`
между маркерами `PAGE_TMPL`:

```powershell
cd firmware\cydv4
python web\build_page.py
```

Скрипт:

- проверяет, что в HTML нет `)HTML"` (иначе сломается raw-строка);
- проверяет 22 плейсхолдера (`__FW__`, `__MV__`, …);
- заменяет блок в `cydv4.ino`.

После правки страницы **всегда** запускайте сборку и коммитьте оба файла:
`web/page.html` и `cydv4.ino`.

## 6. Компиляция

Из корня репозитория:

```powershell
$cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"

& $cli compile `
  --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
  --libraries "$env:USERPROFILE\Documents\Arduino\libraries" `
  --build-path "build" `
  "firmware\cydv4"
```

Почему `PartitionScheme=huge_app`:

- прошивка занимает около **1,09 МБ** (1 142 313 байт);
- в `huge_app` раздел приложения — **3 МБ**, занято **36 %**;
- в стандартном `default` раздел 1,3 МБ, занято ~87 % — запаса почти нет,
  следующая правка может не влезть.

Ожидаемый результат (Arduino CLI 1.5.1, ru):

```
Скетч использует 1142313 байт (36%) памяти устройства. Всего доступно 3145728 байт.
Глобальные переменные используют 72928 байт (22%) динамической памяти, оставляя 254752 байт для локальных переменных. Максимум: 327680 байт.
```

(Точная цифра зависит от версии библиотек; главное — `huge_app` и отсутствие
`Sketch too big`.)

## 7. Прошивка

Плата — в COM5 (проверьте в диспетчере устройств; если другой — замените).

```powershell
& $cli upload `
  -p COM5 `
  --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
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
=== ДОМ 4.4 ===
SD: ok, строк 10168
веб: http://192.168.1.50/
```

Затем:

```powershell
# страница
start http://192.168.1.50/

# данные
curl http://192.168.1.50/data
```

Ожидаемо: JSON с полями `v`, `temp`, `p`, `amp`, `e`, `r`, `tkwh`, `tcost`,
`ykwh`, `ykost`, `mkwh`, `mcost`, `tariff`, `rssi`, `sdRows`.

Если экран показывает «НЕТ ДАННЫХ»:

- `NET` в serial — есть ли Wi-Fi/IP;
- `DIAG` — `gSdOk`, `gTuyaOk`, `gRssi`;
- `ping <IP реле>`, порт 6668;
- `WCLR` — сбросить погоду и проверить заново.

## 9. Частые ошибки

| Ошибка | Причина | Решение |
|---|---|---|
| `secrets.h: No such file` | не скопирован шаблон | `Copy-Item secrets.example.h secrets.h` |
| `Sketch too big` | собрано без `huge_app` | добавить `PartitionScheme=huge_app` |
| `TFT_eSPI` ошибки `_spi_cmd` | не тот `User_Setup.h` | скопировать из `firmware/hw/` |
| Экран чёрный, но serial работает | `User_Setup.h` / подсветка | проверить пины, `TFT_BL 21`, clean build |
| `Failed to connect` | скорость/порт/BOOT | 921600, COM5, BOOT+RST |
| Плата подключается к старой сети | старые `ssid/pass` в NVS | `esptool erase_region 0x9000 0x5000` |
| `WebServer` не компилируется | ядро 3.x | `esp32:esp32@2.0.9` |
| Страница пустая после правки | забыли `build_page.py` | пересобрать и перепрошить |

## 10. Чего НЕ делать

- Не собирать с `esp32c3` и не искать OLED — это другая плата.
- Не ставить `PartitionScheme=default`.
- Не копировать `User_Setup.h` в папку скетча: `TFT_eSPI` его там не ищет.
- Не коммитить `secrets.h`, `build/`, `*.bin`.
- Не прошивать 3.x ядром «на удачу».
- Не публиковать порт 80 платы в интернет: у страницы нет пароля.

## 11. Размер и запас

| Параметр | Значение |
|---|---|
| Размер `.bin` | ~1,15 МБ (1 148 064 байт; код — 1 142 313 байт) |
| Раздел `huge_app` | 3 МБ (3 145 728 байт) |
| Занято | 36 % |
| Свободно | ~1,9 МБ |
| Раздел `default` | 1,3 МБ (1 310 720 байт), занято ~87 % |

Запас в `huge_app` нужен для строковых ресурсов, шрифтов и будущих правок.
