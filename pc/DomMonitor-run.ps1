# DomMonitor-run.ps1 — супервизор: запускает программу и перезапускает её при падении
# Пишет в $env:APPDATA\DomMonitor\supervisor.log
#
# ВАЖНО: запускается ТОЛЬКО ОДИН супервизор. Раньше защиты не было, и каждый
# повторный вызов создавал второй цикл while($true) со СВОИМ экземпляром
# программы — на экране появлялись два окна DomMonitor (31.10.2026).
# Теперь при старте берётся именованный мьютекс: второй запуск сразу выходит.

$mutexName = 'Global\DomMonitorSupervisor_SingleInstance'
$createdNew = $false
$mutex = New-Object System.Threading.Mutex($true, $mutexName, [ref]$createdNew)

if (-not $createdNew) {
    # Супервизор уже работает — второй не нужен, молча выходим
    try { $mutex.Dispose() } catch { }
    Write-Host 'Супервизор DomMonitor уже запущен — второй экземпляр не нужен.'
    exit 0
}

$app = Join-Path $PSScriptRoot 'DomMonitor.ps1'
$logDir = Join-Path $env:APPDATA 'DomMonitor'
if (!(Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }
$log = Join-Path $logDir 'supervisor.log'

function Log($msg) {
    try {
        [System.IO.File]::AppendAllText($log, ("[{0}] {1}`r`n" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg),
            (New-Object System.Text.UTF8Encoding($false)))
    } catch { }
}

Log "=== СУПЕРВИЗОР ЗАПУЩЕН (единственный) ==="

# Если остались окна программы от прошлых запусков — закрыть, чтобы не было двух
try {
    $stale = Get-CimInstance Win32_Process |
        Where-Object { $_.Name -eq 'powershell.exe' -and $_.CommandLine -match 'DomMonitor\.ps1' }
    foreach ($p in $stale) {
        Log ("закрываю лишний экземпляр программы, pid " + $p.ProcessId)
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
    }
} catch { }

$restarts = 0

while ($true) {
    $start = Get-Date
    Log "запускаю программу"
    $p = Start-Process -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $app) -PassThru
    Log ("программа запущена, pid " + $p.Id)

    try { Wait-Process -Id $p.Id -ErrorAction Stop } catch { }

    $lived = ((Get-Date) - $start).TotalMinutes
    $code = 'неизвестен'
    try { $code = $p.ExitCode } catch { }

    Log ("программа завершилась, код " + $code + ", работала " + [Math]::Round($lived, 1) + " мин")

    # если падает слишком часто — не частим
    if ($lived -lt 1) { $restarts++ } else { $restarts = 0 }
    if ($restarts -ge 5) {
        Log "5 падений подряд — пауза 60 секунд"
        Start-Sleep -Seconds 60
        $restarts = 0
    } else {
        Start-Sleep -Seconds 5
    }
}
