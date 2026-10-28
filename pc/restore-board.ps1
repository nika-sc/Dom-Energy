# Восстановление платы «Дом — энергия» одной командой.
#
#   .\restore-board.ps1                    полная прошивка из последнего дампа
#   .\restore-board.ps1 -Port COM5         указать порт
#   .\restore-board.ps1 -NvsOnly           только настройки (калибровка, базы)
#   .\restore-board.ps1 -Backup            снять дамп СВОЕЙ платы перед записью
#   .\restore-board.ps1 -List              показать доступные дампы
#
# ВНИМАНИЕ: запись полного образа затирает MAC-адрес и загрузчик.
# Это ПРАВИЛЬНО для замены сломанной платы на такую же.
# На рабочую чужую плату так писать нельзя.

param(
    [string]$Port = "",
    [switch]$NvsOnly,
    [switch]$Backup,
    [switch]$List
)

$ErrorActionPreference = 'Stop'

$esptool = [string]$env:ESPTOOL_EXE   # можно задать вручную, иначе ищем ниже
$dirs = @((Join-Path $env:APPDATA 'DomMonitor\firmware-archive\flash-dumps'),
          (Join-Path $env:USERPROFILE 'DomMonitor\firmware-archive\flash-dumps'))
$board = '192.168.1.50'

function Find-Esptool {
    if ($esptool -and (Test-Path $esptool)) { return $esptool }
    $cmd = Get-Command 'esptool.exe' -EA SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $alt = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" -Recurse -Filter 'esptool.exe' -EA SilentlyContinue | Select-Object -First 1
    if ($alt) { return $alt.FullName }
    throw 'esptool.exe не найден. Установите Arduino CLI или pip install esptool'
}

function Show-Dumps {
    Write-Host ''
    Write-Host '  Доступные дампы:' -ForegroundColor Cyan
    $found = $false
    foreach ($d in $dirs) {
        if (-not (Test-Path $d)) { continue }
        Get-ChildItem $d -Filter 'cydv4-full-*.bin' -EA SilentlyContinue | ForEach-Object {
            $ok = if ($_.Length -ge 4194304) { 'полный' } else { 'НЕПОЛНЫЙ' }
            Write-Host ("    {0}  {1:N1} МБ  {2}" -f $_.Name, ($_.Length / 1MB), $ok)
            $found = $true
        }
    }
    if (-not $found) { Write-Host '    дампов нет' -ForegroundColor Yellow }
    Write-Host ''
}

function Pick-Dump {
    foreach ($d in $dirs) {
        if (-not (Test-Path $d)) { continue }
        $f = Get-ChildItem $d -Filter 'cydv4-full-*.bin' -EA SilentlyContinue |
             Where-Object { $_.Length -ge 4194304 } |
             Sort-Object LastWriteTime -Descending |
             Select-Object -First 1
        if ($f) { return $f.FullName }
    }
    throw 'Полный дамп не найден. Сначала снимите его с рабочей платы.'
}

if ($List) { Show-Dumps; exit 0 }

$tool = Find-Esptool
Write-Host ''
Write-Host '  ВОССТАНОВЛЕНИЕ ПЛАТЫ «ДОМ — ЭНЕРГИЯ»' -ForegroundColor Cyan
Write-Host '  ====================================' -ForegroundColor Cyan

# --- порт
if (-not $Port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Where-Object { $_ -match 'COM\d+' }
    Write-Host ''
    Write-Host '  Доступные порты:' -ForegroundColor Cyan
    $ports | ForEach-Object { Write-Host "    $_" }
    if (-not $ports) { throw 'Нет ни одного COM-порта. Подключите плату по USB.' }
    $Port = $ports | Where-Object { $_ -ne 'COM1' } | Select-Object -Last 1
    if (-not $Port) { $Port = $ports[0] }
    Write-Host "  использую: $Port"
}

# --- что пишем
$image = if ($NvsOnly) { (Pick-Dump -replace 'cydv4-full', 'cydv4-nvs') } else { Pick-Dump }
if ($NvsOnly) {
    $nvs = Get-ChildItem (Split-Path (Pick-Dump)) -Filter 'cydv4-nvs-*.bin' -EA SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $nvs) { throw 'Дамп NVS не найден.' }
    $image = $nvs.FullName
}
Write-Host ''
Write-Host "  образ: $image"
Write-Host ("  размер: {0:N0} байт" -f (Get-Item $image).Length)

# --- резервная копия СВОЕЙ платы
if ($Backup) {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
    $bak = "cydv4-before-restore-$stamp.bin"
    Write-Host ''
    Write-Host '  Снимаю дамп текущей платы перед записью...' -ForegroundColor Yellow
    & $tool --port $Port --baud 230400 read_flash 0x0 0x400000 $bak *> $null
    if ((Test-Path $bak) -and (Get-Item $bak).Length -ge 4194304) {
        Write-Host "  сохранено: $bak" -ForegroundColor Green
    } else {
        throw 'Не удалось снять резервную копию — запись отменена.'
    }
}

# --- проверка доступности платы
Write-Host ''
Write-Host '  Проверяю плату...' -ForegroundColor Cyan
$id = & $tool --port $Port chip_id 2>&1 | Out-String
$id -split "`n" | Where-Object { $_ -match 'Chip is|MAC|Detected flash' } | ForEach-Object { Write-Host "    $($_.Trim())" }

# --- запись
Write-Host ''
if ($NvsOnly) {
    Write-Host '  Записываю ТОЛЬКО настройки (NVS)...' -ForegroundColor Yellow
    & $tool --port $Port --baud 230400 write_flash 0x9000 $image
    Write-Host '  готово: калибровка, базы и время восстановлены' -ForegroundColor Green
} else {
    Write-Host '  ЗАПИСЫВАЮ ПОЛНЫЙ ОБРАЗ (4 МБ). Не отключайте кабель!' -ForegroundColor Yellow
    & $tool --port $Port --baud 460800 write_flash 0x0 $image
    Write-Host '  готово: прошивка, настройки и таблица разделов восстановлены' -ForegroundColor Green
}

# --- проверка
Write-Host ''
Write-Host '  Жду загрузки (25 секунд)...' -ForegroundColor Cyan
Start-Sleep -Seconds 25
try {
    $d = (Invoke-WebRequest "http://$board/data" -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
    Write-Host '  ПЛАТА РАБОТАЕТ' -ForegroundColor Green
    Write-Host ("    {0} В · {1} °C · {2} Вт" -f $d.v, $d.temp, $d.p)
    Write-Host ("    сегодня {0} кВт·ч · вчера {1} · месяц {2}" -f $d.tkwh, $d.ykwh, $d.mkwh)
} catch {
    Write-Host '  плата не отвечает по сети — проверьте WiFi и IP в secrets.h' -ForegroundColor Yellow
}

Write-Host ''
Write-Host '  Дальше:' -ForegroundColor Cyan
Write-Host '    1. Если тач не работает — команда CALTOUCH и нажать 4 угла'
Write-Host '    2. Вернуть историю: backup-sd.py --restore  или  IMP / IMPEND'
Write-Host '    3. Проверить: python tests\test-board.py'
Write-Host ''
