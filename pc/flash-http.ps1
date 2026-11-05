# ============================================================================
#  Прошивка платы по Wi-Fi (OTA) — без USB-кабеля
# ----------------------------------------------------------------------------
#  flash-http.ps1 [-Ip 192.168.0.51] [-Bin путь.bin] [-Sketch папка] [-Build]
#
#  Если -Bin не указан: берётся самая свежая сборка из %TEMP%\dommon-build-*,
#  а если её нет и указан -Build — эскиз компилируется arduino-cli.
#
#  Протокол: POST /update, тело — multipart с полем firmware (.bin).
#  После успешной записи плата сама перезагружается на новую прошивку.
# ============================================================================
param(
    [string]$Ip      = "192.168.0.51",
    [string]$Bin     = "",
    [string]$Sketch  = "C:\esp32\cydv4",
    [string]$Cli     = "C:\Program Files\Arduino CLI\arduino-cli.exe",
    [string]$Fqbn    = "esp32:esp32:esp32:PartitionScheme=huge_app",
    [switch]$Build,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = "Stop"
function Step($msg) { Write-Host ("[" + (Get-Date -Format "HH:mm:ss") + "] " + $msg) }

# ---------------------------------------------------------------- 1. найти bin
if (-not $Bin) {
    $dirs = Get-ChildItem $env:TEMP -Directory -Filter "dommon-build-*" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending
    foreach ($d in $dirs) {
        $cand = Join-Path $d.FullName "cydv4.ino.bin"
        if (Test-Path $cand) { $Bin = $cand; break }
    }
}
if ($Build -or -not $Bin -or -not (Test-Path $Bin)) {
    if (-not (Test-Path $Cli)) { throw "arduino-cli не найден: $Cli" }
    $out = Join-Path $env:TEMP ("ota-build-" + (Get-Date -Format "yyyyMMdd_HHmmss"))
    Step "компиляция: $Sketch -> $out"
    & $Cli compile --fqbn $Fqbn --output-dir $out $Sketch | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "компиляция не удалась (код $LASTEXITCODE)" }
    $Bin = Join-Path $out "cydv4.ino.bin"
}
if (-not (Test-Path $Bin)) { throw "не найден файл прошивки: $Bin" }
$size = (Get-Item $Bin).Length
Step ("прошивка: {0} ({1:N0} Б)" -f (Split-Path -Leaf $Bin), $size)

# ---------------------------------------------------------------- 2. плата онлайн?
try {
    $before = Invoke-RestMethod ("http://" + $Ip + "/data") -TimeoutSec 10
    Step ("плата онлайн: uptime " + $before.up + " с, v=" + $before.v + " В")
} catch {
    throw ("плата недоступна по HTTP: http://" + $Ip + "/data")
}

# ---------------------------------------------------------------- 3. загрузка
Step "отправляю на http://$Ip/update …"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$eap = $ErrorActionPreference
$ErrorActionPreference = "Continue"      # curl пишет в stderr — это не падение
$cmdLine = 'curl.exe -s -S --max-time ' + $TimeoutSec + ' -F "firmware=@' + $Bin + ';type=application/octet-stream" http://' + $Ip + '/update 2>&1'
$resp = & cmd.exe /c $cmdLine
$code = $LASTEXITCODE
$ErrorActionPreference = $eap
$sw.Stop()
Step ("ответ: " + ($resp | Out-String).Trim() + " | код " + $code + " | " + [math]::Round($sw.Elapsed.TotalSeconds,1) + " с")

# ---------------------------------------------------------------- 4. ждём ребут
$dead = 0
$ok = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Seconds 3
    try {
        $d = Invoke-RestMethod ("http://" + $Ip + "/data") -TimeoutSec 8
        if ($d.up -lt 120) {
            Step ("плата перезагрузилась: uptime " + $d.up + " с, v=" + $d.v + " В, heap " + $d.heap)
            $ok = $true
            break
        }
        $dead = 0
    } catch {
        $dead++
        if ($dead -eq 1) { Step "плата перезагружается (нет ответа)…" }
    }
}
if (-not $ok) { throw "плата не ответила после прошивки — проверь питание и Wi-Fi" }

# ---------------------------------------------------------------- 5. проверка
$page = Invoke-WebRequest ("http://" + $Ip + "/") -TimeoutSec 20 -UseBasicParsing
Step ("страница: HTTP " + $page.StatusCode + ", " + $page.RawContentLength + " Б")
Step "готово"
