# Восстановление устройства

> Сценарий: плата сломалась/заменили, прошивка потеряна, карта стёрта.
> Восстанавливаем **ту же модель** — ESP32-2432S028 (CYD).

## 0. Что понадобится

| Что | Зачем |
|---|---|
| ESP32-2432S028 (CYD) | та же плата; другая может не подойти по пинам |
| USB-кабель с данными | прошивка и serial |
| Arduino CLI 1.5.1 | сборка/заливка |
| Ядро `esp32:esp32@2.0.9` | именно 2.0.9 |
| `TFT_eSPI` + `User_Setup.h` | экран |
| Карта microSD | история |
| Файл `secrets.h` (ваш) | Wi-Fi и ключи Tuya |
| Архив прошивки/репозиторий | исходник |

Если нет `secrets.h`, восстановите его из своей защищённой копии. В
репозитории его нет и быть не должно.

## 1. Восстановить исходник

### Вариант А: из git

```powershell
git clone https://github.com/<USER>/dom-energy.git
cd dom-energy
```

### Вариант Б: из архива прошивок

Скрипт `pc/backup-firmware.py` хранит снимки с `manifest.json` и sha256.

```powershell
python pc\backup-firmware.py --list
python pc\backup-firmware.py --restore cydv4/<папка-снимка> --to firmware\cydv4
```

Проверка целостности:

```powershell
python pc\backup-firmware.py --verify
```

Если архив на NAS, а его нет — скрипт автоматически возьмёт локальную копию
`pc/firmware-archive/`. Пути можно задать `--archive` / `--local`.

> В архиве **нет** `secrets.h`: скрипт специально его пропускает.

## 2. Вернуть `User_Setup.h`

Без него экран не заработает. Файл лежит **в библиотеке**, не в скетче:

```powershell
Copy-Item firmware\hw\User_Setup.h `
  "$env:USERPROFILE\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h" -Force
```

Если библиотека ещё не стоит:

```powershell
arduino-cli lib install TFT_eSPI@2.5.44
```

Проверьте, что в файле:

```cpp
#define ILI9341_2_DRIVER
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
```

## 3. Вернуть `secrets.h`

```powershell
cd firmware\cydv4
Copy-Item secrets.example.h secrets.h
notepad secrets.h
```

Заполните Wi-Fi, Tuya Device ID / Local Key / Client ID / Secret, IP реле,
опционально PC API и координаты погоды. Файл в `.gitignore`.

> Если плата ранее работала с другой сетью, в NVS могли остаться старые
> `ssid/pass`. Тогда `secrets.h` не подействует — см. шаг 5 (очистка NVS)
> или полный дамп.

## 4. Собрать и прошить

```powershell
$cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"

# 1) страница (если правили page.html)
cd firmware\cydv4
python web\build_page.py
cd ..\..

# 2) сборка
Remove-Item build -Recurse -Force -ErrorAction SilentlyContinue
& $cli compile `
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
  --libraries "$env:USERPROFILE\Documents\Arduino\libraries" `
  --build-path "build" `
  "firmware\cydv4"

# 3) заливка
& $cli upload `
  -p COM5 `
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
  --upload-property "upload.speed=921600" `
  --input-dir "build" `
  "firmware\cydv4"
```

Подробности и частые ошибки — [BUILD.md](BUILD.md).

Если порт не открывается: зажать **BOOT**, нажать **RST**, отпустить BOOT,
повторить. Скорость — 921600.

## 5. Если нужно очистить NVS

NVS хранит Wi-Fi, калибровку тача, базы «сегодня/вчера/месяц», время.
Если плата упорно берёт старую сеть:

```powershell
esptool --port COM5 erase_region 0x9000 0x5000
```

После этого:

- Wi-Fi возьмётся из `secrets.h`;
- калибровка тача сбросится — откалибруйте заново (шаг 6);
- базы периодов пересчитаются из карты.

