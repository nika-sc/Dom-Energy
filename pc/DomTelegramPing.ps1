﻿# ============================================================================
#  DomTelegramPing.ps1 — ответный «пинг» в Telegram: пишешь боту «пинг» —
#  получаешь полное состояние платы (значения, возраст данных, карта, облако).
# ----------------------------------------------------------------------------
#  Почему отдельный процесс: Telegram Bot API требует long-poll getUpdates,
#  а приложение занято опросом платы. Скрипт запускается супервизором
#  DomMonitor-run.ps1 и перезапускается при падении.
#
#  ГДЕ ТОКЕН (не в репозитории):
#    1) %APPDATA%\DomMonitor\tg-ping-config.json  — основной файл бота:
#         { "tgBotToken": "123456789:AA…", "tgChatId": 123456789 }
#    2) %APPDATA%\DomMonitor\settings.json — запасной вариант (ключи те же).
#       Приложение свои ключи не стирает (Save-Cfg теперь сохраняет чужие ключи).
#  Если tgChatId не задан — первый ЛИЧНЫЙ чат привязывается автоматически,
#  но только в первые 10 минут после старта и только по команде «пинг»/«помощь».
#  Привязанный чат хранится в tg-ping-chat.json; чтобы перепривязать — удалить
#  tg-ping-chat.json и tgChatId из конфига.
#
#  Команды (личный чат): «пинг», «ping», «/status», «/now», «статус»,
#  «состояние», «помощь»/«/help».
#
#  Проверка вручную: powershell -File DomTelegramPing.ps1 -Once
#  С mock-API:      powershell -File DomTelegramPing.ps1 -Once -ApiBase http://127.0.0.1:8765
# ============================================================================
[CmdletBinding()]
param(
    [string]$ApiBase = 'https://api.telegram.org',
    [string]$BoardUrl = 'http://192.168.1.51',
    [string]$SettingsPath = (Join-Path $env:APPDATA 'DomMonitor\settings.json'),
    [string]$StateDir = (Join-Path $env:APPDATA 'DomMonitor'),
    [int]$PollTimeoutSec = 25,
    [int]$MaxIterations = 0,
    [int]$SleepBetweenSec = 1,
    [int]$PairWindowMin = 10,
    [int]$NoTokenSleepSec = 60,
    [int]$BackoffBaseSec = 5,      # пауза после ошибки Telegram: 5→10→20→40→60 с
    [string]$MutexName = 'Global\DomTelegramPing_SingleInstance',
    [switch]$Once
)

$ErrorActionPreference = 'Continue'
$script:LogPath = Join-Path $StateDir 'tg-ping.log'
$script:OffsetPath = Join-Path $StateDir 'tg-ping-offset.json'
$script:BoundPath = Join-Path $StateDir 'tg-ping-chat.json'
$script:OwnCfgPath = Join-Path $StateDir 'tg-ping-config.json'
$script:StartedAt = Get-Date
$script:PairFrom = $null      # с момента первого валидного токена
$script:LastErrLogAt = @{}

function Log([string]$m, [string]$level = 'info') {
    try {
        if (-not (Test-Path $StateDir)) { New-Item -ItemType Directory -Path $StateDir -Force | Out-Null }
        $stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        [System.IO.File]::AppendAllText($script:LogPath, "[$stamp] $level $m`r`n", (New-Object System.Text.UTF8Encoding($false)))
        if ((Get-Item $script:LogPath).Length -gt 300000) {
            $tail = Get-Content $script:LogPath -Tail 500
            Set-Content -LiteralPath $script:LogPath -Value $tail -Encoding UTF8
        }
    } catch { }
}

# ошибки одного типа пишем не чаще раза в минуту, чтобы не съедать лог
function LogErr([string]$key, [string]$m) {
    $now = Get-Date
    if ($script:LastErrLogAt.ContainsKey($key) -and ($now - $script:LastErrLogAt[$key]).TotalSeconds -lt 60) { return }
    $script:LastErrLogAt[$key] = $now
    Log $m 'err'
}

function Read-JsonFile([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    try { return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json) } catch { return $null }
}

function JsonToMap($o) {
    $h = [ordered]@{}
    if ($null -eq $o) { return $h }
    foreach ($p in $o.PSObject.Properties) { $h[$p.Name] = $p.Value }
    return $h
}

function Read-Settings { return (JsonToMap (Read-JsonFile $SettingsPath)) }
function Read-OwnCfg { return (JsonToMap (Read-JsonFile $script:OwnCfgPath)) }

