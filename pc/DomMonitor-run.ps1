# DomMonitor-run.ps1 — супервизор: запускает программу и перезапускает её при падении,
# а также держит пинг-бота Telegram (DomTelegramPing.ps1): пользователь пишет боту
# «пинг» — получает полное состояние платы. Бот живёт отдельным процессом, чтобы
# long-poll getUpdates не мешал опросу платы.
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
$tgBot = Join-Path $PSScriptRoot 'DomTelegramPing.ps1'
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
$tgProc = $null

function Start-TgBot {
    if (-not (Test-Path $tgBot)) { return $null }
    try {
        $p = Start-Process -FilePath 'powershell.exe' `
            -ArgumentList @('-NoProfile', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File', $tgBot) `
            -PassThru -WindowStyle Hidden
        Log ("пинг-бот Telegram запущен, pid " + $p.Id)
        return $p
    } catch {
        Log ("пинг-бот не запустился: " + $_.Exception.Message)
        return $null
    }
}

# на старте добиваем «осиротевших» ботов от прошлых запусков: иначе два бота на
# одном токене ловят 409 Conflict от Telegram и портят offset
try {
    $staleBot = Get-CimInstance Win32_Process |
        Where-Object { $_.Name -eq 'powershell.exe' -and $_.CommandLine -match 'DomTelegramPing\.ps1' }
    foreach ($pb in $staleBot) {
        Log ("закрываю старый экземпляр пинг-бота, pid " + $pb.ProcessId)
        Stop-Process -Id $pb.ProcessId -Force -ErrorAction SilentlyContinue
    }
} catch { }

if (Test-Path $tgBot) { $tgProc = Start-TgBot } else { Log 'DomTelegramPing.ps1 не найден — пинг-бот выключен' }

while ($true) {
    $start = Get-Date
    Log "запускаю программу"
    # Черное консольное окно рядом с приложением — из-за этого запуска без
    # -WindowStyle Hidden. Прячем консоль: само окно WinForms показывается скриптом.
    $p = Start-Process -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File', $app) `
        -WindowStyle Hidden -PassThru
    Log ("программа запущена, pid " + $p.Id)

    # Ждём приложение, попутно поднимая пинг-бота, если тот упал.
    while (-not $p.HasExited) {
        if ($null -eq $tgProc -or $tgProc.HasExited) {
            if ($null -ne $tgProc) { Log ('пинг-бот завершился (код ' + $tgProc.ExitCode + ') — перезапускаю через 10 с') }
            Start-Sleep -Seconds 10
            $tgProc = Start-TgBot
        }
        Start-Sleep -Seconds 5
    }

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