Не стирайте NVS, если не готовы калибровать тач.

## 6. Откалибровать тач

Калибровка — **только по serial**, с экрана её не запустить.

1. Подключить USB.
2. Открыть монитор порта на **115200**.
3. Ввести `CALTOUCH` + Enter.
4. На экране появится «КАЛИБРОВКА / УГОЛ 1 ИЗ 4».
5. Нажать в центр кружка — по очереди:
   - левый верх;
   - правый верх;
   - левый низ;
   - правый низ.
6. После четвёртого — «ГОТОВО», значения сохраняются в NVS.

Проверка:

```
CAL        # показать yleft/yright/xtop/xbottom
RAW        # сырые значения тача
CALRESET   # сбросить и перезагрузить
```

Если попадания мимо — повторить `CALTOUCH`; допуск мишени 90 px, но лучше
нажимать точно в центр.

## 7. Вернуть историю на карту

### Вариант А: копирование файла (быстрее)

1. Выключить плату, вынуть microSD.
2. Вставить карту в компьютер (FAT32).
3. Взять бэкап `backup-sd\energy-latest.csv` (или свой `energy.csv`).
4. Положить в **корень** карты как `/energy.csv`.
5. Если в бэкапе несколько месяцев — разделить:
   - текущий месяц → `/energy.csv`;
   - прошлые → `/energy-YYYY-MM.csv`.
6. Проверить, что первая строка — заголовок:
   ```csv
   datetime,voltage,power,energy_kwh,relay,cost,temp
   ```
7. Вставить карту, включить плату.

Важно: файл должен быть **в хронологическом порядке**, без дублей.
Если просто дописать старые строки в конец — `/history` и графики могут
сломаться (читатели ищут сегодняшние строки с конца).

### Вариант Б: команда `IMP` (для небольших правок)

1. Открыть serial 115200.
2. Ввести `IMP` — плата ответит `IMP-READY` и будет ждать строки.
3. Отправлять строки CSV (каждая с новой строки). Плата принимает только
   строки, начинающиеся с цифры.
4. Закончить `IMPEND` — ответ `IMP-DONE N`.
5. Проверить `CSV` / `CSVDAY` / `CSVH`.

Ограничения `IMP`:

- лимит 60 секунд;
- касание экрана прерывает импорт;
- файл открывается в `FILE_APPEND`, сортировки нет;
- строки с `energy_kwh=0` — восстановленные, они не увеличивают счётчик.

### После восстановления

```
PERIODRESET   # сбросить базы сегодня/вчера/месяц
```

Затем перезагрузить (кнопка RST или питание). Проверить:

```
CSVDAY
CSV
```

И на экране: главный → «СЕГОДНЯ» → стрелки ‹ ›.

## 8. Проверить тестами

```powershell
$env:BOARD_URL = "http://192.168.1.50"   # ваш IP платы
python tests\test-board.py
```

Быстро:

```powershell
python tests\test-board.py --quick
```

Тесты проверяют:

- доступность `/`, `/data`, `/days`, `/history`, `/events`;
- физические границы (180–260 В, 15–60 °C, 0–5000 Вт, 0–25 А);
- целостность CSV (счётчик не убывает, часы заполнены);
- сходимость интеграла мощности со счётчиком;
- математику «сегодня/вчера», стоимость, прогноз;
- стабильность: 10 загрузок страницы, 10 запросов `/data`, heap.

Код возврата 0 — всё хорошо, 1 — есть провалы.

## 9. Полный дамп флеша

Когда дамп снят — его можно залить на новую плату **той же модели**.
Дамп содержит `secrets.h`-данные в NVS, поэтому храните его как секрет.

### Снять

```powershell
# проверить размер флеша (должно быть 4 МБ)
esptool --port COM5 flash_id

# весь флеш 4 МБ
esptool --port COM5 read_flash 0x0 0x400000 backup.bin

# только NVS (Wi-Fi, калибровка, базы периодов)
esptool --port COM5 read_flash 0x9000 0x5000 nvs.bin
```