# токен и чат: сначала свой файл бота, затем settings.json
function Get-BotConfig {
    $own = Read-OwnCfg
    $set = Read-Settings
    $tok = [string]$own['tgBotToken']
    if ([string]::IsNullOrWhiteSpace($tok)) { $tok = [string]$set['tgBotToken'] }
    $chat = [string]$own['tgChatId']
    if ([string]::IsNullOrWhiteSpace($chat)) { $chat = [string]$set['tgChatId'] }
    return @{ token = $tok; chat = $chat; own = $own }
}

function Save-OwnCfg($own, [string]$token, [string]$chat) {
    try {
        if (Test-Path $script:OwnCfgPath) { Copy-Item -LiteralPath $script:OwnCfgPath -Destination ($script:OwnCfgPath + '.bak') -Force }
        $own['tgBotToken'] = $token
        if ($chat) { $own['tgChatId'] = [long]$chat } else { $own.Remove('tgChatId') }
        $json = $own | ConvertTo-Json -Depth 8
        [System.IO.File]::WriteAllText($script:OwnCfgPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    } catch { Log ('не удалось сохранить tg-ping-config.json: ' + $_.Exception.Message) 'err' }
}

function Get-Offset {
    $o = Read-JsonFile $script:OffsetPath
    if ($null -eq $o) { return 0 }
    try { return [long]$o.offset } catch { return 0 }
}

function Set-Offset([long]$v) {
    try {
        [System.IO.File]::WriteAllText($script:OffsetPath, (@{ offset = $v } | ConvertTo-Json), (New-Object System.Text.UTF8Encoding($false)))
    } catch { }
}

function Get-BoundChat {
    $o = Read-JsonFile $script:BoundPath
    if ($null -eq $o) { return $null }
    try { return [string]$o.chatId } catch { return $null }
}

function Set-BoundChat([string]$id) {
    try {
        [System.IO.File]::WriteAllText($script:BoundPath, (@{ chatId = $id } | ConvertTo-Json), (New-Object System.Text.UTF8Encoding($false)))
    } catch { }
}

function Invoke-Tg([string]$method, [hashtable]$query = @{}, [hashtable]$body = $null) {
    $url = "$ApiBase/bot$($script:Token)/$method"
    if ($query -and $query.Count -gt 0) {
        $parts = @()
        foreach ($k in $query.Keys) { $parts += ([uri]::EscapeDataString([string]$k) + '=' + [uri]::EscapeDataString([string]$query[$k])) }
        $url += '?' + ($parts -join '&')
    }
    if ($null -ne $body) { return Invoke-RestMethod -Uri $url -Method Post -Body $body -TimeoutSec 40 -UseBasicParsing }
    return Invoke-RestMethod -Uri $url -Method Get -TimeoutSec ($PollTimeoutSec + 15) -UseBasicParsing
}

function Get-BoardJson([string]$path, [int]$timeout = 10) {
    try {
        $r = Invoke-WebRequest -Uri ($BoardUrl + $path) -TimeoutSec $timeout -UseBasicParsing
        return ($r.Content | ConvertFrom-Json)
    } catch { return $null }
}

function Get-AgeText($sec) {
    if ($null -eq $sec) { return '—' }
    $s = [double]$sec
    if ($s -lt 0) { return 'нет данных' }
    if ($s -lt 1) { return 'только что' }
    if ($s -lt 90) { return ('{0:0} с' -f $s) }
    if ($s -lt 5400) { return ('{0:0} мин' -f ($s / 60)) }
    return ('{0:0.#} ч' -f ($s / 3600))
}

function Format-Size([double]$bytes) {
    if ($bytes -ge 1GB) { return ('{0:0.0} ГБ' -f ($bytes / 1GB)) }
    if ($bytes -ge 1MB) { return ('{0:0.0} МБ' -f ($bytes / 1MB)) }
    return ('{0:0} КБ' -f ($bytes / 1KB))
}

function Get-StateMessage {
    $d = Get-BoardJson '/data' 12
    if ($null -eq $d) {
        return "🏠 ДОМ — ЭНЕРГИЯ`n`n⛔ Плата не отвечает по локальной сети.`nПроверьте Wi-Fi и питание платы.`n`nВремя ПК: " + (Get-Date -Format 'dd.MM.yyyy HH:mm:ss')
    }
    $st = Get-BoardJson '/storage' 10
    $m = Get-BoardJson '/measure' 10
    $gf = Get-BoardJson '/gapfill/status' 10
    $L = New-Object System.Collections.Generic.List[string]
    $L.Add('🏠 ДОМ — ЭНЕРГИЯ · состояние')
    $L.Add('')
    $L.Add(('Плата: сборка {0} · прошивка {1}' -f $d.build, $d.fw))
    $L.Add(('Uptime: {0:0.#} ч · свободно памяти {1} КБ' -f ([double]$d.up / 3600), [math]::Round([double]$d.heap / 1KB)))
    $netWord = if ($d.net) { 'интернет есть' } else { 'интернета нет' }
    $L.Add(('LAN: Wi-Fi {0} дБм · {1} · реле {2}' -f $d.rssi, $netWord, $(if ($d.r) { 'ВКЛ' } else { 'ВЫКЛ' })))
    $tok = if ($d.tok) { 'токен есть' } else { 'токена нет' }
    $cloud = 'облако: ' + $tok
    if ($null -ne $d.cHold -and [int]$d.cHold -eq 1) { $cloud += ' · пауза (локальные данные свежие)' }
    elseif ($null -ne $d.cHold -and [int]$d.cHold -eq 8) { $cloud += ' · пауза (сетевой бюджет)' }
    if ($null -ne $d.cOk) { $cloud += (' · успешно {0} из {1}' -f $d.cOk, $d.cTry) }
    if ($null -ne $d.cFail -and [int]$d.cFail -gt 0) { $cloud += (' · подряд ошибок {0}' -f $d.cFail) }
    $L.Add($cloud)
    $L.Add('')
    $L.Add('Показатели:')
    $L.Add(('  напряжение {0:0.0} В · возраст {1}' -f [double]$d.v, (Get-AgeText $d.vAge)))
    $L.Add(('  мощность {0:0.0} Вт · ток {1:0.00} А' -f [double]$d.p, [double]$d.amp))
    $L.Add(('  температура реле {0:0.0} °C · возраст {1}' -f [double]$d.temp, (Get-AgeText $d.tAge)))
    if ($null -ne $m -and $null -ne $m.voltage) {
        $L.Add(('  источник V/P/I/T: {0} · возраст {1}' -f $m.voltage.source, (Get-AgeText $m.voltage.ageSec)))
    }
    if ($d.stale) { $L.Add('  ⚠ данные помечены как устаревшие') }
    $L.Add('')
    $L.Add('Энергия и деньги:')
    $L.Add(('  сегодня {0:0.000} кВт·ч · {1:0.00} ₽' -f [double]$d.tkwh, [double]$d.tcost))
    $L.Add(('  вчера {0:0.000} кВт·ч · {1:0.00} ₽' -f [double]$d.ykwh, [double]$d.ykost))
    $L.Add(('  месяц {0:0.00} кВт·ч · {1:0.00} ₽' -f [double]$d.mkwh, [double]$d.mcost))
    $L.Add(('  счётчик {0:0.00} кВт·ч · тариф {1:0.00} ₽ (день {2:0.00} / ночь {3:0.00})' -f [double]$d.e, [double]$d.tariff, [double]$d.tday, [double]$d.tnight))
    $L.Add('')
    if ($null -ne $st) {
        $wOk = if ($null -ne $st.writeOk) { $st.writeOk } else { '—' }
        $wFail = if ($null -ne $st.writeFail) { $st.writeFail } else { '—' }
        $L.Add(('Карта: свободно {0} · строк {1}' -f (Format-Size ([double]$st.freeBytes)), $d.sdRows))
        $L.Add(('  запись: ok {0} · fail {1} · последняя запись {2} назад' -f $wOk, $wFail, (Get-AgeText $st.lastWriteAgeSec)))
    } else {
        $L.Add(('Карта: строк {0} · /storage не ответил' -f $d.sdRows))
    }
    if ($null -ne $gf) {
        $stage = if ($gf.commitStage) { [string]$gf.commitStage } else { 'idle' }
        $inserted = if ($null -ne $d.gapFillOk) { [int]$d.gapFillOk } else { 0 }
        $L.Add(('Пропуски: {0} ({1:0} мин) · gapfill {2} · вставлено сегодня {3}' -f $d.gaps, $d.gapMin, $stage, $inserted))
    }
    try {
        $b = Invoke-WebRequest -Uri ($BoardUrl + '/boots') -TimeoutSec 8 -UseBasicParsing
        $bl = ($b.Content -split "`n") | Where-Object { $_.Trim() } | Select-Object -Last 1
        if ($bl) { $L.Add(('Последняя загрузка: ' + $bl.Trim())) }
    } catch { }
    $L.Add('')
    $L.Add('ПК: ' + (Get-Date -Format 'dd.MM.yyyy HH:mm:ss') + ' · запрос «пинг»')
    $text = ($L -join "`n")
    if ($text.Length -gt 3900) { $text = $text.Substring(0, 3900) + "`n…" }
    return $text
}

function Get-HelpMessage {
    return "🏠 ДОМ — ЭНЕРГИЯ · бот`n`nКоманды:`n• пинг / ping / /status — полное состояние платы`n• помощь / /help — эта справка`n`nПиши только в личный чат с ботом."
}

function Send-Reply([long]$chatId, [string]$text) {
    for ($try = 1; $try -le 3; $try++) {
        try {
            $r = Invoke-Tg 'sendMessage' @{} @{ chat_id = [string]$chatId; text = $text; disable_web_page_preview = 'true' }
            if ($r.ok) { Log ("ответ отправлен в чат $chatId (" + $text.Length + ' симв.)'); return $true }
            if ($null -ne $r.retry_after) { Start-Sleep -Seconds ([Math]::Min(30, [int]$r.retry_after)); continue }
            LogErr 'send' ('Telegram не принял ответ: ' + ($r | ConvertTo-Json -Compress))
            return $false
        } catch {
            $code = $null
            try { $code = [int]$_.Exception.Response.StatusCode } catch { }
            if ($code -eq 429) {
                $ra = 3
                try { $ra = [int](($_.Exception.Response.Headers['Retry-After'] -as [string])) } catch { }
                Start-Sleep -Seconds ([Math]::Min(30, [Math]::Max(1, $ra)))
                continue
            }
            LogErr 'send' ('sendMessage: ' + $_.Exception.Message)
            return $false
        }
    }
    return $false
}

function Test-IsPing([string]$text) {
    $t = (($text.Trim()).ToLowerInvariant() -replace '\s+', ' ')
    foreach ($p in @('пинг', 'ping', '/ping', '/status', '/now', 'статус', 'состояние', 'status')) { if ($t -eq $p) { return $true } }
    return $false
}

function Test-IsHelp([string]$text) {
    $t = ($text.Trim()).ToLowerInvariant()
    foreach ($p in @('помощь', '/help', 'help', 'справка', '/start')) { if ($t -eq $p) { return $true } }
    return $false
}

# ------------------------------------------------------- единственный экземпляр
$mutexName = $MutexName
$createdNew = $false
$mutex = New-Object System.Threading.Mutex($true, $mutexName, [ref]$createdNew)
if (-not $createdNew) {
    Log 'второй экземпляр пинг-бота не нужен — выхожу'
    try { $mutex.Dispose() } catch { }
    exit 0
}

# ------------------------------------------------------------------ главный цикл
$iter = 0
$lastNoTokenLog = [datetime]::MinValue
$failStreak = 0                 # подряд ошибок Telegram: пауза растёт, чтобы не спамить
while ($true) {
    $iter++
    $bc = Get-BotConfig
    $script:Token = [string]$bc.token
    $tokenOk = (-not [string]::IsNullOrWhiteSpace($script:Token)) -and ($script:Token -match '^[0-9]{6,12}:[A-Za-z0-9_-]{20,}$')
    if (-not $tokenOk) {
        if (((Get-Date) - $lastNoTokenLog).TotalMinutes -ge 30) {
            Log ('нет корректного tgBotToken (tg-ping-config.json или settings.json) — жду' + $(if ($script:Token) { ' (значение не похоже на токен)' } else { '' }))
            $lastNoTokenLog = Get-Date
        }
        Start-Sleep -Seconds $NoTokenSleepSec
        if ($Once) { exit 0 }
        continue
    }

    if ($null -eq $script:PairFrom) { $script:PairFrom = Get-Date; Log ('токен получен — окно авто-привязки ' + $PairWindowMin + ' мин началось') }
    # allowlist: задан, но битый — ЗАКРЫТО (никакой авто-привязки)
    $configured = [string]$bc.chat
    $allow = $configured
    if ([string]::IsNullOrWhiteSpace($allow)) { $allow = [string](Get-BoundChat) }
    if (-not [string]::IsNullOrWhiteSpace($configured) -and $configured -notmatch '^\s*-?\d+\s*$') {
        LogErr 'badchat' ('tgChatId задан, но не число: «' + $configured + '» — бот закрыт. Исправьте tgChatId.')
        Start-Sleep -Seconds $NoTokenSleepSec
        if ($Once) { exit 2 }
        continue
    }
    if (-not [string]::IsNullOrWhiteSpace($allow)) { $allow = $allow.Trim() }
    if ($iter -eq 1) { Log ('старт: api=' + $ApiBase + ' · чат=' + $(if ($allow) { $allow } else { 'не привязан (окно ' + $PairWindowMin + ' мин)' })) }

    $offset = Get-Offset
    try {
        $resp = Invoke-Tg 'getUpdates' @{ offset = [string]$offset; timeout = [string]$PollTimeoutSec; allowed_updates = '["message"]' }
    } catch {
        $failStreak++
        $wait = [Math]::Min(60, $BackoffBaseSec * [Math]::Pow(2, [Math]::Min(4, $failStreak - 1)))
        LogErr 'poll' ('getUpdates: ' + $_.Exception.Message + ' — пауза ' + [int]$wait + ' с (подряд ' + $failStreak + ')')
        Start-Sleep -Seconds ([int]$wait)
        if ($Once) { exit 1 }
        continue
    }
    if ($null -eq $resp -or -not $resp.ok) {
        $failStreak++
        $wait = [Math]::Min(60, $BackoffBaseSec * [Math]::Pow(2, [Math]::Min(4, $failStreak - 1)))
        LogErr 'poll' ('getUpdates вернул не ok: ' + ($resp | ConvertTo-Json -Compress) + ' — пауза ' + [int]$wait + ' с')
        Start-Sleep -Seconds ([int]$wait)
        if ($Once) { exit 1 }
        continue
    }
    $failStreak = 0   # успешный опрос — сбрасываем паузу
    foreach ($upd in @($resp.result)) {
        if ($null -eq $upd) { continue }
        $nextOffset = [long]$upd.update_id + 1
        if ($nextOffset -gt (Get-Offset)) { Set-Offset $nextOffset }
        if ($null -eq $upd.message) { continue }
        $msg = $upd.message
        $chat = $msg.chat
        if ($null -eq $chat) { continue }
        if ([string]$chat.type -ne 'private') { Log ('группа/канал ' + $chat.type + ' — игнор'); continue }
        if ($null -ne $msg.from -and [string]$msg.from.id -ne [string]$chat.id) { Log 'from != chat.id — игнор'; continue }
        $chatId = [string]$chat.id
        $text = [string]$msg.text
        if ([string]::IsNullOrWhiteSpace($text)) { continue }
        $isPing = Test-IsPing $text
        $isHelp = Test-IsHelp $text

        if ([string]::IsNullOrWhiteSpace($allow)) {
            # авто-привязка только в окне PairWindowMin и только по понятной команде
            $age = ((Get-Date) - $script:PairFrom).TotalMinutes
            if (-not ($isPing -or $isHelp)) { Log ('не привязан: игнор сообщения вне окна команд (чат ' + $chatId + ')'); continue }
            if ($age -gt $PairWindowMin) {
                Log ('окно авто-привязки истекло (' + [Math]::Round($age) + ' мин) — задайте tgChatId вручную (чат ' + $chatId + ')')
                continue
            }
            $allow = $chatId
            Set-BoundChat $chatId
            $own = $bc.own
            Save-OwnCfg $own $script:Token $chatId
            Log ('привязан первый личный чат: ' + $chatId + ' (окно ' + $PairWindowMin + ' мин)')
        } elseif ($chatId -ne $allow) {
            Log ('чужой чат ' + $chatId + ' — игнор')
            continue
        }

        if ($isPing) {
            Log ('пинг от ' + $chatId)
            try { $state = Get-StateMessage } catch { $state = '🏠 ДОМ — ЭНЕРГИЯ · не удалось собрать состояние: ' + $_.Exception.Message }
            [void](Send-Reply ([long]$chatId) $state)
        } elseif ($isHelp) {
            [void](Send-Reply ([long]$chatId) (Get-HelpMessage))
        } else {
            Log ('неизвестная команда от ' + $chatId + ': ' + $text.Substring(0, [Math]::Min(40, $text.Length)))
        }
    }
    if ($Once -or ($MaxIterations -gt 0 -and $iter -ge $MaxIterations)) { break }
    Start-Sleep -Seconds $SleepBetweenSec
}
try { $mutex.ReleaseMutex() } catch { }
try { $mutex.Dispose() } catch { }
Log 'выход'
