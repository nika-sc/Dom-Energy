# ============================================================================
#  build-and-flash.ps1 — архив → сборка → прошивка → проверка
# ----------------------------------------------------------------------------
#  Один запуск делает всё по правилу:
#    1) архив устройства (состояние, SD, исходники, собранная прошивка,
#       полный дамп flash 4 МБ на 460800 — на 921600 бывают сбои чтения);
#    2) page_html.h из web/page.html;
#    3) сборка arduino-cli;
#    4) заливка на плату;
#    5) проверка: плата отвечает, /data приходит.
#
#  Примеры:
#    powershell -ExecutionPolicy Bypass -File build-and-flash.ps1
#    ... -Sketch C:\esp32\cydv4 -Com COM5
#    ... -NoArchive          (только собрать и прошить, без архива — для отладки)
# ============================================================================
param(
    [string]$Sketch = 'C:\esp32\cydv4',
    [string]$Com    = 'COM5',
    [string]$Board  = 'http://192.168.0.51',
    [switch]$NoArchive
)

$ErrorActionPreference = 'Continue'
$cli   = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
$esptool = 'C:\Users\111\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe'
$libs  = 'C:\Users\111\Documents\Arduino\libraries'
$fqbn  = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'
$root  = 'D:\DSH-Workspace\DEVICE-ARCHIVE'
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$dir   = Join-Path $root $stamp
$log   = Join-Path $root 'flash-log.txt'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color; Add-Content -Path $log -Value ((Get-Date -Format 'HH:mm:ss') + '  ' + $msg) -Encoding UTF8 }

Say ('=== ' + $stamp + ' : архив → сборка → прошивка ===') 'Cyan'
Say ('эскиз: ' + $Sketch)

# ---------------------------------------------------------------- 1. архив
if (-not $NoArchive) {
    Say '--- 1. архив устройства ---'
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\esp32\tools\backup-device.ps1' -Root $root 2>&1 | Out-Null
    # backup-device создаёт свою папку по своему времени — берём последнюю
    $dir = (Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
    Say ('папка архива: ' + $dir)

    $dump = Join-Path $dir 'flash-4mb.bin'
    if (-not (Test-Path $dump)) {
        & $esptool --chip esp32 --port $Com --baud 460800 read_flash 0x0 0x400000 $dump 2>&1 | Out-Null
    }
    if (Test-Path $dump) { Say ('дамп flash: ' + [math]::Round((Get-Item $dump).Length / 1MB, 2) + ' МБ') 'Green' }
    else { Say 'ВНИМАНИЕ: дамп flash не снят — архив неполный' 'Yellow' }

    # исходники «как есть» (в C:\esp32\cydv4 теперь актуальная версия)
    foreach ($f in @('cydv4.ino', 'page_html.h', 'secrets.example.h')) {
        $p = Join-Path $Sketch $f
        if (Test-Path $p) { Copy-Item $p (Join-Path $dir ('src_' + $f)) -Force }
    }
    $web = Join-Path $Sketch 'web\page.html'
    if (Test-Path $web) { Copy-Item $web (Join-Path $dir 'src_web_page.html') -Force }
}

# ---------------------------------------------------------------- 2. страница
Say '--- 2. страница ---'
$webDir = Join-Path $Sketch 'web'
if (Test-Path (Join-Path $webDir 'build_page.py')) {
    Push-Location $webDir
    & 'C:\Users\111\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe' 'build_page.py' 2>&1 | ForEach-Object { Say ('  ' + $_) }
    Pop-Location
}

# ---------------------------------------------------------------- 3. сборка
Say '--- 3. сборка ---'
$bp = Join-Path $env:TEMP ('dommon-build-' + $stamp)
if (Test-Path $bp) { Remove-Item $bp -Recurse -Force -ErrorAction SilentlyContinue }
$out = & $cli compile --fqbn $fqbn --libraries $libs --build-path $bp $Sketch 2>&1 | Out-String
$errs = ($out -split "`n") | Where-Object { $_ -match 'error:' }
if ($errs) {
    $errs | Select-Object -First 5 | ForEach-Object { Say ('  ' + $_.Trim()) 'Red' }
    Say 'СБОРКА НЕ УДАЛАСЬ — прошивка не выполняется' 'Red'
    return
}
$bin = Join-Path $bp 'cydv4.ino.bin'
Say ('bin: ' + [math]::Round((Get-Item $bin).Length / 1KB) + ' КБ') 'Green'
if (-not $NoArchive) { Copy-Item $bin (Join-Path $dir 'firmware.bin') -Force }

# ---------------------------------------------------------------- 4. прошивка
Say '--- 4. прошивка ---'
$u = & $cli upload -p $Com --fqbn $fqbn --upload-property 'upload.speed=921600' --input-dir $bp $Sketch 2>&1 | Out-String
if ($u -match 'Hash of data verified') { Say 'прошито: Hash of data verified' 'Green' }
else {
    ($u -split "`n") | Select-Object -Last 5 | ForEach-Object { Say ('  ' + $_.Trim()) }
    Say 'ПРОШИВКА НЕ ПОДТВЕРДИЛАСЬ' 'Red'; return
}

# ---------------------------------------------------------------- 5. проверка
Say '--- 5. проверка ---'
Start-Sleep -Seconds 40
$ok = $false
for ($i = 0; $i -lt 8; $i++) {
    try {
        $d = Invoke-RestMethod ($Board + '/data') -TimeoutSec 10
        Say ('плата: ' + $d.v + ' В, ' + $d.p + ' Вт, счётчик ' + $d.e + ' кВт·ч, строк ' + $d.sdRows) 'Green'
        $ok = $true; break
    } catch { Start-Sleep -Seconds 8 }
}
if (-not $ok) { Say 'ВНИМАНИЕ: плата не отвечает после прошивки' 'Yellow' }
Say ('готово. архив: ' + $dir) 'Cyan'
