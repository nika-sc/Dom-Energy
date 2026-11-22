# Восстановление — краткая версия

> Полная пошаговая инструкция: [../RESTORE.md](../RESTORE.md).

Порядок:

1. Та же плата **ESP32-2432S028 (CYD)**, USB-кабель, Arduino CLI.
2. Взять исходник из git или `python pc/backup-firmware.py --restore ...`.
3. Вернуть `User_Setup.h` в библиотеку `TFT_eSPI` (не в папку скетча!).
4. Создать `secrets.h` из `secrets.example.h`.
5. Собрать с `PartitionScheme=huge_app`, прошить COM5 на 921600.
6. `CALTOUCH` → 4 угла; `CAL` — проверить; `CALRESET` — сбросить.
7. Вернуть историю: `energy-latest.csv` → `/energy.csv` на карте,
   либо `IMP` … `IMPEND`; затем `PERIODRESET`.
8. `python tests/test-board.py`.

Полный дамп 4 МБ:

```powershell
esptool --port COM5 read_flash 0x0 0x400000 backup.bin
esptool --port COM5 write_flash 0x0 backup.bin
esptool --port COM5 read_flash 0x9000 0x5000 nvs.bin
```

Дамп содержит ключи и Wi-Fi — не публиковать.