Если `esptool` не в PATH:

```powershell
python -m esptool --port COM5 read_flash 0x0 0x400000 backup.bin
```

### Восстановить

```powershell
# на новую плату, полный образ
esptool --port COM5 write_flash 0x0 backup.bin

# только настройки
esptool --port COM5 write_flash 0x9000 nvs.bin
```

Перед записью можно стереть:

```powershell
esptool --port COM5 erase_flash
```

Оговорки:

- `0x400000` — для 4 МБ. Если `flash_id` показал 8 МБ, снимайте `0x800000`.
- `write_flash` не проверяет, что плата того же размера; при ошибке
  размера esptool сообщит.
- Полный дамп восстанавливает и NVS, и калибровку. На другой плате
  калибровка может не подойти — тогда `CALTOUCH`.
- `nvs.bin` без полного образа восстанавливает только настройки; версия
  прошивки должна быть совместима.
- **Не коммитьте** `backup.bin` / `nvs.bin`: там Wi-Fi и ключи.
  `.gitignore` уже игнорирует `flash-dumps/` и `*.bin`.

## 10. Чек-лист восстановления

```
[ ] Плата та же (ESP32-2432S028)
[ ] Arduino CLI 1.5.1, ядро 2.0.9, TFT_eSPI 2.5.44
[ ] User_Setup.h скопирован в библиотеку
[ ] secrets.h создан и заполнен
[ ] page.html → build_page.py (если правили)
[ ] compile с min_spiffs
[ ] upload COM5 921600
[ ] serial 115200: SD ok, веб: http://...
[ ] CALTOUCH, 4 угла
[ ] история на карте (energy.csv)
[ ] PERIODRESET
[ ] tests/test-board.py
[ ] полный дамп снят и сохранён в секретном месте
```

---

## Восстановление одной командой (после полного дампа)

С рабочей платы снят **полный образ флеша 4 МБ**. Если плата умерла, а куплена
такая же — восстановление занимает минуту и не требует компилятора:

```powershell
.\pc\restore-board.ps1
```

Скрипт сам находит порт, показывает дампы, пишет образ и проверяет, что плата
поднялась.

| Команда | Что делает |
|---|---|
| `.\pc\restore-board.ps1` | полная прошивка из последнего дампа (4 МБ) |
| `.\pc\restore-board.ps1 -List` | список доступных дампов |
| `.\pc\restore-board.ps1 -NvsOnly` | только настройки (калибровка, базы, время) |
| `.\pc\restore-board.ps1 -Backup` | снять дамп своей платы ПЕРЕД записью |

**Что внутри образа:** прошивка, настройки (NVS: калибровка тача, базы расчёта,
время, Wi-Fi), таблица разделов. То есть после записи плата сразу готова —
не нужна ни сборка, ни калибровка.

**Вручную то же самое:**

```powershell
# снять (с рабочей платы, 40 секунд)
esptool --port COM5 --baud 230400 read_flash 0x0 0x400000 cydv4-full.bin

# залить (на новую, 60 секунд)
esptool --port COM5 --baud 460800 write_flash 0x0 cydv4-full.bin

# только настройки (20 КБ)
esptool --port COM5 read_flash 0x9000 0x5000 nvs.bin
esptool --port COM5 write_flash 0x9000 nvs.bin
```

> **Внимание:** `write_flash 0x0` затирает загрузчик и таблицу разделов (MAC хранится в eFuse и не затирается). Для замены
> сломанной платы на такую же это ровно то, что нужно. На чужую рабочую плату
> так писать нельзя — используйте `-NvsOnly`.

**История на карте восстанавливается отдельно** — образ флеша её не содержит.
Берите `energy-latest.csv` из бэкапа или используйте `IMP`/`IMPEND` по serial.