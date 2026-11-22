﻿# ============================================================================

#  DOM MONITOR 2.0 — пульт управления устройством на ESP32 (CYD, Wi-Fi)

# ----------------------------------------------------------------------------

#  Источник данных №1 — HTTP  http://192.168.1.51/data    (обновление 5 с)

#  Источник данных №2 — COM5 (резерв, только если HTTP недоступен)

#

#  Вкладки: Экран · Устройство · История · Прошивка · Журнал · Защита · Терминал · Архив

#  ВАЖНО: файл хранится в UTF-8 С BOM (иначе PowerShell 5.1 покажет кракозябры).

# ============================================================================



Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Windows.Forms.DataVisualization

Add-Type -AssemblyName System.Drawing

Add-Type -AssemblyName System.Net.Http
. (Join-Path $PSScriptRoot 'ControlAuth.ps1')

[System.Windows.Forms.Application]::EnableVisualStyles()



$ErrorActionPreference = 'Stop'

if (-not $env:ARDUINO_DIRECTORIES_DATA -and (Test-Path 'D:\DomEnergy\_moved\Arduino15')) {$env:ARDUINO_DIRECTORIES_DATA='D:\DomEnergy\_moved\Arduino15'}
if (-not $env:ARDUINO_DIRECTORIES_USER -and (Test-Path (Join-Path $env:USERPROFILE 'Documents\Arduino'))) {$env:ARDUINO_DIRECTORIES_USER=Join-Path $env:USERPROFILE 'Documents\Arduino'}



# --------------------------------------------------------------- файловый лог

$script:LogDir  = Join-Path $env:APPDATA 'DomMonitor'

if (!(Test-Path $script:LogDir)) { New-Item -ItemType Directory -Path $script:LogDir -Force | Out-Null }

$script:LogFile = Join-Path $script:LogDir 'app.log'



function Write-AppLog($msg) {

    try {

        $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg

        [System.IO.File]::AppendAllText($script:LogFile, $line + "`r`n",

            (New-Object System.Text.UTF8Encoding($false)))

    } catch { }

}



try {

    if ((Test-Path $script:LogFile) -and ((Get-Item $script:LogFile).Length -gt 2MB)) {

        Move-Item $script:LogFile ($script:LogFile + '.old') -Force

    }

} catch { }




# Один экземпляр: супервизор и ручной запуск не должны открывать два окна
# (два экземпляра дублируют опрос платы и мешают друг другу с COM-портом).
$script:instMutex = New-Object System.Threading.Mutex($false, 'Global\DomMonitorSingleInstance')
if (-not $script:instMutex.WaitOne(0)) { exit }

Write-AppLog '=== ЗАПУСК DOM MONITOR 2.0 ==='

Write-AppLog ('PowerShell ' + $PSVersionTable.PSVersion)



trap {

    Write-AppLog '!!! НЕОБРАБОТАННАЯ ОШИБКА !!!'

    Write-AppLog ('тип: ' + $_.Exception.GetType().FullName)

    Write-AppLog ('текст: ' + $_.Exception.Message)

    try { Write-AppLog ('строка: ' + $_.InvocationInfo.ScriptLineNumber + ' — ' + $_.InvocationInfo.Line.Trim()) } catch { }

    try { Write-AppLog ('стек: ' + $_.ScriptStackTrace) } catch { }

    try {

        [System.Windows.Forms.MessageBox]::Show(

            ("Программа остановлена из-за ошибки:`r`n`r`n" + $_.Exception.Message +

             "`r`n`r`nПодробности в файле:`r`n" + $script:LogFile),

            'DOM MONITOR — ошибка', 'OK', 'Error') | Out-Null

    } catch { }

    exit 1

}



# ------------------------------------------------------------------- пути

$script:AppDir     = Split-Path -Parent $MyInvocation.MyCommand.Path

$script:CfgDir     = Join-Path $env:APPDATA 'DomMonitor'

$script:CfgFile    = Join-Path $script:CfgDir 'settings.json'

$script:FlashLog   = Join-Path $env:TEMP 'dommon-flash.log'

$script:ArduinoCli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'

$script:LibsDir    = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries'

$script:DefSketch  = Join-Path (Split-Path -Parent $script:AppDir) 'cydv4'

$script:ArchiveDir = Join-Path $script:CfgDir 'firmware-archive'

$script:TgScript   = Join-Path $script:AppDir 'energy-telegram.ps1'

$script:StateFile  = Join-Path $script:LogDir 'board.json'



if (!(Test-Path $script:CfgDir)) { New-Item -ItemType Directory -Path $script:CfgDir -Force | Out-Null }



# --------------------------------------------------------------- настройки

# ОДИН источник правды: %APPDATA%\DomMonitor\settings.json.

# Мастер настройки пишет сюда же (раньше он писал DomMonitor.json рядом с программой,

# а приложение читало другой файл — из-за этого мастер открывался при каждом старте).

$script:cfg = [ordered]@{

    comPort   = 'COM5'

    baud      = 115200

    sketch    = $script:DefSketch

    board     = 'http://192.168.1.51'

    fqbn      = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'

    boardIp   = '192.168.1.51'

    port      = 'COM5'

    setupDone = $false

    # --- тема интерфейса: light | dark | mono | pink | red | rainbow -----

    theme     = 'dark'

    # --- уведомления (пороги; можно менять в settings.json) ---

    alertOn       = $true

    alertVMin     = 200

    alertVMax     = 250

    alertTMax     = 55

    alertOffMin   = 3

    alertSpikeW   = 500

    alertSpikeX   = 2.0

    # --- Telegram через реле GitHub Actions (токен НЕ в репозитории) ---

    tgRelay   = $false

    tgRepo    = 'nika-sc/Dom-Energy'

    githubToken = ''

    tgScript  = ''

    # --- P2.3: добровольный месячный лимит кВт·ч (0 = лимит не задан) ---
    budgetKwh = 0

    # --- P2.3: добровольный месячный лимит в рублях (0 = не задан) ---
    budgetRub = 0

    # --- P2.4: папка последнего экспорта CSV (диалог запоминает выбор) ---
    csvDir    = ''

}



$script:CfgLegacy = Join-Path $script:AppDir 'DomMonitor.json'

$script:CfgUpgraded = $false



if (Test-Path $script:CfgFile) {

    try {

        $loaded = Get-Content $script:CfgFile -Raw | ConvertFrom-Json

        foreach ($k in @($script:cfg.Keys)) {

            if ($loaded.PSObject.Properties.Name -contains $k) { $script:cfg[$k] = $loaded.$k }

        }

        # старый settings.json без setupDone, но с рабочим адресом платы —

        # значит настройка уже была: мастер при старте не нужен

        if (($loaded.PSObject.Properties.Name -notcontains 'setupDone') -and

            ("$($script:cfg.board)" -match '^https?://\d{1,3}(\.\d{1,3}){3}') -and

            ("$($script:cfg.comPort)" -match '^COM\d+$')) {

            $script:cfg.setupDone = $true

            $script:CfgUpgraded = $true

        }

    } catch { }

}



function Save-Cfg {

    # BLOCKER (ревью 16.11): раньше писались только известные приложению ключи, и
    # чужие настройки (например tgBotToken/tgChatId пинг-бота) стирались при
    # каждом сохранении. Теперь неизвестные ключи из старого файла переносим.
    try {
        $out = [ordered]@{}
        foreach ($k in @($script:cfg.Keys)) { $out[$k] = $script:cfg[$k] }
        if (Test-Path $script:CfgFile) {
            try {
                $old = Get-Content $script:CfgFile -Raw | ConvertFrom-Json
                foreach ($pr in $old.PSObject.Properties) {
                    if (-not $out.Contains($pr.Name)) { $out[$pr.Name] = $pr.Value }
                }
            } catch { }
        }
        $out | ConvertTo-Json -Depth 5 | Set-Content $script:CfgFile -Encoding UTF8
    } catch { }

}



# Старый файл мастера: переносим значения в settings.json и удаляем.

$script:CfgMigrated = $false

if (Test-Path $script:CfgLegacy) {

    try {

        $old = Get-Content $script:CfgLegacy -Raw -Encoding UTF8 | ConvertFrom-Json

        $oIp = [string]$old.boardIp

        if ($oIp -match '^\d{1,3}(\.\d{1,3}){3}$') {

            $script:cfg.board = 'http://' + $oIp

            $script:cfg.boardIp = $oIp

        }

        $oPort = [string]$old.port

        if ($oPort -match '^COM\d+$') { $script:cfg.comPort = $oPort; $script:cfg.port = $oPort }

        if ($old.setupDone -eq $true) { $script:cfg.setupDone = $true }

        if ($null -ne $old.wifi) { $script:cfg.wifi = $old.wifi }

        Save-Cfg

        $script:CfgMigrated = $true

        Remove-Item $script:CfgLegacy -Force -ErrorAction SilentlyContinue

    } catch { }

}



if ("$($script:cfg.board)" -match '^https?://(\d{1,3}(\.\d{1,3}){3})') {

    $script:cfg.boardIp = $Matches[1]

}

if ("$($script:cfg.comPort)" -notmatch '^COM\d+$') { $script:cfg.comPort = 'COM5' }

$script:cfg.port = $script:cfg.comPort

if ("$($script:cfg.fqbn)" -notmatch 'esp32:esp32:') { $script:cfg.fqbn = 'esp32:esp32:esp32:PartitionScheme=min_spiffs' }

if ([string]::IsNullOrWhiteSpace("$($script:cfg.sketch)")) { $script:cfg.sketch = $script:DefSketch }



# ------------------------------------------------------------------ состояние

$script:port         = $null

$script:rxBuf        = ''

$script:rxSeen       = $false

$script:job          = $null

$script:flashPos     = 0



$script:board        = $null          # последний удачный ответ /data

$script:boardAt      = [datetime]::MinValue

$script:boardErr     = ''

$script:boardBad     = 0              # счётчик неудачных опросов подряд



$script:serialData   = $null          # резервный источник — COM5

$script:serialAt     = [datetime]::MinValue

$script:serialStatus = $null

$script:serialZero   = $false



$script:pollTask     = $null

$script:pollAt       = [datetime]::MinValue



$script:days         = @()            # список дней от платы (сначала новые)

$script:daysGaps     = @{}            # дата -> [пропусков, минут]

$script:daysAt       = [datetime]::MinValue

$script:daysTask     = $null



$script:histCache    = @{}            # дата -> массив часов

$script:histAt       = @{}            # дата -> когда получено

$script:histTask     = $null

$script:histTaskDate = ''

$script:histPending  = $null

$script:histErr      = ''

$script:histErrDate  = ''

$script:histSel      = (Get-Date).AddDays(-1).ToString('yyyy-MM-dd')  # по умолчанию вчера

# Выбранный час — объект состояния {Date, Hour, Metric, RequestId}. Индекса серии
# больше нет: клик по любой ячейке часа (даже пустой) выбирает час, а обновление
# данных выбор не сбрасывает. RequestId растёт на каждый выбор/смену даты —
# по нему отсекаем ответы устаревших запросов.
$script:histSelect    = [pscustomobject]@{ Date = $script:histSel; Hour = $null; Metric = 'p'; RequestId = 0 }
$script:histEpoch     = 0        # растёт при смене даты/IP: старые ответы выбрасываем
$script:histTaskEpoch = 0
$script:histCts       = $null    # CancellationTokenSource запроса /history
$script:histRequests  = @{}
$script:histFirstAt   = @{}
$script:gapTask       = $null    # асинхронный /gapfill/status
$script:gapCts        = $null
$script:rangeLoad     = $null    # асинхронная загрузка /range для окна периода
$script:rangeWeather  = $null    # метка погоды окна периода (для фоновой загрузки)
# --- P2.1/P2.2: сравнение периодов и пики нагрузки (окно «Статистика за период»)
$script:rangeSpan     = $null    # выбранный диапазон {From, To, Days}
$script:cmpRows       = @()      # строки ПРЕДЫДУЩЕГО периода той же длины
$script:cmpLoad       = $null    # асинхронная загрузка предыдущего периода
$script:cmpKey        = ''       # для какого периода уже загружены cmpRows
$script:cmpFail       = ''       # ошибка загрузки предыдущего периода
$script:rangeCmpUi    = $null    # ссылки на секции «Сравнение» и «Пики»
$script:rangeCmpTimer = $null    # таймер окна периода (пересчёт секций)
# --- P2.5b: реальный комнатный климат за период (GET /climatehist) -----------
$script:climateHistCache = @{}   # кэш /climatehist по ключу 'from|to' -> {Ok,Err,Days,Totals,At,Ip}
$script:climateHistOrder = @()   # порядок ключей кэша (ограничиваем размер)
$script:climateLoad      = $null # асинхронный запрос /climatehist: один на смену периода




$script:lastUi       = [datetime]::MinValue

$script:lastDev      = [datetime]::MinValue

$script:lastLog      = [datetime]::MinValue

$script:uiReady      = $false

$script:totalPolls   = 0

$script:pollMs       = 0

$script:pollStartAt  = [datetime]::MinValue

$script:pollErr      = 0              # неудачных опросов /data всего

$script:httpErrSeen  = ''             # последняя ошибка HTTP (чтобы не дублировать в журнал)

# --- P0.1: /measure (источник и свежесть V/P/I/T) -----------------------------
$script:measTask    = $null              # асинхронный GET /measure
$script:measAt      = [datetime]::MinValue
$script:measStartAt = [datetime]::MinValue
$script:measMs      = 0
$script:measTotal   = 0
$script:measErrN    = 0
$script:measLogSeen = ''
$script:measure     = $null              # последний удачный ответ /measure
$script:measureAt   = [datetime]::MinValue
$script:measureErr  = ''                 # 'нет ответа /measure' и т.п.
$script:measureBad  = 0


$script:fwVal        = ''             # версия прошивки по HTTP (/ или /data.fw)

$script:fwAt         = [datetime]::MinValue

$script:fwTask       = $null

$script:portFree     = $null          # $true — порт можно открыть, $false — занят другой программой

$script:portTestAt   = [datetime]::MinValue

$script:hoverScr     = -1             # час под курсором на графике «Экран»

$script:hoverHist    = -1             # час под курсором на графике «История»

$script:pinScr       = -1             # закреплённая подсказка (клик)

$script:pinHist      = -1

$script:mouseScr     = New-Object System.Drawing.Point(-1, -1)

$script:mouseHist    = New-Object System.Drawing.Point(-1, -1)

$script:histMetric   = 'p'            # показатель, выбранный кликом по карточке

$script:histPinDate  = (Get-Date).ToString('yyyy-MM-dd')

$script:cardMetric   = [ordered]@{ v = 'v'; p = 'p'; t = 't'; e = 'e' }

$script:beatSec      = 30             # период подробной строки журнала, с



# ------------------------------------------------------------------ помощники

function C([string]$hex) { return [System.Drawing.ColorTranslator]::FromHtml($hex) }

function Fnt([string]$name, [float]$size, [bool]$bold) {

    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }

    return New-Object System.Drawing.Font($name, $size, $style)

}


# ---------------------------------------------------------------- ui-kit: база

function HexOf($c) {
    try { return ('#{0:X2}{1:X2}{2:X2}' -f [int]$c.R, [int]$c.G, [int]$c.B) } catch { return '#000000' }
}

# смешать два цвета: k=0 — первый, k=1 — второй
function MixC([string]$a, [string]$b, [double]$k) {
    try {
        $ca = [System.Drawing.ColorTranslator]::FromHtml($a)
        $cb = [System.Drawing.ColorTranslator]::FromHtml($b)
        $r = [int][Math]::Round($ca.R + ($cb.R - $ca.R) * $k)
        $g = [int][Math]::Round($ca.G + ($cb.G - $ca.G) * $k)
        $bl = [int][Math]::Round($ca.B + ($cb.B - $ca.B) * $k)
        return ('#{0:X2}{1:X2}{2:X2}' -f $r, $g, $bl)
    } catch { return $a }
}

# «ручной» цвет из кода -> токен текущего скина (кнопки, диалоги)
function SkinC([string]$hex) {
    try {
        if ($null -eq $script:LegacyRole -or $null -eq $script:CLR) { return $hex }
        $h = ([string]$hex).ToUpper()
        if ($script:LegacyRole.ContainsKey($h)) {
            $role = [string]$script:LegacyRole[$h]
            if ($script:CLR.Contains($role)) { return [string]$script:CLR[$role] }
        }
    } catch { }
    return $hex
}

# цвет кнопки при наведении: тёмные — светлее, светлые — чуть темнее (иначе не видно)
function Get-HoverC([string]$hex) {
    try {
        $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
        $lum = (0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B) / 255.0
        if ($lum -gt 0.72) { return (MixC $hex '#000000' 0.10) }
        return (MixC $hex '#FFFFFF' 0.16)
    } catch { return $hex }
}

# скругление контрола: GraphicsPath + Region
function Set-Round($ctl, [int]$r = 12) {
    try {
        if ($null -eq $ctl) { return }
        $w = [int]$ctl.Width
        $h = [int]$ctl.Height
        if ($w -le 2 -or $h -le 2) { return }
        $d = [Math]::Min($r * 2, [Math]::Min($w, $h))
        if ($d -lt 4) { return }
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddArc(0, 0, $d, $d, 180, 90)
        $path.AddArc($w - $d - 1, 0, $d, $d, 270, 90)
        $path.AddArc($w - $d - 1, $h - $d - 1, $d, $d, 0, 90)
        $path.AddArc(0, $h - $d - 1, $d, $d, 90, 90)
        $path.CloseFigure()
        $old = $ctl.Region
        $ctl.Region = New-Object System.Drawing.Region($path)
        if ($null -ne $old) { $old.Dispose() }
        $path.Dispose()
    } catch { }
}

function Set-RoundDot($ctl) {
    try {
        if ($null -eq $ctl) { return }
        $w = [int]$ctl.Width
        $h = [int]$ctl.Height
        if ($w -le 1 -or $h -le 1) { return }
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddEllipse(0, 0, $w, $h)
        $old = $ctl.Region
        $ctl.Region = New-Object System.Drawing.Region($path)
        if ($null -ne $old) { $old.Dispose() }
        $path.Dispose()
    } catch { }
}

function Set-Dot($dot, [string]$hex) {
    if ($null -eq $dot) { return }
    try { $dot.BackColor = C $hex } catch { }
}

# двойная буферизация (свойства защищённые — через reflection, без Add-Type)
function Enable-DoubleBuffer($ctl) {
    if ($null -eq $ctl) { return }
    try {
        $flags = [System.Reflection.BindingFlags]::NonPublic -bor [System.Reflection.BindingFlags]::Instance
        $pi = [System.Windows.Forms.Control].GetProperty('DoubleBuffered', $flags)
        if ($null -ne $pi) { $pi.SetValue($ctl, $true, $null) }
        $mi = [System.Windows.Forms.Control].GetMethod('SetStyle', $flags)
        if ($null -ne $mi) {
            $styles = [System.Windows.Forms.ControlStyles]::OptimizedDoubleBuffer -bor [System.Windows.Forms.ControlStyles]::AllPaintingInWmPaint
            $null = $mi.Invoke($ctl, @($styles, $true))
        }
    } catch { }
}

function Enable-DoubleBufferAll($root) {
    if ($null -eq $root) { return }
    Enable-DoubleBuffer $root
    try { foreach ($ch in @($root.Controls)) { Enable-DoubleBufferAll $ch } } catch { }
}

# сглаженная кривая (AddCurve) + полупрозрачная градиентная заливка под ней
function Draw-Curve($g, $pts, $pen, [string]$fillHex, [float]$baseY) {
    try {
        if ($null -eq $g -or $null -eq $pts) { return }
        $n = $pts.Count
        if ($n -lt 2) { return }
        $arr = [System.Drawing.PointF[]]::new($n)
        for ($i = 0; $i -lt $n; $i++) { $arr[$i] = $pts[$i] }
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddCurve($arr, [single]0.45)
        if ($fillHex -and $fillHex -match '^#') {
            $poly = [System.Drawing.PointF[]]::new($n + 2)
            $poly[0] = [System.Drawing.PointF]::new([single]$arr[0].X, [single]$baseY)
            for ($i = 0; $i -lt $n; $i++) { $poly[$i + 1] = $arr[$i] }
            $poly[$n + 1] = [System.Drawing.PointF]::new([single]$arr[$n - 1].X, [single]$baseY)
            $minY = [single]$baseY
            for ($i = 0; $i -lt $n; $i++) { if ($arr[$i].Y -lt $minY) { $minY = [single]$arr[$i].Y } }
            $bx = [single]$arr[0].X
            $bw = [single]($arr[$n - 1].X - $arr[0].X)
            if ($bw -lt 1) { $bw = [single]1 }
            $bh = [single]($baseY - $minY)
            if ($bh -lt 1) { $bh = [single]1 }
            $rect = [System.Drawing.RectangleF]::new($bx, $minY, $bw, $bh)
            $col = [System.Drawing.ColorTranslator]::FromHtml($fillHex)
            $c1 = [System.Drawing.Color]::FromArgb(115, $col.R, $col.G, $col.B)
            $c2 = [System.Drawing.Color]::FromArgb(0, $col.R, $col.G, $col.B)
            $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $c1, $c2, [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
            $g.FillPolygon($br, $poly)
            $br.Dispose()
        }
        $g.DrawPath($pen, $path)
        $path.Dispose()
    } catch { try { Write-AppLog ('curve: ' + $_.Exception.Message) } catch { } }
}

function Num($v, [string]$fmt) {

    if ($null -eq $v) { return '—' }

    try { return ([double]$v).ToString($fmt, [System.Globalization.CultureInfo]::InvariantCulture) }

    catch { return '—' }

}

function Field($o, [string[]]$names, $def = $null) {

    if ($null -eq $o) { return $def }

    foreach ($n in $names) {

        $p = $o.PSObject.Properties[$n]

        if ($null -ne $p -and $null -ne $p.Value -and "$($p.Value)" -ne '') { return $p.Value }

    }

    return $def

}

function Set-Lbl($lbl, [string]$text) {

    if ($null -ne $lbl -and $lbl.Text -ne $text) { $lbl.Text = $text }

}

function Format-Uptime($sec) {

    if ($null -eq $sec) { return '—' }

    $s = [int]$sec

    if ($s -le 0) { return '—' }

    $d = [Math]::Floor($s / 86400)

    $h = [Math]::Floor(($s % 86400) / 3600)

    $m = [Math]::Floor(($s % 3600) / 60)

    if ($d -gt 0) { return "$d д $h ч $m мин" }

    if ($h -gt 0) { return "$h ч $m мин" }

    return "$m мин"

}

function Format-Age($sec) {

    $s = [int]$sec

    if ($s -lt 60) { return "$s с" }

    if ($s -lt 3600) { return ([Math]::Floor($s / 60)).ToString() + ' мин' }

    return ([Math]::Floor($s / 3600)).ToString() + ' ч ' + ([Math]::Floor(($s % 3600) / 60)).ToString() + ' мин'

}

# ----------------------------------------------------------------- время получения показателя

# Плата отдаёт epoch-секунды последнего получения (vAt/tAt/pAt/eAt) и возраст (vAge/...).

# Если поля нет или оно равно 0 — показатель ещё ни разу не приходил.

$script:epoch = [datetime]'1970-01-01T00:00:00Z'   # уже в локальном времени



function Get-Stamp([string[]]$names) {

    $d = Get-Active

    if ($null -eq $d) { return $null }

    $raw = Field $d $names

    if ($null -eq $raw) { return $null }

    $sec = 0.0

    try { $sec = [double]$raw } catch { return $null }

    if ($sec -le 1000000000) { return $null }      # 0 или мусор — данных нет

    try { return $script:epoch.ToLocalTime().AddSeconds($sec) } catch { return $null }

}



function Format-Stamp($dt) {

    if ($null -eq $dt) { return 'нет данных' }

    $now = Get-Date

    if ($dt.Date -eq $now.Date) { return 'получено ' + $dt.ToString('HH:mm') }

    if ($dt.Date -eq $now.Date.AddDays(-1)) { return 'вчера ' + $dt.ToString('HH:mm') }

    return $dt.ToString('dd.MM HH:mm')

}



# цвет по возрасту: серый / жёлтый (>5 мин) / красный (>1 ч)

function Get-StampColor($dt) {

    if ($null -eq $dt) { return $script:CLR.dim2 }

    $age = ((Get-Date) - $dt).TotalSeconds

    if ($age -gt 3600) { return $script:CLR.err }

    if ($age -gt 300) { return $script:CLR.warn }

    return $script:CLR.dim2

}



# «получено 12:31» для карточки: текст + цвет + возраст в секундах

function Get-StampInfo([string[]]$names) {

    $dt = Get-Stamp $names

    $color = Get-StampColor $dt

    $age = 999999.0

    if ($null -ne $dt) { $age = ((Get-Date) - $dt).TotalSeconds }

    return [pscustomobject]@{ Text = (Format-Stamp $dt); Color = $color; At = $dt; Age = $age }

}



# самые свежие из четырёх показателей — для шапки и вкладки «История»

function Get-FreshestStamp {

    $best = $null

    foreach ($n in @(@('vAt'), @('tAt'), @('pAt'), @('eAt'))) {

        $dt = Get-Stamp $n

        if ($null -ne $dt -and ($null -eq $best -or $dt -gt $best)) { $best = $dt }

    }

    return $best

}



function Get-RuDay([string]$iso) {

    try {

        $dt = [datetime]::ParseExact($iso, 'yyyy-MM-dd', $null)

        $names = @('вс', 'пн', 'вт', 'ср', 'чт', 'пт', 'сб')

        return $dt.ToString('dd.MM.yyyy') + ' · ' + $names[[int]$dt.DayOfWeek]

    } catch { return $iso }

}



function New-Lbl($parent, [string]$text, $size, [string]$color, [bool]$bold, $x, $y) {

    $l = New-Object System.Windows.Forms.Label

    $l.Text = $text

    $l.Font = Fnt 'Segoe UI' $size $bold

    $l.ForeColor = C $color

    $l.AutoSize = $true

    $l.Location = New-Object System.Drawing.Point([int]$x, [int]$y)

    if ($null -ne $parent) { $parent.Controls.Add($l) }

    return $l

}



function New-Btn($parent, [string]$text, [string]$bg, [string]$fg, [int]$w, [int]$h) {
    $b = New-Object System.Windows.Forms.Button
    if ($h -le 0) { $h = 30 }
    $b.Text = $text
    $b.Size = New-Object System.Drawing.Size($w, $h)
    $b.FlatStyle = 'Flat'
    $hex = if ($bg -match '^#[0-9A-Fa-f]{6}$') { $bg.ToUpper() } else { '#2A3240' }
    $hex = SkinC $hex
    $b.BackColor = C $hex
    $b.ForeColor = C $fg
    $b.FlatAppearance.BorderSize = 0
    $b.FlatAppearance.MouseOverBackColor = C (Get-HoverC $hex)
    $b.FlatAppearance.MouseDownBackColor = C (Get-HoverC $hex)
    $b.Font = Fnt 'Segoe UI' 9 $true
    $b.Cursor = 'Hand'
    $b.UseVisualStyleBackColor = $false
    $b.Tag = $hex
    if ($null -ne $parent) { $parent.Controls.Add($b) }
    $b.Add_MouseEnter({ param($s, $e) try { if ($null -ne $s.Tag) { $s.BackColor = C (Get-HoverC ([string]$s.Tag)) } } catch { } })
    $b.Add_MouseLeave({ param($s, $e) try { $s.BackColor = C ([string]$s.Tag) } catch { } })
    $b.Add_Resize({ param($s, $e) Set-Round $s 8 })
    Set-Round $b 8
    return $b
}



# ============================================================================

#  HTTP-КАНАЛ К ПЛАТЕ

# ============================================================================

$script:http     = $null   # быстрый клиент: /data

$script:httpSlow = $null   # медленный клиент: /days, /history



function Init-Http {

    try {

        if ($null -eq $script:http) {

            $script:http = New-Object System.Net.Http.HttpClient

            $script:http.Timeout = [TimeSpan]::FromSeconds(4)

            # соединение не держим: у ESP32 мало сокетов, иначе другие клиенты получают отказ

            $script:http.DefaultRequestHeaders.ConnectionClose = $true
            Set-ControlClientHeaders $script:http

        }

        if ($null -eq $script:httpSlow) {

            $script:httpSlow = New-Object System.Net.Http.HttpClient

            $script:httpSlow.Timeout = [TimeSpan]::FromSeconds(20)

            $script:httpSlow.DefaultRequestHeaders.ConnectionClose = $true
            Set-ControlClientHeaders $script:httpSlow

        }

    } catch {

        Write-AppLog ('не удалось создать HttpClient: ' + $_.Exception.Message)

    }

}



function Get-BoardAge {

    if ($null -eq $script:board) { return 999999 }

    return ((Get-Date) - $script:boardAt).TotalSeconds

}



function Board-Online {

    if ($null -ne $script:board -and (Get-BoardAge) -le 12) { return $true }

    if ($null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) { return $true }

    return $false

}



# Какой набор данных показывать: HTTP в приоритете, COM5 — только резерв

function Get-Active {

    if ($null -ne $script:board -and (Get-BoardAge) -le 12) { return $script:board }

    if ($null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) { return $script:serialData }

    return $null

}



function Get-SourceName {

    if ($null -ne $script:board -and (Get-BoardAge) -le 12) { return 'с платы' }

    if ($null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) { return 'резерв COM5' }

    return 'нет связи'

}



# Неблокирующий опрос /data: раз в 5 с, ответ забираем на следующем тике

# текст ошибки из завершившейся задачи (HttpClient прячет код в InnerException)

function Get-TaskError($task) {

    try {

        $ex = $task.Exception

        while ($null -ne $ex.InnerException) { $ex = $ex.InnerException }

        return [string]$ex.Message

    } catch { return 'неизвестная ошибка' }

}



function Get-HttpCode([string]$msg) {

    $m = [regex]::Match("$msg", '\b([45]\d\d)\b')

    if ($m.Success) { return $m.Groups[1].Value }

    return ''

}



# ============================================================================
#  P0.1: ИСТОЧНИК И СВЕЖЕСТЬ ИЗМЕРЕНИЙ (/measure)
#  Плата отдаёт по каждому показателю value, unit, source (local|cloud|none),
#  ageSec, valid, local{value,ageSec}, cloud{value,ageSec} и ttl. Задача подписей —
#  честно показать, ОТКУДА значение и КОГДА оно получено:
#    «локальное реле · 8 с» · «облако · 40 с» · «устарело · 12 мин» ·
#    «нет свежих данных» (source=none) · «нет измерений» · «источник не определён».
#  source=none означает «нет свежих данных», а НЕ ноль: 0 Вт и отрицательная °C —
#  настоящие значения и нулём/пустотой не считаются.
# ============================================================================

function Get-MeasureField($m, [string[]]$names) {
    if ($null -eq $m) { return $null }
    foreach ($n in $names) {
        $p = $m.PSObject.Properties[$n]
        if ($null -ne $p) { return $p.Value }
    }
    return $null
}

function Get-MeasureMetric($m, [string]$key) {
    # узел /measure по ключу ('v','p','i','t') или $null, если такого поля нет
    if ($null -eq $m) { return $null }
    $node = switch ($key) {
        'v' { Get-MeasureField $m @('voltage') }
        'p' { Get-MeasureField $m @('power') }
        'i' { Get-MeasureField $m @('current', 'amperage') }
        't' { Get-MeasureField $m @('temperature', 'temp') }
        default { $null }
    }
    if ($null -eq $node) { return $null }
    $src = [string](Get-MeasureField $node @('source'))
    if (-not $src) { $src = 'unknown' }
    $val = Get-MeasureField $node @('value')
    $age = Get-MeasureField $node @('ageSec')
    $valid = Get-MeasureField $node @('valid')
    if ($null -eq $valid) { $valid = $true }
    $ttl = 90
    $ttlNode = Get-MeasureField $m @('ttl')
    if ($null -ne $ttlNode) {
        $tv = Get-MeasureField $ttlNode $(if ($key -eq 't') { @('tSec') } else { @('vpSec') })
        if ($null -ne $tv -and [int]$tv -gt 0) { $ttl = [int]$tv }
    }
    $ageSec = $null
    if ($null -ne $age) { try { $ageSec = [int]$age } catch { $ageSec = $null } }
    return [pscustomobject]@{
        Key      = $key
        Node     = $node
        Source   = $src
        Value    = $val
        AgeSec   = $ageSec
        Valid    = [bool]$valid
        Ttl      = $ttl
        HasValue = ($null -ne $val)
        Fresh    = ($src -eq 'local' -or $src -eq 'cloud')
    }
}

function Format-MeasureSource($m, [string]$key, [switch]$Short) {
    # Текст «источник · возраст» + цвет. $null — только когда /measure ещё ни
    # разу не ответил (тогда вызывающий оставляет прежнюю подпись).
    if ($null -eq $m) { return $null }
    $mm = Get-MeasureMetric $m $key
    if ($null -eq $mm) { return [pscustomobject]@{ Text = 'нет измерений'; Color = $script:CLR.dim2; Fresh = $false; Source = 'missing' } }
    $src = [string]$mm.Source
    # возраст «дрейфует»: с момента ответа платы прошло время, и если /measure
    # перестал отвечать, старое «4 с» становится неправдой. Считаем честно.
    $age = $mm.AgeSec
    if ($null -ne $age -and $script:measureAt -gt [datetime]::MinValue) {
        $drift = ((Get-Date) - $script:measureAt).TotalSeconds
        if ($drift -gt 0) { $age = $age + [int]$drift }
    }
    if ($src -eq 'local' -or $src -eq 'cloud') {
        if ($null -ne $age -and $age -gt $mm.Ttl) {
            return [pscustomobject]@{ Text = ('устарело · ' + (Format-Age $age)); Color = $script:CLR.warn; Fresh = $false; Source = 'stale' }
        }
        $name = if ($src -eq 'local') { $(if ($Short) { 'лок.реле' } else { 'локальное реле' }) } else { 'облако' }
        $ageTxt = if ($null -ne $age) { Format-Age $age } else { 'возраст ?' }
        return [pscustomobject]@{ Text = ($name + ' · ' + $ageTxt); Color = $script:CLR.dim2; Fresh = $true; Source = $src }
    }
    if ($src -eq 'none') {
        # source=none: свежих данных нет. Но если плата когда-то получала
        # значение (local/cloud с известным value), честно пишем «устарело».
        $cands = New-Object System.Collections.ArrayList
        foreach ($pair in @(@((Get-MeasureField $mm.Node @('local')), 'local'), @((Get-MeasureField $mm.Node @('cloud')), 'cloud'))) {
            $c = $pair[0]
            if ($null -eq $c) { continue }
            if ($null -eq (Get-MeasureField $c @('value'))) { continue }
            $ca = Get-MeasureField $c @('ageSec')
            $sec = 0
            if ($null -ne $ca) { try { $sec = [int]$ca } catch { $sec = 0 } }
            [void]$cands.Add([pscustomobject]@{ Age = $sec })
        }
        if ($cands.Count -gt 0) {
            $best = ($cands | Sort-Object Age | Select-Object -First 1)
            return [pscustomobject]@{ Text = ('устарело · ' + (Format-Age $best.Age)); Color = $script:CLR.warn; Fresh = $false; Source = 'stale' }
        }
        return [pscustomobject]@{ Text = 'нет свежих данных'; Color = $script:CLR.warn; Fresh = $false; Source = 'none' }
    }
    return [pscustomobject]@{ Text = 'источник не определён'; Color = $script:CLR.warn; Fresh = $false; Source = $src }
}

function Apply-MeasureStamps {
    # Третья строка карточек: вместо «получено HH:mm» — «источник · возраст».
    # Если /measure молчит, остаются прежние подписи по /data.
    if ($null -eq $script:measure) { return }
    foreach ($key in @('v', 't')) {
        $c = $script:cards[$key]
        if ($null -eq $c) { continue }
        $s = Format-MeasureSource $script:measure $key
        if ($null -ne $s) { Set-Lbl $c.When $s.Text; $c.When.ForeColor = C $s.Color }
    }
    $c = $script:cards['p']
    if ($null -ne $c) {
        $sp = Format-MeasureSource $script:measure 'p' -Short
        $si = Format-MeasureSource $script:measure 'i' -Short
        $parts = @()
        $bad = $false
        if ($null -ne $sp) { $parts += ('P ' + $sp.Text); if (-not $sp.Fresh) { $bad = $true } }
        if ($null -ne $si) { $parts += ('I ' + $si.Text); if (-not $si.Fresh) { $bad = $true } }
        if ($parts.Count -gt 0) {
            Set-Lbl $c.When ($parts -join ' · ')
            $c.When.ForeColor = C $(if ($bad) { $script:CLR.warn } else { $script:CLR.dim2 })
        }
    }
}

# Неблокирующий опрос /measure: не чаще 5 с, answer забираем на следующем тике.
# При 503/таймауте прежний $script:measure НЕ затираем — меняется только
# $script:measureErr («нет ответа /measure»), значения остаются последними.
function Pump-Measure {
    if ($null -eq $script:http) { return }
    if ($null -ne $script:measTask) {
        if (-not $script:measTask.IsCompleted) { return }
        $task = $script:measTask
        $script:measTask = $null
        $script:measTotal++
        $script:measMs = [int](((Get-Date) - $script:measStartAt).TotalMilliseconds)
        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {
            try {
                $o = $task.Result | ConvertFrom-Json
                if ($null -ne $o -and $null -ne (Get-MeasureField $o @('voltage'))) {
                    $script:measure = $o
                    $script:measureAt = Get-Date
                    $script:measureErr = ''
                    $script:measureBad = 0
                } else {
                    $script:measureErr = 'ответ /measure не разобран'
                }
            } catch {
                $script:measureErr = 'ответ /measure не разобран'
            }
        } else {
            $script:measureBad++
            $script:measErrN++
            $err = Get-TaskError $task
            $code = Get-HttpCode $err
            $script:measureErr = if ($code) { 'нет ответа /measure (HTTP ' + $code + ')' } else { 'нет ответа /measure' }
            if ($script:measLogSeen -eq '') {
                $script:measLogSeen = $script:measureErr
                Add-Log ('P0.1: ' + $script:measureErr + ' — показаны прежние значения') 'warn'
            }
        }
        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) { $script:measLogSeen = '' }
        return
    }
    if (((Get-Date) - $script:measAt).TotalSeconds -lt 5) { return }
    $script:measAt = Get-Date
    $script:measStartAt = Get-Date
    try {
        $script:measTask = $script:http.GetStringAsync("$($script:cfg.board)/measure")
    } catch {
        $script:measTask = $null
        $script:measureBad++
        $script:measErrN++
        $script:measureErr = 'нет ответа /measure'
    }
}

# Компактная панель «Состояние и качество данных» для вкладки «Терминал»:
# связь с платой, Wi-Fi rssi, реле, облако (cOk/cFail/cAge/cWhy), время
# (clockOk), SD (/storage: свободно, writeFail, возраст записи, archiveError)
# и свежесть четырёх показателей. LAN и WAN — раздельно.
function Convert-TermStateLines {
    $out = @()
    $d = $script:board
    $warn = 'warn'; $ok = 'ok'; $dim = 'dim'; $err = 'err'

    # --- связь с платой (LAN): HTTP /data, Wi-Fi rssi, реле
    if ($null -ne $d) {
        $age = [int]((Get-Date) - $script:boardAt).TotalSeconds
        if ($age -lt 0) { $age = 0 }
        $line = 'LAN: плата отвечает · ответ ' + $age + ' с'
        $rssi = Field $d @('rssi')
        if ($null -ne $rssi) { $line += ' · Wi-Fi ' + [int]$rssi + ' dBm' }
        $relay = Field $d @('r')
        if ($null -ne $relay) { $line += ' · реле ' + $(if ([bool]$relay) { 'ВКЛ' } else { 'ВЫКЛ' }) }
        $out += @{ text = $line; color = $(if ($age -le 12) { $ok } else { $warn }) }
    } else {
        $why = if ($script:boardErr) { ' · ' + $script:boardErr } else { '' }
        $out += @{ text = ('LAN: плата не отвечает' + $why + ' — нет ответа /data'); color = $err }
    }

    # --- WAN: интернет с платы (net) и облако — отдельно от LAN
    $cTry = 0; $cOk = 0; $cFail = 0; $cWhy = 0; $cAge = $null; $net = $null
    if ($null -ne $d) {
        $net = Field $d @('net')
        $cTry = [int](Field $d @('cTry') 0); $cOk = [int](Field $d @('cOk') 0); $cFail = [int](Field $d @('cFail') 0)
        $cWhy = [int](Field $d @('cWhy') 0); $cAge = Field $d @('cAge')
    }
    $wan = 'WAN: '
    if ($null -eq $d) { $wan += 'неизвестно — нет ответа платы' }
    elseif ($null -ne $net -and -not [bool]$net) { $wan += 'интернета с платы нет (net=false)' }
    else { $wan += 'интернет есть' }
    if ($null -ne $d) {
        $wan += ' · облако: cTry ' + $cTry + ' · cOk ' + $cOk + ' · cFail ' + $cFail + ' · cWhy ' + $cWhy + ' (' + (Get-CloudWhyRu $cWhy) + ')'
        if ($cTry -eq 0) { $wan += ' · попыток ещё не было' }
        elseif ($null -ne $cAge) { $wan += ' · последняя попытка ' + (Format-TermAge ([int]$cAge)) }
    }
    $colWan = $ok
    if ($null -eq $d) { $colWan = $err }
    elseif ($cFail -gt 0 -or $cWhy -ne 0 -or ($null -ne $net -and -not [bool]$net)) { $colWan = $warn }
    $out += @{ text = $wan; color = $colWan }

    # --- время: clockOk из /measure, часы платы из /data.now
    $nowTxt = if ($null -ne $d) { [string](Field $d @('now') '—') } else { '—' }
    if ($null -ne $script:measure) {
        $cok = Get-MeasureField $script:measure @('clockOk')
        if ($null -ne $cok -and [bool]$cok) {
            $out += @{ text = ('ВРЕМЯ: clockOk=да · часы платы ' + $nowTxt + ' · время синхронизировано (NTP)'); color = $ok }
        } else {
            $out += @{ text = ('ВРЕМЯ: clockOk=НЕТ · часы платы ' + $nowTxt + ' — время не синхронизировано, история по часам ненадёжна'); color = $err }
        }
    } else {
        $out += @{ text = ('ВРЕМЯ: нет ответа /measure — clockOk неизвестен · часы платы ' + $nowTxt); color = $warn }
    }

    # --- SD: /storage — свободно, writeFail, возраст записи, archiveError
    $st = $null
    $rawStorage = [string]$script:termAuxRaw['storage']
    if ($rawStorage) { try { $st = $rawStorage | ConvertFrom-Json } catch { $st = $null } }
    if ($null -ne $st -and [string](Field $st @('ok') '') -ne 'False') {
        $freeB = Field $st @('freeBytes'); $totalB = Field $st @('totalBytes')
        $wOk = Field $st @('writeOk'); $wFail = Field $st @('writeFail'); $wRun = Field $st @('writeFailRun')
        $wAge = Field $st @('lastWriteAgeSec'); $fAge = Field $st @('lastFailAgeSec')
        $wErr = Field $st @('lastWriteErr'); $wMsg = [string](Field $st @('lastWriteMsg') '')
        $ae = ([string](Field $st @('archiveError') 'False') -eq 'True')
        $low = ([string](Field $st @('lowSpace') 'False') -eq 'True')
        $line = 'SD: свободно ' + (Format-TermSize $freeB)
        if ($null -ne $totalB) { $line += ' из ' + (Format-TermSize $totalB) }
        if ($null -ne $wOk) { $line += ' · записей ok ' + [int]$wOk }
        $line += ' · writeFail ' + $(if ($null -ne $wFail) { [int]$wFail } else { '—' })
        if ($null -ne $wRun) { $line += ' (подряд ' + [int]$wRun + ')' }
        if ($null -ne $wAge) { $line += ' · последняя запись ' + (Format-TermAge ([int]$wAge)) }
        elseif ($null -ne $wFail -and [int]$wFail -gt 0 -and $null -ne $fAge) { $line += ' · последний сбой ' + (Format-TermAge ([int]$fAge)) }
        $line += ' · archiveError ' + $(if ($ae) { 'ДА' } else { 'нет' })
        if ($null -ne $wErr -and [int]$wErr -ne 0) { $line += ' · lastWriteErr ' + [int]$wErr }
        if ($wMsg) { $line += ' («' + $wMsg + '»)' }
        $colSd = $ok
        if ($ae -or $low -or ($null -ne $wFail -and [int]$wFail -gt 0) -or ($null -ne $wRun -and [int]$wRun -ge 2)) { $colSd = $warn }
        $out += @{ text = $line; color = $colSd }
    } elseif ($rawStorage) {
        $out += @{ text = ('SD: /storage не дал данных: ' + $rawStorage.Trim()); color = $err }
    } else {
        $out += @{ text = 'SD: ждём ответ /storage'; color = $dim }
    }

    # --- свежесть четырёх показателей: V, P, I, T
    if ($null -eq $script:measure) {
        $out += @{ text = 'СВЕЖЕСТЬ: нет ответа /measure — источник и возраст V/P/I/T неизвестны'; color = $warn }
    } else {
        $parts = @(); $allFresh = $true
        foreach ($nk in @(@('v', 'V'), @('p', 'P'), @('i', 'I'), @('t', 'T'))) {
            $s = Format-MeasureSource $script:measure $nk[0]
            if ($null -eq $s) { $parts += ($nk[1] + ' нет измерений'); $allFresh = $false; continue }
            $parts += ($nk[1] + ' ' + $s.Text)
            if (-not $s.Fresh) { $allFresh = $false }
        }
        $out += @{ text = ('СВЕЖЕСТЬ: ' + ($parts -join ' · ')); color = $(if ($allFresh) { $ok } else { $warn }) }
    }

    # --- состояние самого опроса /measure
    if ($script:measureErr) {
        $tail = ''
        if ($script:measureAt -gt [datetime]::MinValue) {
            $tail = ' — показаны прежние значения, полученные ' + (Format-TermAge ([int]((Get-Date) - $script:measureAt).TotalSeconds))
        }
        $out += @{ text = ('ИЗМЕРЕНИЯ: ' + $script:measureErr + $tail); color = $warn }
    } elseif ($script:measTotal -le 0) {
        $out += @{ text = 'ИЗМЕРЕНИЯ: ждём первый ответ /measure (опрос не чаще 5 с)'; color = $dim }
    } else {
        $out += @{ text = ('ИЗМЕРЕНИЯ: /measure отвечает · ответ ' + $script:measMs + ' мс · опрос не чаще 5 с'); color = $dim }
    }
    return $out
}


function Pump-Board {

    if ($null -eq $script:http) { return }



    if ($null -ne $script:pollTask) {

        if (-not $script:pollTask.IsCompleted) { return }

        $task = $script:pollTask

        $script:pollTask = $null

        $script:totalPolls++

        $script:pollMs = [int](((Get-Date) - $script:pollStartAt).TotalMilliseconds)

        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {

            try {

                $o = $task.Result | ConvertFrom-Json

                if ($null -ne $o -and $null -ne (Field $o @('up')) -and $null -ne (Field $o @('v'))) {

                    $script:board = $o

                    $script:boardAt = Get-Date

                    $script:boardErr = ''

                    $script:boardBad = 0

                    # /data.fw приоритетнее страницы платы

                    if ($null -ne (Field $o @('fw'))) {

                        $script:fwVal = [string](Field $o @('fw'))

                        $script:fwAt = Get-Date

                    }

                } else {

                    $script:boardErr = 'в ответе нет напряжения'

                }

            } catch {

                $script:boardErr = 'ответ платы не разобран'

            }

        } else {

            $script:boardBad++

            $script:pollErr++

            $err = Get-TaskError $task

            $code = Get-HttpCode $err

            if ($code) { $script:boardErr = 'HTTP ' + $code } else { $script:boardErr = 'плата не отвечает' }

            if ($err -ne $script:httpErrSeen) {

                $script:httpErrSeen = $err

                $msg = 'HTTP ошибка'

                if ($code) { $msg += ' ' + $code }

                $msg += ': ' + $err

                Add-Log $msg 'err'

            }

        }

        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) { $script:httpErrSeen = '' }

        return

    }



    if (((Get-Date) - $script:pollAt).TotalSeconds -lt 3) { return }   # 3 с: плата успевает, графики не страдают

    $script:pollAt = Get-Date

    $script:pollStartAt = Get-Date

    try {

        $script:pollTask = $script:http.GetStringAsync("$($script:cfg.board)/data")

    } catch {

        $script:pollTask = $null

        $script:boardBad++

        $script:pollErr++

        $script:boardErr = 'запрос не отправлен'

        Add-Log ('HTTP ошибка: запрос не отправлен — ' + $_.Exception.Message) 'err'

    }

}



# --------------------------------------------- версия прошивки по HTTP (страница /)

# В /data поля fw нет, но страница платы отдаёт «CYD-4.4». Кэшируем: меняется

# только при прошивке. Запрос асинхронный, чтобы не подвешивать интерфейс.

function Pump-Firmware {

    if ($null -eq $script:http) { return }

    if ($null -ne $script:fwTask) {

        if (-not $script:fwTask.IsCompleted) { return }

        $task = $script:fwTask

        $script:fwTask = $null

        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {

            try {

                $m = [regex]::Match([string]$task.Result, 'CYD-\d+\.\d+(?:\.\d+)?')

                if ($m.Success) {

                    if ($script:fwVal -ne $m.Value) { Write-AppLog ('прошивка по HTTP: ' + $m.Value) }

                    $script:fwVal = $m.Value

                    $script:fwAt = Get-Date

                }

            } catch { }

        }

        return

    }

    if ($script:fwVal -and ((Get-Date) - $script:fwAt).TotalMinutes -lt 30) { return }

    if (-not $script:fwVal -and ((Get-Date) - $script:fwAt).TotalSeconds -lt 10) { return }

    $script:fwAt = Get-Date

    try { $script:fwTask = $script:http.GetStringAsync("$($script:cfg.board)/") }

    catch { $script:fwTask = $null }

}



# --------------------------------------------------------- список дней платы

function Pump-Days {

    if ($null -eq $script:httpSlow) { return }



    if ($null -ne $script:daysTask) {

        if (-not $script:daysTask.IsCompleted) { return }

        $task = $script:daysTask

        $script:daysTask = $null

        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {

            try {

                $o = $task.Result | ConvertFrom-Json

                if($o.pending){$script:daysAt=(Get-Date).AddSeconds(-119);return}
                if($o.trunc -or $null -eq $o.PSObject.Properties['days']){throw 'Incomplete days response'}
                $list = @($o.days)

                $gaps = @($o.gaps)

                $script:days = @($list | Where-Object { "$_" -match '^\d{4}-\d{2}-\d{2}$' })

                $script:daysGaps = @{}

                for ($i = 0; $i -lt $script:days.Count; $i++) {

                    if ($i -lt $gaps.Count) {

                        $g = @($gaps[$i])

                        if ($g.Count -ge 2) {

                            $script:daysGaps[$script:days[$i]] = @([int]$g[0], [int]$g[1])

                        }

                    }

                }

            } catch { $script:daysAt=(Get-Date).AddSeconds(-115) }

        } else {$script:daysAt=(Get-Date).AddSeconds(-115)}

        return

    }



    if (((Get-Date) - $script:daysAt).TotalSeconds -lt 120) { return }

    $script:daysAt = Get-Date

    try { $script:daysTask = $script:httpSlow.GetStringAsync("$($script:cfg.board)/days") }

    catch { $script:daysTask = $null }

}



# ------------------------------------------------------- история за один день

# ------------------------------------------------ состояние выбранного часа

function Get-HistSelection { return $script:histSelect }

function Sync-HistChartSelection {
    # График «История» рисуется через Update-MetricHistory и берёт час из Tag.
    # Источник истины — $script:histSelect, сюда только зеркалим.
    if ($null -eq $chartHist) { return }
    $sel = $script:histSelect
    if ($null -eq $sel) { return }
    try { $chartHist.AccessibleName = [string]$sel.Date } catch { }
    try {
        if ($null -ne $sel.Hour) { $chartHist.Tag = [int]$sel.Hour } else { $chartHist.Tag = $null }
    } catch { }
}

function Select-HistTableHour($hour) {
    try {
        if ($null -eq $gridHist -or $gridHist.IsDisposed) { return }
        $gridHist.ClearSelection()
        if ($null -ne $hour) {
            $h = [int]$hour
            if ($h -ge 0 -and $h -le 23 -and $h -lt $gridHist.Rows.Count) { $gridHist.Rows[$h].Selected = $true }
        }
    } catch { }
}

function Reset-HistSelection([string]$date = '') {
    if (-not $date) { $date = [string]$script:histSel }
    if ($date -notmatch '^\d{4}-\d{2}-\d{2}$') { $date = (Get-Date).ToString('yyyy-MM-dd') }
    $script:histSel = $date
    $script:histSelect.Date = $date
    $script:histSelect.Hour = $null
    $script:histSelect.RequestId = [int]$script:histSelect.RequestId + 1
    Sync-HistChartSelection
}

function Cancel-HistRequests([string]$keepDate = '') {
    # Смена даты или адреса платы: старый запрос /history больше не нужен.
    # Отменяем его (закрываем сокет), очередь чистим; ответ, пришедший позже,
    # отбрасывается по $script:histEpoch.
    if ($null -ne $script:histTask) {
        if ((-not $keepDate) -or ([string]$script:histTaskDate -ne $keepDate)) {
            $script:histEpoch = [int]$script:histEpoch + 1
            try { if ($null -ne $script:histCts) { $script:histCts.Cancel() } } catch { }
            try { if ($null -ne $script:histCts) { $script:histCts.Dispose() } } catch { }
            $script:histCts = $null
            $script:histTask = $null
            $script:histTaskDate = ''
        }
    }
    if (-not $script:histRequests) { $script:histRequests = @{} }
    foreach ($k in @($script:histRequests.Keys)) { if ($k -ne $keepDate) { $script:histRequests.Remove($k) } }
    if ($keepDate) {
        if ($script:histPending -and $script:histPending -ne $keepDate) { $script:histPending = $null }
    } else {
        $script:histPending = $null
    }
}

function Set-HistDay([string]$date) {
    if ($date -notmatch '^\d{4}-\d{2}-\d{2}$') { return $script:histSelect }
    $changed = ([string]$script:histSelect.Date -ne $date) -or ([string]$script:histSel -ne $date)
    $script:histSel = $date
    $script:histSelect.Date = $date
    if ($changed) {
        $script:histSelect.Hour = $null
        $script:histSelect.RequestId = [int]$script:histSelect.RequestId + 1
        Cancel-HistRequests $date
    }
    Sync-HistChartSelection
    Request-Day $date
    return $script:histSelect
}

function Set-HistSelectHour([string]$date, $hour, [string]$metric = '') {
    if ($date -notmatch '^\d{4}-\d{2}-\d{2}$') { $date = [string]$script:histSel }
    if ($date -notmatch '^\d{4}-\d{2}-\d{2}$') { $date = (Get-Date).ToString('yyyy-MM-dd') }
    if (([string]$script:histSel -ne $date) -or ([string]$script:histSelect.Date -ne $date)) {
        Set-HistDay $date
    } else {
        Request-Day $date
    }
    if (-not $metric) { $metric = [string]$script:histSelect.Metric }
    if (-not $metric) { $metric = 'p' }
    if ($null -ne $hour) {
        $h = [int]$hour
        if ($h -lt 0) { $h = 0 }
        if ($h -gt 23) { $h = 23 }
        $hour = $h
    }
    $script:histSelect.Metric = $metric
    $script:histSelect.Hour = $hour
    $script:histSelect.RequestId = [int]$script:histSelect.RequestId + 1
    Sync-HistChartSelection
    try { if ($null -ne $chartHist) { Update-MetricHistory $chartHist $date } } catch { }
    Select-HistTableHour $hour
    return $script:histSelect
}

function Request-Day([string]$date) {
    if ($date -notmatch '^\d{4}-\d{2}-\d{2}$') { return }
    # Свежесть кэша: сегодняшний день меняется — 2 минуты; прошедший день
    # уже неподвижен — 10 минут, иначе каждый вход снова ждёт чтения CSV.
    $freshSec = 120
    if ($date -ne (Get-Date).ToString('yyyy-MM-dd')) { $freshSec = 600 }
    if ($script:histCache.ContainsKey($date)) {
        $at=$script:histAt[$date]
        if ($null -ne $at -and ((Get-Date)-$at).TotalSeconds -lt $freshSec) { return }
    }
    if (-not $script:histRequests) { $script:histRequests=@{} }
    if (-not $script:histFirstAt) { $script:histFirstAt=@{} }
    if ($script:histTaskDate -eq $date) { $script:histPending=$date; return }
    if (-not $script:histRequests.ContainsKey($date)) { $script:histRequests[$date]=Get-Date }
    if (-not $script:histFirstAt.ContainsKey($date)) { $script:histFirstAt[$date]=Get-Date }
    # Ответа ещё нет — вкладка пишет «загрузка истории…», а не «нет данных».
    if (-not $script:histCache.ContainsKey($date)) { $script:histPending=$date }
}

function Test-HistLoading([string]$date) {
    # Идёт ли сейчас загрузка именно этого дня (для надписи «загрузка истории…»).
    if (-not $date) { return $false }
    if ($script:histTaskDate -eq $date) { return $true }
    if ($script:histPending -eq $date) { return $true }
    if ($script:histRequests -and $script:histRequests.ContainsKey($date)) { return $true }
    if ($script:histFirstAt -and $script:histFirstAt.ContainsKey($date)) { return $true }
    return $false
}



function Fill-DayGaps($from, $to) {
    # Плата умеет добирать пропущенные строки из облака Tuya: /gapfill?run=1.
    # Запускаем и не ждём: заполнение идёт в фоне на самой плате, история
    # перечитается обычным опросом.
    $ip = [string]$script:cfg.boardIp
    if (-not $ip -or -not $from) { return }
    if (-not $to) { $to = $from }
    try {
        $r = Invoke-RestMethod ('http://' + $ip + '/gapfill?run=1&from=' + $from + '&to=' + $to) -TimeoutSec 12 -Headers (Get-ControlHeaders)
        if ($r.ok) {
            Add-Log ('заполнение пропусков из облака за ' + $from + '…' + $to + ': запущено') 'info'
        } else {
            Add-Log 'заполнение пропусков: плата не приняла запрос' 'warn'
        }
    } catch {
        Add-Log ('заполнение пропусков: ' + $_.Exception.Message) 'warn'
    }
}

# --- ключ управления платой (X-Dom-Key) и сбор всех пропусков -------------
$script:boardToken   = ''
$script:boardTokenAt = [datetime]::MinValue

function Get-BoardControlToken {
    # Управление (настройки, сбор пропусков) плата принимает по заголовку
    # X-Dom-Key. Ключ лежит рядом с исходником платы, в control-secret.h:
    #   #define DOM_CONTROL_TOKEN "..."
    # Если файла нет (программа на другом компьютере) — работаем как раньше:
    # ключ из DOM_CONTROL_KEY или %APPDATA%\DomMonitor\control.key.
    if ($script:boardToken -and ((Get-Date) - $script:boardTokenAt).TotalSeconds -lt 60) { return $script:boardToken }
    $tok = ''
    $candidates = @('C:\esp32\cydv4\control-secret.h')
    try { $candidates += [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\cydv4\control-secret.h')) } catch { }
    foreach ($p in $candidates) {
        try {
            if (-not $p -or -not (Test-Path -LiteralPath $p)) { continue }
            $txt = [System.IO.File]::ReadAllText($p)
            $m = [regex]::Match($txt, 'DOM_CONTROL_TOKEN\s+"?([0-9a-fA-F]{32,128})"?')
            if ($m.Success) { $tok = $m.Groups[1].Value; break }
        } catch { }
    }
    if (-not $tok) { try { $tok = [string](Get-ControlKey) } catch { $tok = '' } }
    $script:boardToken = $tok
    $script:boardTokenAt = Get-Date
    return $tok
}

function Get-BoardHeaders {
    $tok = Get-BoardControlToken
    if ($tok) { return @{ 'X-Dom-Key' = $tok } }
    try { return (Get-ControlHeaders) } catch { return @{} }
}

function Get-GapFillPhaseName($ph) {
    switch ([int]$ph) {
        0 { return 'ожидание' }
        1 { return 'поиск пропусков' }
        2 { return 'запрос облака' }
        3 { return 'запись в файл' }
        default { return ('этап ' + [string]$ph) }
    }
}

function Start-GapFillAll([string]$ip) {
    # GET /gapfill?run=1 — плата сама ищет пропуски (2 суток за проход) и
    # добирает их из облака Tuya; возвращается сразу, работа идёт в фоне.
    if (-not $ip) { $ip = [string]$script:cfg.boardIp }
    return Invoke-RestMethod ('http://' + $ip + '/gapfill?run=1') -TimeoutSec 20 -Headers (Get-BoardHeaders)
}

function Get-GapFillStatus([string]$ip) {
    if (-not $ip) { $ip = [string]$script:cfg.boardIp }
    return Invoke-RestMethod ('http://' + $ip + '/gapfill/status') -TimeoutSec 10 -Headers (Get-BoardHeaders)
}

function Format-GapFillStatus($st, [int]$elapsedSec = 0) {
    if ($null -eq $st) { return 'заполнение: нет ответа от платы' }
    $txt = ('заполнение: ' + (Get-GapFillPhaseName (Field $st @('phase') 0)) +
            ' · пропусков ' + [string](Field $st @('gaps') 0) +
            ' · собрано ' + [string](Field $st @('ins') 0) + ' / ' + [string](Field $st @('insTotal') 0) +
            ' · вставлено ' + [string](Field $st @('insLast') 0))
    $msg = [string](Field $st @('msg') '')
    if ($msg) { $txt += ' · ' + $msg }
    if ([int](Field $st @('sdFault') 0) -eq 1) { $txt += ' · отказ карты' }
    elseif ([int](Field $st @('sdErr') 0) -ne 0) { $txt += ' · ошибка карты ' + [string](Field $st @('sdErr') 0) }
    if ($elapsedSec -gt 0) { $txt += ' · ' + [int]$elapsedSec + ' с' }
    return $txt
}

function Stop-GapFillPoll([string]$note = '') {
    try { if ($null -ne $script:gapPollTimer) { $script:gapPollTimer.Stop() } } catch { }
    try { if ($null -ne $script:gapCts) { $script:gapCts.Cancel(); $script:gapCts.Dispose() } } catch { }
    $script:gapCts = $null
    $script:gapTask = $null
    if ($null -ne $script:gapUi) {
        $script:gapUi.busy = $false
        try { if ($null -ne $script:gapUi.btn) { $script:gapUi.btn.Enabled = $true } } catch { }
        if ($note) { try { $script:gapUi.lbl.Text = $note } catch { } }
    }
}

function Start-GapFillStatusRequest {
    # Асинхронный GET /gapfill/status с ключом управления в заголовке.
    # Окно не замирает: ответ забирает Step-GapFillPoll на следующем тике.
    $ui = $script:gapUi
    if ($null -eq $ui) { return $false }
    if ($null -eq $script:httpSlow) { return $false }
    $ip = [string]$ui.ip
    if (-not $ip) { return $false }
    try {
        if ($null -ne $script:gapCts) { $script:gapCts.Dispose() }
        $script:gapCts = New-Object System.Threading.CancellationTokenSource
        $script:gapCts.CancelAfter([TimeSpan]::FromSeconds(12))
        $req = New-Object System.Net.Http.HttpRequestMessage -ArgumentList ([System.Net.Http.HttpMethod]::Get), ('http://' + $ip + '/gapfill/status')
        $tok = Get-BoardControlToken
        if ($tok) { $null = $req.Headers.TryAddWithoutValidation('X-Dom-Key', $tok) }
        $script:gapTask = $script:httpSlow.SendAsync($req, [System.Net.Http.HttpCompletionOption]::ResponseContentRead, $script:gapCts.Token)
        return $true
    } catch {
        $script:gapTask = $null
        try { if ($null -ne $script:gapCts) { $script:gapCts.Dispose() } } catch { }
        $script:gapCts = $null
        return $false
    }
}

function Step-GapFillPoll {
    # Тик таймера диалога периода (2 с). /gapfill/status теперь асинхронный:
    # плата может думать 10 с, но окно не замирает.
    $ui = $script:gapUi
    if ($null -eq $ui) { try { $script:gapPollTimer.Stop() } catch { } ; Stop-GapFillPoll; return }
    $elapsed = if ($null -ne $ui.sw) { [int]$ui.sw.Elapsed.TotalSeconds } else { 0 }
    if ($elapsed -gt 300) {
        Stop-GapFillPoll 'опрос прекращён: прошло 5 минут, плата ещё считает — откройте период позже'
        return
    }
    if ($null -eq $script:gapTask) {
        if (-not (Start-GapFillStatusRequest)) {
            if ($null -ne $ui.lbl) {
                try { $ui.lbl.ForeColor = C $script:CLR.warn; $ui.lbl.Text = ('заполнение: нет ответа от платы — запрос не отправлен · ' + $elapsed + ' с') } catch { }
            }
        }
        return
    }
    if (-not $script:gapTask.IsCompleted) { return }
    $task = $script:gapTask
    $script:gapTask = $null
    try { if ($null -ne $script:gapCts) { $script:gapCts.Dispose() } } catch { }
    $script:gapCts = $null
    $st = $null
    try {
        if ($task.IsCanceled) { throw 'плата не ответила за 12 с' }
        if ($task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw (Get-TaskError $task) }
        $resp = $task.Result
        if ($null -eq $resp -or -not $resp.IsSuccessStatusCode) { throw 'плата не ответила' }
        $st = ($resp.Content.ReadAsStringAsync().Result) | ConvertFrom-Json
    } catch {
        try { $ui.lbl.ForeColor = C $script:CLR.warn; $ui.lbl.Text = ('заполнение: нет ответа от платы — ' + $_.Exception.Message + ' · ' + $elapsed + ' с') } catch { }
        if ($elapsed -gt 300) { Stop-GapFillPoll 'опрос прекращён: прошло 5 минут' }
        return
    }
    try {
        $ui.lbl.Text = Format-GapFillStatus $st $elapsed
        $active = [int](Field $st @('active') 0)
        if ($active -eq 1) { $ui.lbl.ForeColor = C $script:CLR.dim2 }
        elseif ([int](Field $st @('insLast') 0) -gt 0) { $ui.lbl.ForeColor = C $script:CLR.ok }
        else { $ui.lbl.ForeColor = C $script:CLR.warn }
    } catch { }
    if ([int](Field $st @('active') 0) -eq 0) {
        $ins = [int](Field $st @('insLast') 0)
        $msg = [string](Field $st @('msg') '')
        $final = 'заполнение завершено: вставлено строк ' + $ins
        if ($msg) { $final += ' · ' + $msg }
        Add-Log $final $(if ($ins -gt 0) { 'ok' } else { 'warn' })
        Stop-GapFillPoll $final
        try { $null = Update-RangeGrid $ui.grid $ui.sum $ui.ip $ui.d1.Value.ToString('yyyy-MM-dd') $ui.d2.Value.ToString('yyyy-MM-dd') } catch { }
    }
}

function Pump-History {
    # /history читает CSV на плате (12-65 с) — запрос асинхронный, окно не замирает.
    # Ответ забираем на тике On-Tick (UI-поток); устаревший ответ (сменились дата
    # или адрес платы) отбрасываем по $script:histEpoch.
    if ($null -eq $script:httpSlow) { return }
    if (-not $script:histRequests) { $script:histRequests=@{} }
    if (-not $script:histFirstAt) { $script:histFirstAt=@{} }
    if ($null -ne $script:histTask) {
        if (-not $script:histTask.IsCompleted) { return }
        $task=$script:histTask; $date=[string]$script:histTaskDate
        $epoch=[int]$script:histTaskEpoch
        $script:histTask=$null; $script:histTaskDate=''
        try { if ($null -ne $script:histCts) { $script:histCts.Dispose() } } catch { }
        $script:histCts=$null
        if ($epoch -ne [int]$script:histEpoch) { return }   # ответ устарел: дата/IP сменились
        try {
            if ($task.IsCanceled) {
                if ($script:histPending -eq $date) { $script:histPending=$null }
                return
            }
            if ($task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw ('Плата не отдала историю: ' + (Get-TaskError $task)) }
            $resp=$task.Result
            if ($null -eq $resp -or -not $resp.IsSuccessStatusCode) {
                $code = if ($null -ne $resp) { [int]$resp.StatusCode } else { 0 }
                if ($code -gt 0) { throw ('Плата отдала HTTP ' + $code) }
                throw 'Плата не отдала историю'
            }
            $o=$resp.Content.ReadAsStringAsync().Result | ConvertFrom-Json
            if ($o.pending) {
                if ($script:histFirstAt.ContainsKey($date) -and ((Get-Date)-$script:histFirstAt[$date]).TotalSeconds -gt 240) { throw 'История не готова за 240 секунд' }
                $script:histRequests[$date]=(Get-Date).AddMilliseconds(700)
                return
            }
            if ($o.trunc) { throw 'Получена неполная история; повторяем загрузку' }
            if ($null -eq $o.PSObject.Properties['hours'] -or [string]$o.date -ne $date) { throw 'Неверный ответ истории' }
            $script:histCache[$date]=@($o.hours); $script:histAt[$date]=Get-Date
            $script:histFirstAt.Remove($date)
            if ($script:histPending -eq $date) { $script:histPending=$null }
            if ($script:histErrDate -eq $date) { $script:histErr='' }
        } catch {
            $script:histErr=$_.Exception.Message; $script:histErrDate=$date
            $script:histRequests[$date]=(Get-Date).AddSeconds(5)
            $script:histFirstAt.Remove($date)
        }
        if ($null -ne $script:refreshHistory) { & $script:refreshHistory }
        return
    }
    $due=@($script:histRequests.Keys | Where-Object { $script:histRequests[$_] -le (Get-Date) } | Sort-Object { $script:histRequests[$_] })
    if (-not $due.Count) { return }
    $date=[string]$due[0]; $script:histRequests.Remove($date)
    if (-not $script:histFirstAt.ContainsKey($date)) { $script:histFirstAt[$date]=Get-Date }
    $script:histTaskDate=$date
    try {
        $script:histCts = New-Object System.Threading.CancellationTokenSource
        $script:histTaskEpoch = [int]$script:histEpoch
        $script:histTask = $script:httpSlow.GetAsync("$($script:cfg.board)/history?date=$date", $script:histCts.Token)
    } catch {
        $script:histTask=$null; $script:histTaskDate=''
        try { if ($null -ne $script:histCts) { $script:histCts.Dispose() } } catch { }
        $script:histCts=$null
        $script:histErr='Запрос истории не отправлен'; $script:histErrDate=$date
        $script:histRequests[$date]=(Get-Date).AddSeconds(5)
    }
}
# ============================================================================

#  SERIAL — РЕЗЕРВНЫЙ КАНАЛ (COM5)

# ============================================================================

# «Плата уже настроена» = в settings.json есть setupDone=true и адрес платы.

# Это единственный признак: при нём мастер сам никогда не открывается,

# остаётся только кнопка «Мастер настройки» на вкладке «Устройство».

function Get-CfgBoardIp {

    $ip = [string]$script:cfg.boardIp

    if ($ip -notmatch '^\d{1,3}(\.\d{1,3}){3}$') {

        $m = [regex]::Match([string]$script:cfg.board, '^https?://(\d{1,3}(\.\d{1,3}){3})')

        if ($m.Success) { $ip = $m.Groups[1].Value }

    }

    if ($ip -match '^\d{1,3}(\.\d{1,3}){3}$') { return $ip }

    return ''

}



function Test-CfgReady {

    if ($script:cfg.setupDone -ne $true) { return $false }

    return ((Get-CfgBoardIp) -ne '')

}



function Get-PortList {

    $usb = @()

    try {

        $rows = Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%COM%'" -ErrorAction Stop

        foreach ($r in $rows) {

            $n = [string]$r.Name

            if ($n -notmatch 'USB|CH340|CP210|FTDI|Silicon') { continue }

            $m = [regex]::Match($n, 'COM\d+')

            if ($m.Success) { $usb += $m.Value }

        }

        $usb = @($usb | Sort-Object -Unique)

    } catch { }

    if ($usb.Count -gt 0) { return $usb }

    return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)

}



function Get-UsbSerialPort {

    try {

        $rows = Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%COM%'" -ErrorAction Stop

        foreach ($r in $rows) {

            $n = [string]$r.Name

            if ($n -match 'CH340|CP210|FTDI|USB-SERIAL|USB Serial') {

                $m = [regex]::Match($n, 'COM\d+')

                if ($m.Success) { return $m.Value }

            }

        }

    } catch { }

    return $null

}



function Auto-PickPort {

    $ports = @(Get-PortList)

    if ($ports.Count -eq 0) { return $false }

    $usb = Get-UsbSerialPort

    if ($usb -and $usb -ne $script:cfg.comPort) {

        $old = $script:cfg.comPort

        $script:cfg.comPort = $usb

        Save-Cfg

        Add-Log "порт платы — $usb (был $old)"

        return $true

    }

    if ($ports -notcontains $script:cfg.comPort) {

        $script:cfg.comPort = [string]$ports[0]

        Save-Cfg

        Add-Log "порт $($script:cfg.comPort) выбран автоматически" 'warn'

        return $true

    }

    return $false

}



# ------------------------------------------------------------ индикатор COM

# Один источник правды для шильдика порта в шапке: «занят монитором» /

# «свободен» / «ошибка». Раньше индикатор HTTP мог показывать «РЕЗЕРВ COM5»

# одновременно с «COM5 закрыт» — эти состояния больше не противоречат.

$script:portNames   = @()

$script:portNamesAt = [datetime]::MinValue



function Test-ComPresent([string]$name) {

    if ([string]::IsNullOrWhiteSpace($name)) { return $false }

    if (((Get-Date) - $script:portNamesAt).TotalSeconds -gt 10) {

        $script:portNamesAt = Get-Date

        try { $script:portNames = @(Get-PortList) } catch { $script:portNames = @() }

    }

    return ($script:portNames -contains $name)

}



# Можно ли открыть порт: пробуем короткое открытие/закрытие не чаще раза в 60 с.

# DTR/RTS не поднимаем — плата не перезагружается. Если порт держит другая

# программа, Windows вернёт «отказано в доступе» — это и есть «занят».

function Test-PortFree([string]$name) {

    if (((Get-Date) - $script:portTestAt).TotalSeconds -lt 60) { return $script:portFree }

    $script:portTestAt = Get-Date

    $p = $null

    try {

        $p = New-Object System.IO.Ports.SerialPort $name, $script:cfg.baud, 'None', 8, 'One'

        $p.ReadTimeout = 200

        $p.DtrEnable = $false

        $p.RtsEnable = $false

        $p.Open()

        $p.Close()

        $script:portFree = $true

    } catch {

        $msg = [string]$_.Exception.Message

        if ($msg -match 'доступ|access|занят|busy|denied|отказано') { $script:portFree = $false }

        else { $script:portFree = $null }

        try { if ($null -ne $p -and $p.IsOpen) { $p.Close() } } catch { }

    }

    return $script:portFree

}



function Update-ComBadge {
    if ($null -eq $lblCom) { return }
    $com = [string]$script:cfg.comPort
    $col = $script:CLR.dim2
    if ($script:port -and $script:port.IsOpen) {
        Set-Lbl $lblCom ($com + ' — открыт (монитор)')
        $col = $script:CLR.ok
    } elseif (-not (Test-ComPresent $com)) {
        Set-Lbl $lblCom ($com + ' — нет устройства')
        $col = $script:CLR.err
    } elseif ((Test-PortFree $com) -eq $false) {
        Set-Lbl $lblCom ($com + ' — занят другой программой')
        $col = $script:CLR.warn
    } else {
        Set-Lbl $lblCom ($com + ' — закрыт')
        $col = $script:CLR.dim2
    }
    $lblCom.ForeColor = C $col
    Set-Dot $script:dotCom $col

    if ($null -ne $script:tipMain) {
        $script:tipMain.SetToolTip($lblCom, "порт нужен для команд Пульт/Перезагрузить; данные идут по HTTP`r`nоткрыт (монитор) — порт держит эта программа`r`nзакрыт — порт свободен, работаем по HTTP`r`nзанят другой программой — порт захвачен другим ПО`r`nнет устройства — адаптер не найден в системе")
    }
}



function Connect-Serial {
    param([switch]$Force)

    if ($script:port -and $script:port.IsOpen) { return $true }

    # Если HTTP только что ответил, порт не открываем: на CH340 открытие
    # дёргает DTR и плата перезагружается. Порт нужен только как резерв.
    if (-not $Force -and $script:boardAt -and ((Get-Date) - $script:boardAt).TotalSeconds -lt 60) {
        return $false
    }

    try {

        $p = New-Object System.IO.Ports.SerialPort $script:cfg.comPort, $script:cfg.baud, 'None', 8, 'One'

        $p.ReadTimeout = 300

        # DTR/RTS не поднимаем: на CH340 это держит плату в сбросе

        $p.DtrEnable = $false

        $p.RtsEnable = $false

        $p.Open()

        $script:port = $p

        $script:rxBuf = ''

        $script:rxSeen = $false

        Update-ComBadge

        Set-Lbl $btnSerial 'Отключить'

        Add-Log "порт $($script:cfg.comPort) открыт (резервный канал)"

        return $true

    } catch {

        Update-ComBadge

        Set-Lbl $btnSerial 'Подключить'

        Add-Log ("не удалось открыть $($script:cfg.comPort): " + $_.Exception.Message) 'warn'

        return $false

    }

}



function Disconnect-Serial {

    try { if ($script:port -and $script:port.IsOpen) { $script:port.Close() } } catch { }

    $script:port = $null

    Update-ComBadge

    Set-Lbl $btnSerial 'Подключить'

}



function Send-Serial([string]$cmd) {

    if (-not ($script:port -and $script:port.IsOpen)) {

        Add-Log 'команда не отправлена: порт закрыт (нужен USB)' 'warn'

        return $false

    }

    try { $script:port.Write($cmd + "`n"); return $true }

    catch { Add-Log ('не удалось отправить команду: ' + $_.Exception.Message) 'err'; return $false }

}



function Handle-Line($line) {

    $line = "$line".Trim()

    if (!$line) { return }

    if ($script:uiReady -and $script:chkRaw.Checked) {

        $script:txtLog.AppendText('<< ' + $line + "`r`n")

    }

    if (-not $line.StartsWith('{')) { return }

    try { $obj = $line | ConvertFrom-Json } catch { return }



    $t = [string]$obj.t

    if ($t -eq 'boot' -or $t -eq 'ready' -or $t -eq 'status') {

        $script:serialStatus = $obj

        if ($t -eq 'boot')  { Add-Log ("плата загрузилась, прошивка " + $obj.fw) }

        if ($t -eq 'ready') { Add-Log ("плата готова, IP " + $obj.ip) 'ok' }

        if ($null -ne $obj.fw) { Set-Lbl $lblFwChip ([string]$obj.fw) }

        return

    }

    if ($t -eq 'data') {

        $v = [double](Field $obj @('v') 0)

        if ($null -ne (Field $obj @('vSeen')) -or $v -gt 0) {

            $script:serialData = $obj

            $script:serialAt = Get-Date

            $script:serialZero = $false

        } elseif (-not $script:serialZero) {

            $script:serialZero = $true

            Add-Log 'serial присылает напряжение 0 — данные берём только по HTTP' 'warn'

        }

    }

}




# ---------------------------------------------------------------- ui-kit: скины

# 12 токенов из docs/дизайн-токены (плата, веб и приложение — одна палитра)
$script:SkinOrder = @('light', 'dark', 'mono', 'pink', 'red', 'rainbow')

$script:SkinRu = @{
    light   = 'светлая'
    dark    = 'тёмная'
    mono    = 'ЧБ'
    pink    = 'розовая'
    red     = 'красная'
    rainbow = 'радужная'
}

$script:SkinData = @{
    light = @{ bg = '#F8FAFF'; surf = '#FFFFFF'; elev = '#EFF4FF'; text = '#0F172A'; muted = '#64748B'
               line = '#DBE4F5'; accent = '#2563EB'; temp = '#8B5CF6'; power = '#F59E0B'
               money = '#DB2777'; good = '#10B981'; bad = '#EF4444' }
    dark  = @{ bg = '#0B111B'; surf = '#141C2A'; elev = '#1C2636'; text = '#E8EEFC'; muted = '#93A4BE'
               line = '#293D54'; accent = '#22D3EE'; temp = '#F472B6'; power = '#FBBF24'
               money = '#F472B6'; good = '#34D399'; bad = '#F87171' }
    mono  = @{ bg = '#000000'; surf = '#1A1A1A'; elev = '#282828'; text = '#FFFFFF'; muted = '#A0A0A0'
               line = '#464646'; accent = '#FFFFFF'; temp = '#FFFFFF'; power = '#CDCDCD'
               money = '#FFFFFF'; good = '#FFFFFF'; bad = '#FFFFFF' }
    pink  = @{ bg = '#FFF0F6'; surf = '#FFFFFF'; elev = '#FFE4EE'; text = '#3C1428'; muted = '#965A78'
               line = '#FFCDE1'; accent = '#EC4899'; temp = '#DB2777'; power = '#A855F7'
               money = '#BE185D'; good = '#10B981'; bad = '#EF4444' }
    red   = @{ bg = '#140808'; surf = '#200C0C'; elev = '#301212'; text = '#FFE6E6'; muted = '#C89696'
               line = '#501E1E'; accent = '#EF4444'; temp = '#FB923C'; power = '#FACC15'
               money = '#F87171'; good = '#4ADE80'; bad = '#FF0000' }
    rainbow = @{ bg = '#101125'; surf = '#191E36'; elev = '#202942'; text = '#F0F3FF'; muted = '#AFB9D6'
                 line = '#343E60'; accent = '#67E8F9'; temp = '#F49AC2'; power = '#FBBF24'
                 money = '#FFE600'; good = '#00FF96'; bad = '#FF3C3C' }
}

# палитра приложения из 12 токенов: те же имена ключей, что были в $script:CLR
function Get-Skin([string]$name) {
    if ([string]::IsNullOrEmpty($name)) { $name = 'dark' }
    $name = ([string]$name).Trim().ToLower()
    if (-not $script:SkinData.ContainsKey($name)) { $name = 'dark' }
    $d = $script:SkinData[$name]
    $bg = $d.bg
    $surf = $d.surf
    $cBg = [System.Drawing.ColorTranslator]::FromHtml($bg)
    $lum = (0.2126 * $cBg.R + 0.7152 * $cBg.G + 0.0722 * $cBg.B) / 255.0
    return [ordered]@{
        bg     = $bg
        header = (MixC $surf $bg 0.42)
        nav    = (MixC $surf $bg 0.18)
        footer = (MixC $bg $surf 0.25)
        panel  = $d.elev
        card   = $surf
        card2  = $d.elev
        line   = $d.line
        text   = $d.text
        dim    = $d.muted
        dim2   = (MixC $d.muted $bg 0.55)
        accent = $d.accent
        ok     = $d.good
        warn   = $d.power
        err    = $d.bad
        lcdBg  = '#05070A'
        lcdFg  = $d.good
        temp   = $d.temp
        power  = $d.power
        money  = $d.money
        good   = $d.good
        bad    = $d.bad
        shadow = (MixC $bg '#000000' 0.17)
        btnA   = (MixC $d.accent $surf 0.78)
        btnN   = $d.elev
        btnD   = (MixC $d.bad $surf 0.80)
        btnOk  = (MixC $d.good $surf 0.74)
        skin   = $name
        dark   = ($lum -lt 0.45)
    }
}

# цвета, которые в коде заданы «руками» (до появления палитры)

$script:LegacyRole = @{
    '#05070A' = 'lcdBg'; '#0A0C10' = 'lcdBg'; '#0F1218' = 'bg'; '#12161C' = 'panel'
    '#14181E' = 'panel'; '#151B26' = 'card2'; '#171B22' = 'panel'; '#1F2530' = 'line'
    '#222836' = 'card2'; '#232A36' = 'card2'; '#232B39' = 'card2'; '#24405A' = 'btnA'
    '#252C38' = 'line'; '#2A3240' = 'btnN'; '#2A3444' = 'card2'; '#2B4A66' = 'btnA'
    '#2C3442' = 'btnN'; '#3E4654' = 'btnN'; '#3E7FB8' = 'accent'; '#46536A' = 'line'
    '#4A3138' = 'btnD'; '#43272E' = 'btnD'
    '#5AC8FF' = 'accent'; '#5C6575' = 'dim2'; '#63D68B' = 'ok'; '#7D879B' = 'dim'
    '#8A93A3' = 'dim'; '#8B94A5' = 'dim'; '#8FA3BF' = 'dim'; '#9AA6B8' = 'dim'
    '#9BB4CF' = 'dim'; '#9BE3B4' = 'ok'; '#9FB2CC' = 'dim'; '#A78BFA' = 'temp'
    '#A8C6E8' = 'text'; '#C3CCDA' = 'text'; '#D5DCE7' = 'text'; '#D6E2F5' = 'text'
    '#DEE5EF' = 'text'; '#E6ECF5' = 'text'; '#E85D5D' = 'err'; '#F2B44B' = 'warn'
    '#F2F6FC' = 'text'; '#F472B6' = 'temp'; '#F8B4B4' = 'err'; '#1F7A46' = 'btnOk'
    '#2A4034' = 'btnOk'; '#2E4A6B' = 'btnA'; '#1B5E8C' = 'btnA'; '#0E1015' = 'bg'
    '#12151B' = 'header'; '#151920' = 'nav'; '#101318' = 'footer'; '#1B2029' = 'card'
}

# объекты рисования графиков: пересоздаются при смене темы
function New-Draw {
    $c = $script:CLR
    $rgb = {
        param($hex, $alpha)
        $col = [System.Drawing.ColorTranslator]::FromHtml([string]$hex)
        return [System.Drawing.Color]::FromArgb([int]$alpha, $col.R, $col.G, $col.B)
    }
    $solid = {
        param($hex)
        return New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml([string]$hex))
    }
    $pen = {
        param($hex, $width, $alpha)
        $col = [System.Drawing.ColorTranslator]::FromHtml([string]$hex)
        if ($alpha -lt 255) { $col = [System.Drawing.Color]::FromArgb([int]$alpha, $col.R, $col.G, $col.B) }
        return New-Object System.Drawing.Pen ($col, [single]$width)
    }

    $d = @{
        fontAxis  = Fnt 'Segoe UI' 8 $false
        fontSmall = Fnt 'Segoe UI' 7.5 $false
        fontTitle = Fnt 'Segoe UI' 10 $true
        fontVal   = Fnt 'Segoe UI' 8.5 $true

        brDim     = & $solid $c.dim
        brDim2    = & $solid $c.dim2
        brText    = & $solid $c.text
        brAccent  = & $solid $c.accent
        brWarn    = & $solid $c.warn
        brOk      = & $solid $c.ok
        brVolt    = & $solid $c.accent
        brTemp    = & $solid $c.temp
        brBar     = & $solid (MixC $c.accent $c.card2 0.45)
        brBarHi   = & $solid $c.accent
        brBarDim  = & $solid (MixC $c.accent $c.card 0.72)
        brHover   = New-Object System.Drawing.SolidBrush (& $rgb $c.accent 24)
        brGap     = New-Object System.Drawing.SolidBrush (& $rgb $c.warn 26)
        brFill    = New-Object System.Drawing.SolidBrush (& $rgb $c.accent 22)
        brTipBg   = New-Object System.Drawing.SolidBrush (& $rgb $c.card 246)
        brTipText = & $solid $c.text
        brTipDim  = & $solid $c.dim

        penGrid      = & $pen $c.line 1 40
        penGrid2     = & $pen $c.line 1 78
        penVolt      = & $pen $c.accent 2 255
        penViolet    = & $pen $c.accent 1.6 255
        penNow       = & $pen $c.warn 1 255
        penTip       = & $pen $c.line 1 255
        penHover     = & $pen $c.accent 1 150
        penCursor    = & $pen $c.accent 1 190
        penTemp      = & $pen $c.temp 1.6 255
        penVoltHi    = & $pen $c.accent 3 255
        penTempHi    = & $pen $c.temp 3 255
        penVioletDim = & $pen $c.accent 1.2 90
        penTempDim   = & $pen $c.temp 1.2 90
        penSparkV    = & $pen $c.accent 1.8 255
        penSparkP    = & $pen $c.power 1.8 255
        penSparkT    = & $pen $c.temp 1.8 255
        penSparkE    = & $pen $c.money 1.8 255
        brSparkV     = New-Object System.Drawing.SolidBrush (& $rgb $c.accent 30)
        brSparkP     = New-Object System.Drawing.SolidBrush (& $rgb $c.power 30)
        brSparkT     = New-Object System.Drawing.SolidBrush (& $rgb $c.temp 30)
        brSparkE     = New-Object System.Drawing.SolidBrush (& $rgb $c.money 30)
    }
    $d.penNow.DashStyle = 'Dash'
    $d.penCursor.DashStyle = 'Dash'
    return $d
}

# ============================================================================

#  ПАЛИТРА И ФОРМА


# ============================================================================

$script:CLR = Get-Skin $script:cfg.theme
$script:draw = New-Draw



$form                = New-Object System.Windows.Forms.Form

$form.Text           = 'DOM MONITOR — пульт управления'

$form.Size           = New-Object System.Drawing.Size(1140, 872)

$form.MinimumSize    = New-Object System.Drawing.Size(980, 760)

$form.StartPosition  = 'CenterScreen'

$form.BackColor      = C $script:CLR.bg

$form.ForeColor      = C $script:CLR.text

$form.Font           = Fnt 'Segoe UI' 9 $false



# ------------------------------------------------------------------ шапка

$top = New-Object System.Windows.Forms.Panel
$top.Dock = 'Top'
$top.Height = 80
$top.BackColor = C $script:CLR.header
$form.Controls.Add($top)

# заголовок
$lblTitle = New-Lbl $top 'DOM MONITOR' 15.5 $script:CLR.accent $true 16 11
$lblSub   = New-Lbl $top 'ESP32 · 192.168.1.51 · обновление 5 с' 7.5 $script:CLR.dim2 $false 18 54

# --- статус связи: цветная точка + одна строка текста -----------------------
$script:dotHttp = New-Object System.Windows.Forms.Panel
$script:dotHttp.Size = New-Object System.Drawing.Size(8, 8)
$script:dotHttp.Location = New-Object System.Drawing.Point(180, 22)
$script:dotHttp.BackColor = C $script:CLR.err
$top.Controls.Add($script:dotHttp)
Set-RoundDot $script:dotHttp

$lblHttp = New-Object System.Windows.Forms.Label
$lblHttp.Text = 'HTTP —'
$lblHttp.Font = Fnt 'Segoe UI' 9.5 $true
$lblHttp.ForeColor = C $script:CLR.err
$lblHttp.BackColor = C $script:CLR.header
$lblHttp.TextAlign = 'MiddleLeft'
$lblHttp.AutoSize = $false
$lblHttp.Size = New-Object System.Drawing.Size(198, 22)
$lblHttp.Location = New-Object System.Drawing.Point(194, 13)
$top.Controls.Add($lblHttp)

# --- второй ряд: прошивка · свежесть данных · климат · COM ------------------
$lblFwChip = New-Object System.Windows.Forms.Label
$lblFwChip.Text = 'прошивка —'
$lblFwChip.Font = Fnt 'Segoe UI' 7.5 $false
$lblFwChip.ForeColor = C $script:CLR.dim
$lblFwChip.BackColor = C $script:CLR.card
$lblFwChip.TextAlign = 'MiddleCenter'
$lblFwChip.Size = New-Object System.Drawing.Size(116, 20)
$lblFwChip.Location = New-Object System.Drawing.Point(200, 48)
$top.Controls.Add($lblFwChip)

$lblFresh = New-Object System.Windows.Forms.Label
$lblFresh.Text = 'данные —'
$lblFresh.Font = Fnt 'Segoe UI' 8 $false
$lblFresh.ForeColor = C $script:CLR.dim2
$lblFresh.BackColor = C $script:CLR.card
$lblFresh.TextAlign = 'MiddleCenter'
$lblFresh.Size = New-Object System.Drawing.Size(92, 20)
$lblFresh.Location = New-Object System.Drawing.Point(326, 50)
$top.Controls.Add($lblFresh)

$script:lblClimate = New-Object System.Windows.Forms.Label
$script:lblClimate.Text = 'климат —'
$script:lblClimate.Font = Fnt 'Segoe UI' 8 $false
$script:lblClimate.ForeColor = C $script:CLR.dim2
$script:lblClimate.BackColor = C $script:CLR.card
$script:lblClimate.AutoSize = $true
$script:lblClimate.Location = New-Object System.Drawing.Point(428, 51)
$top.Controls.Add($script:lblClimate)

$script:dotCom = New-Object System.Windows.Forms.Panel
$script:dotCom.Size = New-Object System.Drawing.Size(8, 8)
$script:dotCom.Location = New-Object System.Drawing.Point(580, 55)
$script:dotCom.BackColor = C $script:CLR.dim2
$top.Controls.Add($script:dotCom)
Set-RoundDot $script:dotCom

$lblCom = New-Object System.Windows.Forms.Label
$lblCom.Text = 'COM5 — закрыт'
$lblCom.Font = Fnt 'Segoe UI' 8 $false
$lblCom.ForeColor = C $script:CLR.dim2
$lblCom.BackColor = C $script:CLR.card
$lblCom.TextAlign = 'MiddleLeft'
$lblCom.AutoSize = $false
$lblCom.AutoEllipsis = $true
$lblCom.Size = New-Object System.Drawing.Size(180, 20)
$lblCom.Location = New-Object System.Drawing.Point(594, 47)
$top.Controls.Add($lblCom)

# --- крупные кнопки справа (высота 30 px) -----------------------------------
$btnSnd     = New-Btn $top '♪ Звуки' '#24405A' $script:CLR.text 92 30
$btnSnd.Add_Click({ Show-Sounds })
$btnPult    = New-Btn $top 'Экран платы' '#2A3240' $script:CLR.text 108 30
$btnReboot  = New-Btn $top '⟳ Перезагрузка' '#4A3138' $script:CLR.text 138 30
$btnSerial  = New-Btn $top 'Подключить' '#24405A' $script:CLR.text 104 30
$script:btnTheme = New-Btn $top '◐ Тема' '#2A3240' $script:CLR.text 84 30
$script:btnTheme.Add_Click({ Switch-Theme })

$btnSnd.Location    = New-Object System.Drawing.Point(568, 12)
$btnPult.Location   = New-Object System.Drawing.Point(668, 12)
$btnReboot.Location = New-Object System.Drawing.Point(784, 12)
$btnSerial.Location = New-Object System.Drawing.Point(930, 12)
$script:btnTheme.Location = New-Object System.Drawing.Point(1042, 12)

# --- яркость экрана платы: ползунок в шапке, работает по HTTP ---------------
$script:trkBright = New-Object System.Windows.Forms.TrackBar
$script:trkBright.Minimum = 5
$script:trkBright.Maximum = 100
$script:trkBright.TickFrequency = 25
$script:trkBright.Value = 100
$script:trkBright.Size = New-Object System.Drawing.Size(130, 26)
$script:trkBright.Location = New-Object System.Drawing.Point(944, 50)
$top.Controls.Add($script:trkBright)

$script:lblBright = New-Lbl $top '100 %' 8.5 $script:CLR.dim2 $true 1082 56

$script:trkBright.Add_Scroll({
    $v = [int]$script:trkBright.Value
    if ($null -ne $script:lblBright) { $script:lblBright.Text = "$v %" }
    try {
        $null = $script:http.GetStringAsync("$($script:cfg.board)/disp?bright=$v").Result
        Add-Log ("яркость экрана: $v %")
    } catch { Add-Log "яркость: плата не ответила" 'warn' }
})

# подсказки
$script:tipMain = New-Object System.Windows.Forms.ToolTip
$script:tipMain.AutoPopDelay = 20000
$script:tipMain.InitialDelay = 350
$script:tipMain.ReshowDelay = 200
$script:tipMain.ShowAlways = $true
$script:tipMain.SetToolTip($btnPult, 'включить/выключить подсветку экрана платы (команда disp toggle по serial; нужен открытый порт)')
$script:tipMain.SetToolTip($btnReboot, 'перезагрузить плату (HTTP /reboot; COM — только если HTTP недоступен)')
$script:tipMain.SetToolTip($btnSerial, 'открыть/закрыть COM-порт для команд Пульт и Перезагрузить; данные идут по HTTP')
$script:tipMain.SetToolTip($lblFwChip, 'версия прошивки: из /data, из serial или со страницы платы (CYD-4.4)')
$script:tipMain.SetToolTip($btnSnd, 'звуки платы: тест мелодий и громкости')
$script:tipMain.SetToolTip($script:btnTheme, 'тема интерфейса: клик — следующая из шести (светлая, тёмная, ЧБ, розовая, красная, радужная)')
$script:tipMain.SetToolTip($lblHttp, 'канал связи с платой по HTTP — данные обновляются каждые 5 с')
$script:tipMain.SetToolTip($lblCom, 'COM-порт для команд Пульт/Перезагрузить (данные идут по HTTP)')
$script:tipMain.SetToolTip($script:trkBright, 'яркость подсветки экрана платы (5-100 %), применяется сразу по HTTP')

# ------------------------------------------------------------------ вкладки

$navBar = New-Object System.Windows.Forms.Panel

$navBar.Dock = 'Top'

$navBar.Height = 46

$navBar.BackColor = C $script:CLR.nav

$form.Controls.Add($navBar)



$script:navBtn = @{}



function New-NavButton($key, $text, $x) {

    $b = New-Object System.Windows.Forms.Button

    $b.Text = $text

    $b.Size = New-Object System.Drawing.Size(132, 40)

    $b.Location = New-Object System.Drawing.Point([int]$x, 3)

    $b.FlatStyle = 'Flat'

    $b.Font = Fnt 'Segoe UI' 9.5 $true

    $b.ForeColor = C $script:CLR.dim

    $b.BackColor = C $script:CLR.nav

    $b.FlatAppearance.BorderSize = 0

    $b.FlatAppearance.MouseOverBackColor = C (SkinC '#222836')

    $b.Cursor = 'Hand'

    $navBar.Controls.Add($b)

    $script:navBtn[$key] = $b

    return $b

}



# ------------------------------------------------------------------ подвал

$footer = New-Object System.Windows.Forms.Panel

$footer.Dock = 'Bottom'

$footer.Height = 26

$footer.BackColor = C $script:CLR.footer

$form.Controls.Add($footer)



$lblFootLeft = New-Lbl $footer 'ожидание данных…' 8 $script:CLR.dim2 $false 16 6

$lblFootRight = New-Object System.Windows.Forms.Label

$lblFootRight.Text = ''

$lblFootRight.Font = Fnt 'Segoe UI' 8 $false

$lblFootRight.ForeColor = C $script:CLR.dim2

$lblFootRight.AutoSize = $false

$lblFootRight.TextAlign = 'MiddleRight'

$lblFootRight.Size = New-Object System.Drawing.Size(560, 18)

$lblFootRight.Location = New-Object System.Drawing.Point(400, 5)

$footer.Controls.Add($lblFootRight)



$pageHost = New-Object System.Windows.Forms.Panel

$pageHost.Dock = 'Fill'

$pageHost.BackColor = C $script:CLR.bg

$form.Controls.Add($pageHost)

$form.Controls.SetChildIndex($pageHost, 0)

$form.Controls.SetChildIndex($navBar, 1)

$form.Controls.SetChildIndex($top, 2)

$form.Controls.SetChildIndex($footer, 3)



$script:pages = [ordered]@{}



function Add-Page($key, $page) {

    $page.Dock = 'Fill'

    $page.Visible = $false

    $page.BackColor = C $script:CLR.bg

    $pageHost.Controls.Add($page)

    $script:pages[$key] = $page

}



function New-TabPage {

    $p = New-Object System.Windows.Forms.Panel

    $p.BackColor = C $script:CLR.bg

    return $p

}



function Show-Page($key) {
    if (-not $script:pages.Contains($key)) { return }

    # без мерцания: пока переключаем видимость, раскладка приостановлена
    $locked = ($null -ne $pageHost -and $pageHost.IsHandleCreated)
    if ($locked) { $pageHost.SuspendLayout() }
    try {
        foreach ($k in @($script:pages.Keys)) {
            $script:pages[$k].Visible = ($k -eq $key)
            $b = $script:navBtn[$k]
            if ($null -ne $b) {
                if ($k -eq $key) {
                    $b.ForeColor = C $script:CLR.accent
                    $b.BackColor = C $script:CLR.card
                } else {
                    $b.ForeColor = C $script:CLR.dim
                    $b.BackColor = C $script:CLR.nav
                }
            }
        }
    } finally {
        if ($locked) { $pageHost.ResumeLayout($true) }
    }

    # раскладку страницы считаем уже после ResumeLayout:
    # иначе ClientSize ещё старый и Layout-* выходит по «малому» размеру
    try {
        if ($key -eq 'screen') { Layout-Screen; $chartScreen.Invalidate() }
        if ($key -eq 'hist')   { Layout-History; Update-EnergyChart $chartHist $script:histSel }
        if ($key -eq 'dev')    { Layout-Device }
        if ($key -eq 'flash')  { Layout-Flash }
        if ($key -eq 'log')    { Layout-Log }
        if ($key -eq 'prot')   { Layout-Prot; Load-Prot }
        if ($key -eq 'term')   { Layout-Term; Refresh-Terminal }
        if ($key -eq 'arch')   { Layout-Arch; Refresh-Archive }
    } catch {
        Write-AppLog ('страница ' + $key + ': ' + $_.Exception.Message)
    }
}



# ============================================================================

#  ВКЛАДКА «ЭКРАН»

# ============================================================================

$pgScreen = New-TabPage

Add-Page 'screen' $pgScreen



$lcdWrap = New-Object System.Windows.Forms.Panel

$lcdWrap.Size = New-Object System.Drawing.Size(356, 278)

$lcdWrap.Location = New-Object System.Drawing.Point(14, 12)

$lcdWrap.BackColor = C $script:CLR.bg

$pgScreen.Controls.Add($lcdWrap)



$lblLcdCap = New-Lbl $lcdWrap 'ЭКРАН ПЛАТЫ' 7.5 $script:CLR.dim2 $true 2 0



$lcd = New-Object System.Windows.Forms.PictureBox

$lcd.Cursor = 'Hand'

$lcd.BackColor = C $script:CLR.lcdBg

$lcd.ForeColor = C $script:CLR.lcdFg

$lcd.Location = New-Object System.Drawing.Point(0, 20)

$lcd.Size = New-Object System.Drawing.Size(356, 236)

$lcd.SizeMode = 'Zoom'

$lcd.Add_Paint({ param($s, $e) Draw-DeviceScreen $e.Graphics $s })

$lcdWrap.Controls.Add($lcd)
$script:lcd = $lcd



$lblLcdHint = New-Lbl $lcdWrap 'копия экрана устройства · 240×320 · обновляется по воздуху' 7.5 $script:CLR.dim2 $false 2 260



function New-Card($parent, [string]$caption, [float]$valSize) {
    # мягкая тень: второй прямоугольник со смещением 2 px, чуть темнее фона
    $sh = New-Object System.Windows.Forms.Panel
    $sh.Size = New-Object System.Drawing.Size(200, 150)
    $sh.BackColor = C $script:CLR.shadow
    $sh.Enabled = $false
    $sh.TabStop = $false

    $p = New-Object System.Windows.Forms.Panel
    $p.BackColor = C $script:CLR.card
    $p.Size = New-Object System.Drawing.Size(200, 150)

    # внутренние отступы 12 px
    $cap = New-Lbl $p $caption 8.5 $script:CLR.dim $true 12 9

    # значение: крупно, но не больше 20 pt
    $vs = $valSize
    if ($vs -lt 16) { $vs = 16 }
    if ($vs -gt 20) { $vs = 20 }

    $val = New-Object System.Windows.Forms.Label
    $val.Text = '—'
    $val.AutoSize = $false
    $val.Font = Fnt 'Segoe UI' $vs $true
    $val.ForeColor = C $script:CLR.text
    $val.Location = New-Object System.Drawing.Point(12, 26)
    $val.Size = New-Object System.Drawing.Size(176, 38)
    $val.TextAlign = 'MiddleLeft'
    $p.Controls.Add($val)

    $sub = New-Object System.Windows.Forms.Label
    $sub.Text = ''
    $sub.AutoSize = $false
    $sub.AutoEllipsis = $true
    $sub.Font = Fnt 'Segoe UI' 8 $false
    $sub.ForeColor = C $script:CLR.dim2
    $sub.Location = New-Object System.Drawing.Point(12, 66)
    $sub.Size = New-Object System.Drawing.Size(176, 17)
    $p.Controls.Add($sub)

    # третья строка: когда показатель реально получен («получено 12:31»)
    $when = New-Object System.Windows.Forms.Label
    $when.Text = 'получено —'
    $when.AutoSize = $false
    $when.AutoEllipsis = $true
    $when.Font = Fnt 'Segoe UI' 7.5 $false
    $when.ForeColor = C $script:CLR.dim2
    $when.Location = New-Object System.Drawing.Point(12, 83)
    $when.Size = New-Object System.Drawing.Size(176, 15)
    $p.Controls.Add($when)

    # мини-график за сутки внизу карточки (рисуется по /history)
    $spark = New-Object System.Windows.Forms.PictureBox
    $spark.BackColor = C $script:CLR.card
    $spark.Location = New-Object System.Drawing.Point(12, 102)
    $spark.Size = New-Object System.Drawing.Size(176, 42)
    $spark.Cursor = 'Hand'
    if (-not $script:sparkHover) { $script:sparkHover = @{} }
    $spark.Add_MouseMove({
        param($s, $e)
        try {
            $vals = Get-SparkValues ([string]$s.Tag)
            $n = 0; for ($i = 0; $i -lt 24; $i++) { if ($null -ne $vals[$i]) { $n++ } }
            if ($n -lt 2) { return }
            $idx = [int][Math]::Round(($e.X - 2) * 23.0 / [Math]::Max(1, ($s.Width - 4)))
            if ($idx -lt 0) { $idx = 0 }
            if ($idx -gt 23) { $idx = 23 }
            if (-not $script:sparkHover.ContainsKey($s) -or $script:sparkHover[$s] -ne $idx) {
                $script:sparkHover[$s] = $idx
                $s.Invalidate()
            }
        } catch { }
    })
    $spark.Add_MouseLeave({
        param($s, $e)
        try {
            if ($script:sparkHover.ContainsKey($s)) { $script:sparkHover.Remove($s) }
            $s.Invalidate()
        } catch { }
    })
    $p.Controls.Add($spark)

    if ($null -ne $parent) {
        $parent.Controls.Add($sh)
        $parent.Controls.Add($p)
        $p.BringToFront()
    }

    Set-Round $sh 12
    Set-Round $p 12
    $p.Add_Resize({ param($s, $e) Set-Round $s 12 })
    $sh.Add_Resize({ param($s, $e) Set-Round $s 12 })

    return [pscustomobject]@{ Panel = $p; Cap = $cap; Val = $val; Sub = $sub; When = $when; Spark = $spark; Shadow = $sh }
}



$script:cards = [ordered]@{}

$script:cards['v']    = New-Card $pgScreen 'НАПРЯЖЕНИЕ'        22

$script:cards['t']    = New-Card $pgScreen 'ТЕМПЕРАТУРА РЕЛЕ'  15

$script:cards['r']    = New-Card $pgScreen 'РЕЛЕ'              15

$script:cards['p']    = New-Card $pgScreen 'МОЩНОСТЬ'          15

$script:cards['e']    = New-Card $pgScreen 'РАСХОД СЕГОДНЯ'    15

$script:cards['cost'] = New-Card $pgScreen 'СТОИМОСТЬ СЕГОДНЯ' 15

$script:cards['y']    = New-Card $pgScreen 'ВЧЕРА'             15

$script:cards['m']    = New-Card $pgScreen 'МЕСЯЦ'             15

$script:cards['all']  = New-Card $pgScreen 'СЧЁТЧИК ВСЕГО'     15



# --- мини-графики и клик по карточке -----------------------------------------

# Клик по карточке открывает «Историю» с выбранным показателем; спарклайн

# рисуется по тем же часовым данным /history, что и большие графики.

$script:cardSparkMetric = @{ v = 'v'; p = 'p'; t = 't'; e = 'e' }

$script:cardSparkTitle  = @{ v = 'напряжение'; p = 'мощность'; t = 'температура'; e = 'расход' }

foreach ($ck in @($script:cardSparkMetric.Keys)) {

    $cc = $script:cards[$ck]

    if ($null -eq $cc) { continue }

    $tp = $script:cardSparkTitle[$ck]

    foreach ($ctl in @($cc.Panel, $cc.Cap, $cc.Val, $cc.Sub, $cc.When, $cc.Spark)) {

        if ($null -eq $ctl) { continue }

        $ctl.Tag = $ck

        $ctl.Cursor = 'Hand'

        $ctl.Add_Click({ param($sender, $e) Open-CardHistory ([string]$sender.Tag) })

        if ($null -ne $script:tipMain) {

            $script:tipMain.SetToolTip($ctl, ('мини-график за сутки: ' + $tp + "`r`nклик — открыть вкладку «История» с этим показателем"))

        }

    }

    if ($null -ne $cc.Spark) {

        $cc.Spark.Add_Paint({ param($sender, $e) Draw-Sparkline $e.Graphics $sender ([string]$sender.Tag) })

    }

}



function New-EnergyChart {
    # Один график на три величины: энергия столбиками (кВт·ч, левая шкала),
    # напряжение и температура линиями (две правые шкалы). Всё в одной библиотеке .NET.
    $ch = New-Object System.Windows.Forms.DataVisualization.Charting.Chart
    $ch.BackColor = C $script:CLR.card
    $ch.Dock = 'None'
    $ch.AntiAliasing = 'All'

    $area = New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea
    $area.Name = 'main'
    $area.BackColor = 'Transparent'
    $area.AxisX.MajorGrid.Enabled = $false
    $area.AxisX.Interval = 3
    $area.AxisX.LabelStyle.ForeColor = C $script:CLR.dim
    $area.AxisX.LineColor = C $script:CLR.line
    $area.AxisX.LabelStyle.Font = Fnt 'Segoe UI' 8 $false
    $area.AxisY.LabelStyle.ForeColor = C $script:CLR.dim
    $area.AxisY.LineColor = C $script:CLR.line
    $area.AxisY.MajorGrid.LineColor = C $script:CLR.line
    $area.AxisY.MajorGrid.LineDashStyle = 'Dot'
    $area.AxisY.LabelStyle.Font = Fnt 'Segoe UI' 8 $false
    $area.AxisY.Title = 'Вт·ч'
    $area.AxisY.TitleFont = Fnt 'Segoe UI' 8 $false
    $area.AxisY.TitleForeColor = C $script:CLR.dim
    $area.AxisY2.Enabled = 'True'
    $area.AxisY2.LabelStyle.ForeColor = C $script:CLR.dim
    $area.AxisY2.LineColor = C $script:CLR.line
    $area.AxisY2.LabelStyle.Font = Fnt 'Segoe UI' 8 $false
    $area.AxisY2.Title = 'В'
    $area.AxisY2.TitleFont = Fnt 'Segoe UI' 8 $false
    $area.AxisY2.TitleForeColor = C $script:CLR.dim
    $area.AxisY2.MajorGrid.Enabled = $false

    $area.Position.Auto = $false
    # над областью графика — строка легенды (4 пункта), иначе она ложится
    # прямо на столбики (наложение подписей на график).
    $area.Position.Height = 42
    $area.Position.Width = 96
    $area.Position.Y = 12
    $area.Position.X = 0
    $area.InnerPlotPosition.Auto = $true
    $ch.ChartAreas.Add($area)

    # вторая область: температура своей шкалой, по оси X выровнена с первой
    $areaT = New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea
    $areaT.Name = 'temp'
    $areaT.BackColor = 'Transparent'
    $areaT.Position.Auto = $false
    $areaT.Position.Height = 36
    $areaT.Position.Width = 96
    $areaT.Position.Y = 56
    $areaT.Position.X = 0
    $areaT.AxisX.MajorGrid.Enabled = $false
    $areaT.AxisX.LabelStyle.Enabled = $false
    $areaT.AxisX.LineColor = C $script:CLR.line
    $areaT.AxisX.Interval = 3
    $areaT.AxisY.Title = '°C'
    $areaT.AxisY.TitleFont = Fnt 'Segoe UI' 8 $false
    $areaT.AxisY.TitleForeColor = C $script:CLR.dim
    $areaT.AxisY.LabelStyle.ForeColor = C $script:CLR.dim
    $areaT.AxisY.LabelStyle.Font = Fnt 'Segoe UI' 8 $false
    $areaT.AxisY.LineColor = C $script:CLR.line
    $areaT.AxisY.MajorGrid.LineColor = C $script:CLR.line
    $areaT.AxisY.MajorGrid.LineDashStyle = 'Dot'
    $ch.ChartAreas.Add($areaT)

    $mk = {
        param($name, $type, $ax, $color, $w)
        $s = New-Object System.Windows.Forms.DataVisualization.Charting.Series
        $s.Name = $name
        $s.ChartType = $type
        $s.ChartArea = 'main'
        $s.YAxisType = 'Primary'
        if ($ax) { $s.YAxisType = 'Secondary' }
        if ($name -like 'Температура*') { $s.ChartArea = 'temp'; $s.YAxisType = 'Primary' }
        $s.Color = C $color
        $s.BorderWidth = $w
        $s.IsVisibleInLegend = $true
        $s.LegendText = $name
        $s.Font = Fnt 'Segoe UI' 8 $false
        if ($type -eq 'Column') {
            $s['PointWidth'] = '0.55'
        } else {
            $s.BorderDashStyle = 'Solid'
            $s.MarkerStyle = 'Circle'
            $s.MarkerSize = 5
            $s.MarkerColor = C $color
        }
        return $s
    }

    $ch.Series.Add((& $mk 'Энергия, Вт·ч' 'Column' $false $script:CLR.accent 1))
    # для недели и месяца: приход и расход отдельными столбиками (день/ночь)
    $sDay = New-Object System.Windows.Forms.DataVisualization.Charting.Series
    $sDay.Name = 'День, Вт·ч'; $sDay.ChartType = 'Column'; $sDay.ChartArea = 'main'
    $sDay.YAxisType = 'Primary'; $sDay.Color = C $script:CLR.money; $sDay.BorderWidth = 1
    $sDay.LegendText = 'День, Вт·ч'; $sDay.Font = Fnt 'Segoe UI' 8 $false
    $sDay['PointWidth'] = '0.7'; $sDay.IsVisibleInLegend = $false
    $ch.Series.Add($sDay)
    $sNo = New-Object System.Windows.Forms.DataVisualization.Charting.Series
    $sNo.Name = 'Ночь, Вт·ч'; $sNo.ChartType = 'Column'; $sNo.ChartArea = 'main'
    $sNo.YAxisType = 'Primary'; $sNo.Color = C $script:CLR.accent; $sNo.BorderWidth = 1
    $sNo.LegendText = 'Ночь, Вт·ч'; $sNo.Font = Fnt 'Segoe UI' 8 $false
    $sNo['PointWidth'] = '0.7'; $sNo.IsVisibleInLegend = $false
    $ch.Series.Add($sNo)
    # вчерашний день приглушённым пунктиром для сравнения
    $sPrev = New-Object System.Windows.Forms.DataVisualization.Charting.Series
    $sPrev.Name = 'Вчера, В'; $sPrev.ChartType = 'Line'; $sPrev.ChartArea = 'main'
    $sPrev.YAxisType = 'Secondary'; $sPrev.Color = [System.Drawing.Color]::FromArgb(150, 150, 160, 175)
    $sPrev.BorderWidth = 1; $sPrev.BorderDashStyle = 'Dash'
    $sPrev.LegendText = 'Вчера, В'; $sPrev.Font = Fnt 'Segoe UI' 8 $false
    $sPrev.IsVisibleInLegend = $true
    $ch.Series.Add($sPrev)
    $ch.Series.Add((& $mk 'Напряжение, В' 'Line' $true $script:CLR.accent 2))
    $ch.Series.Add((& $mk 'Температура, °C' 'Line' $false $script:CLR.temp 2))

    $ch.Legends.Clear()
    $lg = New-Object System.Windows.Forms.DataVisualization.Charting.Legend
    $lg.Name = 'main'
    $lg.Docking = 'Top'
    $lg.Alignment = 'Near'
    $lg.BackColor = 'Transparent'
    $lg.ForeColor = C $script:CLR.text
    $lg.Font = Fnt 'Segoe UI' 8 $false
    $lg.LegendStyle = 'Row'
    # Легенда по умолчанию «внутри области диаграммы» и рисовалась поверх
    # столбиков (наложение подписей на график). Выносим её в отдельную полосу
    # сверху — Chart сам освобождает место под неё.
    try { $lg.IsDockedInsideChartArea = $false } catch { }
    try { $lg.Docking = 'Top' } catch { }
    $ch.Legends.Add($lg)
    foreach ($s in $ch.Series) { $s.Legend = 'main' }

    # подсказка: на одном часе — сразу все три значения
    $ch.Add_GetToolTipText({
        param($sender, $e)
        try {
            $hit = $sender.HitTest($e.X, $e.Y)
            if ($null -eq $hit -or $null -eq $hit.Object) { return }
            if ($sender.AccessibleDescription -eq 'metric-history') {
                if ($hit.Object -isnot [System.Windows.Forms.DataVisualization.Charting.DataPoint]) { return }
                $hour = [int][Math]::Round([double]$hit.Object.XValue)
                $records = @($script:histCache[$sender.AccessibleName])
                $record = $records | Where-Object { [int]$_.h -eq $hour -and [int]$_.n -gt 0 } | Select-Object -First 1
                if ($null -eq $record) { $e.Text = ('{0:00}:00 · нет измерений' -f $hour); return }
                $parts = @(('{0:00}:00' -f $hour))
                $rawK = Field $record @('kwh'); $rawP = Field $record @('p'); $rawPm = Field $record @('pmax')
                $rawV = Field $record @('v'); $rawT = Field $record @('t')
                if ($null -ne $rawK) { $parts += ((Num $rawK '0.000') + ' кВт·ч') } else { $parts += 'кВт·ч: нет измерений' }
                if ($null -ne $rawP) { $parts += ((Num $rawP '0') + ' Вт') } else { $parts += 'Вт: нет измерений' }
                if ($null -ne $rawPm) { $parts += ('пик ' + (Num $rawPm '0') + ' Вт') } else { $parts += 'пик: нет измерений' }
                if ($null -ne $rawV) { $parts += ((Num $rawV '0.0') + ' В') } else { $parts += 'В: нет измерений' }
                if ($null -ne $rawT) { $parts += ((Num $rawT '0.0') + ' °C') } else { $parts += '°C: нет измерений' }
                $e.Text = ($parts -join ' · ')
                return
            }
            $x = $null
            if ($hit.Object -is [System.Windows.Forms.DataVisualization.Charting.DataPoint]) { $x = [double]$hit.Object.XValue }
            if ($null -eq $x) { return }
            $hh = [int][Math]::Round($x)
            $parts = @(('{0:00}:00' -f $hh))
            $gotAny = $false
            foreach ($s in $sender.Series) {
                if (-not $s.IsVisibleInLegend -and -not ($s.Name -like '*Вт·ч*')) { continue }
                for ($i = 0; $i -lt $s.Points.Count; $i++) {
                    $pt = $s.Points[$i]
                    if ([int][Math]::Round([double]$pt.XValue) -ne $hh) { continue }
                    if ($pt.IsEmpty) { break }   # нет измерения — ложный 0 не печатаем
                    $v = [double]$pt.YValues[0]
                    if ($s.Name -like '*день*' -or $s.Name -like '*Ночь*' -or $s.Name -like 'Энергия*') {
                        $unit = ' Вт·ч'
                    } elseif ($s.Name -like 'Вчера*' -or $s.Name -like 'Напряжение*') { $unit = ' В'
                    } else { $unit = ' °C' }
                    $parts += (('{0:0.#}' -f $v) + $unit)
                    # дельта к предыдущему часу и мин/макс серии — для наглядности
                    if ($i -gt 0 -and -not $s.Points[$i - 1].IsEmpty) {
                        $pv = [double]$s.Points[$i - 1].YValues[0]
                        $dv = $v - $pv
                        if ([Math]::Abs($dv) -ge 0.05) { $parts += (('{0:+0.#;-0.#}' -f $dv) + $unit + ' за час') }
                    }
                    $mMin = 99999.0; $mMax = -99999.0
                    foreach ($pp in $s.Points) {
                        if ($pp.IsEmpty) { continue }
                        $yy = [double]$pp.YValues[0]
                        if ($yy -lt $mMin) { $mMin = $yy }
                        if ($yy -gt $mMax) { $mMax = $yy }
                    }
                    if ($mMax -gt -99999.0) { $parts += (('мин {0:0.#} / макс {1:0.#}' -f $mMin, $mMax) + $unit) }
                    $gotAny = $true
                    break
                }
            }
            if (-not $gotAny) { $e.Text = ('{0:00}:00 · нет измерений' -f $hh) } else { $e.Text = ($parts -join "`n") }
        } catch { }
    })

    return $ch
}

# ---- асинхронная загрузка /range для графика недели/месяца ----
function Start-ChartRange([string]$key,[string]$from,[string]$to) {
    if (-not $script:chartTasks) { $script:chartTasks=@{} }
    if (-not $script:chartRangeNext) { $script:chartRangeNext=@{} }
    if (-not $script:chartRangeError) { $script:chartRangeError=@{} }
    if ($script:chartTasks.ContainsKey($key)) { return }
    if ($script:chartRangeNext.ContainsKey($key) -and $script:chartRangeNext[$key] -gt (Get-Date)) { return }
    try {
        $url='http://'+$script:cfg.boardIp+'/range?from='+$from+'&to='+$to
        $script:chartTasks[$key]=[pscustomobject]@{task=$script:httpSlow.GetStringAsync($url);from=$from;to=$to;start=(Get-Date)}
    } catch {
        $script:chartRangeError[$key]=$_.Exception.Message
        $script:chartRangeNext[$key]=(Get-Date).AddSeconds(5)
    }
}

function Pump-ChartRange {
    if (-not $script:chartTasks) { return }
    if (-not $script:chartRangeCache) { $script:chartRangeCache=@{} }
    if (-not $script:chartRangeAt) { $script:chartRangeAt=@{} }
    if (-not $script:chartRangeNext) { $script:chartRangeNext=@{} }
    if (-not $script:chartRangeError) { $script:chartRangeError=@{} }
    foreach($key in @($script:chartTasks.Keys)) {
        $item=$script:chartTasks[$key]
        if (-not $item.task.IsCompleted -and ((Get-Date)-$item.start).TotalSeconds -le 25) { continue }
        $script:chartTasks.Remove($key)
        try {
            if ($item.task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw 'Нет ответа на запрос периода' }
            $o=$item.task.Result|ConvertFrom-Json
            if ($o.pending) {$script:chartRangeNext[$key]=(Get-Date).AddSeconds(1);$script:chartRangeError.Remove($key);continue}
            if ($o.trunc -or $o.ok -eq $false -or $null -eq $o.PSObject.Properties['rows']) { throw 'Неполный ответ периода' }
            $script:chartRangeCache[$key]=@($o.rows);$script:chartRangeAt[$key]=Get-Date
            $script:chartRangeError.Remove($key);$script:chartRangeNext.Remove($key)
        } catch {
            $script:chartRangeError[$key]=$_.Exception.Message
            $script:chartRangeNext[$key]=(Get-Date).AddSeconds(5)
            Write-AppLog ('график периода: '+$_.Exception.Message)
        }
        Refresh-ChartNow
    }
}

function Refresh-ChartNow {
    try {
        Update-EnergyChart $chartScreen (Get-Date).ToString('yyyy-MM-dd')
    } catch { }
}

. (Join-Path $PSScriptRoot 'MetricHistory.ps1')
function Update-EnergyChart($ch, [string]$date) {
    if ($null -ne $chartHist -and [object]::ReferenceEquals($ch, $chartHist)) {
        # выбор часа живёт в объекте состояния; графику он передаётся через Tag
        try { Sync-HistChartSelection } catch { }
        Update-MetricHistory $ch $date
        try { Sync-HistChartSelection } catch { }
        return
    }
    try {
        if ($null -eq $ch) { return }
        $mode = [string]$script:chartMode
        if ($mode -eq '') { $mode = 'day' }
        foreach ($name in @('Энергия, Вт·ч', 'День, Вт·ч', 'Ночь, Вт·ч', 'Напряжение, В', 'Температура, °C', 'Вчера, В')) {
            $s = $ch.Series[$name]
            if ($null -ne $s) { $s.Points.Clear() }
        }
        $ch.Series['Энергия, Вт·ч'].IsVisibleInLegend = ($mode -eq 'day')
        $ch.Series['Напряжение, В'].IsVisibleInLegend = $true
        $ch.Series['Температура, °C'].IsVisibleInLegend = $true
        $ch.Series['Вчера, В'].IsVisibleInLegend = ($mode -eq 'day')
        $sDay = $ch.Series['День, Вт·ч']; $sNo = $ch.Series['Ночь, Вт·ч']
        $sDay.IsVisibleInLegend = ($mode -ne 'day'); $sNo.IsVisibleInLegend = ($mode -ne 'day')

        if ($mode -eq 'day') {
            $ch.Titles.Clear()
            $rows = @()
            if ($script:histCache -and $script:histCache.ContainsKey($date)) { $rows = @($script:histCache[$date]) }
            $map = @{}
            foreach ($r in $rows) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }

            # вчера — для сравнения
            $prevDate = ([datetime]$date).AddDays(-1).ToString('yyyy-MM-dd')
            $prevRows = @()
            $prevMap = @{}
            if ($script:histCache -and $script:histCache.ContainsKey($prevDate)) {
                $prevRows = @($script:histCache[$prevDate])
                foreach ($r in $prevRows) { if ($null -ne $r.h) { $prevMap[[int]$r.h] = $r } }
            }

            # 24 ячейки всегда на месте. null — пустая точка (пробел на графике),
            # 0 — валидная точка: линию через пропуск не тянем, ложный ноль не рисуем.
            $anyK = $false
            for ($h = 0; $h -le 23; $h++) {
                $r = if ($map.ContainsKey($h)) { $map[$h] } else { $null }
                $hasR = ($null -ne $r -and [int](Field $r @('n') 0) -gt 0)
                $rawK = if ($hasR) { Field $r @('kwh') } else { $null }
                $rawV = if ($hasR) { Field $r @('v') } else { $null }
                $rawT = if ($hasR) { Field $r @('t') } else { $null }
                if ($null -ne $rawK) { $anyK = $true }
                $p = $ch.Series['Энергия, Вт·ч'].Points.AddXY($h, $(if ($null -ne $rawK) { [Math]::Round([double]$rawK * 1000, 0) } else { 0 }))
                $ch.Series['Энергия, Вт·ч'].Points[$p].IsEmpty = ($null -eq $rawK)
                $p = $ch.Series['Напряжение, В'].Points.AddXY($h, $(if ($null -ne $rawV) { [Math]::Round([double]$rawV, 1) } else { 0 }))
                $ch.Series['Напряжение, В'].Points[$p].IsEmpty = ($null -eq $rawV)
                $p = $ch.Series['Температура, °C'].Points.AddXY($h, $(if ($null -ne $rawT) { [Math]::Round([double]$rawT, 1) } else { 0 }))
                $ch.Series['Температура, °C'].Points[$p].IsEmpty = ($null -eq $rawT)
                $pr = if ($prevMap.ContainsKey($h)) { $prevMap[$h] } else { $null }
                $rawPV = if ($null -ne $pr -and [int](Field $pr @('n') 0) -gt 0) { Field $pr @('v') } else { $null }
                $p = $ch.Series['Вчера, В'].Points.AddXY($h, $(if ($null -ne $rawPV) { [Math]::Round([double]$rawPV, 1) } else { 0 }))
                $ch.Series['Вчера, В'].Points[$p].IsEmpty = ($null -eq $rawPV)
            }
            if (-not $anyK) {
                $ttl = New-Object System.Windows.Forms.DataVisualization.Charting.Title
                $ttl.Text = 'Нет измерений за этот день'
                $ttl.Font = Fnt 'Segoe UI' 9 $false
                $ttl.ForeColor = C $script:CLR.warn
                $ttl.Alignment = 'TopLeft'
                $ttl.DockedToChartArea = 'main'
                $ch.Titles.Add($ttl)
            }
            $ch.ChartAreas['main'].AxisX.Interval = 3
            $ch.ChartAreas['main'].AxisX.Minimum = 0
            $ch.ChartAreas['main'].AxisX.Maximum = 24
            $ch.ChartAreas['temp'].AxisX.Minimum = 0
            $ch.ChartAreas['temp'].AxisX.Maximum = 24
            $ch.ChartAreas['main'].AxisY.Title = 'Вт·ч'
        } else {
            # неделя / месяц: столбики по дням, день и ночь разными цветами
            $days = if ($mode -eq 'week') { 7 } else { 30 }
            $from = (Get-Date).AddDays(-($days - 1)).ToString('yyyy-MM-dd')
            $to = (Get-Date).ToString('yyyy-MM-dd')
            $key = $mode + $from
            if (-not $script:chartRangeCache) { $script:chartRangeCache = @{} }
            if (-not $script:chartRangeAt) { $script:chartRangeAt = @{} }
            if (-not $script:chartRangeCache.ContainsKey($key) -or -not $script:chartRangeAt.ContainsKey($key) -or ((Get-Date)-$script:chartRangeAt[$key]).TotalSeconds -ge 60) {
                Start-ChartRange $key $from $to
            }
            if (-not $script:chartRangeCache.ContainsKey($key)) {
                # НЕ грузим /range синхронно в таймере: плата может думать 30+ секунд,
                # из-за этого таймер переполнял стек. Ставим задачу и показываем «загрузка…».
                Start-ChartRange $key $from $to
                $ttl = New-Object System.Windows.Forms.DataVisualization.Charting.Title
                $ttl.Text = if($script:chartRangeError -and $script:chartRangeError.ContainsKey($key)){'Нет связи · повтор через несколько секунд'}else{'Загружаю данные за период…'}
                $ttl.Font = Fnt 'Segoe UI' 9 $false
                $ttl.ForeColor = C $script:CLR.dim
                $ttl.Alignment = 'TopLeft'
                $ttl.DockedToChartArea = 'main'
                $ch.Titles.Clear()
                $ch.Titles.Add($ttl)
                return
            }
            $ch.Titles.Clear()
            $rows = @($script:chartRangeCache[$key])
            if ($script:chartRangeError -and $script:chartRangeError.ContainsKey($key)) {
                $ttl = New-Object System.Windows.Forms.DataVisualization.Charting.Title
                $ttl.Text = 'Не удалось обновить · показаны сохранённые данные'
                $ttl.ForeColor = C $script:CLR.warn
                $ch.Titles.Add($ttl)
            }
            if ($rows.Count -eq 0) {
                $ttl = New-Object System.Windows.Forms.DataVisualization.Charting.Title
                $ttl.Text = 'За выбранный период нет измерений'
                $ttl.Font = Fnt 'Segoe UI' 9 $false
                $ttl.ForeColor = C $script:CLR.warn
                $ttl.Alignment = 'TopLeft'
                $ttl.DockedToChartArea = 'main'
                $ch.Titles.Add($ttl)
                return
            }
            $i = 0
            foreach ($r in $rows) {
                $dd = [string](Field $r @('d') '')
                $lbl = if ($dd.Length -ge 10) { $dd.Substring(8, 2) + '.' + $dd.Substring(5, 2) } else { $dd }
                # dayKwh/nightKwh: null — пробел (пустая точка), 0 — валидный ноль
                foreach ($pair in @(@($sDay, 'dayKwh'), @($sNo, 'nightKwh'))) {
                    $ser = $pair[0]
                    $rawD = Field $r @([string]$pair[1])
                    if ($null -ne $rawD) {
                        $idx = $ser.Points.AddXY($i, [Math]::Round([double]$rawD * 1000, 0))
                    } else {
                        $idx = $ser.Points.AddXY($i, 0)
                        $ser.Points[$idx].IsEmpty = $true
                    }
                    $ser.Points[$idx].AxisLabel = $lbl
                }
                # Period API stores extrema, not an arithmetic mean; null — пробел
                $vminR = Field $r @('vmin'); $vmaxR = Field $r @('vmax')
                if ($null -ne $vminR -and $null -ne $vmaxR -and (Field $r @('vvalid') $true)) {
                    $vmin = [double]$vminR; $vmax = [double]$vmaxR
                    $idx = $ch.Series['Напряжение, В'].Points.AddXY($i, [Math]::Round(($vmin + $vmax) / 2, 1))
                    $ch.Series['Напряжение, В'].Points[$idx].ToolTip = "$lbl · $vmin–$vmax В · середина диапазона"
                } else {
                    $idx = $ch.Series['Напряжение, В'].Points.AddXY($i, 0)
                    $ch.Series['Напряжение, В'].Points[$idx].IsEmpty = $true
                }
                $tminR = Field $r @('tmin'); $tmaxR = Field $r @('tmax')
                $tvalid = Field $r @('tvalid') ($null -ne $tminR -and $null -ne $tmaxR)
                if ($tvalid -and $null -ne $tminR -and $null -ne $tmaxR -and [int](Field $r @('rows') 0) -gt 0) {
                    $tmin = [double]$tminR; $tmax = [double]$tmaxR
                    $idx = $ch.Series['Температура, °C'].Points.AddXY($i, [Math]::Round(($tmin + $tmax) / 2, 1))
                    $ch.Series['Температура, °C'].Points[$idx].ToolTip = "$lbl · $tmin–$tmax °C · середина диапазона"
                } else {
                    $idx = $ch.Series['Температура, °C'].Points.AddXY($i, 0)
                    $ch.Series['Температура, °C'].Points[$idx].IsEmpty = $true
                }
                $i++
            }
            $ch.ChartAreas['main'].AxisX.Interval = if ($mode -eq 'week') { 1 } else { 5 }
            $ch.ChartAreas['main'].AxisX.Minimum = 0
            $ch.ChartAreas['main'].AxisX.Maximum = [Math]::Max(1, $i)
            $ch.ChartAreas['temp'].AxisX.Minimum = 0
            $ch.ChartAreas['temp'].AxisX.Maximum = [Math]::Max(1, $i)
            $ch.ChartAreas['main'].AxisY.Title = 'Вт·ч'
        }

        # Если валидных точек нет вообще (все IsEmpty), авто-шкала Chart бросает
        # InvalidOperationException «Оси области диаграммы - не указаны min/max».
        # Задаём фиксированные границы явно — так пустой день рисуется пустым.
        $anyY = $false
        foreach ($nm in @('Энергия, Вт·ч', 'День, Вт·ч', 'Ночь, Вт·ч')) {
            if ($anyY) { break }
            if (-not $ch.Series[$nm]) { continue }
            foreach ($pt in $ch.Series[$nm].Points) { if (-not $pt.IsEmpty) { $anyY = $true; break } }
        }
        if (-not $anyY) {
            $nPts = $ch.Series['Энергия, Вт·ч'].Points.Count
            $ch.ChartAreas['main'].AxisX.Minimum = 0
            $ch.ChartAreas['main'].AxisX.Maximum = [Math]::Max(1, $nPts - 1)
            $ch.ChartAreas['main'].AxisY.Minimum = 0
            $ch.ChartAreas['main'].AxisY.Maximum = 1
        }

        # шкалы напряжения и температуры — по фактическим точкам, 0 не считаем «пусто»
        $vSet = $false; $vlo = 0.0; $vhi = 0.0
        foreach ($name in @('Напряжение, В', 'Вчера, В')) {
            foreach ($pt in $ch.Series[$name].Points) {
                if ($pt.IsEmpty) { continue }
                $y = [double]$pt.YValues[0]
                if (-not $vSet) { $vlo = $y; $vhi = $y; $vSet = $true }
                else {
                    if ($y -lt $vlo) { $vlo = $y }
                    if ($y -gt $vhi) { $vhi = $y }
                }
            }
        }
        if (-not $vSet) { $vlo = 210; $vhi = 250 }
        $lo = [Math]::Max(0, [Math]::Floor($vlo) - 5)
        $hi = [Math]::Min(300, [Math]::Ceiling($vhi) + 5)
        if ($hi -le $lo) { $hi = $lo + 10 }
        $ch.ChartAreas['main'].AxisY2.Minimum = $lo
        $ch.ChartAreas['main'].AxisY2.Maximum = $hi

        $tSet = $false; $tm = 0.0; $tx = 0.0
        foreach ($pt in $ch.Series['Температура, °C'].Points) {
            if ($pt.IsEmpty) { continue }
            $y = [double]$pt.YValues[0]
            if (-not $tSet) { $tm = $y; $tx = $y; $tSet = $true }
            else {
                if ($y -lt $tm) { $tm = $y }
                if ($y -gt $tx) { $tx = $y }
            }
        }
        if (-not $tSet) { $tm = 15; $tx = 25 }
        if (($tx - $tm) -lt 0.5) { $tm = $tm - 1; $tx = $tx + 1 }
        $ch.ChartAreas['temp'].AxisY.Minimum = [Math]::Floor($tm) - 2
        $ch.ChartAreas['temp'].AxisY.Maximum = [Math]::Ceiling($tx) + 2
    } catch { Write-AppLog ('chart: ' + $_.Exception.Message) }
}

function Draw-Tooltip($g, [string[]]$lines, [int]$x, [int]$y, [int]$w, [int]$h) {

    try {

        if ($null -eq $g -or $null -eq $lines -or $lines.Count -eq 0) { return }

        $D = $script:draw

        $padX = 9; $padY = 7; $step = 3

        $tw = 0.0; $th = $padY * 2.0

        for ($i = 0; $i -lt $lines.Count; $i++) {

            $f = if ($i -eq 0) { $D.fontVal } else { $D.fontAxis }

            $sz = $g.MeasureString([string]$lines[$i], $f)

            if ($sz.Width -gt $tw) { $tw = $sz.Width }

            $th += $sz.Height + $step

        }

        $tw = [int][Math]::Ceiling($tw) + $padX * 2

        $th = [int][Math]::Ceiling($th - $step)

        $tx = $x + 14

        if (($tx + $tw) -gt ($w - 4)) { $tx = $x - [int]($tw / 2) }

        if (($tx + $tw) -gt ($w - 4)) { $tx = $w - $tw - 4 }

        if ($tx -lt 4) { $tx = 4 }

        $ty = $y - $th - 14

        if ($ty -lt 4) { $ty = $y + 18 }

        if (($ty + $th) -gt ($h - 4)) { $ty = $h - $th - 4 }

        if ($ty -lt 4) { $ty = 4 }

        $rect = New-Object System.Drawing.Rectangle -ArgumentList $tx, $ty, $tw, $th

        $g.FillRectangle($D.brTipBg, $rect)

        $g.DrawRectangle($D.penTip, $rect)

        $yy = [float]($ty + $padY)

        for ($i = 0; $i -lt $lines.Count; $i++) {

            $f = if ($i -eq 0) { $D.fontVal } else { $D.fontAxis }

            $br = if ($i -eq 0) { $D.brTipText } else { $D.brTipDim }

            $g.DrawString([string]$lines[$i], $f, $br, [float]($tx + $padX), $yy)

            $yy += $g.MeasureString([string]$lines[$i], $f).Height + $step

        }

    } catch { }

}



# карта «час → запись» для выбранной даты


function Get-SparkValues([string]$metric) {
    # 24 значения по часам за сегодня. $null = нет измерения (пробел на графике),
    # 0 = валидная точка (например, 0 °C или 0 Вт в простое). null к нулю не приводим.
    $date = (Get-Date).ToString('yyyy-MM-dd')
    $out = New-Object 'object[]' 24
    for ($i = 0; $i -lt 24; $i++) { $out[$i] = $null }
    if (-not $script:histCache) { return ,$out }
    if (-not $script:histCache.ContainsKey($date)) { return ,$out }
    foreach ($r in @($script:histCache[$date])) {
        if ($null -eq $r.h) { continue }
        $hh = [int]$r.h
        if ($hh -lt 0 -or $hh -gt 23) { continue }
        $raw = $null
        switch ($metric) {
            'v' { $raw = Field $r @('v') }
            'p' { $raw = Field $r @('p') }
            't' { $raw = Field $r @('t') }
            'e' { $raw = Field $r @('kwh') }
        }
        if ($null -ne $raw) { $out[$hh] = [double]$raw } else { $out[$hh] = $null }
    }
    return ,$out
}

function Draw-Sparkline($g, $pic, [string]$metric) {
    try {
        if ($null -eq $g -or $null -eq $pic) { return }
        # наведение: индекс часа из MouseMove (кладёт сам PictureBox)
        $hover = -1
        if ($pic.Tag -and $script:sparkHover -and $script:sparkHover.ContainsKey($pic)) { $hover = [int]$script:sparkHover[$pic] }
        $D = $script:draw
        $w = [int]$pic.Width; $h = [int]$pic.Height
        if ($w -lt 20 -or $h -lt 10) { return }
        $vals = Get-SparkValues $metric

        $pen = $D.penSparkE
        $fill = $script:CLR.money
        switch ($metric) {
            'v' { $pen = $D.penSparkV; $fill = $script:CLR.accent }
            'p' { $pen = $D.penSparkP; $fill = $script:CLR.power }
            't' { $pen = $D.penSparkT; $fill = $script:CLR.temp }
            'e' { $pen = $D.penSparkE; $fill = $script:CLR.money }
        }

        $n = 0
        $min = 1.0e12; $max = -1.0e12
        for ($i = 0; $i -lt 24; $i++) {
            $vv = $vals[$i]
            if ($null -eq $vv) { continue }   # null — нет измерения, не ноль
            $n++
            if ($vv -lt $min) { $min = $vv }
            if ($vv -gt $max) { $max = $vv }
        }

        if ($n -eq 0) {
            $g.DrawString('нет измерений за сегодня', $D.fontSmall, $D.brDim2, 0, [float](($h - 12) / 2))
            return
        }

        if (($max - $min) -lt 0.001) { $min = $min - 0.5; $max = $max + 0.5 }

        # 0 — валидная точка; null — разрыв: линию через пропуск не тянем
        $pad = 2
        $runs = New-Object 'System.Collections.Generic.List[object]'
        $run = $null
        for ($i = 0; $i -lt 24; $i++) {
            $vv = $vals[$i]
            if ($null -eq $vv) { $run = $null; continue }
            $px = [float]($pad + ($w - 2 * $pad) * $i / 23.0)
            $py = [float]($h - $pad - ($h - 2 * $pad) * (($vv - $min) / ($max - $min)))
            if ($null -eq $run) {
                $run = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
                $runs.Add($run)
            }
            $run.Add([System.Drawing.PointF]::new([single]$px, [single]$py))
        }
        if ($runs.Count -eq 0) { return }

        # сглаженная линия + градиентная заливка под ней (каждый непрерывный участок)
        foreach ($seg in $runs) {
            if ($seg.Count -ge 2) { Draw-Curve $g $seg $pen $fill ([float]($h - 1)) }
            $last = $seg[$seg.Count - 1]
            $g.FillEllipse($pen.Brush, [float]($last.X - 2), [float]($last.Y - 2), 4, 4)
        }

        # подписи шкалы не рисуем: значения есть строкой выше, а точное — в подсказке при наведении

        # вертикальная линия и точка под курсором + подсказка
        if ($hover -ge 0 -and $hover -lt 24) {
            $hv = $vals[$hover]
            $hx = [float]($pad + ($w - 2 * $pad) * $hover / 23.0)
            $g.DrawLine($D.penCursor, $hx, 0, $hx, $h)
            if ($null -ne $hv) {
                $hy = [float]($h - $pad - ($h - 2 * $pad) * (($hv - $min) / ($max - $min)))
                $g.FillEllipse($pen.Brush, [float]($hx - 2.5), [float]($hy - 2.5), 5, 5)
                $unit = switch ($metric) { 'v' { ' В' } 'p' { ' Вт' } 't' { ' °C' } 'e' { ' кВт·ч' } default { '' } }
                $lines = @(('{0:0.0}{1}' -f $hv, $unit), ('{0:00}:00' -f $hover))
                Draw-Tooltip $g $lines ([int]$hx) 0 $w $h
            } else {
                Draw-Tooltip $g @('нет измерений', ('{0:00}:00' -f $hover)) ([int]$hx) 0 $w $h
            }
        }

    } catch { Write-AppLog ('spark: ошибка ' + $_.Exception.Message) }
}



# клик по карточке: «История» с выбранным показателем

function Open-CardHistory([string]$metric) {

    if (-not $script:cardMetric.Contains($metric)) { return }

    $script:histMetric = $metric
    try { $script:histSelect.Metric = $metric } catch { }

    if (-not $script:histSel) { $script:histSel = (Get-Date).ToString('yyyy-MM-dd') }

    Request-Day $script:histSel

    Show-Page 'hist'

    Update-HistoryView

    Add-Log ('карточка «' + $script:cardSparkTitle[$metric] + '» → вкладка «История»')

}



# ---------------------------------------------------------------- график суток на экране
$script:chartMode = 'day'
$chartScreen = New-EnergyChart
$chartScreen.Location = New-Object System.Drawing.Point(14, 300)
$chartScreen.Size = New-Object System.Drawing.Size(1000, 380)
$pgScreen.Controls.Add($chartScreen)

$script:chartBtns = @{}
foreach ($spec in @(@('day','День'), @('week','7 дней'), @('month','30 дней'))) {
    $bb = New-Btn $pgScreen $spec[1] (SkinC '#1B2230') $script:CLR.text 74 24
    $bb.Tag = $spec[0]
    $script:chartBtns[$spec[0]] = $bb
    $bb.Add_Click({
        param($s, $e)
        $script:chartMode = [string]$s.Tag
        foreach ($k in $script:chartBtns.Keys) {
            $b = $script:chartBtns[$k]
            if ($k -eq $script:chartMode) { $b.BackColor = C (SkinC '#24405A') } else { $b.BackColor = C (SkinC '#1B2230') }
        }
        try { Add-Log ('график: режим ' + $script:chartMode) } catch { }
        Update-EnergyChart $chartScreen (Get-Date).ToString('yyyy-MM-dd')
    })
    $pgScreen.Controls.Add($bb)
}
foreach ($k in $script:chartBtns.Keys) { if ($k -eq $script:chartMode) { $script:chartBtns[$k].BackColor = C (SkinC '#24405A') } }

$btnChartPng = New-Btn $pgScreen 'PNG' (SkinC '#1B2230') $script:CLR.text 58 24
$btnChartPng.Add_Click({
    try {
        $dlg = New-Object System.Windows.Forms.SaveFileDialog
        $dlg.Filter = 'PNG картинка (*.png)|*.png'
        $dlg.FileName = 'dom-energy-' + (Get-Date).ToString('yyyy-MM-dd') + '.png'
        if ($dlg.ShowDialog() -eq 'OK') {
            $chartScreen.SaveImage($dlg.FileName, [System.Windows.Forms.DataVisualization.Charting.ChartImageFormat]::Png)
            Add-Log ('график сохранён: ' + $dlg.FileName) 'ok'
        }
    } catch { Add-Log ('PNG: ' + $_.Exception.Message) 'err' }
})
$pgScreen.Controls.Add($btnChartPng)


function Layout-Screen {
    if (-not $script:uiReady) { return }
    $pw = $pgScreen.ClientSize.Width
    $ph = $pgScreen.ClientSize.Height
    if ($pw -lt 500 -or $ph -lt 380) { return }

    $m = 14
    $lcdW = 356
    $lcdWrap.Location = New-Object System.Drawing.Point($m, 12)
    $lcdWrap.Size = New-Object System.Drawing.Size($lcdW, 278)
    $lcd.Size = New-Object System.Drawing.Size(178, 237)
    $lblLcdHint.Location = New-Object System.Drawing.Point(2, 258)

    $cx = $m + $lcdW + $m
    $cw = $pw - $cx - $m
    $gap = 10
    $cardH = 150
    $cardW = [int](($cw - 2 * $gap) / 3)
    if ($cardW -lt 120) { $cardW = 120 }

    $i = 0
    foreach ($k in @($script:cards.Keys)) {
        $c = $script:cards[$k]
        $col = $i % 3
        $row = [Math]::Floor($i / 3)
        $px = $cx + $col * ($cardW + $gap)
        $py = $m + $row * ($cardH + $gap)
        $c.Panel.Size = New-Object System.Drawing.Size($cardW, $cardH)
        $c.Panel.Location = New-Object System.Drawing.Point($px, $py)
        if ($null -ne $c.Shadow) {
            $c.Shadow.Size = New-Object System.Drawing.Size($cardW, $cardH)
            $c.Shadow.Location = New-Object System.Drawing.Point(($px + 2), ($py + 2))
        }
        $c.Val.Width = $cardW - 24
        $c.Sub.Width = $cardW - 24
        $c.When.Width = $cardW - 24
        if ($null -ne $c.Spark) {
            $c.Spark.Location = New-Object System.Drawing.Point(12, ($cardH - 34))
            $c.Spark.Size = New-Object System.Drawing.Size(($cardW - 24), 26)
        }
        $i++
    }

    $chartTop = $m + 3 * $cardH + 2 * $gap + $m + 30
    $chartH = $ph - $chartTop - $m
    if ($script:chartBtns) {
        $bx = $pw - $m - 58 - 6
        $btnChartPng.Location = New-Object System.Drawing.Point($bx, ($chartTop - 28))
        $bx -= (74 + 6)
        foreach ($k in @('month','week','day')) {
            $script:chartBtns[$k].Location = New-Object System.Drawing.Point($bx, ($chartTop - 28))
            $bx -= (74 + 6)
        }
    }
    if ($chartH -lt 110) { $chartH = 110 }
    $chartScreen.Location = New-Object System.Drawing.Point($m, $chartTop)
    $chartScreen.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $chartH)
    $chartScreen.Invalidate()
}



# ----------------------------------------------------------------- график дня

$chartScreen.Add_Paint({
    param($sender, $e)
    # теперь рисует библиотека Chart, старый GDI+ код не нужен
    if ($sender -is [System.Windows.Forms.DataVisualization.Charting.Chart]) { return }
    try {

        $g = $e.Graphics

        $g.SmoothingMode = 'AntiAlias'

        $g.TextRenderingHint = 'ClearTypeGridFit'

        $w = $sender.Width

        $h = $sender.Height

        $D = $script:draw



        $padL = 44; $padR = 16; $padT = 52; $padB = 28

        $plotW = $w - $padL - $padR

        $plotH = $h - $padT - $padB

        if ($plotW -lt 80 -or $plotH -lt 40) { return }



        $today = (Get-Date).ToString('yyyy-MM-dd')

        $dayLabel = (Get-Date).ToString('dd.MM')

        $g.DrawString("НАПРЯЖЕНИЕ ЗА СУТКИ · $dayLabel", $D.fontTitle, $D.brText, [float]$padL, 10)

        $g.DrawString('В', $D.fontAxis, $D.brDim, 10, [float]($padT - 26))



        # собираем часовые точки

        $hours = $null

        if ($script:histCache.ContainsKey($today)) { $hours = @($script:histCache[$today]) }

        $map = @{}

        foreach ($r in $hours) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }



        $live = $null

        if ($null -ne $script:board -and (Get-BoardAge) -le 12) { $live = [double](Field $script:board @('v') 0) }



        $pts = New-Object System.Collections.ArrayList

        for ($hh = 0; $hh -lt 24; $hh++) {

            if (-not $map.ContainsKey($hh)) { continue }

            $vv = [double](Field $map[$hh] @('v') 0)

            if ($vv -le 0) { continue }

            [void]$pts.Add([pscustomobject]@{ x = ($hh + 0.5); v = $vv })

        }



        $now = Get-Date

        $nowX = $now.Hour + $now.Minute / 60.0

        if ($null -ne $live -and $live -gt 0) {

            [void]$pts.Add([pscustomobject]@{ x = $nowX; v = $live })

        }



        if ($pts.Count -eq 0) {

            $msg = 'история за сегодня недоступна — плата не отвечает'

            if ($null -ne $live -and $live -gt 0) { $msg = 'график появится, когда плата отдаст /history' }

            $g.DrawString($msg, $D.fontAxis, $D.brDim, [float]($padL + 8), [float]($padT + 8))

            return

        }



        $minV = 9999.0; $maxV = 0.0; $sumV = 0.0

        foreach ($p in $pts) {

            if ($p.v -lt $minV) { $minV = $p.v }

            if ($p.v -gt $maxV) { $maxV = $p.v }

            $sumV += $p.v

        }

        $avgV = $sumV / $pts.Count

        if (($maxV - $minV) -lt 8) { $mid = ($maxV + $minV) / 2; $minV = $mid - 4; $maxV = $mid + 4 }

        $lo = [Math]::Floor(($minV - 2) / 5) * 5

        $hi = [Math]::Ceiling(($maxV + 2) / 5) * 5

        if (($hi - $lo) -lt 10) { $hi = $lo + 10 }



        $xOf = { param($hr) $padL + $plotW * ($hr / 24.0) }

        $yOf = { param($v) $padT + $plotH - $plotH * (($v - $lo) / ($hi - $lo)) }



        # пропуски часов — янтарные полосы

        $gapsToday = $null

        if ($script:daysGaps.ContainsKey($today)) { $gapsToday = $script:daysGaps[$today] }

        $nowH = (Get-Date).Hour
        for ($hh = 0; $hh -lt 24; $hh++) {

            # будущие часы не закрашиваем: это ещё не пропуск
            if ($hh -gt $nowH) { continue }

            $has = $map.ContainsKey($hh) -and ([double](Field $map[$hh] @('v') 0)) -gt 0

            if (-not $has) {

                $x1 = & $xOf $hh

                $x2 = & $xOf ($hh + 1)

                $g.FillRectangle($D.brGap, [float]$x1, [float]$padT, [float]($x2 - $x1), [float]$plotH)

            }

        }



        # горизонтальная сетка и подписи вольт

        for ($i = 0; $i -le 4; $i++) {

            $vv = $lo + ($hi - $lo) * $i / 4.0

            $yy = & $yOf $vv

            $g.DrawLine($D.penGrid, [float]$padL, [float]$yy, [float]($padL + $plotW), [float]$yy)

            $lbl = ([int][Math]::Round($vv)).ToString()

            $sz = $g.MeasureString($lbl, $D.fontAxis)

            $g.DrawString($lbl, $D.fontAxis, $D.brDim, [float]($padL - 8 - $sz.Width), [float]($yy - $sz.Height / 2))

        }



        # вертикальная сетка и подписи часов

        for ($hh = 0; $hh -le 24; $hh += 3) {

            $xx = & $xOf $hh

            $g.DrawLine($D.penGrid, [float]$xx, [float]$padT, [float]$xx, [float]($padT + $plotH))

            $lbl = if ($hh -eq 24) { '24' } else { $hh.ToString('00') }

            $sz = $g.MeasureString($lbl, $D.fontAxis)

            $g.DrawString($lbl, $D.fontAxis, $D.brDim, [float]($xx - $sz.Width / 2), [float]($padT + $plotH + 6))

        }



        # линия напряжения: сглаженная кривая (AddCurve) + заливка под ней
        $sorted = @($pts | Sort-Object x)
        $seg = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
        $prev = $null
        foreach ($p in $sorted) {
            if ($null -ne $prev -and ($p.x - $prev.x) -ge 6) {
                Draw-Curve $g $seg $D.penVolt $script:CLR.accent ([float]($padT + $plotH))
                $seg = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            }
            $seg.Add([System.Drawing.PointF]::new([single](& $xOf $p.x), [single](& $yOf $p.v)))
            $prev = $p
        }
        Draw-Curve $g $seg $D.penVolt $script:CLR.accent ([float]($padT + $plotH))

        # точка «сейчас»

        if ($null -ne $live -and $live -gt 0) {

            $lx = & $xOf $nowX

            $ly = & $yOf $live

            $g.DrawLine($D.penNow, [float]$lx, [float]$padT, [float]$lx, [float]($padT + $plotH))

            $g.FillEllipse($D.brAccent, [float]($lx - 4), [float]($ly - 4), 8, 8)

            $txtNow = 'сейчас ' + (Num $live '0') + ' В'

            $sz = $g.MeasureString($txtNow, $D.fontVal)

            $tx = $lx + 8

            if (($tx + $sz.Width) -gt ($padL + $plotW)) { $tx = $lx - 8 - $sz.Width }

            $g.DrawString($txtNow, $D.fontVal, $D.brAccent, [float]$tx, [float]($ly - 18))

        }



        # --- температура второй линией (своя шкала справа) ---
        $tPts = New-Object System.Collections.ArrayList
        for ($hh = 0; $hh -lt 24; $hh++) {
            if (-not $map.ContainsKey($hh)) { continue }
            $tv = [double](Field $map[$hh] @('t') 0)
            if ($tv -le 0.1 -or $tv -lt -40 -or $tv -gt 120) { continue }   # 0 = нет данных
            [void]$tPts.Add([pscustomobject]@{ x = ($hh + 0.5); v = $tv })
        }
        $liveT = $null
        if ($null -ne $script:board -and (Get-BoardAge) -le 12) { $liveT = [double](Field $script:board @('temp') 0) }
        if ($null -ne $liveT -and $liveT -gt -40) { [void]$tPts.Add([pscustomobject]@{ x = $nowX; v = $liveT }) }

        if ($tPts.Count -ge 2) {
            $tmin = 999.0; $tmax = -999.0
            foreach ($p in $tPts) { if ($p.v -lt $tmin) { $tmin = $p.v }; if ($p.v -gt $tmax) { $tmax = $p.v } }
            if (($tmax - $tmin) -lt 4) { $mid = ($tmax + $tmin) / 2; $tmin = $mid - 2; $tmax = $mid + 2 }
            $yOfT = {
                param($val)
                $padT + $plotH * (1 - (($val - $tmin) / ($tmax - $tmin)))
            }
            $tSorted = @($tPts | Sort-Object x)
            $tseg = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            foreach ($p in $tSorted) {
                $tseg.Add([System.Drawing.PointF]::new([single](& $xOf $p.x), [single](& $yOfT $p.v)))
            }
            $colT = [System.Drawing.ColorTranslator]::FromHtml([string]$script:CLR.temp)
            $penT = New-Object System.Drawing.Pen ($colT, [float]1.8)
            $penT.DashStyle = 'Dash'
            $g.DrawLines($penT, $tseg.ToArray())
            $penT.Dispose()

            # подписи правой шкалы: °C
            $g.DrawString('°C', $D.fontAxis, $D.brDim, [float]($w - $padR + 2), [float]($padT - 26))
            $g.DrawString(('{0:0}' -f $tmax), $D.fontAxis, $D.brDim, [float]($w - $padR + 2), [float]$padT)
            $g.DrawString(('{0:0}' -f $tmin), $D.fontAxis, $D.brDim, [float]($w - $padR + 2), [float]($padT + $plotH - 12))

            # легенда под заголовком, отдельной строкой
            $g.DrawString('— напряжение, В', $D.fontAxis, $D.brDim, [float]($padL + 8), 32)
            $g.DrawString('--- температура, °C', $D.fontAxis, $D.brTemp, [float]($padL + 120), 32)
        }

        # подпись справа: мин / сред / макс

        $stat = 'мин ' + (Num $minV '0') + ' · сред ' + (Num $avgV '0') + ' · макс ' + (Num $maxV '0') + ' В'

        $sz2 = $g.MeasureString($stat, $D.fontAxis)

        $g.DrawString($stat, $D.fontAxis, $D.brDim, [float]($padL + $plotW - $sz2.Width), 13)



        # пометка о пропусках

        if ($null -ne $gapsToday -and [int]$gapsToday[0] -gt 0) {

            $gm = [int]$gapsToday[1]

            $gt = 'пропуски: ' + [int]$gapsToday[0] + ' (' + (Format-Age ($gm * 60)) + ')'

            $g.DrawString($gt, $D.fontAxis, $D.brWarn, [float]$padL, 44)

        } elseif ($null -ne $gapsToday) {

            $g.DrawString('пропусков нет — данные за сутки полные', $D.fontAxis, $D.brOk, [float]$padL, 44)

        }



        # --- интерактив: вертикальная линия, точка на кривой и подсказка «чч:мм · значение»
        $hhTip = $script:hoverScr
        if ($hhTip -lt 0) { $hhTip = $script:pinScr }
        if ($hhTip -ge 0 -and $hhTip -le 23) {
            $x1 = & $xOf $hhTip
            $x2 = & $xOf ($hhTip + 1)
            $cx = ($x1 + $x2) / 2.0

            # лёгкая подсветка часа и вертикальная линия под курсором
            $g.FillRectangle($D.brHover, [float]$x1, [float]$padT, [float]($x2 - $x1), [float]$plotH)
            $g.DrawLine($D.penCursor, [float]$cx, [float]$padT, [float]$cx, [float]($padT + $plotH))

            # значение этого часа — точкой на кривой и подписью «чч:мм · значение»
            $vTip = 0.0
            $hasTip = $false
            if ($map.ContainsKey($hhTip)) {
                $vTip = [double](Field $map[$hhTip] @('v') 0)
                if ($vTip -gt 0) { $hasTip = $true }
            } elseif ($hhTip -eq (Get-Date).Hour -and $live -gt 0) {
                $vTip = $live
                $hasTip = $true
            }
            if ($hasTip) {
                $cy = & $yOf $vTip
                $g.FillEllipse($D.brAccent, [float]($cx - 4), [float]($cy - 4), 8, 8)
                $lblTip = ('{0:00}:00 · {1} В' -f $hhTip, (Num $vTip '0'))
                $szTip = $g.MeasureString($lblTip, $D.fontVal)
                $txTip = $cx + 10
                if (($txTip + $szTip.Width) -gt ($padL + $plotW)) { $txTip = $cx - 10 - $szTip.Width }
                $g.DrawString($lblTip, $D.fontVal, $D.brAccent, [float]$txTip, [float]($cy - 20))
            }

            $tipLines = Get-ScreenHourLines $hhTip
            # при наведении — у курсора, при закреплённом клике — у выбранного часа
            $mx = if ($script:hoverScr -ge 0 -and $script:mouseScr.X -ge 0) { [int]$script:mouseScr.X } else { [int]($cx + 14) }
            $my = if ($script:hoverScr -ge 0 -and $script:mouseScr.Y -ge 0) { [int]$script:mouseScr.Y } else { [int]($padT + 70) }
            Draw-Tooltip $g $tipLines $mx $my $w $h
        }

    } catch {

        try { Write-AppLog ('ошибка отрисовки графика: ' + $_.Exception.Message) } catch { }

    }

})



# --- мышь на графике «Экран»: час по X, подсказка, клик закрепляет -----------

function Get-ScreenChartHour([int]$x, [int]$y, [int]$w, [int]$h) {

    $padL = 44; $padR = 16; $padT = 52; $padB = 28

    $plotW = $w - $padL - $padR

    $plotH = $h - $padT - $padB

    if ($plotW -lt 80 -or $plotH -lt 40) { return -1 }

    if ($x -lt $padL -or $x -gt ($padL + $plotW)) { return -1 }

    if ($y -lt ($padT - 10) -or $y -gt ($padT + $plotH + 22)) { return -1 }

    $hr = [int][Math]::Floor((($x - $padL) / [double]$plotW) * 24.0)

    if ($hr -lt 0) { $hr = 0 }

    if ($hr -gt 23) { $hr = 23 }

    return $hr

}



$chartScreen.Add_MouseMove({

    param($sender, $e)

    $hr = Get-ScreenChartHour $e.X $e.Y $sender.Width $sender.Height

    $script:mouseScr = New-Object System.Drawing.Point -ArgumentList $e.X, $e.Y

    if ($hr -ne $script:hoverScr) {

        $script:hoverScr = $hr

        $sender.Invalidate()

    }

})



$chartScreen.Add_MouseDown({
    param($sender, $e)
    try {
        $hit = $sender.HitTest($e.X, $e.Y)
        if ($null -ne $hit -and (($hit.Object -is [System.Windows.Forms.DataVisualization.Charting.DataPoint]) -or ($null -ne $hit.ChartArea -and [string]$hit.ChartElementType -in @('PlottingArea','Gridlines','StripLines')))) {
            $hv = if($hit.Object -is [System.Windows.Forms.DataVisualization.Charting.DataPoint]){[int][Math]::Round([double]$hit.Object.XValue)}else{[int][Math]::Round($hit.ChartArea.AxisX.PixelPositionToValue($e.X))}
            $day = (Get-Date).ToString('yyyy-MM-dd')
            if($script:chartMode -ne 'day') {
                $days=if($script:chartMode -eq 'week'){7}else{30}
                $key=$script:chartMode+(Get-Date).AddDays(-($days-1)).ToString('yyyy-MM-dd')
                $rangeRows=@($script:chartRangeCache[$key])
                if($hv -lt 0 -or $hv -ge $rangeRows.Count){return}
                $day=[string]$rangeRows[$hv].d
                $hv=-1
            }
            Set-HistDay $day
            Show-Page 'hist'
            Update-HistoryView
            # выбранный час — объект состояния; пустая ячейка выбирается так же
            if($hv -ge 0){ Set-HistSelectHour $day $hv }
            Add-Log ('график: ' + $day + ' → вкладка «История»')
            return
        }
    } catch { }

    $hr = Get-ScreenChartHour $e.X $e.Y $sender.Width $sender.Height
    if ($hr -ge 0) {
        $script:pinScr = if ($script:pinScr -eq $hr) { -1 } else { $hr }
        Add-Log ('график напряжения: выбран час ' + ('{0:00}:00' -f $hr))
        $sender.Invalidate()
    } else {
        $script:pinScr = -1
        $sender.Invalidate()
    }
})

$chartScreen.Add_MouseLeave({

    param($sender, $e)

    if ($script:hoverScr -ne -1) {

        $script:hoverScr = -1

        $script:mouseScr = New-Object System.Drawing.Point -ArgumentList -1, -1

        $sender.Invalidate()

    }

})



function New-Grid($parent, [int]$rowH) {

    $grid = New-Object System.Windows.Forms.DataGridView

    $grid.BackgroundColor = C $script:CLR.panel

    $grid.BorderStyle = 'None'

    $grid.EnableHeadersVisualStyles = $false

    $grid.ColumnHeadersDefaultCellStyle.BackColor = C $script:CLR.card2

    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C $script:CLR.text

    $grid.ColumnHeadersDefaultCellStyle.SelectionBackColor = C $script:CLR.card2

    $grid.ColumnHeadersDefaultCellStyle.Font = Fnt 'Segoe UI' 8.5 $true

    $grid.ColumnHeadersHeight = 28

    $grid.DefaultCellStyle.BackColor = C $script:CLR.panel

    $grid.DefaultCellStyle.ForeColor = C $script:CLR.text

    $grid.DefaultCellStyle.SelectionBackColor = C $script:CLR.card2

    $grid.DefaultCellStyle.SelectionForeColor = C $script:CLR.text

    $grid.AlternatingRowsDefaultCellStyle.BackColor = C $script:CLR.card

    $grid.GridColor = C $script:CLR.line

    $grid.RowHeadersVisible = $false

    $grid.AllowUserToAddRows = $false

    $grid.AllowUserToDeleteRows = $false

    $grid.AllowUserToResizeRows = $false

    $grid.ReadOnly = $true

    $grid.MultiSelect = $false

    $grid.RowTemplate.Height = $rowH

    $grid.AutoSizeColumnsMode = 'Fill'

    if ($null -ne $parent) { $parent.Controls.Add($grid) }

    return $grid

}



# ============================================================================

#  ВКЛАДКА «ИСТОРИЯ»

# ============================================================================

$pgHist = New-TabPage

Add-Page 'hist' $pgHist



$btnPrev    = New-Btn $pgHist '‹' '#2A3240' $script:CLR.text 40 30

$btnNext    = New-Btn $pgHist '›' '#2A3240' $script:CLR.text 40 30

$btnToday   = New-Btn $pgHist 'Сегодня'  '#24405A' $script:CLR.text 96 30

$btnReload  = New-Btn $pgHist '⟳ Обновить' '#2A3240' $script:CLR.text 110 30

$btnRange   = New-Btn $pgHist 'Период'  '#24405A' $script:CLR.text 96 30

$btnRange.Add_Click({ Show-Range })




$lblHistDay = New-Object System.Windows.Forms.Label

$lblHistDay.Text = '—'

$lblHistDay.Font = Fnt 'Segoe UI' 11 $true

$lblHistDay.ForeColor = C $script:CLR.text

$lblHistDay.BackColor = C $script:CLR.bg

$lblHistDay.AutoSize = $false

$lblHistDay.TextAlign = 'MiddleLeft'

$lblHistDay.Size = New-Object System.Drawing.Size(214, 30)

$lblHistDay.Location = New-Object System.Drawing.Point(62, 12)

$pgHist.Controls.Add($lblHistDay)



$lblHistSum = New-Object System.Windows.Forms.Label

$lblHistSum.Text = 'загрузка истории…'

$lblHistSum.Font = Fnt 'Segoe UI' 8.5 $false

$lblHistSum.ForeColor = C $script:CLR.dim

$lblHistSum.BackColor = C $script:CLR.bg

$lblHistSum.AutoSize = $false

$lblHistSum.TextAlign = 'MiddleRight'

$lblHistSum.Size = New-Object System.Drawing.Size(520, 30)

$pgHist.Controls.Add($lblHistSum)



$gridHist = New-Grid $pgHist 22

foreach ($col in @(

    @('h',   'Час',        60),

    @('kwh', 'кВт·ч',      70),

    @('p',   'Средняя Вт', 90),

    @('pmax','Пик Вт',     80),

    @('pmaxT','Время пика', 76),

    @('pmaxDurT','Длит.',   56),

    @('v',   'Вольт',      70),

    @('t',   'Темп',       60),

    @('n',   'Записей',    70)

)) {

    $null = $gridHist.Columns.Add($col[0], $col[1])

    $gridHist.Columns[$col[0]].FillWeight = $col[2]

}



$chartHist = New-EnergyChart

$chartHist.BackColor = C $script:CLR.bg

$pgHist.Controls.Add($chartHist)

$script:lblMetricSelected = New-Lbl $pgHist 'Выберите час на графике' 9 $script:CLR.text $true 14 76
$script:lblMetricSelected.AutoSize = $false
$script:lblMetricSelected.AutoEllipsis = $true



$btnHistTg = New-Btn $pgHist 'Отчёт в Telegram' '#24405A' $script:CLR.text 160 30

$btnHistCsv = New-Btn $pgHist '⤓ CSV дня' '#2A4034' $script:CLR.text 110 30



$chartHist.Add_Paint({
    param($sender, $e)
    # рисует библиотека Chart
    if ($sender -is [System.Windows.Forms.DataVisualization.Charting.Chart]) { return }

    param($sender, $e)

    try {

        $g = $e.Graphics

        $g.SmoothingMode = 'AntiAlias'

        $g.TextRenderingHint = 'ClearTypeGridFit'

        $w = $sender.Width

        $h = $sender.Height

        $D = $script:draw



        $padL = 44; $padR = 52; $padT = 46; $padB = 28

        $plotW = $w - $padL - $padR

        $plotH = $h - $padT - $padB

        if ($plotW -lt 80 -or $plotH -lt 40) { return }



        $date = $script:histSel

        if ($script:histPinDate -ne $date) {

            $script:histPinDate = $date

            $script:pinHist = -1

            $script:hoverHist = -1

        }



        # показатель выбирается кликом по карточке (v/p/t/e)

        $metric = [string]$script:histMetric

        $names = @{ v = 'НАПРЯЖЕНИЕ'; p = 'МОЩНОСТЬ'; t = 'ТЕМПЕРАТУРА'; e = 'РАСХОД' }

        if (-not $names.ContainsKey($metric)) { $metric = 'p' }

        $title = $names[$metric] + ' · ' + $date.Substring(8, 2) + '.' + $date.Substring(5, 2)

        $g.DrawString($title, $D.fontTitle, $D.brText, [float]$padL, 10)



        $barKwh = ($metric -eq 'e')

        $legend1 = 'столбики — ' + $(if ($barKwh) { 'кВт·ч' } else { 'Вт' })

        $legend2 = 'линия — В'

        $legend3 = 'линия — °C'

        $g.DrawString($legend1, $D.fontAxis, $(if ($barKwh -or $metric -eq 'p') { $D.brAccent } else { $D.brDim }), [float]$padL, 27)

        $g.DrawString($legend2, $D.fontAxis, $(if ($metric -eq 'v') { $D.brAccent } else { $D.brVolt }), [float]($padL + 120), 27)

        $g.DrawString($legend3, $D.fontAxis, $(if ($metric -eq 't') { $D.brTemp } else { $D.brDim }), [float]($padL + 215), 27)



        $hours = $null

        if ($script:histCache.ContainsKey($date)) { $hours = @($script:histCache[$date]) }

        if ($null -eq $hours -or $hours.Count -eq 0) {

            $msg = 'данных за этот день нет'

            if ($script:histErrDate -eq $date -and $script:histErr) { $msg = $script:histErr }

            elseif (Test-HistLoading $date) { $msg = 'загрузка истории…' }

            $g.DrawString($msg, $D.fontAxis, $D.brDim, [float]($padL + 8), [float]($padT + 8))

            return

        }



        $map = @{}

        foreach ($r in $hours) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }



        $maxBar = 0.0

        $vMin = 9999.0; $vMax = 0.0

        $tMin = 9999.0; $tMax = -9999.0

        $hasTemp = $false

        for ($hh = 0; $hh -lt 24; $hh++) {

            if (-not $map.ContainsKey($hh)) { continue }

            $bv = if ($barKwh) { [double](Field $map[$hh] @('kwh') 0) } else { [double](Field $map[$hh] @('p') 0) }

            if ($bv -gt $maxBar) { $maxBar = $bv }

            $vm = [double](Field $map[$hh] @('v') 0)

            if ($vm -gt 0) {

                if ($vm -lt $vMin) { $vMin = $vm }

                if ($vm -gt $vMax) { $vMax = $vm }

            }

            $tm = Field $map[$hh] @('t')

            if ($null -ne $tm) {

                $tf = [double]$tm

                $hasTemp = $true

                if ($tf -lt $tMin) { $tMin = $tf }

                if ($tf -gt $tMax) { $tMax = $tf }

            }

        }

        if ($maxBar -le 0) { $maxBar = if ($barKwh) { 0.1 } else { 100 } }

        if ($barKwh) { $maxBar = [Math]::Ceiling($maxBar * 115) / 100 }

        else { $maxBar = [Math]::Ceiling(($maxBar * 1.15) / 50) * 50 }



        $hasVolt = ($vMax -gt 0)

        if ($hasVolt) {

            if (($vMax - $vMin) -lt 8) { $mid = ($vMax + $vMin) / 2; $vMin = $mid - 4; $vMax = $mid + 4 }

            $vLo = [Math]::Floor(($vMin - 2) / 5) * 5

            $vHi = [Math]::Ceiling(($vMax + 2) / 5) * 5

            if (($vHi - $vLo) -lt 10) { $vHi = $vLo + 10 }

        }

        if ($hasTemp) {

            if (($tMax - $tMin) -lt 4) { $midT = ($tMax + $tMin) / 2; $tMin = $midT - 2; $tMax = $midT + 2 }

            else { $padT2 = ($tMax - $tMin) * 0.15; $tMin -= $padT2; $tMax += $padT2 }

        }



        $slot = $plotW / 24.0

        $barW = [Math]::Max(3, $slot * 0.62)



        # вертикальная сетка и часы

        for ($hh = 0; $hh -le 24; $hh += 2) {

            $xx = [float]($padL + $slot * $hh)

            $g.DrawLine($D.penGrid, $xx, [float]$padT, $xx, [float]($padT + $plotH))

            if ($hh -lt 24) {

                $lbl = $hh.ToString('00')

                $sz = $g.MeasureString($lbl, $D.fontAxis)

                $g.DrawString($lbl, $D.fontAxis, $D.brDim, [float]($xx + $slot / 2 - $sz.Width / 2), [float]($padT + $plotH + 6))

            }

        }



        # левая шкала — ватты или кВт·ч

        for ($i = 0; $i -le 4; $i++) {

            $vv = $maxBar * $i / 4.0

            $yy = [float]($padT + $plotH - $plotH * $i / 4.0)

            $g.DrawLine($D.penGrid, [float]$padL, $yy, [float]($padL + $plotW), $yy)

            $lbl = if ($barKwh) { Num $vv '0.00' } else { ([int][Math]::Round($vv)).ToString() }

            $sz = $g.MeasureString($lbl, $D.fontAxis)

            $g.DrawString($lbl, $D.fontAxis, $D.brDim, [float]($padL - 6 - $sz.Width), [float]($yy - $sz.Height / 2))

        }



        # правая шкала — вольты

        if ($hasVolt) {

            for ($i = 0; $i -le 4; $i++) {

                $vv = $vLo + ($vHi - $vLo) * $i / 4.0

                $yy = [float]($padT + $plotH - $plotH * $i / 4.0)

                $lbl = ([int][Math]::Round($vv)).ToString()

                $g.DrawString($lbl, $D.fontAxis, $D.brVolt, [float]($padL + $plotW + 6), [float]($yy - 6))

            }

        }



        # столбики

        $barsDim = ($metric -ne 'p' -and $metric -ne 'e')

        for ($hh = 0; $hh -lt 24; $hh++) {

            if (-not $map.ContainsKey($hh)) { continue }

            $bv = if ($barKwh) { [double](Field $map[$hh] @('kwh') 0) } else { [double](Field $map[$hh] @('p') 0) }

            if ($bv -le 0) { continue }

            $bh = [float]($plotH * $bv / $maxBar)

            if ($bh -lt 1) { $bh = 1 }

            $xx = [float]($padL + $slot * $hh + ($slot - $barW) / 2)

            $by = [float]($padT + $plotH - $bh)

            $br = $D.brBar

            if ($barsDim) { $br = $D.brBarDim }

            elseif ($bv -ge ($maxBar * 0.45)) { $br = $D.brBarHi }

            $g.FillRectangle($br, $xx, $by, [float]$barW, $bh)

        }



        # линия напряжения: сглаженная кривая, у выбранного показателя — с заливкой
        if ($hasVolt) {
            $vPen = $D.penViolet
            if ($metric -eq 'v') { $vPen = $D.penVoltHi }
            elseif ($metric -eq 't' -or $metric -eq 'e') { $vPen = $D.penVioletDim }

            $fillV = $null
            if ($metric -eq 'v') { $fillV = $script:CLR.accent }

            $run = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $prevX = -999.0
            for ($hh = 0; $hh -lt 24; $hh++) {
                $okH = $map.ContainsKey($hh)
                $vm = 0.0
                if ($okH) { $vm = [double](Field $map[$hh] @('v') 0) }
                if (-not $okH -or $vm -le 0) {
                    if ($run.Count -ge 2) { Draw-Curve $g $run $vPen $fillV ([float]($padT + $plotH)) }
                    $run = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
                    $prevX = -999.0
                    continue
                }
                $px = [float]($padL + $slot * ($hh + 0.5))
                if ($run.Count -gt 0 -and ($px - $prevX) -gt ($slot * 1.6)) {
                    if ($run.Count -ge 2) { Draw-Curve $g $run $vPen $fillV ([float]($padT + $plotH)) }
                    $run = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
                }
                $py = [float]($padT + $plotH - $plotH * (($vm - $vLo) / ($vHi - $vLo)))
                $run.Add([System.Drawing.PointF]::new([single]$px, [single]$py))
                $g.FillEllipse($D.brVolt, [float]($px - 2), [float]($py - 2), 4, 4)
                $prevX = $px
            }
            if ($run.Count -ge 2) { Draw-Curve $g $run $vPen $fillV ([float]($padT + $plotH)) }
        }

        # линия температуры (своя шкала, подписи — в легенде и подсказке)
        if ($hasTemp) {
            $tPen = $D.penTemp
            if ($metric -eq 't') { $tPen = $D.penTempHi }
            elseif ($metric -eq 'v' -or $metric -eq 'e') { $tPen = $D.penTempDim }

            $fillT = $null
            if ($metric -eq 't') { $fillT = $script:CLR.temp }

            $runT = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $prevT = -999.0
            for ($hh = 0; $hh -lt 24; $hh++) {
                $tm = $null
                if ($map.ContainsKey($hh)) { $tm = Field $map[$hh] @('t') }
                if ($null -eq $tm) {
                    if ($runT.Count -ge 2) { Draw-Curve $g $runT $tPen $fillT ([float]($padT + $plotH)) }
                    $runT = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
                    $prevT = -999.0
                    continue
                }
                $tf = [double]$tm
                $px = [float]($padL + $slot * ($hh + 0.5))
                if ($runT.Count -gt 0 -and ($px - $prevT) -gt ($slot * 1.6)) {
                    if ($runT.Count -ge 2) { Draw-Curve $g $runT $tPen $fillT ([float]($padT + $plotH)) }
                    $runT = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
                }
                $py = [float]($padT + $plotH - $plotH * (($tf - $tMin) / ($tMax - $tMin)))
                $runT.Add([System.Drawing.PointF]::new([single]$px, [single]$py))
                $g.FillEllipse($D.brTemp, [float]($px - 2), [float]($py - 2), 4, 4)
                $prevT = $px
            }
            if ($runT.Count -ge 2) { Draw-Curve $g $runT $tPen $fillT ([float]($padT + $plotH)) }
        }

        # --- интерактив: подсветка столбика и подсказка

        $hhTip = $script:hoverHist

        if ($hhTip -lt 0) { $hhTip = $script:pinHist }

        if ($hhTip -ge 0 -and $hhTip -le 23) {

            $x1 = [float]($padL + $slot * $hhTip)

            $x2 = [float]($x1 + $slot)

            $g.FillRectangle($D.brHover, $x1, [float]$padT, [float]($x2 - $x1), [float]$plotH)

            $g.DrawLine($D.penHover, $x1, [float]$padT, $x1, [float]($padT + $plotH))

            $g.DrawLine($D.penHover, $x2, [float]$padT, $x2, [float]($padT + $plotH))

            $tipLines = Get-HistHourLines $date $hhTip

            $mx = if ($script:hoverHist -ge 0 -and $script:mouseHist.X -ge 0) { [int]$script:mouseHist.X } else { [int]($x1 + $slot / 2) }

            $my = if ($script:hoverHist -ge 0 -and $script:mouseHist.Y -ge 0) { [int]$script:mouseHist.Y } else { [int]($padT + $plotH / 2) }

            Draw-Tooltip $g $tipLines $mx $my $w $h

        }

    } catch {

        try { Write-AppLog ('ошибка отрисовки истории: ' + $_.Exception.Message) } catch { }

    }

})



# --- мышь на графике «История»: столбик по X, подсказка, клик закрепляет ------

function Get-HistChartHour([int]$x, [int]$y, [int]$w, [int]$h) {

    $padL = 44; $padR = 52; $padT = 46; $padB = 28

    $plotW = $w - $padL - $padR

    $plotH = $h - $padT - $padB

    if ($plotW -lt 80 -or $plotH -lt 40) { return -1 }

    if ($x -lt $padL -or $x -gt ($padL + $plotW)) { return -1 }

    if ($y -lt ($padT - 10) -or $y -gt ($padT + $plotH + 22)) { return -1 }

    $slot = $plotW / 24.0

    $hh = [int][Math]::Floor(($x - $padL) / $slot)

    if ($hh -lt 0) { $hh = 0 }

    if ($hh -gt 23) { $hh = 23 }

    return $hh

}



# строки подсказки для столбика истории (с пометкой незакончившегося часа)

# «112 записей», «64 записи», «1 запись»

function Format-Records([int]$n) {

    $m10 = $n % 10

    $m100 = $n % 100

    if ($m10 -eq 1 -and $m100 -ne 11) { return ($n.ToString() + ' запись') }

    if ($m10 -ge 2 -and $m10 -le 4 -and ($m100 -lt 12 -or $m100 -gt 14)) { return ($n.ToString() + ' записи') }

    return ($n.ToString() + ' записей')

}



function Get-HistHourLines([string]$date, [int]$hour) {
    # Строки подсказки для столбика истории. null-поле — «нет измерений»,
    # а не 0: раньше отсутствующая мощность/кВт·ч печаталась нулём.
    $hh = [Math]::Max(0, [Math]::Min(23, $hour))
    $head = ('{0:00}:00–{1:00}:00' -f $hh, ($hh + 1))
    $now = Get-Date
    $partial = ($date -eq $now.ToString('yyyy-MM-dd') -and $hh -eq $now.Hour)
    $map = @{}
    if ($script:histCache -and $script:histCache.ContainsKey($date)) {
        foreach ($r in @($script:histCache[$date])) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }
    }
    if (-not $map.ContainsKey($hh)) {
        if ($partial) { $head += ' (час не закончился)' }
        return @($head, 'нет измерений за этот час')
    }
    $r = $map[$hh]
    $n = [int](Field $r @('n') 0)
    if ($n -le 0) {
        if ($partial) { $head += ' (час не закончился)' }
        return @($head, ('нет измерений · ' + (Format-Records $n)))
    }
    $rawK = Field $r @('kwh')
    $rawP = Field $r @('p')
    $rawPm = Field $r @('pmax')
    $pkT = Format-PeakTime $r
    $pkD = Format-PeakDur $r

    $parts = @()
    if ($null -ne $rawK) { $parts += ((Num $rawK '0.00') + ' кВт·ч') } else { $parts += 'кВт·ч: нет измерений' }
    if ($null -ne $rawP) { $parts += ('средняя ' + (Num $rawP '0') + ' Вт') } else { $parts += 'средняя: нет измерений' }
    if ($null -ne $rawPm) {
        $pkTxt = (Num $rawPm '0') + ' Вт'
        if ($pkT -ne '—') { $pkTxt += ' в ' + $pkT }
        if ($pkD -ne '—') { $pkTxt += ', ' + $pkD }
        $parts += ('пик ' + $pkTxt)
    } else {
        $parts += 'пик: нет измерений'
    }
    $parts += (Format-Records $n)
    $vals = $parts -join ' · '
    if ($partial) { return @(($head + ' (час не закончился)'), $vals) }
    return @(($head + ' · ' + ($(if ($null -ne $rawK) { (Num $rawK '0.00') + ' кВт·ч' } else { 'нет измерений' }))), $vals)
}

$chartHist.Add_MouseMove({

    $hr = Get-HistChartHour $e.X $e.Y $sender.Width $sender.Height

    $script:mouseHist = New-Object System.Drawing.Point -ArgumentList $e.X, $e.Y

    if ($hr -ne $script:hoverHist) {

        $script:hoverHist = $hr

        $sender.Invalidate()

    }

})



$chartHist.Add_MouseDown({
    param($sender, $e)
    try {
        # легенда: скрыть/показать показатель — выбор часа при этом не сбрасываем
        $hit = $sender.HitTest($e.X, $e.Y)
        if ($null -ne $hit -and $hit.Object -is [System.Windows.Forms.DataVisualization.Charting.LegendItem]) {
            $name = [string]$hit.Object.SeriesName
            if ($name -and $null -ne $sender.Series[$name]) {
                if (-not $script:metricHidden) { $script:metricHidden = @{} }
                $script:metricHidden[$name] = -not $script:metricHidden[$name]
                Update-MetricHistory $sender $script:histSel
                try { Sync-HistChartSelection } catch { }
                return
            }
        }
        # час: столбик или пустое место графика — выбирается любая ячейка 0..23
        $hour = $null
        if ($null -ne $hit -and $hit.Object -is [System.Windows.Forms.DataVisualization.Charting.DataPoint]) {
            $hour = [int][Math]::Round([double]$hit.Object.XValue)
        } elseif ($null -ne $hit -and $null -ne $hit.ChartArea -and [string]$hit.ChartElementType -in @('PlottingArea','Gridlines','StripLines')) {
            $hour = [int][Math]::Round($hit.ChartArea.AxisX.PixelPositionToValue($e.X))
        }
        if ($null -eq $hour) {
            # запасной путь: иногда HitTest не отдаёт область построения
            try { $hour = [int][Math]::Round($sender.ChartAreas['combined'].AxisX.PixelPositionToValue($e.X)) } catch { $hour = $null }
        }
        if ($null -eq $hour -or $hour -lt 0 -or $hour -gt 23) { return }
        Set-HistSelectHour $script:histSel $hour
        Add-Log ('история: выбран час ' + ('{0:00}:00–{1:00}:00' -f $hour, ($hour + 1)))
    } catch { Write-AppLog ('выбор часа: ' + $_.Exception.Message) }
})

# клик по строке таблицы истории тоже выбирает час (та же объекта состояния)
$gridHist.Add_CellClick({
    param($sender, $e)
    try {
        if ($null -eq $e) { return }
        if ($e.RowIndex -lt 0 -or $e.RowIndex -gt 23) { return }
        Set-HistSelectHour $script:histSel $e.RowIndex
    } catch { }
})

$chartHist.Add_MouseLeave({

    param($sender, $e)

    if ($script:hoverHist -ne -1) {

        $script:hoverHist = -1

        $script:mouseHist = New-Object System.Drawing.Point -ArgumentList -1, -1

        $sender.Invalidate()

    }

})



function Layout-History {

    if (-not $script:uiReady) { return }

    $pw = $pgHist.ClientSize.Width

    $ph = $pgHist.ClientSize.Height

    if ($pw -lt 520 -or $ph -lt 380) { return }



    $m = 14

    $btnPrev.Location   = New-Object System.Drawing.Point($m, 12)

    $lblHistDay.Location = New-Object System.Drawing.Point(($m + 48), 12)

    $btnNext.Location   = New-Object System.Drawing.Point(($m + 268), 12)

    $btnToday.Location  = New-Object System.Drawing.Point(($m + 316), 12)

    $btnReload.Location = New-Object System.Drawing.Point(($m + 420), 12)

$btnRange.Location  = New-Object System.Drawing.Point(($m + 604), 12)

    # «Отчёт в Telegram» и «CSV дня» прижимаем к правому краю: на узком окне они обрезались
    $btnHistCsv.Location = New-Object System.Drawing.Point(($pw - $m - $btnHistCsv.Width), 12)

    $btnHistTg.Location = New-Object System.Drawing.Point(($pw - $m - $btnHistCsv.Width - 8 - 160), 12)

    $lblHistSum.TextAlign = 'MiddleLeft'

    $lblHistSum.Size = New-Object System.Drawing.Size(($pw - 2 * $m), 22)

    $lblHistSum.Location = New-Object System.Drawing.Point($m, 50)

    $script:lblMetricSelected.Location = New-Object System.Drawing.Point($m, 76)
    $script:lblMetricSelected.Size = New-Object System.Drawing.Size(($pw - 2 * $m), 22)



    $top2 = 105

    $availH = $ph - $top2 - $m



    if ($pw -ge 1020) {

        $tableW = [int](($pw - 3 * $m) * 0.37)

        $gridHist.Location = New-Object System.Drawing.Point($m, $top2)

        $gridHist.Size = New-Object System.Drawing.Size($tableW, $availH)

        $chartHist.Location = New-Object System.Drawing.Point(($m + $tableW + $m), $top2)

        $chartHist.Size = New-Object System.Drawing.Size(($pw - $tableW - 3 * $m), $availH)

    } else {

        $tableH = [int]($availH * 0.38)

        $gridHist.Location = New-Object System.Drawing.Point($m, $top2)

        $gridHist.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $tableH)

        $chartHist.Location = New-Object System.Drawing.Point($m, ($top2 + $tableH + $m))

        $chartHist.Size = New-Object System.Drawing.Size(($pw - 2 * $m), ($availH - $tableH - $m))

    }

    Update-EnergyChart $chartHist $script:histSel

}



# ============================================================================

#  ВКЛАДКА «УСТРОЙСТВО»

# ============================================================================

$pgDev = New-TabPage

Add-Page 'dev' $pgDev



$gridDev = New-Grid $pgDev 23

$null = $gridDev.Columns.Add('prop', 'Свойство')

$null = $gridDev.Columns.Add('val', 'Значение')

$gridDev.Columns[0].FillWeight = 38

$gridDev.Columns[1].FillWeight = 62



$lblPort = New-Lbl $pgDev 'Порт платы:' 9 $script:CLR.dim $false 14 0

$cmbPort = New-Object System.Windows.Forms.ComboBox

$cmbPort.DropDownStyle = 'DropDownList'

$cmbPort.FlatStyle = 'Flat'

$cmbPort.BackColor = C $script:CLR.card

$cmbPort.ForeColor = C $script:CLR.text

$cmbPort.Font = Fnt 'Segoe UI' 9 $false

$cmbPort.Size = New-Object System.Drawing.Size(100, 26)

$pgDev.Controls.Add($cmbPort)



$btnPortApply = New-Btn $pgDev 'Применить' '#24405A' $script:CLR.text 100 28

$btnPortScan  = New-Btn $pgDev 'Список'    '#2A3240' $script:CLR.text 84 28



$lblBoardCap = New-Lbl $pgDev 'Адрес платы:' 9 $script:CLR.dim $false 14 0

$txtBoard = New-Object System.Windows.Forms.TextBox

$txtBoard.Text = $script:cfg.board

$txtBoard.BackColor = C $script:CLR.card

$txtBoard.ForeColor = C $script:CLR.text

$txtBoard.BorderStyle = 'FixedSingle'

$txtBoard.Font = Fnt 'Consolas' 9 $false

$txtBoard.Size = New-Object System.Drawing.Size(210, 24)

$pgDev.Controls.Add($txtBoard)



$btnBoardApply = New-Btn $pgDev 'Сменить адрес' '#24405A' $script:CLR.text 124 28



# состояние связи с платой: «плата не отвечает: 192.168.1.51» + кнопка мастера

$lblBoardState = New-Lbl $pgDev '● адрес платы не настроен' 9 $script:CLR.dim2 $false 470 0



function Layout-Device {

    if (-not $script:uiReady) { return }

    $pw = $pgDev.ClientSize.Width

    $ph = $pgDev.ClientSize.Height

    if ($pw -lt 400 -or $ph -lt 300) { return }

    $m = 14

    $gridDev.Location = New-Object System.Drawing.Point($m, $m)

    $gridDev.Size = New-Object System.Drawing.Size(($pw - 2 * $m), ($ph - $m - 92))



    $y1 = $ph - 76

    $lblPort.Location = New-Object System.Drawing.Point($m, ($y1 + 5))

    $cmbPort.Location = New-Object System.Drawing.Point(($m + 100), $y1)

    $btnPortApply.Location = New-Object System.Drawing.Point(($m + 210), ($y1 - 1))

    $btnPortScan.Location = New-Object System.Drawing.Point(($m + 318), ($y1 - 1))



    $y2 = $ph - 40

    $lblBoardCap.Location = New-Object System.Drawing.Point($m, ($y2 + 5))

    $txtBoard.Location = New-Object System.Drawing.Point(($m + 100), $y2)

    $btnBoardApply.Location = New-Object System.Drawing.Point(($m + 320), ($y2 - 2))

    if ($null -ne $lblBoardState) {

        $lblBoardState.Location = New-Object System.Drawing.Point(($m + 470), ($y2 + 5))

    }



    # «Мастер настройки» — единственный вход в мастер при готовых настройках

    if ($null -ne $script:btnWizard) {

        $script:btnWizard.Location = New-Object System.Drawing.Point(($pw - $m - $script:btnWizard.Width), ($y2 - 2))

    }

}



# ============================================================================

#  ВКЛАДКА «ПРОШИВКА»

# ============================================================================

$pgFlash = New-TabPage

Add-Page 'flash' $pgFlash



$lblSketch = New-Lbl $pgFlash 'Скетч:' 9 $script:CLR.dim $false 14 18



# Список прошивок: CYD-4.4 (основная) и старый dom-monitor (ESP32-C3, OLED).

# По умолчанию — CYD: именно эта плата стоит в щитке.

$txtSketch = New-Object System.Windows.Forms.ComboBox

$txtSketch.DropDownStyle = 'DropDown'

$txtSketch.FlatStyle = 'Flat'

$txtSketch.Font = Fnt 'Consolas' 9 $false

$txtSketch.BackColor = C (SkinC '#12161C')

$txtSketch.ForeColor = C '#D5DCE7'

[void]$txtSketch.Items.Add($script:DefSketch)

[void]$txtSketch.Items.Add((Join-Path (Split-Path -Parent $script:AppDir) 'dom-monitor'))

$txtSketch.Text = $script:cfg.sketch

$txtSketch.Location = New-Object System.Drawing.Point(66, 14)

$txtSketch.Size = New-Object System.Drawing.Size(600, 24)

$pgFlash.Controls.Add($txtSketch)



$btnBrowse = New-Btn $pgFlash '…' '#2A3240' $script:CLR.text 36 24

$btnBrowse.Location = New-Object System.Drawing.Point(672, 14)



$lblFqbn = New-Lbl $pgFlash 'FQBN:' 9 $script:CLR.dim $false 14 50



$txtFqbn = New-Object System.Windows.Forms.TextBox

$txtFqbn.Text = $script:cfg.fqbn

$txtFqbn.BackColor = C (SkinC '#12161C')

$txtFqbn.ForeColor = C '#9BB4CF'

$txtFqbn.BorderStyle = 'FixedSingle'

$txtFqbn.Font = Fnt 'Consolas' 9 $false

$txtFqbn.Location = New-Object System.Drawing.Point(66, 46)

$txtFqbn.Size = New-Object System.Drawing.Size(600, 24)

$pgFlash.Controls.Add($txtFqbn)



$btnFlash   = New-Btn $pgFlash 'ПРОШИТЬ' '#1F7A46' '#FFFFFF' 176 42

$btnCompile = New-Btn $pgFlash 'Только собрать' '#2E4A6B' '#FFFFFF' 150 42

$btnOta     = New-Btn $pgFlash 'Прошить по Wi-Fi' '#1B5E8C' '#FFFFFF' 168 42



$lblFlashState = New-Lbl $pgFlash 'готово к работе' 9 $script:CLR.dim $false 370 62



$txtFlashLog = New-Object System.Windows.Forms.TextBox

$txtFlashLog.Multiline = $true

$txtFlashLog.ScrollBars = 'Vertical'

$txtFlashLog.ReadOnly = $true

$txtFlashLog.BackColor = C $script:CLR.lcdBg

$txtFlashLog.ForeColor = C $script:CLR.lcdFg

$txtFlashLog.Font = Fnt 'Consolas' 9 $false

$txtFlashLog.BorderStyle = 'FixedSingle'

$txtFlashLog.WordWrap = $false

$pgFlash.Controls.Add($txtFlashLog)



function Layout-Flash {

    if (-not $script:uiReady) { return }

    $pw = $pgFlash.ClientSize.Width

    $ph = $pgFlash.ClientSize.Height

    if ($pw -lt 400 -or $ph -lt 300) { return }

    $m = 14

    $txtSketch.Width = $pw - 2 * $m - 66 - 44

    $txtFqbn.Width = $pw - 2 * $m - 66 - 44

    $btnBrowse.Location = New-Object System.Drawing.Point(($pw - $m - 36), 14)

    $btnFlash.Location = New-Object System.Drawing.Point($m, 82)

    $btnCompile.Location = New-Object System.Drawing.Point(($m + 190), 82)

    $btnOta.Location     = New-Object System.Drawing.Point(($m + 350), 82)

    $lblFlashState.Location = New-Object System.Drawing.Point(($m + 356), 94)

    $txtFlashLog.Location = New-Object System.Drawing.Point($m, 136)

    $txtFlashLog.Size = New-Object System.Drawing.Size(($pw - 2 * $m), ($ph - 136 - $m))

}




# ============================================================================
#  ВКЛАДКА «ЗАЩИТА» — те же уставки, что в веб-морде (автомат Tuya + плата)
# ============================================================================
$pgProt = New-TabPage
Add-Page 'prot' $pgProt

$script:protFields = @{}
$protDefs = @(
    @{ k = 'vmax'; t = 'Перенапряжение, В'; d = '250' },
    @{ k = 'vmin'; t = 'Просадка, В';       d = '200' },
    @{ k = 'tmax'; t = 'Перегрев, °C';      d = '60'  },
    @{ k = 'imax'; t = 'Перегруз, А';       d = '25'  }
)
foreach ($d in $protDefs) {
    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text = $d.t
    $lbl.AutoSize = $true
    $lbl.ForeColor = C $script:CLR.dim
    $lbl.Tag = $d.k
    $pgProt.Controls.Add($lbl)
    $tb = New-Object System.Windows.Forms.TextBox
    $tb.Text = $d.d
    $tb.Width = 80
    $tb.BackColor = C $script:CLR.bg
    $tb.ForeColor = C '#D6E2F5'
    $pgProt.Controls.Add($tb)
    $script:protFields[$d.k + '_lbl'] = $lbl
    $script:protFields[$d.k] = $tb
}

$script:lblProtInfo = New-Object System.Windows.Forms.Label
$script:lblProtInfo.AutoSize = $false
$script:lblProtInfo.ForeColor = C '#9FB2CC'
$pgProt.Controls.Add($script:lblProtInfo)

$btnProtRead  = New-Btn $pgProt 'Прочитать'          '#2E4A6B' '#FFFFFF' 150 32
$btnProtApply = New-Btn $pgProt 'Записать в автомат' '#1F7A46' '#FFFFFF' 200 32
$btnProtLocal = New-Btn $pgProt 'Только в плату'     '#2A3240' $script:CLR.text 170 32

function Layout-Prot {
    if (-not $script:uiReady) { return }
    $w = $pgProt.ClientSize.Width
    $x = 24; $y = 24
    foreach ($k in @('vmax', 'vmin', 'tmax', 'imax')) {
        $lbl = $script:protFields[$k + '_lbl']
        $tb  = $script:protFields[$k]
        $lbl.Location = New-Object System.Drawing.Point($x, ($y + 4))
        $tb.Location  = New-Object System.Drawing.Point(($x + 170), $y)
        $y += 38
    }
    $btnProtRead.Location  = New-Object System.Drawing.Point($x, ($y + 10))
    $btnProtApply.Location = New-Object System.Drawing.Point(($x + 165), ($y + 10))
    $btnProtLocal.Location = New-Object System.Drawing.Point(($x + 380), ($y + 10))
    $script:lblProtInfo.Location = New-Object System.Drawing.Point($x, ($y + 56))
    $script:lblProtInfo.Size = New-Object System.Drawing.Size(([Math]::Max(400, $w - 2 * $x)), 120)
}

function Decode-Prot($b64) {
    try {
        $bytes = [Convert]::FromBase64String($b64)
    } catch { return @() }
    $out = @()
    for ($i = 0; $i + 3 -lt $bytes.Length; $i += 4) {
        $out += [pscustomobject]@{ code = $bytes[$i]; en = $bytes[$i+1]; val = (($bytes[$i+2] -shl 8) -bor $bytes[$i+3]) }
    }
    return $out
}

function Load-Prot {
    $ip = [string]$script:cfg.boardIp
    try {
        $p = Invoke-RestMethod ("http://" + $ip + "/protect") -TimeoutSec 10
        if ($p.vmax) { $script:protFields['vmax'].Text = [string][int]$p.vmax }
        if ($p.vmin) { $script:protFields['vmin'].Text = [string][int]$p.vmin }
        if ($p.tmax) { $script:protFields['tmax'].Text = [string][int]$p.tmax }
        if ($p.pmax) { $script:protFields['imax'].Text = [string][math]::Round([double]$p.pmax / 230) }
        $lines = @()
        $lines += ('состояние: ' + $(if ($p.on -eq 1) { 'включена' } else { 'выключена' }) +
                   ' · отключение реле: ' + $(if ($p.cut -eq 1) { 'разрешено' } else { 'запрещено' }) +
                   ' · срабатываний: ' + $p.trig)
        $a2 = Decode-Prot $p.a2
        $a1 = Decode-Prot $p.a1
        $map2 = @{ 1 = 'ток, А'; 3 = 'U макс, В'; 4 = 'U мин, В'; 8 = 'утечка, мА' }
        $map1 = @{ 3 = 't мин, °C'; 4 = 't ср, °C'; 5 = 't макс, °C' }
        $txt2 = @(); foreach ($r in $a2) { if ($r.en) { $txt2 += (($map2[[int]$r.code]) + ' ' + $r.val) } }
        $txt1 = @(); foreach ($r in $a1) { if ($r.en) { $txt1 += (($map1[[int]$r.code]) + ' ' + $r.val) } }
        $lines += ('в автомате: ' + (($txt2 + $txt1) -join ' · '))
        $script:lblProtInfo.Text = ($lines -join "`r`n")
        Add-Log 'защита: настройки прочитаны с платы' 'ok'
    } catch {
        Add-Log ('защита: не удалось прочитать — ' + $_.Exception.Message) 'err'
    }
}

function Send-Prot([bool]$apply) {
    $ip = [string]$script:cfg.boardIp
    $q = '/protect?' + $(if ($apply) { 'apply=1&' } else { '' }) +
         'vmax=' + $script:protFields['vmax'].Text.Trim() +
         '&vmin=' + $script:protFields['vmin'].Text.Trim() +
         '&tmax=' + $script:protFields['tmax'].Text.Trim() +
         '&imax=' + $script:protFields['imax'].Text.Trim()
    try {
        $r = Invoke-RestMethod ("http://" + $ip + $q) -TimeoutSec 90 -Headers (Get-ControlHeaders)
        if ($apply) {
            $ok = ($r.apply -and ($r.apply.a1 -eq 1 -or $r.apply.a2 -eq 1))
            Add-Log ('защита: записано в автомат — ' + $(if ($ok) { 'успешно' } else { 'не подтверждено' })) $(if ($ok) { 'ok' } else { 'err' })
        } else {
            Add-Log 'защита: записано только в плату' 'ok'
        }
        Load-Prot
    } catch {
        Add-Log ('защита: ошибка записи — ' + $_.Exception.Message) 'err'
    }
}

$btnProtRead.Add_Click({ Load-Prot })
$btnProtApply.Add_Click({
    $res = [System.Windows.Forms.MessageBox]::Show(
        "Записать пороги в автомат Tuya?`r`n`r`nОни будут срабатывать даже при выключенной плате.",
        'Защита', 'YesNo', 'Warning')
    if ($res -eq 'Yes') { Send-Prot $true }
})
$btnProtLocal.Add_Click({ Send-Prot $false })

# ============================================================================

#  ВКЛАДКА «ЖУРНАЛ»

# ============================================================================

$pgLog = New-TabPage

Add-Page 'log' $pgLog



$script:txtLog = New-Object System.Windows.Forms.TextBox

$script:txtLog.Multiline = $true

$script:txtLog.ScrollBars = 'Vertical'

$script:txtLog.ReadOnly = $true

$script:txtLog.BackColor = C (SkinC '#0A0C10')

$script:txtLog.ForeColor = C '#A8C6E8'

$script:txtLog.Font = Fnt 'Consolas' 9 $false

$script:txtLog.BorderStyle = 'FixedSingle'

$script:txtLog.WordWrap = $false

$pgLog.Controls.Add($script:txtLog)



$script:chkRaw = New-Object System.Windows.Forms.CheckBox

$script:chkRaw.Text = 'показывать служебные строки порта'

$script:chkRaw.AutoSize = $true

$script:chkRaw.ForeColor = C $script:CLR.dim

$pgLog.Controls.Add($script:chkRaw)



$btnClearLog = New-Btn $pgLog 'Очистить' '#2A3240' $script:CLR.text 110 28



function Layout-Log {

    if (-not $script:uiReady) { return }

    $pw = $pgLog.ClientSize.Width

    $ph = $pgLog.ClientSize.Height

    if ($pw -lt 400 -or $ph -lt 300) { return }

    $m = 14

    $script:txtLog.Location = New-Object System.Drawing.Point($m, $m)

    $script:txtLog.Size = New-Object System.Drawing.Size(($pw - 2 * $m), ($ph - $m - 52))

    $script:chkRaw.Location = New-Object System.Drawing.Point($m, ($ph - 36))

    $btnClearLog.Location = New-Object System.Drawing.Point(($m + 320), ($ph - 40))

}



# ============================================================================
#  ВКЛАДКА «ТЕРМИНАЛ» — что плата делает прямо сейчас
#  4 блока: сводка /data, события /vlog, облако /clog (с расшифровкой этапа),
#  карта/файлы/загрузки/пропуски (/storage /files /boots /gapfill/status),
#  плюс полный список полей /data ключ=значение.
#  /vlog и /clog — раз в 3 с; файловые ответы — по одному за тик (у платы
#  мало сокетов), полный круг примерно раз в 15 с. Ошибки красным.
# ============================================================================
$pgTerm = New-TabPage
Add-Page 'term' $pgTerm

# --- состояние терминала ----------------------------------------------------
$script:termAt      = [datetime]::MinValue   # когда последний раз читали /vlog и /clog
$script:termTaskV   = $null                  # фоновая задача /vlog
$script:termTaskC   = $null                  # фоновая задача /clog
$script:termStart   = [datetime]::MinValue
$script:termBoardAt = [datetime]::MinValue   # какой ответ /data уже нарисован
$script:termErrSeen = ''
# /boots, /files, /storage, /gapfill/status читаем ПО ОДНОМУ запросу за тик:
# у платы мало сокетов, пачкой её нельзя. Полный круг — примерно 15 секунд.
$script:termAuxAt    = [datetime]::MinValue
$script:termAuxStart = [datetime]::MinValue
$script:termAuxTask  = $null
$script:termAuxName  = ''
$script:termAuxQueue = @()
$script:termAuxRaw   = @{}
$script:termAuxWhen  = @{}
$script:termAuxDirty = $true

$lblTermHead = New-Lbl $pgTerm 'ТЕРМИНАЛ ПЛАТЫ — что плата делает сейчас, события, облако, файлы, загрузки и пропуски. Автообновление раз в 3 секунды.' 9 $script:CLR.dim2 $false 14 10

$btnTermRefresh = New-Btn $pgTerm 'Обновить' '#24405A' $script:CLR.text 120 28

$script:chkTermAuto = New-Object System.Windows.Forms.CheckBox
$script:chkTermAuto.Text = 'автообновление'
$script:chkTermAuto.Checked = $true
$script:chkTermAuto.AutoSize = $true
$script:chkTermAuto.ForeColor = C $script:CLR.dim
$script:chkTermAuto.Font = Fnt 'Segoe UI' 9 $false
$pgTerm.Controls.Add($script:chkTermAuto)

function New-TermBox($parent) {
    $tb = New-Object System.Windows.Forms.RichTextBox
    $tb.ReadOnly = $true
    $tb.BackColor = C (SkinC '#0A0C10')
    $tb.ForeColor = C '#A8C6E8'
    $tb.Font = Fnt 'Consolas' 9 $false
    $tb.BorderStyle = 'FixedSingle'
    $tb.WordWrap = $false
    $tb.ScrollBars = 'Vertical'
    $tb.DetectUrls = $false
    if ($null -ne $parent) { $parent.Controls.Add($tb) }
    return $tb
}

$lblTermNowCap  = New-Lbl $pgTerm 'ЧТО ПЛАТА ДЕЛАЕТ СЕЙЧАС · /data' 8 $script:CLR.dim2 $true 14 0
$lblTermEvCap   = New-Lbl $pgTerm 'СОБЫТИЯ ПЛАТЫ · /vlog' 8 $script:CLR.dim2 $true 14 0
$lblTermClCap   = New-Lbl $pgTerm 'ОБЛАКО · /clog (когда · этап · почему · код HTTP · длина)' 8 $script:CLR.dim2 $true 14 0
$lblTermAuxCap  = New-Lbl $pgTerm 'КАРТА, ФАЙЛЫ, ЗАГРУЗКИ, ПРОПУСКИ · /storage /files /boots /gapfill/status' 8 $script:CLR.dim2 $true 14 0
$lblTermDataCap = New-Lbl $pgTerm 'ВСЕ ПОЛЯ /data (ключ=значение)' 8 $script:CLR.dim2 $true 14 0
$script:txtTermNow = New-TermBox $pgTerm
$script:txtTermEv  = New-TermBox $pgTerm
$script:txtTermCl  = New-TermBox $pgTerm
$script:txtTermAux = New-TermBox $pgTerm
$script:txtTermSt  = New-TermBox $pgTerm
$lblTermStateCap = New-Lbl $pgTerm 'СОСТОЯНИЕ И КАЧЕСТВО ДАННЫХ · плата/LAN · WAN/облако · время · SD · свежесть V/P/I/T' 8 $script:CLR.dim2 $true 14 0
$script:txtTermState = New-TermBox $pgTerm
$script:termStateKey = ''


$lblTermState = New-Lbl $pgTerm 'обновление…' 8 $script:CLR.dim2 $false 14 0
$lblTermState.AutoSize = $false
$lblTermState.Size = New-Object System.Drawing.Size(600, 18)

function Layout-Term {
    if (-not $script:uiReady) { return }
    $pw = $pgTerm.ClientSize.Width
    $ph = $pgTerm.ClientSize.Height
    if ($pw -lt 400 -or $ph -lt 300) { return }
    $m = 14
    $btnW = 120
    $btnTermRefresh.Location = New-Object System.Drawing.Point(($pw - $m - $btnW), 6)
    $script:chkTermAuto.Location = New-Object System.Drawing.Point(($pw - $m - $btnW - 16 - 128), 11)
    $capH = 18
    $stateH = 18
    $nowCapY = 36
    $nowBoxH = 100
    # P0.1: отдельная полоса «Состояние и качество данных» между сводкой
    # /data и журналами. Высота подстраивается: на низком окне панель
    # прячется, чтобы не ломать журналы.
    $stateCapY = $nowCapY + $capH + $nowBoxH + 10
    $stateBoxH = $ph - 448
    if ($stateBoxH -gt 96) { $stateBoxH = 96 }
    if ($stateBoxH -lt 62) { $stateBoxH = 0 }
    $midCapY = if ($stateBoxH -gt 0) { $stateCapY + $capH + $stateBoxH + 10 } else { $stateCapY }
    # нижний ряд: карта/файлы/пропуски слева, все поля /data справа
    $botBoxH = [int](($ph - $midCapY - $capH - 10 - $stateH - 12) * 0.42)
    if ($botBoxH -lt 96) { $botBoxH = 96 }
    $botCapY = $ph - $stateH - 8 - $botBoxH - $capH
    $midBoxH = $botCapY - ($midCapY + $capH) - 10
    if ($midBoxH -lt 70) { $midBoxH = 70 }
    $colW = [int](($pw - 3 * $m) / 2)
    if ($colW -lt 160) { $colW = 160 }
    $col2W = [Math]::Max(160, $pw - 2 * $m - $colW - $m)
    $lblTermNowCap.Location = New-Object System.Drawing.Point($m, $nowCapY)
    $script:txtTermNow.Location = New-Object System.Drawing.Point($m, ($nowCapY + $capH))
    $script:txtTermNow.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $nowBoxH)
    $lblTermEvCap.Location = New-Object System.Drawing.Point($m, $midCapY)
    $lblTermClCap.Location = New-Object System.Drawing.Point(($m + $colW + $m), $midCapY)
    $script:txtTermEv.Location = New-Object System.Drawing.Point($m, ($midCapY + $capH))
    $script:txtTermEv.Size = New-Object System.Drawing.Size($colW, $midBoxH)
    $script:txtTermCl.Location = New-Object System.Drawing.Point(($m + $colW + $m), ($midCapY + $capH))
    $script:txtTermCl.Size = New-Object System.Drawing.Size($col2W, $midBoxH)
    $lblTermAuxCap.Location = New-Object System.Drawing.Point($m, $botCapY)
    $lblTermDataCap.Location = New-Object System.Drawing.Point(($m + $colW + $m), $botCapY)
    $script:txtTermAux.Location = New-Object System.Drawing.Point($m, ($botCapY + $capH))
    $script:txtTermAux.Size = New-Object System.Drawing.Size($colW, $botBoxH)
    $script:txtTermSt.Location = New-Object System.Drawing.Point(($m + $colW + $m), ($botCapY + $capH))
    $script:txtTermSt.Size = New-Object System.Drawing.Size($col2W, $botBoxH)
    if ($stateBoxH -gt 0) {
        $lblTermStateCap.Visible = $true
        $script:txtTermState.Visible = $true
        $lblTermStateCap.Location = New-Object System.Drawing.Point($m, $stateCapY)
        $script:txtTermState.Location = New-Object System.Drawing.Point($m, ($stateCapY + $capH))
        $script:txtTermState.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $stateBoxH)
    } else {
        $lblTermStateCap.Visible = $false
        $script:txtTermState.Visible = $false
    }
    $lblTermState.Location = New-Object System.Drawing.Point($m, ($ph - $stateH - 2))
    $lblTermState.Size = New-Object System.Drawing.Size(([Math]::Max(200, $pw - 2 * $m)), $stateH)
}

# --- рисование строк терминала: роль цвета -> цвет текущего скина ---
function Set-TermLines($box, $lines) {
    if ($null -eq $box) { return }
    try { $box.SuspendLayout() } catch { }
    try {
        $box.Clear()
        foreach ($ln in @($lines)) {
            $txt = if ($ln -is [string]) { $ln } else { [string]$ln.text }
            $role = if ($ln -is [string]) { 'normal' } else { [string]$ln.color }
            $hex = switch ($role) {
                'err'  { $script:CLR.err }
                'warn' { $script:CLR.warn }
                'ok'   { $script:CLR.ok }
                'dim'  { $script:CLR.dim2 }
                default { $script:CLR.text }
            }
            $box.SelectionStart = $box.TextLength
            $box.SelectionLength = 0
            $box.SelectionColor = C $hex
            $box.AppendText($txt + "`n")
        }
        $box.SelectionStart = 0
        $box.SelectionLength = 0
    } catch { } finally { try { $box.ResumeLayout() } catch { } }
}

function Format-TermAge([int]$sec) {
    if ($sec -lt 0) { $sec = 0 }
    if ($sec -lt 60) { return ($sec.ToString() + ' с назад') }
    if ($sec -lt 3600) { return ([int]($sec / 60)).ToString() + ' мин назад' }
    if ($sec -lt 86400) { return ([int]($sec / 3600)).ToString() + ' ч назад' }
    return ([int]($sec / 86400)).ToString() + ' д назад'
}

function Format-TermUptime($sec) {
    if ($null -eq $sec) { return '—' }
    $s = [int]$sec
    if ($s -le 0) { return '—' }
    $dn = [int][Math]::Floor($s / 86400); $hh = [int][Math]::Floor(($s % 86400) / 3600); $mm = [int][Math]::Floor(($s % 3600) / 60)
    if ($dn -gt 0) { return ($dn.ToString() + ' д ' + $hh.ToString() + ' ч') }
    if ($hh -gt 0) { return ($hh.ToString() + ' ч ' + $mm.ToString() + ' мин') }
    return ($mm.ToString() + ' мин')
}

function Format-TermKB($v) {
    if ($null -eq $v) { return '—' }
    try { return ([Math]::Round([double]$v / 1024, 0)).ToString() + ' КБ' } catch { return '—' }
}

function Format-TermSize($v) {
    if ($null -eq $v) { return '—' }
    try { $n = [double]$v } catch { return '—' }
    if ($n -lt 1024) { return ([Math]::Round($n, 0)).ToString() + ' Б' }
    if ($n -lt 1048576) { return ([Math]::Round($n / 1024, 1)).ToString() + ' КБ' }
    if ($n -lt 1073741824) { return ([Math]::Round($n / 1048576, 1)).ToString() + ' МБ' }
    return ([Math]::Round($n / 1073741824, 2)).ToString() + ' ГБ'
}

function Get-ClogStageRu([string]$st) {
    switch ($st) {
        'ok'      { return 'всё прошло' }
        'clock'   { return 'нет времени (NTP)' }
        'connect' { return 'нет соединения с облаком' }
        'print'   { return 'отправка запроса' }
        'hdr'     { return 'чтение заголовков' }
        'http'    { return 'HTTP-код ошибки' }
        'short'   { return 'ответ короче нужного' }
        'empty'   { return 'пустой ответ' }
        'budget'  { return 'кончился бюджет сети' }
        'dns'     { return 'DNS не отвечает' }
        default   { return ('этап ' + $st) }
    }
}

function Get-CloudWhyRu([int]$why) {
    switch ($why) {
        0 { return 'ок' }
        1 { return 'нет Wi-Fi' }
        2 { return 'нет интернета' }
        3 { return 'нет токена' }
        4 { return 'запрос не удался' }
        5 { return 'success=false' }
        6 { return 'в ответе нет напряжения' }
        default { return ('причина ' + $why) }
    }
}

function Convert-TermVlogLines([string]$raw, $upSec = $null) {
    # /vlog — события качества напряжения (кольцо на плате): JSON с items.
    # Если прошивка отдаст текст, покажем его как есть — обе формы поддержаны.
    $txt = ([string]$raw).Trim()
    if (-not $txt) { return @(@{ text = 'нет ответа от платы'; color = 'warn' }) }
    $out = @()
    if (-not $txt.StartsWith('{')) {
        foreach ($ln in ($txt -split "`r?`n")) {
            $t = ([string]$ln).TrimEnd()
            if (-not $t) { continue }
            $col = 'normal'
            if ($t -match '(?i)(err|fail|ошиб|сбой|timeout|отказ)') { $col = 'err' }
            elseif ($t -match '(?i)(warn|предупр|slow)') { $col = 'warn' }
            $out += @{ text = $t; color = $col }
        }
        if ($out.Count -eq 0) { $out += @{ text = 'событий нет'; color = 'ok' } }
        return $out
    }
    try { $j = $txt | ConvertFrom-Json } catch { return @(@{ text = 'не удалось разобрать /vlog'; color = 'warn' }) }
    $items = @($j.items | Where-Object { $null -ne $_ })
    $n = [int](Field $j @('n') $items.Count)
    $up = 0
    if ($null -ne $upSec) { $up = [int]$upSec } elseif ($null -ne (Field $j @('up'))) { $up = [int](Field $j @('up')) }
    if ($n -le 0 -and $items.Count -eq 0) {
        # n=0 — это не пустое поле: так и пишем, что событий нет.
        $out += @{ text = 'событий нет'; color = 'ok' }
        $out += @{ text = 'за всё время работы не было просадок и всплесков напряжения (n=0)'; color = 'dim' }
        return $out
    }
    $out += @{ text = ('событий: ' + $n + ' · ниже последние ' + $items.Count); color = 'dim' }
    foreach ($it in $items) {
        $t = [int](Field $it @('t') 0)
        $vv = [string](Field $it @('v') 0)
        $ss = [string](Field $it @('s') 0)
        $kk = [int](Field $it @('k') 0)
        $when = if ($up -gt 0) { Format-TermAge ($up - $t) } else { ('через ' + (Format-TermUptime $t) + ' работы') }
        if ($kk -eq 1) {
            $out += @{ text = ($when + ' · всплеск до ' + $vv + ' В · держалось ' + $ss + ' с'); color = 'warn' }
        } else {
            $out += @{ text = ($when + ' · просадка до ' + $vv + ' В · держалась ' + $ss + ' с'); color = 'err' }
        }
    }
    return $out
}

function Convert-TermClogLines([string]$raw, $upSec = $null) {
    # /clog — кольцо облачных запросов: uptime_ms stage why http len,
    # затем строка cTry/cOk/cFail и строка pwrfill. Расшифровываем этапы.
    $txt = ([string]$raw).Trim()
    if (-not $txt) { return @(@{ text = 'нет ответа от платы'; color = 'warn' }) }
    $out = @()
    $up = 0
    if ($null -ne $upSec) { $up = [int]$upSec } elseif ($null -ne $script:board) { $up = [int](Field $script:board @('up') 0) }
    $shown = 0
    foreach ($ln in ($txt -split "`r?`n")) {
        $t = ([string]$ln).TrimEnd()
        if (-not $t) { continue }
        if ($t -match '^uptime_ms') {
            $out += @{ text = 'последние облачные запросы (новые снизу): когда · этап · почему · HTTP · длина'; color = 'dim' }
            continue
        }
        if ($t -match 'cTry=') {
            $col = if ($t -match 'cFail=[1-9]') { 'warn' } else { 'ok' }
            $out += @{ text = $t; color = $col }
            continue
        }
        if ($t -match '^pwrfill\b') {
            $out += @{ text = $t; color = 'dim' }
            continue
        }
        $parts = @($t -split '\s+')
        if ($parts.Count -ge 5 -and $parts[0] -match '^\d+$') {
            $at = 0; $why = 0; $http = 0; $len = 0
            [void][long]::TryParse($parts[0], [ref]$at)
            [void][int]::TryParse($parts[2], [ref]$why)
            [void][int]::TryParse($parts[3], [ref]$http)
            [void][int]::TryParse($parts[4], [ref]$len)
            $when = if ($up -gt 0 -and $at -gt 0) { Format-TermAge ([int]([Math]::Max(0, $up - [int]($at / 1000)))) } else { 'в работе ' + (Format-TermUptime ([int]($at / 1000))) }
            $col = 'normal'
            if ($http -ge 400 -or ($why -ne 0 -and $http -eq 0)) { $col = 'err' }
            elseif ($parts[1] -eq 'budget') { $col = 'warn' }
            elseif ($len -le 0) { $col = 'warn' }
            elseif ($why -eq 0 -and $http -ge 200 -and $http -lt 300) { $col = 'ok' }
            $line = $when + ' · ' + (Get-ClogStageRu $parts[1])
            if ($why -ne 0) { $line += ' · why ' + $why + ' (' + (Get-CloudWhyRu $why) + ')' }
            $line += ' · HTTP ' + $http + ' · ' + $len + ' Б'
            $out += @{ text = $line; color = $col }
            $shown++
            continue
        }
        $out += @{ text = $t; color = 'normal' }
    }
    if ($shown -eq 0) { $out += @{ text = 'облачных запросов ещё не было — журнал /clog пуст'; color = 'dim' } }
    return $out
}

function Convert-TermStatsLines($b) {
    # Сводка «что плата делает сейчас» — только поля, по которым видно,
    # где она застревает (блок loop, история, карта, страница, память, облако).
    $out = @()
    if ($null -eq $b) {
        $out += @{ text = 'нет ответа /data — плата занята или не отвечает'; color = 'warn' }
        return $out
    }
    $age = [int]((Get-Date) - $script:boardAt).TotalSeconds
    if ($age -lt 0) { $age = 0 }
    $blk = [string](Field $b @('blk') '—')
    $blkMs = [int](Field $b @('blkMs') 0)
    $loopMax = [int](Field $b @('loopMaxAll') 0)
    $loopMs = [int](Field $b @('loopMs') 0)
    $histMs = [int](Field $b @('histMs') 0)
    $sdMs = [int](Field $b @('sdMs') 0)
    $pageMs = [int](Field $b @('pageMs') 0)
    $heap = [int](Field $b @('heap') 0)
    $free = [int](Field $b @('free') 0)
    $maxAlloc = [int](Field $b @('maxAlloc') 0)
    $cTry = [int](Field $b @('cTry') 0); $cOk = [int](Field $b @('cOk') 0); $cFail = [int](Field $b @('cFail') 0)
    $cAge = [int](Field $b @('cAge') 0); $cWhy = [int](Field $b @('cWhy') 0)
    $gapN = [int](Field $b @('gapN') 0); $gapAct = [int](Field $b @('gapAct') 0); $gaps = [int](Field $b @('gaps') 0)
    $out += @{ text = ('ДАННЫЕ ' + (Get-Date).ToString('HH:mm:ss') + ' · ответу платы ' + $age + ' с · прошивка ' + [string](Field $b @('fw') '—') + ' · в работе ' + (Format-TermUptime (Field $b @('up'))) + ' · время платы ' + [string](Field $b @('now') '—')); color = 'dim' }
    $colBlk = if ($blkMs -gt 800) { 'err' } elseif ($blkMs -gt 200) { 'warn' } else { 'normal' }
    $out += @{ text = ('СЕЙЧАС: blk=' + $blk + ' blkMs=' + $blkMs + ' · loopMaxAll=' + $loopMax + ' мс · loopMs=' + $loopMs + ' мс'); color = $colBlk }
    $colHist = if ($histMs -gt 8000) { 'err' } elseif ($histMs -gt 3000) { 'warn' } else { 'normal' }
    $out += @{ text = ('КАРТА/ИСТОРИЯ: sdMs=' + $sdMs + ' мс · histMs=' + $histMs + ' мс · pageMs=' + $pageMs + ' мс'); color = $colHist }
    $colHeap = if ($heap -lt 30000) { 'err' } elseif ($heap -lt 60000) { 'warn' } else { 'normal' }
    $out += @{ text = ('ПАМЯТЬ: heap=' + (Format-TermKB $heap) + ' · free=' + (Format-TermKB $free) + ' · maxAlloc=' + (Format-TermKB $maxAlloc)); color = $colHeap }
    $colCloud = if ($cFail -gt 0) { 'warn' } else { 'ok' }
    $cWhen = if ($cTry -le 0) { 'попыток ещё не было' } else { 'последняя попытка ' + (Format-TermAge $cAge) }
    $out += @{ text = ('ОБЛАКО: cTry=' + $cTry + ' cOk=' + $cOk + ' cFail=' + $cFail + ' · cWhy=' + $cWhy + ' (' + (Get-CloudWhyRu $cWhy) + ') · ' + $cWhen); color = $colCloud }
    $colGap = if ($gapAct -gt 0 -or $gapN -gt 0) { 'warn' } else { 'ok' }
    $out += @{ text = ('ПРОПУСКИ: gapN=' + $gapN + ' gapAct=' + $gapAct + ' · gaps=' + $gaps + ' мин · карта ' + [string](Field $b @('sdRows') '—') + ' строк'); color = $colGap }
    return $out
}

function Format-TermVal($v) {
    if ($null -eq $v) { return '—' }
    if ($v -is [bool]) { if ($v) { return '1' } else { return '0' } }
    if ($v -is [System.Array]) {
        $parts = @()
        foreach ($x in $v) {
            if ($x -is [System.Array]) { $parts += ('[' + ((@($x) | ForEach-Object { [string]$_ }) -join ',') + ']') }
            else { $parts += [string]$x }
        }
        return ($parts -join ' ')
    }
    return [string]$v
}

function Convert-TermDataLines($b) {
    # /data целиком: все поля ключ=значение, по два в строке.
    if ($null -eq $b) { return @(@{ text = 'нет ответа /data — плата занята или не отвечает'; color = 'warn' }) }
    $pairs = @()
    foreach ($p in $b.PSObject.Properties) {
        $vs = Format-TermVal $p.Value
        if ($vs.Length -gt 44) { $vs = $vs.Substring(0, 43) + '…' }
        $pairs += ($p.Name + '=' + $vs)
    }
    $out = @()
    for ($i = 0; $i -lt $pairs.Count; $i += 2) {
        $a = [string]$pairs[$i]
        if ($i + 1 -lt $pairs.Count) { $out += @{ text = ($a.PadRight(52) + [string]$pairs[$i + 1]); color = 'normal' } }
        else { $out += @{ text = $a; color = 'normal' } }
    }
    return $out
}

function Convert-TermAuxLines {
    # Карта, файлы, журнал загрузок и состояние добора пропусков.
    $out = @()
    $rawStorage = [string]$script:termAuxRaw['storage']
    $st = $null
    if ($rawStorage) { try { $st = $rawStorage | ConvertFrom-Json } catch { $st = $null } }
    if ($null -ne $st -and [string](Field $st @('ok') '') -ne 'False') {
        $total = [double](Field $st @('totalBytes') 0); $freeB = [double](Field $st @('freeBytes') 0); $used = [double](Field $st @('usedBytes') 0)
        $low = ([string](Field $st @('lowSpace') 'False') -eq 'True')
        $col = if ($low) { 'warn' } else { 'normal' }
        $out += @{ text = ('КАРТА: свободно ' + (Format-TermSize $freeB) + ' из ' + (Format-TermSize $total) + ' (занято ' + (Format-TermSize $used) + ')' + $(if ($low) { ' · МЕСТА МАЛО' } else { '' })); color = $col }
        $out += @{ text = ('  политика ' + [string](Field $st @('policy') '—') + ' · сама удаляет ' + $(if ([string](Field $st @('autoDelete') 'False') -eq 'True') { 'да' } else { 'нет' }) + ' · archiveError ' + [string](Field $st @('archiveError') '—') + ' · rotations ' + [string](Field $st @('rotationsThisBoot') '—')); color = 'dim' }
    } elseif ($rawStorage) {
        $out += @{ text = ('КАРТА: /storage ответил: ' + $rawStorage.Trim()); color = 'warn' }
    } else {
        $out += @{ text = 'КАРТА: ждём /storage'; color = 'dim' }
    }

    $rawFiles = [string]$script:termAuxRaw['files']
    if ($rawFiles) {
        $fj = $null
        try { $fj = $rawFiles | ConvertFrom-Json } catch { $fj = $null }
        $files = @()
        if ($null -ne $fj) { $files = @($fj.files | Where-Object { $null -ne $_ } | Sort-Object { [double](Field $_ @('size') 0) } -Descending) }
        if ($files.Count -gt 0) {
            $out += @{ text = ('ФАЙЛЫ на карте (' + $files.Count + '):'); color = 'dim' }
            $i = 0
            foreach ($f in $files) {
                if ($i -ge 7) { $out += @{ text = ('  … ещё ' + ($files.Count - 7) + ' файлов'); color = 'dim' }; break }
                $out += @{ text = ('  ' + ([string](Field $f @('name') '?')).PadRight(24) + (Format-TermSize (Field $f @('size') 0))); color = 'normal' }
                $i++
            }
        } else {
            $out += @{ text = 'ФАЙЛЫ: на карте нет файлов истории'; color = 'dim' }
        }
    } else {
        $out += @{ text = 'ФАЙЛЫ: ждём /files'; color = 'dim' }
    }

    $rawBoots = [string]$script:termAuxRaw['boots']
    if ($rawBoots) {
        $bl = @($rawBoots -split "`r?`n" | Where-Object { $_.Trim() })
        if ($bl.Count -gt 0) {
            $out += @{ text = ('ЗАГРУЗКИ · ' + $bl[0].Trim()); color = 'normal' }
            if ($bl.Count -gt 1) {
                $from = [Math]::Max(1, $bl.Count - 3)
                for ($k = $from; $k -lt $bl.Count; $k++) { $out += @{ text = ('  ' + $bl[$k].Trim()); color = 'dim' } }
            }
        } else {
            $out += @{ text = 'ЗАГРУЗКИ: журнал пуст'; color = 'dim' }
        }
    } else {
        $out += @{ text = 'ЗАГРУЗКИ: ждём /boots'; color = 'dim' }
    }

    $rawGap = [string]$script:termAuxRaw['gapfill']
    if ($rawGap) {
        $gj = $null
        try { $gj = $rawGap | ConvertFrom-Json } catch { $gj = $null }
        if ($null -ne $gj) {
            $act = if ([string](Field $gj @('active') 0) -eq '1') { 'идёт' } else { 'спит' }
            $out += @{ text = ('ПРОПУСКИ: заполнение ' + $act + ' · этап ' + (Get-GapFillPhaseName (Field $gj @('phase') 0)) + ' · gaps ' + [string](Field $gj @('gaps') 0) + ' · ins ' + [string](Field $gj @('ins') 0) + '/' + [string](Field $gj @('insTotal') 0) + ' · сегодня пропусков ' + [string](Field $gj @('todayN') 0) + ' (' + [string](Field $gj @('todayMin') 0) + ' мин)'); color = 'normal' }
            $msg = [string](Field $gj @('msg') '')
            $out += @{ text = ('  sdErr ' + [string](Field $gj @('sdErr') 0) + ' · csv ' + (Format-TermSize (Field $gj @('csv') 0)) + ' · bak ' + (Format-TermSize (Field $gj @('bak') 0)) + ' · файл ' + [string](Field $gj @('file') '—') + ' ' + (Format-TermSize (Field $gj @('fileSz') 0)) + $(if ($msg) { ' · ' + $msg } else { '' })); color = 'dim' }
        }
    } else {
        $out += @{ text = 'ПРОПУСКИ: ждём /gapfill/status'; color = 'dim' }
    }
    return $out
}

function Render-TermAll {
    if ($null -eq $script:txtTermSt) { return }
    if ($script:boardAt -ne $script:termBoardAt -or $script:txtTermSt.TextLength -eq 0) {
        $script:termBoardAt = $script:boardAt
        Set-TermLines $script:txtTermNow (Convert-TermStatsLines $script:board)
        Set-TermLines $script:txtTermSt (Convert-TermDataLines $script:board)
    }
    if ($script:termAuxDirty -or $script:txtTermAux.TextLength -eq 0) {
        $script:termAuxDirty = $false
        Set-TermLines $script:txtTermAux (Convert-TermAuxLines)
    }
    $stKey = '' + $script:boardAt.Ticks + '|' + $script:measureAt.Ticks + '|' + $script:measureErr + '|' + $script:boardErr + '|' + $(if ($script:termAuxWhen.ContainsKey('storage')) { $script:termAuxWhen['storage'].Ticks } else { 0 })
    if ($stKey -ne $script:termStateKey -or $script:txtTermState.TextLength -eq 0) {
        $script:termStateKey = $stKey
        Set-TermLines $script:txtTermState (Convert-TermStateLines)
    }
}

function Get-TermAuxUrl([string]$name) {
    $base = [string]$script:cfg.board
    switch ($name) {
        'boots'   { return ($base + '/boots') }
        'files'   { return ($base + '/files') }
        'storage' { return ($base + '/storage') }
        'gapfill' { return ($base + '/gapfill/status') }
    }
    return ''
}

function Refresh-Terminal([switch]$force) {
    if (-not $script:uiReady) { return }
    if ($force) {
        $script:termAt = [datetime]::MinValue
        $script:termBoardAt = [datetime]::MinValue
        $script:termAuxAt = [datetime]::MinValue
        $script:termAuxQueue = @('boots','files','storage','gapfill')
    }
    try { Layout-Term } catch { }
    Pump-Terminal -manual:$force
}

function Pump-Terminal([switch]$manual) {
    if (-not $script:uiReady) { return }
    if ($null -eq $pgTerm -or -not $pgTerm.Visible) { return }
    Render-TermAll

    # --- ответы /vlog и /clog (раз в 3 с) ---
    if ($null -ne $script:termTaskV) {
        $waited = ((Get-Date) - $script:termStart).TotalSeconds
        $done = ($script:termTaskV.IsCompleted -and ($null -eq $script:termTaskC -or $script:termTaskC.IsCompleted))
        if (-not $done -and $waited -lt 8) { return }
        $vlogRaw = $null; $clogRaw = $null
        try { if ($script:termTaskV.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) { $vlogRaw = $script:termTaskV.Result } } catch { }
        try { if ($null -ne $script:termTaskC -and $script:termTaskC.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) { $clogRaw = $script:termTaskC.Result } } catch { }
        $script:termTaskV = $null; $script:termTaskC = $null
        $upSec = $null
        if ($null -ne $script:board) { $upSec = [int](Field $script:board @('up') 0) }
        if ($null -eq $vlogRaw) {
            Set-TermLines $script:txtTermEv @(@{ text = 'нет ответа на /vlog (плата занята или не отвечает)'; color = 'warn' })
        } else {
            Set-TermLines $script:txtTermEv (Convert-TermVlogLines $vlogRaw $upSec)
        }
        if ($null -eq $clogRaw) {
            Set-TermLines $script:txtTermCl @(@{ text = 'нет ответа на /clog (плата занята или не отвечает)'; color = 'warn' })
        } else {
            Set-TermLines $script:txtTermCl (Convert-TermClogLines $clogRaw $upSec)
        }
        $script:termAt = Get-Date
        Set-Lbl $lblTermState ('обновлено ' + (Get-Date).ToString('HH:mm:ss') + ' · следующее через 3 с')
    }

    # --- /boots, /files, /storage, /gapfill/status: по одному за тик ---
    if ($null -ne $script:termAuxTask) {
        if ($script:termAuxTask.IsCompleted) {
            $name = $script:termAuxName
            try {
                if ($script:termAuxTask.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {
                    $script:termAuxRaw[$name] = [string]$script:termAuxTask.Result
                    $script:termAuxWhen[$name] = Get-Date
                    $script:termAuxDirty = $true
                } else {
                    $script:termAuxRaw[$name] = ''
                    $script:termAuxDirty = $true
                }
            } catch { }
            $script:termAuxTask = $null; $script:termAuxName = ''
        } elseif (((Get-Date) - $script:termAuxStart).TotalSeconds -gt 10) {
            $script:termAuxTask = $null; $script:termAuxName = ''
        }
        return
    }

    if (-not $manual -and $script:chkTermAuto.Checked -eq $false) { return }
    if (-not $script:cfg.board) { Set-Lbl $lblTermState 'адрес платы не задан — вкладка «Устройство»'; return }
    if ($null -eq $script:http) { Init-Http }
    if ($null -eq $script:http) { return }

    if ($script:termAuxQueue.Count -eq 0 -and ((Get-Date) - $script:termAuxAt).TotalSeconds -ge 10) {
        $script:termAuxQueue = @('boots','files','storage','gapfill')
        $script:termAuxAt = Get-Date
    }
    if ($script:termAuxQueue.Count -gt 0) {
        $name = [string]$script:termAuxQueue[0]
        $script:termAuxQueue = @($script:termAuxQueue | Select-Object -Skip 1)
        $url = Get-TermAuxUrl $name
        if ($url) {
            try {
                $script:termAuxStart = Get-Date
                $script:termAuxTask = $script:http.GetStringAsync($url)
                $script:termAuxName = $name
            } catch {
                $script:termAuxTask = $null; $script:termAuxName = ''
            }
        }
        return
    }

    # --- новый круг /vlog и /clog, когда прошло 3 с ---
    if ($null -eq $script:termTaskV -and ((Get-Date) - $script:termAt).TotalSeconds -ge 3) {
        try {
            $script:termStart = Get-Date
            $base = [string]$script:cfg.board
            $script:termTaskV = $script:http.GetStringAsync($base + '/vlog')
            $script:termTaskC = $script:http.GetStringAsync($base + '/clog')
            Set-Lbl $lblTermState 'запрос к плате…'
        } catch {
            $script:termTaskV = $null; $script:termTaskC = $null
            if ($script:termErrSeen -ne $_.Exception.Message) {
                $script:termErrSeen = $_.Exception.Message
                Add-Log ('терминал: ' + $_.Exception.Message) 'warn'
            }
        }
    }
}

$btnTermRefresh.Add_Click({
    $script:termAuxQueue = @('boots','files','storage','gapfill')
    Refresh-Terminal -force
})

$script:chkTermAuto.Add_CheckedChanged({
    if ($script:chkTermAuto.Checked) { $script:termAt = [datetime]::MinValue; $script:termAuxAt = [datetime]::MinValue }
})

# ============================================================================
#  P1.4 «АРХИВ» И P1.5 «ЦЕНТР ДИАГНОСТИКИ» (Windows)
# ----------------------------------------------------------------------------
#  • карточка архива: задача DomEnergy-DailyArchive, последняя УСПЕШНАЯ
#    проверенная копия и её возраст, путь, число снимков/файлов, свободное
#    место, последняя ошибка; честные подписи про включённый ПК;
#  • «Снимок сейчас» — archive-run.ps1 в фоне (прогресс из run-last.err.txt),
#    второй снимок одновременно не запускается (плюс общий lock archive-sd.py);
#  • «Тест восстановления» — archive-sd.py --restore в НОВУЮ временную папку,
#    своя проверка SHA-256 каждого файла, отчёт, затем удаляется ТОЛЬКО эта папка;
#  • диагностика: обезличенный отчёт (версия/build/uptime, LAN/WAN/SD, коды
#    ошибок, возраст данных, счётчики, последний gapfill) без IP, MAC, SSID,
#    токенов, device id и сырых измерений; предпросмотр + «Скопировать»;
#    автоматических отправок нет.
#  Скрипты архива лежат рядом с приложением (C:\esp32\tools): archive-run.ps1
#  и archive-sd.py; путь к архиву — как у задачи планировщика.
# ============================================================================

$script:ArchRoot     = 'D:\DomEnergy\DomEnergy-Archive'
$script:ArchRunner   = Join-Path $script:AppDir 'archive-run.ps1'
$script:ArchSd       = Join-Path $script:AppDir 'archive-sd.py'
$script:ArchTaskName = 'DomEnergy-DailyArchive'

# --- состояние карточки архива ---
$script:archState      = $null
$script:archStateAt    = [datetime]::MinValue
$script:archVerifyAt   = [datetime]::MinValue

# --- статус задачи планировщика (schtasks, опрос в фоне) ---
$script:archTask       = $null
$script:archTaskAt     = [datetime]::MinValue
$script:archTaskProc   = $null
$script:archTaskFiles  = $null
$script:archTaskStart  = [datetime]::MinValue
$script:archTaskErr    = ''

# --- ручной снимок (archive-run.ps1) ---
$script:archRunProc    = $null
$script:archRunStart   = [datetime]::MinValue
$script:archRunOut     = ''
$script:archRunErr     = ''
$script:archRunState   = 'снимок ещё не запускался из приложения'
$script:archRunOkAt    = [datetime]::MinValue

# --- тест восстановления (archive-sd.py --restore) ---
$script:archRestoreProc     = $null
$script:archRestoreStart    = [datetime]::MinValue
$script:archRestoreTmp      = ''
$script:archRestoreManifest = ''
$script:archRestoreState    = 'тест восстановления не запускался'
$script:archRestoreLast     = ''
$script:archRestoreLog      = Join-Path $script:LogDir 'archive-restore.log'

# --- диагностика: /storage и /gapfill/status ---
$script:archStore      = $null
$script:archStoreAt    = [datetime]::MinValue
$script:archGapfill    = $null
$script:archGapAt      = [datetime]::MinValue
$script:archDiagTask   = $null
$script:archDiagName   = ''
$script:archDiagStart  = [datetime]::MinValue
$script:archDiagAt     = [datetime]::MinValue
$script:archDiagText   = ''
$script:archLinesKey   = ''

try { Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue } catch { }

# ---------------------------------------------------------------------------
#  Чистые помощники: их можно проверять оффлайн (фикстуры в _p14_selftest.ps1)
# ---------------------------------------------------------------------------

function Find-ArchivePython {
    # python.exe нужен archive-sd.py: и задача, и кнопки используют тот же путь.
    $cand = New-Object System.Collections.ArrayList
    [void]$cand.Add((Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'))
    [void]$cand.Add((Join-Path $env:LOCALAPPDATA 'Programs\Python\Python311\python.exe'))
    [void]$cand.Add('C:\Python312\python.exe')
    [void]$cand.Add('C:\Python311\python.exe')
    foreach ($cmdName in @('python', 'python3')) {
        try {
            $c = Get-Command $cmdName -ErrorAction SilentlyContinue
            if ($null -ne $c -and $c.Source) { [void]$cand.Add([string]$c.Source) }
        } catch { }
    }
    foreach ($p in $cand) {
        try {
            if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { return [string]$p }
        } catch { }
    }
    return ''
}

function Get-ArchiveDriveRoot {
    try {
        $r = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($script:ArchRoot))
        if ($r) { return $r.TrimEnd('\') }
    } catch { }
    return 'D:'
}

function Format-SchTaskHex([string]$raw) {
    # Last Result — это код возврата процесса (0 = успех); в отчёте — число и hex.
    if ($null -eq $raw) { return '—' }
    $t = ([string]$raw).Trim()
    if (-not $t) { return '—' }
    $n = 0
    if (-not [int]::TryParse($t, [ref]$n)) { return $t }
    $u = [uint32]([int64]$n -band [int64]4294967295)
    return ('0x' + $u.ToString('X8'))
}

function Get-SchTaskResultRu([string]$raw) {
    if ($null -eq $raw) { return '—' }
    $t = ([string]$raw).Trim()
    if (-not $t) { return '—' }
    $n = 0
    if (-not [int]::TryParse($t, [ref]$n)) { return $t }
    if ($n -eq 0) { return 'успешно (0)' }
    if ($n -eq 267009) { return 'ещё выполняется (267009)' }
    if ($n -eq 267011) { return 'ещё не запускалась (267011)' }
    return ('код ' + $n + ' · ' + (Format-SchTaskHex $t))
}

function Get-SchTaskStateRu([string]$state, [string]$status) {
    $s = ([string]$state).Trim().ToLowerInvariant()
    $st = ([string]$status).Trim().ToLowerInvariant()
    if ($s -eq 'disabled' -or $s -eq 'отключена' -or $s -eq 'отключено') { return 'ОТКЛЮЧЕНА' }
    if ($st -eq 'running' -or $st -eq 'выполняется') { return 'выполняется' }
    if ($st -eq 'queued' -or $st -eq 'в очереди') { return 'в очереди' }
    if ($st -eq 'ready' -or $st -eq 'готово' -or $st -eq 'готова') { return 'готова' }
    if ($st -eq 'disabled' -or $st -eq 'отключена' -or $st -eq 'отключено') { return 'ОТКЛЮЧЕНА' }
    if (-not $st) { return 'состояние неизвестно' }
    return $st
}

function Convert-SchTaskDate([string]$text) {
    if (-not $text) { return $null }
    $t = ([string]$text).Trim()
    if (-not $t -or $t -eq 'N/A' -or $t -eq 'Н/Д') { return $null }
    $inv = [Globalization.CultureInfo]::InvariantCulture
    foreach ($fmt in @('dd.MM.yyyy H:mm:ss', 'dd.MM.yyyy HH:mm:ss', 'M/d/yyyy h:mm:ss tt', 'M/d/yyyy H:mm:ss', 'M/d/yyyy h:mm tt', 'M/d/yyyy H:mm')) {
        try { return [datetime]::ParseExact($t, $fmt, $inv) } catch { }
    }
    try { return [datetime]::Parse($t) } catch { return $null }
}

function Convert-SchTasksListText([string]$text) {
    # Разбор `schtasks /Query /TN <имя> /FO LIST /V`.
    # Ключи бывают английские и русские (локаль Windows), блоков может быть
    # несколько (по одному на триггер) — берём первое непустое значение поля.
    $res = [pscustomobject]@{
        Found      = $false
        TaskName   = ''
        Status     = ''
        LastRun    = ''
        LastResult = ''
        NextRun    = ''
        State      = ''
        TaskToRun  = ''
        Blocks     = 0
        Raw        = $text
    }
    if ($null -eq $text) { return $res }
    $raw = [string]$text
    if (-not $raw.Trim()) { return $res }
    if ($raw.Trim() -match '^(?i)(ERROR|ОШИБКА)') { return $res }

    $aliases = @{
        'taskname'                        = 'TaskName'
        'имя задачи'                      = 'TaskName'
        'status'                          = 'Status'
        'состояние'                       = 'Status'
        'last run time'                   = 'LastRun'
        'время последнего запуска'        = 'LastRun'
        'время прошлого запуска'          = 'LastRun'
        'last result'                     = 'LastResult'
        'последний результат'             = 'LastResult'
        'код последнего результата'       = 'LastResult'
        'прошлый результат'               = 'LastResult'
        'код прошлого результата'         = 'LastResult'
        'next run time'                   = 'NextRun'
        'время следующего запуска'        = 'NextRun'
        'scheduled task state'            = 'State'
        'состояние задачи планировщика'   = 'State'
        'состояние назначенной задачи'    = 'State'
        'task to run'                     = 'TaskToRun'
        'задача для запуска'              = 'TaskToRun'
        'задача для выполнения'           = 'TaskToRun'
        'выполняемая задача'              = 'TaskToRun'
    }
    $map = @{}
    $blocks = 0
    foreach ($line in ($raw -split "`r?`n")) {
        $ln = ([string]$line).Trim()
        if (-not $ln) { continue }
        $m = [regex]::Match($ln, '^([^:]{1,60}?):\s*(.*)$')
        if (-not $m.Success) { continue }
        $k = $m.Groups[1].Value.Trim().ToLowerInvariant()
        $v = $m.Groups[2].Value.Trim()
        if ($aliases.ContainsKey($k)) {
            $canon = [string]$aliases[$k]
            if ($canon -eq 'TaskName') { $blocks++ }
            if ($v -and -not $map.ContainsKey($canon)) { $map[$canon] = $v }
        }
    }
    $res.Blocks = $blocks
    foreach ($k in @($map.Keys)) {
        try { $res.$k = [string]$map[$k] } catch { }
    }
    $res.Found = ($blocks -gt 0 -or $map.ContainsKey('LastRun') -or $map.ContainsKey('LastResult'))
    return $res
}

function Convert-ArchiveManifestText([string]$json) {
    # Манифест снимка (snapshots/**/*.json и latest.json — один формат):
    # {format, createdUtc, url, storage, downloadedBytes, files:{name:{size,sha256}}}.
    $res = [pscustomobject]@{
        Ok         = $false
        Error      = ''
        Format     = 0
        CreatedUtc = $null
        FileCount  = 0
        TotalBytes = 0
        Files      = @()
        Device     = ''
        Url        = ''
    }
    if ($null -eq $json -or -not ([string]$json).Trim()) { $res.Error = 'пустой манифест'; return $res }
    $j = $null
    try { $j = ([string]$json) | ConvertFrom-Json } catch { $res.Error = 'манифест не разобран'; return $res }
    if ($null -eq $j) { $res.Error = 'манифест не разобран'; return $res }
    try {
        $fmt = Field $j @('format')
        if ($null -ne $fmt) { $res.Format = [int]$fmt }
    } catch { }
    $cr = [string](Field $j @('createdUtc') '')
    if ($cr) {
        try {
            $dt = [datetime]::Parse($cr, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal)
            $res.CreatedUtc = $dt
        } catch {
            try { $res.CreatedUtc = ([datetime]::Parse($cr)).ToUniversalTime() } catch { $res.CreatedUtc = $null }
        }
    }
    $filesNode = Field $j @('files')
    $items = New-Object System.Collections.ArrayList
    $total = 0L
    if ($null -ne $filesNode) {
        foreach ($p in $filesNode.PSObject.Properties) {
            $size = 0L
            $sha = ''
            try { $size = [int64](Field $p.Value @('size') 0) } catch { $size = 0 }
            try { $sha = ([string](Field $p.Value @('sha256') '')).Trim().ToLowerInvariant() } catch { $sha = '' }
            if ($sha -notmatch '^[0-9a-f]{64}$') { $res.Error = 'в манифесте некорректный SHA-256'; continue }
            [void]$items.Add([pscustomobject]@{ Name = [string]$p.Name; Size = $size; Sha256 = $sha })
            $total += $size
        }
    }
    $res.Files = @($items.ToArray())
    $res.FileCount = $res.Files.Count
    $res.TotalBytes = $total
    try { $res.Url = [string](Field $j @('url') '') } catch { }
    try {
        $stNode = Field $j @('storage')
        if ($null -ne $stNode) { $res.Device = [string](Field $stNode @('device') '') }
    } catch { }
    $res.Ok = ($null -ne $res.CreatedUtc -and $res.FileCount -gt 0 -and -not $res.Error)
    if (-not $res.Ok -and -not $res.Error) { $res.Error = 'в манифесте нет файлов или времени' }
    return $res
}

function Convert-StorageJson([string]$raw) {
    # /storage платы: в ответе ДВА поля freeBytes (второе не согласуется с
    # totalBytes-usedBytes), ConvertFrom-Json оставляет последнее и врёт.
    # Берём ПЕРВОЕ значение каждого известного ключа.
    if ($null -eq $raw -or -not ([string]$raw).Trim()) { return $null }
    $t = [string]$raw
    $o = [pscustomobject]@{}
    $keys = @('ok', 'policy', 'autoDelete', 'device', 'bootId', 'sdGeneration', 'totalBytes', 'usedBytes',
              'freeBytes', 'archiveError', 'rotationsThisBoot', 'lowSpace', 'writeOk', 'writeFail',
              'writeFailRun', 'lastWriteAgeSec', 'lastFailAgeSec', 'lastWriteErr', 'lastWriteMsg')
    foreach ($k in $keys) {
        $m = [regex]::Match($t, '"' + [regex]::Escape($k) + '"\s*:\s*("(?:[^"\\]|\\.)*"|true|false|-?\d+(?:\.\d+)?)')
        if (-not $m.Success) { continue }
        $v = $m.Groups[1].Value
        if ($v.StartsWith('"')) {
            $v = $v.Substring(1, $v.Length - 2)
        } elseif ($v -eq 'true') { $v = $true }
        elseif ($v -eq 'false') { $v = $false }
        else {
            try { $v = [double]$v } catch { }
        }
        $o | Add-Member -NotePropertyName $k -NotePropertyValue $v -Force
    }
    if ($null -eq $o.PSObject.Properties['ok']) { return $null }
    return $o
}

function Get-SanitizedText([string]$text) {
    # m6 (ревью): свободный текст ошибок может содержать C:\Users\<имя>\...
    # и другие локальные пути — маскируем их до публикации отчёта.
    if (-not [string]::IsNullOrEmpty($text)) {
        $text = [regex]::Replace($text, '(?i)\b[A-Z]:\\Users\\[^\\\s\"'']+', '[путь скрыт]')
        $text = [regex]::Replace($text, '(?i)\b[A-Z]:\\[^\s\"'']+', '[путь скрыт]')
    }
    # Обезличивание свободного текста (ошибки, сообщения платы): адреса, IP,
    # MAC, токены, длинные hex-строки. Уже отформатированные числа не трогаем.
    if ($null -eq $text) { return '' }
    $t = [string]$text
    $t = [regex]::Replace($t, '(?i)\bhttps?://[^\s"'']+', '<адрес скрыт>')
    $t = [regex]::Replace($t, '(?i)\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b', '<IP скрыт>')
    $t = [regex]::Replace($t, '(?i)\b(?:[0-9a-f]{2}[:\-]){5}[0-9a-f]{2}\b', '<MAC скрыт>')
    $t = [regex]::Replace($t, '(?i)\b(device|ssid|token|password|passwd|secret|api[_-]?key)\s*[=:]\s*[^\s,;]+', '$1=<скрыто>')
    $t = [regex]::Replace($t, '(?i)\b[A-Fa-f0-9]{32,}\b', '<hex скрыт>')
    $t = [regex]::Replace($t, '[\r\n\t]+', ' ')
    $t = [regex]::Replace($t, '\s{2,}', ' ')
    return $t.Trim()
}


# ---------------------------------------------------------------------------
#  Состояние архива на ПК (только чтение)
# ---------------------------------------------------------------------------

function Get-ArchiveSnapshotManifestFile {
    $dir = Join-Path $script:ArchRoot 'snapshots'
    if (-not (Test-Path -LiteralPath $dir)) { return $null }
    try {
        $files = @(Get-ChildItem -LiteralPath $dir -Recurse -File -Filter '*.json' -ErrorAction SilentlyContinue)
        if ($files.Count -eq 0) { return $null }
        return ($files | Sort-Object Name | Select-Object -Last 1)
    } catch { return $null }
}

function Test-ArchiveManifestObjects($manifest) {
    # Независимая проверка сохранности: каждый объект gzip распаковывается,
    # сверяется SHA-256 и размер. Это и есть «проверенная копия».
    $r = [pscustomobject]@{ Ok = 0; Bad = 0; Ms = 0; BadNames = @() }
    if ($null -eq $manifest -or -not $manifest.Ok) { return $r }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    foreach ($f in @($manifest.Files)) {
        $p = Join-Path (Join-Path (Join-Path $script:ArchRoot 'objects') $f.Sha256.Substring(0, 2)) ($f.Sha256 + '.gz')
        $good = $false
        try {
            if (Test-Path -LiteralPath $p -PathType Leaf) {
                $sha = [System.Security.Cryptography.SHA256]::Create()
                $fs = [IO.File]::OpenRead($p)
                try {
                    $gz = New-Object System.IO.Compression.GZipStream($fs, [System.IO.Compression.CompressionMode]::Decompress)
                    try {
                        $count = 0L
                        $buf = New-Object byte[] 65536
                        while (($n = $gz.Read($buf, 0, $buf.Length)) -gt 0) {
                            $sha.TransformBlock($buf, 0, $n, $null, 0) | Out-Null
                            $count += $n
                        }
                        $sha.TransformFinalBlock($buf, 0, 0) | Out-Null
                        $hex = ''
                        foreach ($b in $sha.Hash) { $hex += $b.ToString('x2') }
                        $good = ($count -eq [int64]$f.Size -and $hex -eq $f.Sha256)
                    } finally { $gz.Dispose() }
                } finally { $fs.Dispose(); $sha.Dispose() }
            }
        } catch { $good = $false }
        if ($good) { $r.Ok++ } else { $r.Bad++; $r.BadNames += [string]$f.Name }
    }
    $sw.Stop()
    $r.Ms = [int]$sw.Elapsed.TotalMilliseconds
    return $r
}

function Get-ArchiveState {
    param([switch]$Verify)
    $root = $script:ArchRoot
    $st = [pscustomobject]@{
        When             = Get-Date
        Root             = $root
        Exists           = $false
        Drive            = (Get-ArchiveDriveRoot)
        FreeBytes        = $null
        TotalBytes       = $null
        Latest           = $null
        LatestFile       = ''
        Snapshots        = 0
        Objects          = 0
        ObjectsBytes     = 0
        Zips             = 0
        LastZipAt        = $null
        LastZipBytes     = 0
        LastZipSha       = $false
        RunLastWhen      = $null
        RunLastLine      = ''
        LastError        = ''
        LastErrorWhen    = $null
        VerifiedAt       = $null
        VerifyOk         = 0
        VerifyBad        = 0
        VerifyBadNames   = @()
        VerifyMs         = 0
    }
    if (-not (Test-Path -LiteralPath $root)) { return $st }
    $st.Exists = $true

    # свободное место на диске архива
    try {
        $di = New-Object System.IO.DriveInfo (([string]$st.Drive) + '\')
        $st.FreeBytes = [int64]$di.AvailableFreeSpace
        $st.TotalBytes = [int64]$di.TotalSize
    } catch { }

    # последняя успешная копия: latest.json пишется атомарно после проверок
    $latestPath = Join-Path $root 'latest.json'
    if (Test-Path -LiteralPath $latestPath) {
        try {
            $st.Latest = Convert-ArchiveManifestText ([IO.File]::ReadAllText($latestPath, [Text.Encoding]::UTF8))
            $st.LatestFile = $latestPath
        } catch { $st.Latest = $null }
    }
    if ($null -eq $st.Latest) {
        $man = Get-ArchiveSnapshotManifestFile
        if ($null -ne $man) {
            try {
                $st.Latest = Convert-ArchiveManifestText ([IO.File]::ReadAllText($man.FullName, [Text.Encoding]::UTF8))
                $st.LatestFile = $man.FullName
            } catch { $st.Latest = $null }
        }
    }

    # снимки в snapshots/YYYY-MM/*.json
    $snapDir = Join-Path $root 'snapshots'
    if (Test-Path -LiteralPath $snapDir) {
        try {
            $snap = @(Get-ChildItem -LiteralPath $snapDir -Recurse -File -Filter '*.json' -ErrorAction SilentlyContinue)
            $st.Snapshots = $snap.Count
        } catch { }
    }

    # объекты и полные независимые копии считаем не каждый тик (это дороже)
    $prev = $script:archState
    if ($Verify -or $null -eq $prev) {
        $objDir = Join-Path $root 'objects'
        if (Test-Path -LiteralPath $objDir) {
            try {
                $obj = @(Get-ChildItem -LiteralPath $objDir -Recurse -File -ErrorAction SilentlyContinue)
                $st.Objects = $obj.Count
                $bytes = 0L
                foreach ($o in $obj) { $bytes += [int64]$o.Length }
                $st.ObjectsBytes = $bytes
            } catch { }
        }
    } elseif ($null -ne $prev) {
        $st.Objects = $prev.Objects
        $st.ObjectsBytes = $prev.ObjectsBytes
    }

    try {
        # m10 (ревью): PRIVATE-ZIP содержат полный дамп flash+NVS (Wi-Fi/токены).
    # Показываем, но подписываем честно — это не публичный архив.
    $zips = @(Get-ChildItem -LiteralPath $root -File -Filter '*full-device-*-PRIVATE.zip' -ErrorAction SilentlyContinue)
        $st.Zips = $zips.Count
        if ($zips.Count -gt 0) {
            $last = $zips | Sort-Object LastWriteTime | Select-Object -Last 1
            $st.LastZipAt = $last.LastWriteTime
            $st.LastZipBytes = [int64]$last.Length
            $st.LastZipSha = (Test-Path -LiteralPath ($last.FullName + '.sha256'))
        }
    } catch { }

    # итог последнего запуска задачи (python пишет туда же, что и задача)
    $rlPath = Join-Path $root 'run-last.err.txt'
    if (-not (Test-Path -LiteralPath $rlPath)) { $rlPath = Join-Path $root 'run-last.out.txt' }
    if (Test-Path -LiteralPath $rlPath) {
        try {
            $st.RunLastWhen = (Get-Item -LiteralPath $rlPath).LastWriteTime
            $lines = @(Get-Content -LiteralPath $rlPath -Tail 60 -ErrorAction SilentlyContinue)
            for ($i = $lines.Count - 1; $i -ge 0; $i--) {
                $ln = ([string]$lines[$i]).Trim()
                if ($ln) {
                    $ln = [regex]::Replace($ln, '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d+\s+', '')
                    $ln = [regex]::Replace($ln, '^(INFO|WARNING|ERROR)\s+', '')
                    $st.RunLastLine = (Get-SanitizedText $ln)
                    break
                }
            }
        } catch { }
    }

    # последняя ошибка из archive.log (переживает перезапуски и ручные запуски)
    $logPath = Join-Path $root 'archive.log'
    if (Test-Path -LiteralPath $logPath) {
        try {
            $tail = @(Get-Content -LiteralPath $logPath -Tail 300 -ErrorAction SilentlyContinue)
            for ($i = $tail.Count - 1; $i -ge 0; $i--) {
                $ln = ([string]$tail[$i]).Trim()
                if ($ln -match '(?i)\b(ERROR|FAILED|Traceback)\b') {
                    $st.LastError = (Get-SanitizedText $ln)
                    $m = [regex]::Match($ln, '^(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})')
                    if ($m.Success) {
                        try { $st.LastErrorWhen = [datetime]::ParseExact(($m.Groups[1].Value + ' ' + $m.Groups[2].Value), 'yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture) } catch { }
                    }
                    break
                }
            }
        } catch { }
    }

    # независимая проверка SHA-256 последней копии
    if ($Verify -and $null -ne $st.Latest) {
        $v = Test-ArchiveManifestObjects $st.Latest
        $st.VerifyOk = $v.Ok
        $st.VerifyBad = $v.Bad
        $st.VerifyBadNames = @($v.BadNames)
        $st.VerifyMs = $v.Ms
        $st.VerifiedAt = Get-Date
    } elseif ($null -ne $prev -and $null -ne $prev.VerifiedAt) {
        $st.VerifiedAt = $prev.VerifiedAt
        $st.VerifyOk = $prev.VerifyOk
        $st.VerifyBad = $prev.VerifyBad
        $st.VerifyBadNames = @($prev.VerifyBadNames)
        $st.VerifyMs = $prev.VerifyMs
    }
    return $st
}

# ---------------------------------------------------------------------------
#  Задача планировщика: schtasks в фоне (без блокировки окна)
# ---------------------------------------------------------------------------

function Start-ArchiveTaskQuery {
    if ($null -ne $script:archTaskProc) { return }
    try {
        $out = Join-Path $script:LogDir 'archive-task.txt'
        $err = Join-Path $script:LogDir 'archive-task.err.txt'
        [IO.File]::WriteAllText($out, '')
        [IO.File]::WriteAllText($err, '')
        $script:archTaskFiles = @{ out = $out; err = $err }
        $script:archTaskStart = Get-Date
        $script:archTaskProc = Start-Process -FilePath 'schtasks.exe' -ArgumentList @('/Query', '/TN', $script:ArchTaskName, '/FO', 'LIST', '/V') -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
        $null = $script:archTaskProc.Handle
    } catch {
        $script:archTaskProc = $null
        $script:archTaskErr = 'не удалось запустить schtasks: ' + $_.Exception.Message
        $script:archTaskAt = Get-Date
    }
}

function Pump-ArchiveTaskQuery {
    if ($null -eq $script:archTaskProc) { return }
    try {
        if (-not $script:archTaskProc.HasExited) {
            if (((Get-Date) - $script:archTaskStart).TotalSeconds -gt 20) {
                try { $script:archTaskProc.Kill() } catch { }
                try { $script:archTaskProc.Dispose() } catch { }
                $script:archTaskProc = $null
                $script:archTaskErr = 'schtasks не ответил за 20 с'
            }
            return
        }
        $code = -1
        try { $code = [int]$script:archTaskProc.ExitCode } catch { }
        try { $script:archTaskProc.Dispose() } catch { }
        $script:archTaskProc = $null
    } catch {
        $script:archTaskProc = $null
        $script:archTaskErr = 'schtasks: ' + $_.Exception.Message
        return
    }
    # Кодировка консольного вывода schtasks зависит от локали Windows
    # (OEM 866, ANSI 1251 или UTF-8). Пробуем варианты и берём тот, в котором
    # поля реально разобрались, — иначе карточка врала бы «задача не найдена».
    $bytes = $null
    try {
        if ($null -ne $script:archTaskFiles -and (Test-Path -LiteralPath $script:archTaskFiles.out)) {
            $bytes = [IO.File]::ReadAllBytes([string]$script:archTaskFiles.out)
        }
    } catch { }
    $res = $null
    $bestScore = -1
    if ($null -ne $bytes -and $bytes.Length -gt 0) {
        $encCodes = @(866, 1251, 65001, 0)
        try { $encCodes += [int][Console]::OutputEncoding.CodePage } catch { }
        foreach ($cp in $encCodes) {
            try {
                $enc = if ($cp -eq 0) { [Text.Encoding]::Default } else { [Text.Encoding]::GetEncoding([int]$cp) }
                $cand = Convert-SchTasksListText ($enc.GetString($bytes))
                $score = 0
                if ($cand.Found) { $score += 10 }
                if ($cand.TaskName) { $score += 2 }
                if ($cand.LastRun) { $score += 2 }
                if ($cand.LastResult -ne '') { $score += 2 }
                if ($score -gt $bestScore) { $bestScore = $score; $res = $cand }
            } catch { }
        }
    }
    if ($null -eq $res) { $res = Convert-SchTasksListText '' }
    $res | Add-Member -NotePropertyName ExitCode -NotePropertyValue $code -Force
    $script:archTask = $res
    $script:archTaskAt = Get-Date
    if (-not $res.Found) {
        $errTxt = ''
        try {
            if ($null -ne $script:archTaskFiles -and (Test-Path -LiteralPath $script:archTaskFiles.err)) {
                $errTxt = ([IO.File]::ReadAllText([string]$script:archTaskFiles.err)).Trim()
            }
        } catch { }
        if (-not $errTxt) { $errTxt = 'задача ' + $script:ArchTaskName + ' не найдена (schtasks, код ' + $code + ')' }
        $script:archTaskErr = (Get-SanitizedText $errTxt)
    } else {
        $script:archTaskErr = ''
    }
    $script:archLinesKey = ''
}

# ---------------------------------------------------------------------------
#  Ручной снимок: archive-run.ps1 в фоне
# ---------------------------------------------------------------------------

function Start-ArchiveSnapshot {
    if ($null -ne $script:archRunProc) { return 'Снимок уже идёт: второй одновременно не запускается.' }
    if ($null -ne $script:archRestoreProc) { return 'Идёт тест восстановления — дождитесь окончания.' }
    if (-not (Test-Path -LiteralPath $script:ArchRunner -PathType Leaf)) { return ('не найден ' + $script:ArchRunner) }
    $py = Find-ArchivePython
    if (-not $py) { return 'не найден python.exe — archive-sd.py без него не работает' }
    $board = [string]$script:cfg.board
    if ($board -notmatch '^https?://[A-Za-z0-9\.\-:\[\]]+$') { $board = 'http://192.168.1.51' }
    $dest = $script:ArchRoot
    foreach ($v in @($script:ArchRunner, $py, $board, $dest)) {
        if ($v -match '["\r\n]') { return 'недопустимый символ в параметрах архива' }
    }
    try {
        $script:archRunOut = Join-Path $script:LogDir 'archive-run.out.txt'
        $script:archRunErr = Join-Path $script:LogDir 'archive-run.err.txt'
        [IO.File]::WriteAllText($script:archRunOut, '')
        [IO.File]::WriteAllText($script:archRunErr, '')
        $script:archRunStart = Get-Date
        $script:archRunState = 'запускаю снимок…'
        $script:archRunProc = Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -PassThru -ArgumentList @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $script:ArchRunner + '"'),
            '-Python', ('"' + $py + '"'), '-Board', ('"' + $board + '"'), '-Destination', ('"' + $dest + '"')
        ) -RedirectStandardOutput $script:archRunOut -RedirectStandardError $script:archRunErr
        $null = $script:archRunProc.Handle
        Add-Log ('архив: снимок запущен вручную (плата ' + $board + ')') 'ok'
        Update-ArchiveCard
        return ''
    } catch {
        $script:archRunProc = $null
        $script:archRunState = 'не удалось запустить снимок: ' + $_.Exception.Message
        return $script:archRunState
    }
}

function Get-ArchivePythonLogLines {
    $p = Join-Path $script:ArchRoot 'run-last.err.txt'
    if (-not (Test-Path -LiteralPath $p)) { return @() }
    try { return @(Get-Content -LiteralPath $p -Tail 80 -Encoding UTF8 -ErrorAction SilentlyContinue) } catch { return @() }
}

function Pump-ArchiveSnapshot {
    if ($null -eq $script:archRunProc) { return }
    $running = $true
    try { $running = (-not $script:archRunProc.HasExited) } catch { $script:archRunProc = $null; return }
    if ($running) {
        $el = [int]((Get-Date) - $script:archRunStart).TotalSeconds
        if ($el -gt 900) {
            try { $script:archRunProc.Kill() } catch { }
        }
        $done = 0
        $last = ''
        foreach ($ln in @(Get-ArchivePythonLogLines)) {
            $s = ([string]$ln).Trim()
            if (-not $s) { continue }
            if ($s -match ':\s+\d+\s+bytes\s*$') { $done++ }
            $last = [regex]::Replace($s, '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d+\s+', '')
            $last = [regex]::Replace($last, '^(INFO|WARNING|ERROR)\s+', '')
        }
        $last = (Get-SanitizedText $last)
        $est = ''
        if ($null -ne $script:archState -and $null -ne $script:archState.Latest -and $script:archState.Latest.FileCount -gt 0) {
            $est = ' из ~' + $script:archState.Latest.FileCount
        }
        $script:archRunState = 'идёт снимок · ' + $el + ' с · файлов ' + $done + $est + $(if ($last) { ' · ' + $last } else { '' })
        return
    }

    # процесс завершился — подводим итог
    $code = -1
    try { $code = [int]$script:archRunProc.ExitCode } catch { }
    try { $script:archRunProc.Dispose() } catch { }
    $script:archRunProc = $null
    $elapsed = [int]((Get-Date) - $script:archRunStart).TotalSeconds
    $logged = @(Get-ArchivePythonLogLines)
    $complete = $null
    $errors = @()
    foreach ($ln in $logged) {
        $s = ([string]$ln).Trim()
        if (-not $s) { continue }
        if ($s -match 'COMPLETE:\s+(\d+)\s+files,\s+(\d+)\s+new downloaded bytes') { $complete = $matches }
        if ($s -match '(?i)(FAILED|Traceback|PermissionError|OSError|ValueError|RuntimeError)') { $errors += $s }
    }
    if ($code -eq 0 -and $null -ne $complete) {
        $bytes = [double]$complete[2]
        $script:archRunOkAt = Get-Date
        $script:archRunState = 'снимок готов ' + (Get-Date).ToString('HH:mm:ss') + ' за ' + $elapsed + ' с · файлов ' + $complete[1] + ' · новых данных ' + (Format-TermSize $bytes) + ' · SHA-256 проверен при записи'
        Add-Log ('архив: снимок готов — ' + $complete[1] + ' файлов, ' + (Format-TermSize $bytes)) 'ok'
    } else {
        $why = ''
        if ($errors.Count -gt 0) { $why = (Get-SanitizedText ([string]$errors[$errors.Count - 1])) }
        if (-not $why) {
            foreach ($p in @($script:archRunErr, $script:archRunOut)) {
                try {
                    if ($p -and (Test-Path -LiteralPath $p)) {
                        $txt = ([IO.File]::ReadAllText($p)).Trim()
                        if ($txt) { $why = (Get-SanitizedText (($txt -split "`r?`n")[0])) }
                    }
                } catch { }
                if ($why) { break }
            }
        }
        if (-not $why) { $why = 'нет строки ошибки в run-last.err.txt' }
        if ($why -match '(?i)(PermissionError|Errno 13|lock)') { $why = 'архив занят другим снимком (общая блокировка; повторите позже)' }
        $script:archRunState = 'снимок НЕ удался (код ' + $code + '): ' + $why
        Add-Log ('архив: снимок не удался, код ' + $code + ' — ' + $why) 'err'
    }
    $script:archStateAt = [datetime]::MinValue
    $script:archLinesKey = ''
    try {
        if ($null -ne $pgArch -and $pgArch.Visible) {
            try { $script:archState = Get-ArchiveState -Verify } catch { }
            $script:archStateAt = Get-Date
            $script:archVerifyAt = Get-Date
        }
    } catch { }
    Update-ArchiveCard
}

# ---------------------------------------------------------------------------
#  Тест восстановления: archive-sd.py --restore во временную папку
# ---------------------------------------------------------------------------

function Remove-ArchiveRestoreTemp([string]$path) {
    # Удаляем ТОЛЬКО свою временную папку восстановления: проверяем путь,
    # имя, отсутствие симлинков и повторных точек. Ничего вне её не трогаем.
    if (-not $path) { return 'путь не задан' }
    $full = ''
    try { $full = [IO.Path]::GetFullPath($path) } catch { return 'путь не разобран' }
    $tmpRoot = ''
    try { $tmpRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\' } catch { return 'TEMP не определён' }
    if (-not $full.StartsWith($tmpRoot, [StringComparison]::OrdinalIgnoreCase)) { return 'отказ: папка не внутри %TEMP%' }
    $leaf = [IO.Path]::GetFileName($full)
    if ($leaf -notmatch '^DomEnergy-Restore-\d{8}-\d{6}-[0-9a-f]{4}$') { return 'отказ: имя папки не наше' }
    if (-not (Test-Path -LiteralPath $full)) { return 'папки уже нет' }
    try {
        foreach ($it in @(Get-ChildItem -LiteralPath $full -Recurse -Force -ErrorAction Stop)) {
            if (($it.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return 'отказ: внутри симлинк/повторная точка' }
        }
        foreach ($f in @(Get-ChildItem -LiteralPath $full -Recurse -File -Force -ErrorAction Stop)) {
            [IO.File]::Delete($f.FullName)
        }
        $dirs = @(Get-ChildItem -LiteralPath $full -Recurse -Directory -Force -ErrorAction Stop)
        $dirs = @($dirs | Sort-Object { $_.FullName.Length } -Descending)
        foreach ($d in $dirs) { [IO.Directory]::Delete($d.FullName, $false) }
        [IO.Directory]::Delete($full, $false)
        return ''
    } catch {
        return ('не удалось удалить: ' + $_.Exception.Message)
    }
}

function Start-ArchiveRestore {
    if ($null -ne $script:archRunProc) { return 'Идёт снимок — тест восстановления одновременно не запускается.' }
    if ($null -ne $script:archRestoreProc) { return 'Тест восстановления уже идёт.' }
    if (-not (Test-Path -LiteralPath $script:ArchSd -PathType Leaf)) { return ('не найден ' + $script:ArchSd) }
    $py = Find-ArchivePython
    if (-not $py) { return 'не найден python.exe — тест невозможен' }
    $man = Get-ArchiveSnapshotManifestFile
    if ($null -eq $man) { return 'в архиве нет манифестов снимков — тест невозможен' }
    $tmp = Join-Path $env:TEMP ('DomEnergy-Restore-' + (Get-Date).ToString('yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0, 4)))
    if (Test-Path -LiteralPath $tmp) { return 'временная папка уже существует — не рискую' }
    $out = Join-Path $script:LogDir 'archive-restore.out.txt'
    $err = Join-Path $script:LogDir 'archive-restore.err.txt'
    try {
        [IO.File]::WriteAllText($out, '')
        [IO.File]::WriteAllText($err, '')
        $script:archRestoreTmp = $tmp
        $script:archRestoreManifest = $man.FullName
        $script:archRestoreStart = Get-Date
        $script:archRestoreState = 'восстанавливаю последний снимок во временную папку…'
        foreach ($v in @($script:ArchSd, $script:ArchRoot, $man.FullName, $tmp)) {
            if ($v -match '["\r\n]') { return 'недопустимый символ в параметрах восстановления' }
        }
        $script:archRestoreProc = Start-Process -FilePath $py -WindowStyle Hidden -PassThru -ArgumentList @(
            ('"' + $script:ArchSd + '"'), '--dest', ('"' + $script:ArchRoot + '"'),
            '--restore', ('"' + $man.FullName + '"'), '--restore-dir', ('"' + $tmp + '"')
        ) -RedirectStandardOutput $out -RedirectStandardError $err
        $null = $script:archRestoreProc.Handle
        Add-Log ('архив: тест восстановления — манифест ' + (Split-Path $man.FullName -Leaf)) 'ok'
        Update-ArchiveCard
        return ''
    } catch {
        $script:archRestoreProc = $null
        $script:archRestoreState = 'не удалось запустить тест: ' + $_.Exception.Message
        return $script:archRestoreState
    }
}

function Pump-ArchiveRestore {
    if ($null -eq $script:archRestoreProc) { return }
    $running = $true
    try { $running = (-not $script:archRestoreProc.HasExited) } catch { $script:archRestoreProc = $null; return }
    if ($running) {
        $el = [int]((Get-Date) - $script:archRestoreStart).TotalSeconds
        if ($el -gt 300) { try { $script:archRestoreProc.Kill() } catch { } }
        $script:archRestoreState = 'идёт тест восстановления · ' + $el + ' с · пишет только во временную папку'
        return
    }
    $code = -1
    try { $code = [int]$script:archRestoreProc.ExitCode } catch { }
    try { $script:archRestoreProc.Dispose() } catch { }
    $script:archRestoreProc = $null
    $tmp = [string]$script:archRestoreTmp
    $manPath = [string]$script:archRestoreManifest
    $elapsed = [int]((Get-Date) - $script:archRestoreStart).TotalSeconds
    $logTxt = ''
    $errTxt = ''
    try {
        $errP = Join-Path $script:LogDir 'archive-restore.err.txt'
        if (Test-Path -LiteralPath $errP) { $errTxt = ([IO.File]::ReadAllText($errP)).Trim() }
    } catch { }
    try {
        $outP = Join-Path $script:LogDir 'archive-restore.out.txt'
        if (Test-Path -LiteralPath $outP) { $logTxt = ([IO.File]::ReadAllText($outP)).Trim() }
    } catch { }

    # 1) разбор манифеста и своя проверка SHA-256 восстановленных файлов
    $man = $null
    try { if ($manPath -and (Test-Path -LiteralPath $manPath)) { $man = Convert-ArchiveManifestText ([IO.File]::ReadAllText($manPath, [Text.Encoding]::UTF8)) } } catch { $man = $null }
    $okFiles = 0
    $bad = @()
    $checked = 0
    if ($null -ne $man -and $man.Ok -and $tmp) {
        foreach ($f in @($man.Files)) {
            $checked++
            $p = Join-Path $tmp $f.Name
            if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { $bad += ($f.Name + ' — файла нет'); continue }
            try {
                $sz = (Get-Item -LiteralPath $p).Length
                if ([int64]$sz -ne [int64]$f.Size) { $bad += ($f.Name + ' — размер ' + $sz + ' вместо ' + $f.Size); continue }
                $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($h -ne $f.Sha256) { $bad += ($f.Name + ' — SHA-256 не совпал'); continue }
                $okFiles++
            } catch {
                $bad += ($f.Name + ' — не удалось проверить: ' + $_.Exception.Message)
            }
        }
    }
    $restoredCount = -1
    foreach ($ln in (@($logTxt, $errTxt) -join "`r`n") -split "`r?`n") {
        if ($ln -match 'Restored (\d+) files') { $restoredCount = [int]$matches[1] }
    }

    # 2) отчёт: одна строка на файл в журнал приложения + сводка в карточку
    foreach ($f in @($man.Files)) {
        if ($null -eq $f) { continue }
        $p = Join-Path $tmp $f.Name
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            Add-Log ('тест восстановления: ' + $f.Name + ' — ' + (Format-TermSize $f.Size) + ', SHA-256 сошёлся') 'ok'
        }
    }
    foreach ($b in $bad) { Add-Log ('тест восстановления: ' + $b) 'err' }

    $ok = ($code -eq 0) -and ($null -ne $man) -and ($man.Ok) -and ($checked -gt 0) -and ($bad.Count -eq 0) -and ($okFiles -eq $checked)
    if ($restoredCount -ge 0 -and $null -ne $man -and $man.Ok -and $restoredCount -ne $man.FileCount) { $ok = $false }

    # 3) удаляем ТОЛЬКО эту временную папку
    $delNote = Remove-ArchiveRestoreTemp $tmp
    $delTxt = if ($delNote) { 'временная папка: ' + $delNote } else { 'временная папка удалена' }
    $shaTxt = if ($checked -gt 0) { ('SHA-256 сошлись у ' + $okFiles + ' из ' + $checked) } else { 'файлы не проверены' }

    if ($ok) {
        $script:archRestoreState = 'тест восстановления УСПЕШЕН · ' + $okFiles + ' файлов · ' + $shaTxt + ' · ' + $elapsed + ' с · ' + $delTxt
        Add-Log ('архив: тест восстановления успешен — ' + $okFiles + ' файлов, ' + $shaTxt + ', ' + $delTxt) 'ok'
    } else {
        $why = ''
        if ($bad.Count -gt 0) { $why = [string]$bad[0] }
        elseif ($code -ne 0) {
            $why = (Get-SanitizedText (($errTxt + "`n" + $logTxt) -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -Last 1))
            if ($why -match '(?i)(PermissionError|Errno 13|lock)') { $why = 'архив занят другим снимком (общая блокировка; повторите позже)' }
            elseif (-not $why) { $why = 'код ' + $code + ', вывода нет' }
            else { $why = 'код ' + $code + ': ' + $why }
        } else { $why = 'неполный/битый манифест или нет файлов' }
        $script:archRestoreState = 'тест восстановления НЕ УДАЛСЯ · ' + $why + ' · ' + $delTxt
        Add-Log ('архив: тест восстановления не удался — ' + (Get-SanitizedText $why)) 'err'
    }
    $script:archRestoreLast = $script:archRestoreState
    $script:archRestoreTmp = ''
    $script:archRestoreManifest = ''
    $script:archLinesKey = ''
    try {
        $line = '[' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') + '] ' + $(if ($ok) { 'OK' } else { 'FAIL' }) + ' files=' + $okFiles + '/' + $checked + ' restored=' + $restoredCount + ' ' + $delTxt
        [IO.File]::AppendAllText($script:archRestoreLog, $line + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
    } catch { }
    Update-ArchiveCard
}


# ---------------------------------------------------------------------------
#  Обезличенный отчёт диагностики (без IP, MAC, SSID, токенов, device id
#  и без самих значений измерений — только источники, возраст, состояния)
# ---------------------------------------------------------------------------

function Get-AnonDiagnosticsReport {
    $L = New-Object System.Collections.ArrayList
    $now = Get-Date
    [void]$L.Add('DOM ENERGY · обезличенный отчёт для диагностики')
    [void]$L.Add('сформирован: ' + $now.ToString('dd.MM.yyyy HH:mm:ss') + ' · приложение Windows DOM MONITOR 2.0')
    if ($script:appStart -gt [datetime]::MinValue) {
        [void]$L.Add('приложение в работе: ' + (Format-Uptime ([int]($now - $script:appStart).TotalSeconds)))
    }
    [void]$L.Add('')
    [void]$L.Add('== ПЛАТА ==')
    $d = $script:board
    if ($null -eq $d) {
        $why = Get-SanitizedText ([string]$script:boardErr)
        if (-not $why) { $why = 'нет ответа /data' }
        [void]$L.Add('плата: НЕ отвечает — ' + $why)
    } else {
        $age = [int]($now - $script:boardAt).TotalSeconds
        if ($age -lt 0) { $age = 0 }
        [void]$L.Add('версия прошивки: ' + (Get-SanitizedText ([string](Field $d @('fw') '—'))) + ' · build: ' + (Get-SanitizedText ([string](Field $d @('build') '—'))))
        [void]$L.Add('uptime платы: ' + (Format-Uptime (Field $d @('up'))) + ' · ответ /data: ' + $age + ' с назад (LAN/HTTP)')
        $rssi = Field $d @('rssi')
        $relay = Field $d @('r')
        $lan = 'LAN: плата отвечает'
        if ($null -ne $rssi) { $lan += ' · Wi-Fi ' + [int]$rssi + ' dBm' }
        if ($null -ne $relay) { $lan += ' · реле ' + $(if ([bool]$relay) { 'ВКЛ' } else { 'ВЫКЛ' }) }
        [void]$L.Add($lan)
        $net = Field $d @('net')
        $cTry = [int](Field $d @('cTry') 0)
        $cOk = [int](Field $d @('cOk') 0)
        $cFail = [int](Field $d @('cFail') 0)
        $cWhy = [int](Field $d @('cWhy') 0)
        $cAge = Field $d @('cAge')
        $wan = 'WAN: '
        if ($null -ne $net -and -not [bool]$net) { $wan += 'интернета с платы нет (net=false)' } else { $wan += 'интернет есть' }
        $wan += ' · облако: cTry ' + $cTry + ' · cOk ' + $cOk + ' · cFail ' + $cFail + ' · cWhy ' + $cWhy + ' (' + (Get-CloudWhyRu $cWhy) + ')'
        if ($null -ne $cAge) { $wan += ' · последняя попытка ' + (Format-Age ([int]$cAge)) + ' назад' }
        [void]$L.Add($wan)
        [void]$L.Add('WAN задержки, мс: DNS ' + [string](Field $d @('msDns') '—') + ' · TLS ' + [string](Field $d @('msTls') '—') + ' · облако ' + [string](Field $d @('msCloud') '—') + ' · Tuya ' + [string](Field $d @('msTuya') '—') + ' · запись SD ' + [string](Field $d @('msWrite') '—'))
        [void]$L.Add('облачный канал: cliSt ' + [string](Field $d @('cliSt') '—') + ' · HTTP ' + [string](Field $d @('cliHttp') '—') + ' · длина ответа ' + [string](Field $d @('cliLen') '—') + ' Б')
        if ($null -ne $script:measure) {
            $cok = Get-MeasureField $script:measure @('clockOk')
            if ($null -ne $cok -and [bool]$cok) { [void]$L.Add('время: синхронизировано (clockOk=да) · часы платы ' + (Get-SanitizedText ([string](Field $d @('now') '—')))) }
            else { [void]$L.Add('время: НЕ синхронизировано (clockOk=нет) — история по часам ненадёжна') }
        } else {
            [void]$L.Add('время: clockOk неизвестен — нет ответа /measure')
        }
        [void]$L.Add('производительность: loopMs ' + [string](Field $d @('loopMs') '—') + ' · loopMaxMs ' + [string](Field $d @('loopMaxMs') '—') + ' · loopMaxAll ' + [string](Field $d @('loopMaxAll') '—'))
        [void]$L.Add('  blk ' + (Get-SanitizedText ([string](Field $d @('blk') '—'))) + ' blkMs ' + [string](Field $d @('blkMs') '—') + ' · histMs ' + [string](Field $d @('histMs') '—') + ' · sdMs ' + [string](Field $d @('sdMs') '—') + ' · pageMs ' + [string](Field $d @('pageMs') '—'))
        [void]$L.Add('  heap ' + (Format-TermKB (Field $d @('heap'))) + ' · free ' + (Format-TermKB (Field $d @('free'))) + ' · maxAlloc ' + (Format-TermKB (Field $d @('maxAlloc'))) + ' · apiN ' + [string](Field $d @('apiN') '—'))
        [void]$L.Add('  pageN ' + [string](Field $d @('pageN') '—') + ' · pageSend ' + [string](Field $d @('pageSend') '—') + ' · pageCut ' + [string](Field $d @('pageCut') '—') + ' · redraw ' + [string](Field $d @('redraw') '—'))
        [void]$L.Add('пропуски (плата): gaps ' + [string](Field $d @('gaps') '—') + ' · gapN ' + [string](Field $d @('gapN') '—') + ' · gapAct ' + [string](Field $d @('gapAct') '—') + ' · строк истории ' + [string](Field $d @('sdRows') '—'))
    }
    [void]$L.Add('')
    [void]$L.Add('== СВЕЖЕСТЬ ИЗМЕРЕНИЙ (источник и возраст, без значений) ==')
    if ($null -eq $script:measure) {
        [void]$L.Add('нет ответа /measure — источник и возраст V/P/I/T неизвестны')
    } else {
        foreach ($nk in @(@('v', 'V'), @('p', 'P'), @('i', 'I'), @('t', 'T'))) {
            $s = Format-MeasureSource $script:measure $nk[0]
            if ($null -eq $s) { [void]$L.Add($nk[1] + ': нет измерений'); continue }
            [void]$L.Add($nk[1] + ': ' + (Get-SanitizedText ([string]$s.Text)))
        }
        if ($script:measureErr) {
            [void]$L.Add('опрос /measure: ' + (Get-SanitizedText ([string]$script:measureErr)))
        } else {
            [void]$L.Add('опрос /measure: отвечает · ответ ' + [int]$script:measMs + ' мс · не чаще 5 с')
        }
    }
    [void]$L.Add('')
    [void]$L.Add('== SD И ПРОПУСКИ ==')
    $stAge = if ($script:archStoreAt -ne [datetime]::MinValue) { [int]((Get-Date) - $script:archStoreAt).TotalSeconds } else { -1 }
    $st = $script:archStore
    if ($null -ne $st) {
        $freeB = Field $st @('freeBytes')
        $totalB = Field $st @('totalBytes')
        $line = 'SD: ok=' + $(if ([string](Field $st @('ok') '') -eq 'False') { 'НЕТ' } else { 'да' })
        if ($null -ne $freeB) { $line += ' · свободно ' + (Format-TermSize $freeB) }
        if ($null -ne $totalB) { $line += ' из ' + (Format-TermSize $totalB) }
        if ($stAge -ge 0) { $line += ' · данные ' + $stAge + ' с назад' }
        $line += ' · политика ' + (Get-SanitizedText ([string](Field $st @('policy') '—'))) + ' · сама удаляет: ' + $(if ([string](Field $st @('autoDelete') 'False') -eq 'True') { 'да' } else { 'нет' })
        [void]$L.Add($line)
        [void]$L.Add('  запись: ok ' + [string](Field $st @('writeOk') '—') + ' · fail ' + [string](Field $st @('writeFail') '—') + ' · подряд ' + [string](Field $st @('writeFailRun') '—') + ' · последняя запись ' + (Format-Age ([int](Field $st @('lastWriteAgeSec') 0))) + ' назад · archiveError ' + (Get-SanitizedText ([string](Field $st @('archiveError') '—'))))
        [void]$L.Add('  lowSpace ' + [string](Field $st @('lowSpace') '—') + ' · lastWriteErr ' + [string](Field $st @('lastWriteErr') '—') + ' · lastWriteMsg «' + (Get-SanitizedText ([string](Field $st @('lastWriteMsg') ''))) + '»')
    } else {
        [void]$L.Add('SD: /storage пока не ответил')
    }
    $gfAge = if ($script:archGapAt -ne [datetime]::MinValue) { [int]((Get-Date) - $script:archGapAt).TotalSeconds } else { -1 }
    $gf = $script:archGapfill
    if ($null -ne $gf) {
        $gfLine = 'пропуски: commitStage ' + (Get-SanitizedText ([string](Field $gf @('commitStage') '—'))) + ' · commitCode ' + (Get-SanitizedText ([string](Field $gf @('commitCode') '—'))) + ' · noData ' + [string](Field $gf @('noData') '—') + ' · active ' + [string](Field $gf @('active') '—')
        if ($gfAge -ge 0) { $gfLine += ' · данные ' + $gfAge + ' с назад' }
        [void]$L.Add($gfLine)
        [void]$L.Add('  gaps ' + [string](Field $gf @('gaps') '—') + ' · todayN ' + [string](Field $gf @('todayN') '—') + ' · todayMin ' + [string](Field $gf @('todayMin') '—') + ' · sdFault ' + [string](Field $gf @('sdFault') '—') + ' · msg «' + (Get-SanitizedText ([string](Field $gf @('msg') ''))) + '»')
    } else {
        [void]$L.Add('пропуски: /gapfill/status пока не ответил')
    }
    [void]$L.Add('')
    [void]$L.Add('== АРХИВ НА ПК ==')
    $a = $script:archState
    if ($null -eq $a -or -not $a.Exists) {
        [void]$L.Add('архив: папка ' + $script:ArchRoot + ' не найдена или ещё не прочитана')
    } else {
        if ($null -ne $a.Latest -and $a.Latest.Ok) {
            $when = '—'
            $ageTxt = '—'
            if ($null -ne $a.Latest.CreatedUtc) {
                $when = $a.Latest.CreatedUtc.ToLocalTime().ToString('dd.MM.yyyy HH:mm:ss')
                $ageTxt = Format-Age ([int](($now.ToUniversalTime() - $a.Latest.CreatedUtc).TotalSeconds))
            }
            [void]$L.Add('последняя проверенная копия: ' + $when + ' · возраст ' + $ageTxt + ' · файлов ' + $a.Latest.FileCount + ' · ' + (Format-TermSize $a.Latest.TotalBytes))
        } else {
            [void]$L.Add('последняя копия: манифест не найден')
        }
        if ($null -ne $a.VerifiedAt) {
            [void]$L.Add('  SHA-256 последней копии: ' + $a.VerifyOk + ' из ' + ($a.VerifyOk + $a.VerifyBad) + ' объектов совпали · проверено ' + $a.VerifiedAt.ToString('dd.MM HH:mm:ss'))
        }
        [void]$L.Add('  снимков ' + $a.Snapshots + ' · объектов ' + $a.Objects + ' · полных независимых копий ' + $a.Zips + ' · свободно на ' + $a.Drive + ' ' + (Format-TermSize $a.FreeBytes))
        [void]$L.Add('  последний запуск задачи: ' + $(if ($null -ne $a.RunLastWhen) { $a.RunLastWhen.ToString('dd.MM HH:mm:ss') } else { '—' }) + ' · ' + (Get-SanitizedText ([string]$a.RunLastLine)))
        if ($a.LastError) { [void]$L.Add('  последняя ошибка архива: ' + (Get-SanitizedText ([string]$a.LastError))) }
        else { [void]$L.Add('  ошибок архива в журнале нет') }
    }
    if ($null -ne $script:archTask -and $script:archTask.Found) {
        $stTask = Get-SchTaskStateRu ([string]$script:archTask.State) ([string]$script:archTask.Status)
        [void]$L.Add('задача ' + (([string]$script:archTask.TaskName).TrimStart('\')) + ': ' + $stTask + ' · последний запуск ' + $(if ($script:archTask.LastRun) { $script:archTask.LastRun } else { '—' }) + ' · код ' + (Get-SchTaskResultRu ([string]$script:archTask.LastResult)))
    } elseif ($script:archTaskErr) {
        [void]$L.Add('задача ' + $script:ArchTaskName + ': ' + (Get-SanitizedText ([string]$script:archTaskErr)))
    }
    if ($script:archRunState) { [void]$L.Add('ручной снимок: ' + (Get-SanitizedText ([string]$script:archRunState))) }
    if ($script:archRestoreLast) { [void]$L.Add('тест восстановления: ' + (Get-SanitizedText ([string]$script:archRestoreLast))) }
    [void]$L.Add('')
    [void]$L.Add('== ОШИБКИ ТЕКУЩЕГО СОСТОЯНИЯ ==')
    $errs = @()
    if ($script:boardErr) { $errs += ('/data: ' + (Get-SanitizedText ([string]$script:boardErr))) }
    if ($script:measureErr) { $errs += ('/measure: ' + (Get-SanitizedText ([string]$script:measureErr))) }
    if ($script:archTaskErr) { $errs += ('задача ' + $script:ArchTaskName + ': ' + (Get-SanitizedText ([string]$script:archTaskErr))) }
    if ($null -ne $script:archState -and $script:archState.LastError) { $errs += ('архив: ' + (Get-SanitizedText ([string]$script:archState.LastError))) }
    if ($errs.Count -eq 0) { [void]$L.Add('ошибок в текущем состоянии нет') }
    else { foreach ($e in $errs) { [void]$L.Add([string]$e) } }
    [void]$L.Add('')
    [void]$L.Add('== ЧЕГО В ОТЧЁТЕ НЕТ ==')
    [void]$L.Add('IP-адресов, MAC, имени Wi-Fi, токенов и ключей, device id платы, сырых значений измерений.')
    [void]$L.Add('Отчёт никуда не отправляется автоматически — только предпросмотр и копирование в буфер.')
    return $L.ToArray()
}

# ---------------------------------------------------------------------------
#  Текст карточки «Архив»
# ---------------------------------------------------------------------------

function Format-ArchiveStatusLines {
    $out = New-Object System.Collections.ArrayList
    $st = $script:archState
    $task = $script:archTask
    if ($null -eq $st) {
        [void]$out.Add(@{ text = 'АРХИВ: читаю состояние…'; color = 'dim' })
        return $out.ToArray()
    }
    if (-not $st.Exists) {
        [void]$out.Add(@{ text = ('АРХИВ: папка ' + $st.Root + ' не найдена'); color = 'err' })
        [void]$out.Add(@{ text = 'Проверьте, что диск D: подключён и архив создан archive-run.ps1.'; color = 'warn' })
        return $out.ToArray()
    }

    # --- задача планировщика
    if ($null -ne $task -and $task.Found) {
        $stateRu = Get-SchTaskStateRu ([string]$task.State) ([string]$task.Status)
        $col = 'ok'
        if ($stateRu -match 'ОТКЛЮЧЕНА') { $col = 'err' }
        elseif ($stateRu -match '(?i)(выполняется|в очереди)') { $col = 'warn' }
        [void]$out.Add(@{ text = ('ЗАДАЧА ' + $script:ArchTaskName + ': ' + $stateRu); color = $col })
        $line = '  запуск ' + $(if ($task.LastRun) { $task.LastRun } else { '—' }) + ' · код ' + (Get-SchTaskResultRu ([string]$task.LastResult))
        if ($task.NextRun) { $line += ' · следующий ' + $task.NextRun }
        if ($task.LastResult -ne '' -and $task.LastResult -ne '0') { $col = 'warn' }
        [void]$out.Add(@{ text = $line; color = $col })
        $lr = Convert-SchTaskDate ([string]$task.LastRun)
        if ($null -ne $lr) {
            $hrs = [int]((Get-Date) - $lr).TotalHours
            if ($hrs -gt 26) {
                [void]$out.Add(@{ text = ('  внимание: задача не запускалась ~' + $hrs + ' ч — возможно, ПК был выключен.'); color = 'warn' })
                [void]$out.Add(@{ text = '  Копия с ПК требует включённого ПК: за пропущенные дни снимков не будет.'; color = 'warn' })
            }
        }
    } elseif ($script:archTaskErr) {
        [void]$out.Add(@{ text = ('ЗАДАЧА ' + $script:ArchTaskName + ': ' + (Get-SanitizedText ([string]$script:archTaskErr))); color = 'err' })
    } else {
        [void]$out.Add(@{ text = ('ЗАДАЧА ' + $script:ArchTaskName + ': читаю schtasks…'); color = 'dim' })
    }

    # --- последняя проверенная копия
    if ($null -ne $st.Latest -and $st.Latest.Ok -and $null -ne $st.Latest.CreatedUtc) {
        $when = $st.Latest.CreatedUtc.ToLocalTime().ToString('dd.MM.yyyy HH:mm:ss')
        $ageSec = [int]((Get-Date).ToUniversalTime() - $st.Latest.CreatedUtc).TotalSeconds
        if ($ageSec -lt 0) { $ageSec = 0 }
        [void]$out.Add(@{ text = ('ПОСЛЕДНЯЯ ПРОВЕРЕННАЯ КОПИЯ: ' + $when); color = 'ok' })
        $line = '  возраст ' + (Format-Age $ageSec) + ' · файлов ' + $st.Latest.FileCount + ' · ' + (Format-TermSize $st.Latest.TotalBytes)
        if ($null -ne $st.VerifiedAt) {
            $line += ' · SHA-256: ' + $st.VerifyOk + ' из ' + ($st.VerifyOk + $st.VerifyBad) + ' объектов совпали'
        }
        $col = 'normal'
        if ($ageSec -gt 7 * 86400) { $col = 'err' } elseif ($ageSec -gt 36 * 3600) { $col = 'warn' }
        [void]$out.Add(@{ text = $line; color = $col })
        if ($null -ne $st.VerifiedAt) {
            [void]$out.Add(@{ text = ('  проверено ' + $st.VerifiedAt.ToString('dd.MM HH:mm:ss') + ' · ' + $st.VerifyMs + ' мс · объекты gzip не удаляются'); color = $(if ($st.VerifyBad -gt 0) { 'err' } else { 'dim' }) })
            if ($st.VerifyBad -gt 0) {
                [void]$out.Add(@{ text = ('  ПОВРЕЖДЕНО объектов: ' + $st.VerifyBad + ' — ' + (($st.VerifyBadNames) -join ', ')); color = 'err' })
            }
        }
    } else {
        [void]$out.Add(@{ text = 'ПОСЛЕДНЯЯ ПРОВЕРЕННАЯ КОПИЯ: снимков ещё нет'; color = 'warn' })
    }

    # --- путь, снимки, место
    [void]$out.Add(@{ text = ('АРХИВ: ' + $st.Root); color = 'normal' })
    $line = '  снимков ' + $st.Snapshots + ' · объектов ' + $st.Objects + $(if ($st.ObjectsBytes -gt 0) { ' (' + (Format-TermSize $st.ObjectsBytes) + ')' } else { '' })
    if ($null -ne $st.FreeBytes) { $line += ' · свободно на ' + $st.Drive + ' ' + (Format-TermSize $st.FreeBytes) }
    if ($null -ne $st.TotalBytes) { $line += ' из ' + (Format-TermSize $st.TotalBytes) }
    [void]$out.Add(@{ text = $line; color = 'normal' })

    # --- полные независимые копии
    if ($st.Zips -gt 0) {
        $line = 'ПОЛНЫЕ НЕЗАВИСИМЫЕ КОПИИ: ' + $st.Zips + ' · последняя ' + $(if ($null -ne $st.LastZipAt) { $st.LastZipAt.ToString('dd.MM HH:mm') } else { '—' })
        [void]$out.Add(@{ text = $line; color = 'ok' })
        [void]$out.Add(@{ text = '  PRIVATE = полный flash+NVS (Wi-Fi и токены). Не публиковать и не пересылать.'; color = 'warn' })
        [void]$out.Add(@{ text = ('  ' + (Format-TermSize $st.LastZipBytes) + ' · ZIP + .sha256 рядом: ' + $(if ($st.LastZipSha) { 'есть' } else { 'НЕТ' }) + ' · это полные копии, а не счётчик строк'); color = $(if ($st.LastZipSha) { 'dim' } else { 'warn' }) })
    } else {
        [void]$out.Add(@{ text = 'ПОЛНЫЕ НЕЗАВИСИМЫЕ КОПИИ: не найдены'; color = 'warn' })
        [void]$out.Add(@{ text = '  ежедневные снимки SD — не замена полной копии устройства'; color = 'dim' })
    }

    # --- ошибки и итог последнего запуска
    if ($st.LastError) {
        $when = if ($null -ne $st.LastErrorWhen) { $st.LastErrorWhen.ToString('dd.MM HH:mm:ss') + ' · ' } else { '' }
        [void]$out.Add(@{ text = ('ПОСЛЕДНЯЯ ОШИБКА: ' + $when + $st.LastError); color = 'err' })
    } else {
        [void]$out.Add(@{ text = 'ПОСЛЕДНЯЯ ОШИБКА: нет'; color = 'ok' })
    }
    if ($st.RunLastWhen) {
        $tail = if ($st.RunLastLine) { ' · ' + $st.RunLastLine } else { '' }
        [void]$out.Add(@{ text = ('ПОСЛЕДНИЙ ЗАПУСК: ' + $st.RunLastWhen.ToString('dd.MM HH:mm:ss') + $tail); color = 'dim' })
    }

    # --- ручной снимок и тест восстановления
    $colSnap = 'dim'
    if ($null -ne $script:archRunProc) { $colSnap = 'warn' }
    elseif ($script:archRunState -match '(?i)^снимок готов') { $colSnap = 'ok' }
    elseif ($script:archRunState -match '(?i)(НЕ удался|не удалось)') { $colSnap = 'err' }
    [void]$out.Add(@{ text = ('СНИМОК СЕЙЧАС: ' + $script:archRunState); color = $colSnap })
    $colRes = 'dim'
    if ($null -ne $script:archRestoreProc) { $colRes = 'warn' }
    elseif ($script:archRestoreState -match '(?i)УСПЕШЕН') { $colRes = 'ok' }
    elseif ($script:archRestoreState -match '(?i)(НЕ УДАЛСЯ|не удалось)') { $colRes = 'err' }
    [void]$out.Add(@{ text = ('ТЕСТ ВОССТАНОВЛЕНИЯ: ' + $script:archRestoreState); color = $colRes })
    if ($script:archRestoreLast -and $script:archRestoreState -eq 'тест восстановления не запускался') {
        [void]$out.Add(@{ text = ('  ' + $script:archRestoreLast); color = 'dim' })
    }

    # --- честные подписи
    [void]$out.Add(@{ text = 'Копия с ПК требует включённого ПК: пока ПК выключен, ежедневная задача не выполняется.'; color = 'dim' })
    [void]$out.Add(@{ text = 'Два снимка одновременно не запускаются: пока идёт снимок или тест, кнопки ждут.'; color = 'dim' })
    [void]$out.Add(@{ text = 'Тест пишет только во временную папку %TEMP% и удаляет её; на карту платы ничего не пишется.'; color = 'dim' })
    return $out.ToArray()
}


# ---------------------------------------------------------------------------
#  Страница «Архив»: карточка архива + центр обезличенной диагностики.
#  Это ОТДЕЛЬНАЯ новая вкладка: зафиксированные экран, шапка и раскладка
#  главного экрана не меняются, существующие вкладки остаются на местах.
# ---------------------------------------------------------------------------

$pgArch = New-TabPage
Add-Page 'arch' $pgArch

$lblArchHead = New-Lbl $pgArch 'АРХИВ И ДИАГНОСТИКА — независимая копия SD на этом ПК и обезличенный отчёт. Автоматических отправок нет.' 9 $script:CLR.dim2 $false 14 10
$lblArchCap  = New-Lbl $pgArch 'АРХИВ SD · КОПИЯ НА ЭТОМ ПК' 8 $script:CLR.dim2 $true 14 0
$lblDiagCap  = New-Lbl $pgArch 'ДИАГНОСТИКА · ОБЕЗЛИЧЕННЫЙ ОТЧЁТ' 8 $script:CLR.dim2 $true 14 0

$script:txtArch = New-TermBox $pgArch
try { $script:txtArch.Font = Fnt 'Consolas' 8.5 $false; $script:txtArch.WordWrap = $false } catch { }
$script:txtDiag = New-TermBox $pgArch
try { $script:txtDiag.Font = Fnt 'Consolas' 8.5 $false; $script:txtDiag.WordWrap = $true } catch { }

$btnArchSnap    = New-Btn $pgArch 'Снимок сейчас' '#1F7A46' '#FFFFFF' 150 30
$btnArchRestore = New-Btn $pgArch 'Тест восстановления' '#2E4A6B' '#FFFFFF' 180 30
$btnArchRefresh = New-Btn $pgArch 'Обновить' '#24405A' $script:CLR.text 110 30
$lblArchHint = New-Lbl $pgArch 'обновление…' 8 $script:CLR.dim2 $false 14 0
$lblArchHint.AutoSize = $false
$lblArchHint.Size = New-Object System.Drawing.Size(300, 18)

$btnDiagRefresh = New-Btn $pgArch 'Обновить' '#24405A' $script:CLR.text 110 30
$btnDiagCopy    = New-Btn $pgArch 'Скопировать' '#2E4A6B' '#FFFFFF' 130 30
$lblDiagState = New-Lbl $pgArch 'предпросмотр · в буфер — только по кнопке' 8 $script:CLR.dim2 $false 14 0
$lblDiagState.AutoSize = $false
$lblDiagState.Size = New-Object System.Drawing.Size(300, 18)

if ($null -ne $script:tipMain) {
    try {
        $script:tipMain.SetToolTip($btnArchSnap, "Сразу снять копию SD через archive-run.ps1.`r`nВторой снимок одновременно не запускается. Работает в фоне, окно не блокирует.")
        $script:tipMain.SetToolTip($btnArchRestore, "Восстановить последний снимок во временную папку и сверить SHA-256 каждого файла.`r`nЗатем удаляется ТОЛЬКО эта временная папка; на карту платы ничего не пишется.")
        $script:tipMain.SetToolTip($btnDiagCopy, 'Копирует в буфер ровно тот обезличенный текст, что в предпросмотре. Автоматической отправки нет.')
    } catch { }
}

function Layout-Arch {
    if (-not $script:uiReady) { return }
    $pw = $pgArch.ClientSize.Width
    $ph = $pgArch.ClientSize.Height
    if ($pw -lt 420 -or $ph -lt 320) { return }
    $m = 14
    $capH = 18
    $capY = 36
    $btnH = 30
    $btnY = $ph - $m - $btnH
    $statusY = $btnY - 22
    $boxY = $capY + $capH
    $boxH = $statusY - 6 - $boxY
    if ($boxH -lt 120) { $boxH = 120 }
    $colW = [int](($pw - 3 * $m) * 0.58)
    if ($colW -lt 340) { $colW = 340 }
    if ($colW -gt ($pw - 2 * $m - 260)) { $colW = [Math]::Max(300, $pw - 2 * $m - 260) }
    $col2X = $m + $colW + $m
    $col2W = [Math]::Max(260, $pw - 2 * $m - $colW - $m)

    $lblArchCap.Location = New-Object System.Drawing.Point($m, $capY)
    $lblDiagCap.Location = New-Object System.Drawing.Point($col2X, $capY)

    $script:txtArch.Location = New-Object System.Drawing.Point($m, $boxY)
    $script:txtArch.Size = New-Object System.Drawing.Size($colW, $boxH)
    $script:txtDiag.Location = New-Object System.Drawing.Point($col2X, $boxY)
    $script:txtDiag.Size = New-Object System.Drawing.Size($col2W, $boxH)

    $btnArchSnap.Location = New-Object System.Drawing.Point($m, $btnY)
    $btnArchRestore.Location = New-Object System.Drawing.Point(($m + 158), $btnY)
    $btnArchRefresh.Location = New-Object System.Drawing.Point(($m + 346), $btnY)
    $hintX = $m + 464
    $hintW = $colW - 464
    if ($hintW -lt 100) { $hintW = 100 }
    $lblArchHint.Location = New-Object System.Drawing.Point($hintX, ($btnY + 6))
    $lblArchHint.Size = New-Object System.Drawing.Size($hintW, 18)

    $btnDiagRefresh.Location = New-Object System.Drawing.Point($col2X, $btnY)
    $btnDiagCopy.Location = New-Object System.Drawing.Point(($col2X + 118), $btnY)
    $stX = $col2X + 256
    $stW = $col2W - 256
    if ($stW -lt 100) { $stW = 100 }
    $lblDiagState.Location = New-Object System.Drawing.Point($stX, ($btnY + 6))
    $lblDiagState.Size = New-Object System.Drawing.Size($stW, 18)
}

function Update-ArchiveCard {
    if ($null -eq $script:txtArch) { return }
    try {
        $lines = Format-ArchiveStatusLines
        $key = (($lines | ForEach-Object { [string]$_.text }) -join "`n")
        if ($key -ne $script:archLinesKey) {
            $script:archLinesKey = $key
            Set-TermLines $script:txtArch $lines
        }
        $busy = ($null -ne $script:archRunProc) -or ($null -ne $script:archRestoreProc)
        if ($null -ne $btnArchSnap) { $btnArchSnap.Enabled = -not $busy }
        if ($null -ne $btnArchRestore) { $btnArchRestore.Enabled = -not $busy }
        if ($null -ne $lblArchHint) {
            $hint = 'обновлено ' + (Get-Date).ToString('HH:mm:ss')
            if ($busy) { $hint = 'идёт операция · кнопки ждут' }
            elseif ($null -ne $script:archState -and $null -ne $script:archState.VerifiedAt) {
                $hint = 'SHA-256 проверен ' + $script:archState.VerifiedAt.ToString('HH:mm:ss')
            }
            Set-Lbl $lblArchHint $hint
            $lblArchHint.ForeColor = C $(if ($busy) { $script:CLR.warn } else { $script:CLR.dim2 })
        }
    } catch { Write-AppLog ('архив: карточка ' + $_.Exception.Message) }
}

function Pump-ArchiveDiag {
    if ($null -eq $pgArch -or -not $pgArch.Visible) { return }
    if ($null -eq $script:http) { try { Init-Http } catch { } }
    if ($null -eq $script:http) { return }
    if ($null -ne $script:archDiagTask) {
        if (-not $script:archDiagTask.IsCompleted) {
            if (((Get-Date) - $script:archDiagStart).TotalSeconds -gt 8) { $script:archDiagTask = $null; $script:archDiagName = '' }
            return
        }
        $task = $script:archDiagTask
        $name = [string]$script:archDiagName
        $script:archDiagTask = $null
        $script:archDiagName = ''
        $raw = $null
        try {
            if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) { $raw = [string]$task.Result }
        } catch { }
        if ($null -ne $raw -and $raw.Trim()) {
            if ($name -eq 'storage') {
                $parsed = Convert-StorageJson $raw
                if ($null -ne $parsed) { $script:archStore = $parsed; $script:archStoreAt = Get-Date }
            } elseif ($name -eq 'gapfill') {
                try { $script:archGapfill = $raw | ConvertFrom-Json; $script:archGapAt = Get-Date } catch { }
            }
        }
        return
    }
    $now = Get-Date
    if (($now - $script:archDiagAt).TotalSeconds -lt 5) { return }
    $want = ''
    if ($null -eq $script:archStore -or (($now - $script:archStoreAt).TotalSeconds -gt 15)) { $want = 'storage' }
    elseif ($null -eq $script:archGapfill -or (($now - $script:archGapAt).TotalSeconds -gt 15)) { $want = 'gapfill' }
    else { return }
    $base = [string]$script:cfg.board
    if (-not $base) { return }
    $script:archDiagAt = $now
    try {
        $script:archDiagStart = $now
        $script:archDiagName = $want
        if ($want -eq 'storage') { $script:archDiagTask = $script:http.GetStringAsync($base + '/storage') }
        else { $script:archDiagTask = $script:http.GetStringAsync($base + '/gapfill/status') }
    } catch {
        $script:archDiagTask = $null
        $script:archDiagName = ''
    }
}

function Pump-ArchiveDiagRender {
    if ($null -eq $script:txtDiag) { return }
    $txt = @(Get-AnonDiagnosticsReport)
    $joined = ($txt -join "`r`n")
    if ($joined -eq $script:archDiagText) { return }
    if ($script:txtDiag.Focused) { return }
    $script:archDiagText = $joined
    $lines = New-Object System.Collections.ArrayList
    foreach ($ln in $txt) {
        $s = [string]$ln
        $role = 'normal'
        if ($s -match '^==') { $role = 'dim' }
        elseif ($s -match '(?i)(НЕ отвечает|НЕ УДАЛСЯ|ошибк|внимание|поврежд|не найден)') { $role = 'warn' }
        [void]$lines.Add(@{ text = $s; color = $role })
    }
    Set-TermLines $script:txtDiag $lines.ToArray()
    if ($null -ne $lblDiagState) {
        Set-Lbl $lblDiagState ('обновлено ' + (Get-Date).ToString('HH:mm:ss') + ' · отправки нет')
    }
}

function Pump-Archive {
    Pump-ArchiveTaskQuery
    Pump-ArchiveSnapshot
    Pump-ArchiveRestore
    $visible = ($null -ne $pgArch -and $pgArch.Visible)
    if (-not $visible) { return }
    if ($null -ne $script:archTaskProc -or $null -ne $script:archRunProc -or $null -ne $script:archRestoreProc) {
        Update-ArchiveCard
    }
    if ($null -eq $script:archTaskProc -and ((Get-Date) - $script:archTaskAt).TotalSeconds -ge 60) {
        Start-ArchiveTaskQuery
    }
    if (((Get-Date) - $script:archStateAt).TotalSeconds -ge 15) {
        $verify = $false
        if ($null -eq $script:archState) { $verify = $true }
        elseif (((Get-Date) - $script:archVerifyAt).TotalSeconds -ge 90) { $verify = $true }
        try {
            $script:archState = Get-ArchiveState -Verify:$verify
            if ($verify) { $script:archVerifyAt = Get-Date }
        } catch {
            Write-AppLog ('архив: состояние — ' + $_.Exception.Message)
        }
        $script:archStateAt = Get-Date
        Update-ArchiveCard
    }
    Pump-ArchiveDiag
    Pump-ArchiveDiagRender
}

function Refresh-Archive {
    param([switch]$Force)
    if (-not $script:uiReady) { return }
    try { Layout-Arch } catch { }
    if ($Force) {
        $script:archStateAt = [datetime]::MinValue
        $script:archTaskAt = [datetime]::MinValue
        $script:archVerifyAt = [datetime]::MinValue
        $script:archDiagAt = [datetime]::MinValue
        $script:archLinesKey = ''
    }
    Pump-Archive
    Update-ArchiveCard
    Pump-ArchiveDiagRender
}

$btnArchSnap.Add_Click({
    $msg = Start-ArchiveSnapshot
    if ($msg) {
        Add-Log ('архив: ' + $msg) 'warn'
        $script:archRunState = $msg
        $script:archLinesKey = ''
        Update-ArchiveCard
    }
})

$btnArchRestore.Add_Click({
    $msg = Start-ArchiveRestore
    if ($msg) {
        Add-Log ('архив: ' + $msg) 'warn'
        $script:archRestoreState = $msg
        $script:archLinesKey = ''
        Update-ArchiveCard
    }
})

$btnArchRefresh.Add_Click({
    Add-Log 'архив: обновляю состояние'
    Refresh-Archive -Force
})

$btnDiagRefresh.Add_Click({
    $script:archDiagAt = [datetime]::MinValue
    $script:archStoreAt = [datetime]::MinValue
    $script:archGapAt = [datetime]::MinValue
    $script:archDiagText = ''
    Pump-ArchiveDiag
    Pump-ArchiveDiagRender
    if ($null -ne $lblDiagState) { Set-Lbl $lblDiagState 'обновляю данные платы…' }
})

$btnDiagCopy.Add_Click({
    try {
        if (-not $script:archDiagText) { Pump-ArchiveDiagRender }
        [System.Windows.Forms.Clipboard]::SetText([string]$script:archDiagText)
        if ($null -ne $lblDiagState) {
            Set-Lbl $lblDiagState ('скопировано ' + (Get-Date).ToString('HH:mm:ss') + ' · без IP/MAC')
        }
        Add-Log 'диагностика: обезличенный отчёт скопирован в буфер' 'ok'
    } catch {
        if ($null -ne $lblDiagState) { Set-Lbl $lblDiagState 'не удалось скопировать — выделите текст и Ctrl+C' }
        Add-Log ('диагностика: копирование не удалось — ' + $_.Exception.Message) 'warn'
    }
})

# последний тест восстановления из журнала (переживает перезапуск приложения)
function Load-ArchiveRestoreLast {
    try {
        if (-not (Test-Path -LiteralPath $script:archRestoreLog)) { return }
        $lines = @(Get-Content -LiteralPath $script:archRestoreLog -Tail 20 -Encoding UTF8 -ErrorAction SilentlyContinue)
        for ($i = $lines.Count - 1; $i -ge 0; $i--) {
            $ln = ([string]$lines[$i]).Trim()
            if (-not $ln) { continue }
            $script:archRestoreLast = 'последний тест ' + $ln
            break
        }
    } catch { }
}
Load-ArchiveRestoreLast
# ============================================================================

#  ОБНОВЛЕНИЕ ИНТЕРФЕЙСА

# ============================================================================

function Add-LogAppend([string]$line) {

    Write-AppLog ('UI ' + $line)

    if (-not $script:uiReady) { return }

    try {

        $script:txtLog.AppendText($line + "`r`n")

        if ($script:txtLog.Lines.Count -gt 2500) {

            $keep = $script:txtLog.Lines[($script:txtLog.Lines.Count - 1800)..($script:txtLog.Lines.Count - 1)]

            $script:txtLog.Text = ($keep -join "`r`n")

        }

        $script:txtLog.SelectionStart = $script:txtLog.TextLength

        $script:txtLog.ScrollToCaret()

    } catch { }

}



# действие пользователя / событие: «[17:09:46] ● текст»

function Add-Log($text, $color = 'normal') {

    $stamp = (Get-Date).ToString('HH:mm:ss')

    $mark = '·'

    switch ($color) {

        'ok'   { $mark = '✓' }

        'warn' { $mark = '▲' }

        'err'  { $mark = '✗' }

        'data' { $mark = '●' }

    }

    Add-LogAppend ("[$stamp] $mark $text")

}



# подробная строка состояния: «17:09:46  HTTP 42 мс · 239 В · 143 Вт · …»

function Add-LogLine([string]$text) {

    Add-LogAppend ((Get-Date).ToString('HH:mm:ss') + '  ' + $text)

}



function Update-Header {

    if ($null -ne $script:lblClimate -and $null -ne $script:board) {

        $eok = Field $script:board @('eok')

        if ($null -ne $eok -and [int]$eok -eq 1) {

            $et = Field $script:board @('et')

            $eh = Field $script:board @('eh')

            $ep = Field $script:board @('ep')

            $txt = 'климат '

            if ($null -ne $et) { $txt += (Num $et '0.0') + '°' }

            if ($null -ne $eh) { $txt += ' · ' + (Num $eh '0') + '%' }

            if ($null -ne $ep) { $txt += ' · ' + [Math]::Round([double]$ep * 0.75006) + ' мм' }

            # погода (раньше её в приложении не было вовсе)
            $wt = Field $script:board @('wT')
            $wd = Field $script:board @('wDesc')
            if ($null -ne $wt -and [string]$wt -ne '') {
                $txt += ' · улица ' + (Num $wt '0.0') + '°'
                if ($wd) { $txt += ' ' + [string]$wd }
            }
            # прогноз на завтра: минимум/максимум и вероятность дождя
            $wtmin = Field $script:board @('wTminT')
            $wtmax = Field $script:board @('wTmaxT')
            $wrain = Field $script:board @('wPopT')
            if ((Field $script:board @('wCodeT') 0) -gt 0 -and $null -ne $wtmin -and $null -ne $wtmax) {
                $txt += ' · завтра ' + (Num $wtmin '0') + '…' + (Num $wtmax '0') + '°'
                if ($null -ne $wrain -and [double]$wrain -gt 0) { $txt += ', дождь ' + (Num $wrain '0') + '%' }
            }

            Set-Lbl $script:lblClimate $txt

            $script:lblClimate.ForeColor = C $script:CLR.text

        } else {

            Set-Lbl $script:lblClimate 'климат: датчик не найден'

            $script:lblClimate.ForeColor = C $script:CLR.dim2

        }

    }

    $age = Get-BoardAge

    $comOpen = ($null -ne $script:port -and $script:port.IsOpen)

    if ($null -ne $script:board -and $age -le 12) {

        Set-Lbl $lblHttp ('HTTP ' + [int]$age + ' с · ' + (Num (Field $script:board @('p') 0) '0') + ' Вт')

        $lblHttp.ForeColor = C $script:CLR.ok

        Set-Dot $script:dotHttp $script:CLR.ok

    } elseif ($comOpen -and $null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) {

        # резерв — только когда порт действительно открыт монитором,

        # иначе в шапке одновременно стояли «РЕЗЕРВ COM5» и «COM5 закрыт»

        Set-Lbl $lblHttp 'РЕЗЕРВ COM5'

        $lblHttp.ForeColor = C $script:CLR.warn

        Set-Dot $script:dotHttp $script:CLR.warn

    } else {

        $txt = 'НЕТ СВЯЗИ'

        if ($null -ne $script:board -and $age -lt 86400) { $txt += ' · ' + [int]$age + ' с' }

        Set-Lbl $lblHttp $txt

        $lblHttp.ForeColor = C $script:CLR.err

        Set-Dot $script:dotHttp $script:CLR.err

    }



    # ------------------------------------------------------------------ данные <время>

    # «данные 12:31» — возраст самых свежих показателей; старше 5 минут — «устарело N мин»

    try {

        $fresh = Get-FreshestStamp

        if ($null -eq $fresh) {

            Set-Lbl $lblFresh 'данные —'

            $lblFresh.ForeColor = C $script:CLR.dim2

        } else {

            $fa = ((Get-Date) - $fresh).TotalSeconds

            if ($fa -gt 300) {

                $fm = [Math]::Floor($fa / 60)

                $ftxt = if ($fm -lt 60) { [int]$fm } else { [int][Math]::Floor($fm / 60) }

                $funit = if ($fm -lt 60) { ' мин' } else { ' ч' }

                Set-Lbl $lblFresh ('устарело ' + $ftxt + $funit)

                $lblFresh.ForeColor = C $script:CLR.warn

            } else {

                Set-Lbl $lblFresh ('данные ' + $fresh.ToString('HH:mm'))

                $lblFresh.ForeColor = C $script:CLR.ok

            }

        }

    } catch {

        Set-Lbl $lblFresh 'данные —'

        $lblFresh.ForeColor = C $script:CLR.dim2

    }



    Update-ComBadge



    # версия прошивки: /data.fw > serial > страница платы (CYD-4.4)

    $fwShow = ''

    if ($script:fwVal) { $fwShow = [string]$script:fwVal }

    elseif ($null -ne $script:serialStatus -and $null -ne $script:serialStatus.fw) { $fwShow = [string]$script:serialStatus.fw }

    if ($fwShow) {

        Set-Lbl $lblFwChip ('прошивка ' + $fwShow)

        $lblFwChip.ForeColor = C $script:CLR.text

    } else {

        Set-Lbl $lblFwChip 'прошивка —'

        $lblFwChip.ForeColor = C $script:CLR.dim

    }



    # вкладка «Устройство»: состояние связи + подсказка про мастер

    if ($null -ne $lblBoardState) {

        $bip = Get-CfgBoardIp

        if ($null -ne $script:board -and $age -le 12) {

            Set-Lbl $lblBoardState '● плата на связи'

            $lblBoardState.ForeColor = C $script:CLR.dim2

        } elseif ($bip) {

            Set-Lbl $lblBoardState ('● плата не отвечает: ' + $bip)

            $lblBoardState.ForeColor = C $script:CLR.warn

        } else {

            Set-Lbl $lblBoardState '● плата не настроена'

            $lblBoardState.ForeColor = C $script:CLR.dim2

        }

    }

}



function Draw-DeviceScreen($g, $pic) {
    # Полная копия главного экрана платы: те же карточки, шрифты и цвета скина.
    try {
        if ($null -eq $g -or $null -eq $pic) { return }
        $W = 240.0; $H = 320.0
        $k = [Math]::Min([double]$pic.Width / $W, [double]$pic.Height / $H)
        $ox = ([double]$pic.Width - $W * $k) / 2
        $oy = ([double]$pic.Height - $H * $k) / 2
        $g.TranslateTransform([single]$ox, [single]$oy)
        $g.ScaleTransform([single]$k, [single]$k)
        $g.SmoothingMode = 'AntiAlias'
        $g.TextRenderingHint = 'ClearTypeGridFit'

        $b = $script:board
        $cl = $script:CLR
        $bg = C (SkinC '#0b111b')
        $card = C (SkinC '#12161C')
        $text = C (SkinC '#FFFFFF')
        $dim = C (SkinC '#8FA3BF')
        $acc = C (SkinC '#22d3ee')
        $fBig = Fnt 'Segoe UI' 18 $true
        $fMid = Fnt 'Segoe UI' 10 $true
        $fSm = Fnt 'Segoe UI' 9 $true
        $fCap = Fnt 'Segoe UI' 6 $false

        $g.Clear($bg)
        # шапка
        $g.DrawString('ДОМ — ЭНЕРГИЯ', $fMid, (New-Object System.Drawing.SolidBrush $text), 8, 5)
        $g.DrawString((Get-Date).ToString('HH:mm'), $fMid, (New-Object System.Drawing.SolidBrush $dim), 196, 5)

        $round = { param($x, $y, $w, $h, $br)
            $p = New-Object System.Drawing.Drawing2D.GraphicsPath
            $r = 6
            $p.AddArc([single]$x, [single]$y, $r*2, $r*2, 180, 90)
            $p.AddArc([single]($x+$w-$r*2), [single]$y, $r*2, $r*2, 270, 90)
            $p.AddArc([single]($x+$w-$r*2), [single]($y+$h-$r*2), $r*2, $r*2, 0, 90)
            $p.AddArc([single]$x, [single]($y+$h-$r*2), $r*2, $r*2, 90, 90)
            $p.CloseFigure()
            return $p
        }

        $drawCard = { param($x, $y, $w, $h, $cap, $val, $unit, $sub)
            $pth = & $round $x $y $w $h
            $g.FillPath((New-Object System.Drawing.SolidBrush $card), $pth)
            $g.DrawString($cap, $fCap, (New-Object System.Drawing.SolidBrush $dim), [single]($x+5), [single]($y+4))
            $g.DrawString($val, $fSm, (New-Object System.Drawing.SolidBrush $text), [single]($x+5), [single]($y+13))
            if ($unit) { $g.DrawString($unit, $fCap, (New-Object System.Drawing.SolidBrush $dim), [single]($x+62), [single]($y+18)) }
            if ($sub) { $g.DrawString($sub, $fCap, (New-Object System.Drawing.SolidBrush $dim), [single]($x+5), [single]($y+$h-11)) }
            $pth.Dispose()
        }

        $v = '—'; $p = '—'; $cur = '—'; $tt = '—'; $hh = '—'; $pp = '—'
        if ($null -ne $b) {
            $vv = Field $b @('v'); if ($null -ne $vv) { $v = (Num $vv '0') }
            $pp2 = Field $b @('p'); if ($null -ne $pp2) { $p = (Num $pp2 '0') }
            $cur = if ($null -ne $vv -and [double]$vv -gt 1 -and $null -ne $pp2) { Num ([double]$pp2 / [double]$vv) '0.00' } else { '0.00' }
            $t2 = Field $b @('temp'); if ($null -ne $t2) { $tt = (Num $t2 '0.0') }
            $h2 = Field $b @('eh'); if ($null -ne $h2) { $hh = (Num $h2 '0') }
            $p3 = Field $b @('ep'); if ($null -ne $p3) { $pp = [string][Math]::Round([double]$p3 * 0.75006) }
        }

        & $drawCard 6 26 112 52 'НАПРЯЖЕНИЕ' $v 'В' ''
        & $drawCard 122 26 112 52 'ТЕМП. РЕЛЕ' $tt '°C' ''
        & $drawCard 6 82 112 52 'МОЩНОСТЬ' $p 'Вт' ('ток ' + $cur + ' А')
        & $drawCard 122 82 112 52 'КЛИМАТ' ($hh + '%') '' ($pp + ' мм · ' + $tt + '°C')
        $todayV = '—'
        if ($null -ne $b) { $tv2 = Field $b @('tkwh'); if ($null -ne $tv2) { $todayV = Num $tv2 '0.00' } }
        & $drawCard 6 138 228 40 'СЕГОДНЯ' $todayV 'кВт·ч' ''

        # климат-строки
        $g.DrawString('ВЛАЖНОСТЬ', $fCap, (New-Object System.Drawing.SolidBrush $dim), 8, 186)
        $g.DrawString(($hh + ' %'), $fSm, (New-Object System.Drawing.SolidBrush $acc), 8, 196)
        $g.DrawString('ДАВЛЕНИЕ', $fCap, (New-Object System.Drawing.SolidBrush $dim), 126, 186)
        $g.DrawString(($pp + ' мм'), $fSm, (New-Object System.Drawing.SolidBrush $text), 126, 196)

        # полоса «сколько сейчас»
        $barW = 228.0
        $pct = 0.0
        if ($null -ne $b) {
            $pm = [double](Field $b @('pmax') 300)
            if ($pm -le 0) { $pm = 300 }
            $pv = [double](Field $b @('p') 0)
            $pct = [Math]::Min(1.0, $pv / $pm)
        }
        $g.FillRectangle((New-Object System.Drawing.SolidBrush (C (SkinC '#1B2230'))), 6, 214, [single]$barW, 8)
        if ($pct -gt 0) {
            $g.FillRectangle((New-Object System.Drawing.SolidBrush $acc), 6, 214, [single]($barW * $pct), 8)
        }

        # нижняя строка
        $g.DrawString((Get-Date).ToString('dd.MM.yyyy'), $fCap, (New-Object System.Drawing.SolidBrush $dim), 6, 228)
        if ($null -eq $b) {
            $g.DrawString('нет связи с платой — ждём /data', $fSm, (New-Object System.Drawing.SolidBrush $dim), 6, 244)
        }
    } catch { Write-AppLog ('deviceScreen: ' + $_.Exception.Message) }
}

function Set-Card($key, [string]$value, [string]$sub, [string]$valueColor, [string]$subColor) {
    try { if ($script:lcd) { $script:lcd.Invalidate() } } catch { }

    $c = $script:cards[$key]

    if ($null -eq $c) { return }

    Set-Lbl $c.Val $value

    Set-Lbl $c.Sub $sub

    if ($valueColor) { $c.Val.ForeColor = C $valueColor }

    if ($subColor)   { $c.Sub.ForeColor = C $subColor }

}



# третья строка карточки: когда показатель реально получен

function Set-CardStamp($key, [string[]]$names) {

    $c = $script:cards[$key]

    if ($null -eq $c) { return }

    $si = Get-StampInfo $names

    Set-Lbl $c.When $si.Text

    $c.When.ForeColor = C $si.Color

}



# карточки, которые считаются из того или иного показателя платы

$script:cardStamp = [ordered]@{

    'v'    = @('vAt')

    't'    = @('tAt')

    'p'    = @('pAt')

    'e'    = @('eAt')

    'cost' = @('eAt')

    'y'    = @('eAt')

    'm'    = @('eAt')

    'all'  = @('eAt')

}



function Update-Cards {

    $d = Get-Active

    $online = Board-Online

    $src = Get-SourceName

    $inv = [System.Globalization.CultureInfo]::InvariantCulture



    if ($null -eq $d) {

        if (-not $script:lcdOffline) {

            $script:lcdOffline = $true

            $lcd.ForeColor = C '#3E4654'

            $lcd.Text = $lcd.Text + "`n`n        НЕТ СВЯЗИ"

        }

        Set-Card 'v'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 't'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'r'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'p'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'e'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'cost' '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'y'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'm'    '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        Set-Card 'all'  '—' 'нет связи с платой' $script:CLR.dim2 $script:CLR.dim2

        foreach ($ck in @($script:cardStamp.Keys)) {

            $cc = $script:cards[$ck]

            if ($null -ne $cc) {

                Set-Lbl $cc.When 'нет данных'

                $cc.When.ForeColor = C $script:CLR.dim2

            }

        }

        $rc = $script:cards['r']

        if ($null -ne $rc) { Set-Lbl $rc.When '' }

        return

    }



    $txtMain  = if ($online) { $script:CLR.text } else { $script:CLR.dim2 }

    $txtGreen = if ($online) { '#63D68B' } else { $script:CLR.dim2 }

    $txtSub   = if ($online) { $script:CLR.dim2 } else { $script:CLR.dim2 }



    # ------------------------------------------------ напряжение

    $v = [double](Field $d @('v', 'voltage') 0)

    $subV = 'в сети 220–240 В'

    $stale = [bool](Field $d @('stale') $false)

    $vAge = Field $d @('vAge')

    if ($stale -and $null -ne $vAge) { $subV = '⚠ устарело ' + (Format-Age $vAge) }

    elseif ($script:histCache.ContainsKey((Get-Date).ToString('yyyy-MM-dd'))) {

        $hs = @($script:histCache[(Get-Date).ToString('yyyy-MM-dd')])

        $vmin = 9999.0; $vmax = 0.0

        foreach ($r in $hs) {

            $vv = [double](Field $r @('v') 0)

            if ($vv -gt 0) { if ($vv -lt $vmin) { $vmin = $vv }; if ($vv -gt $vmax) { $vmax = $vv } }

        }

        if ($vmax -gt 0) { $subV = 'мин ' + (Num $vmin '0') + ' · макс ' + (Num $vmax '0') + ' В за сутки' }

    }

    $colV = if ($stale) { $script:CLR.warn } else { $txtSub }

    $vSeen = Field $d @('vSeen')

    # P0.1: /measure главнее /data — источник и свежесть. Если свежих данных
    # нет и значения не было — «—», а не 0. 0 В и отрицательные °C реальны.
    $mvV = Get-MeasureMetric $script:measure 'v'
    if ($null -ne $mvV) {
        if ($mvV.HasValue) { $v = [double]$mvV.Value }
        if (-not $mvV.HasValue) {
            Set-Card 'v' '— В' 'нет свежих данных' $txtMain $script:CLR.warn
        } else {
            $vCol = if ((-not $mvV.Fresh) -or $stale) { $script:CLR.warn } else { $colV }
            Set-Card 'v' ((Num $v '0') + ' В') $subV $txtMain $vCol
        }
    } elseif ($null -ne $vSeen -and [int]$vSeen -eq 0) {
        Set-Card 'v' '— В' 'ожидание данных реле' $txtMain $script:CLR.warn
    } else {
        Set-Card 'v' ((Num $v '0') + ' В') $subV $txtMain $colV
    }



    # ------------------------------------------------ температура

    $t = Field $d @('temp', 'temperature')

    # P0.1: температура из /measure; 0 и отрицательные значения — настоящие
    $mt = Get-MeasureMetric $script:measure 't'
    if ($null -ne $mt) { $t = $(if ($mt.HasValue) { $mt.Value } else { $null }) }

    $tSeen = Field $d @('tSeen')
    if ($null -ne $t -and ($null -eq $tSeen -or [int]$tSeen -ne 0)) {

        $tf = [double]$t

        $tc = $txtMain

        if ($tf -ge 60) { $tc = '#E85D5D' } elseif ($tf -ge 45) { $tc = '#F2B44B' }

        $subT = 'корпус реле'
        $tempStale = [double](Field $d @('tAge') 0) -gt 180
        if ($tempStale) { $subT = '⚠ температура устарела'; $tc = $script:CLR.warn }
        $eokC = Field $d @('eok')
        if (-not $tempStale -and $null -ne $eokC -and [int]$eokC -eq 1) {
            $etC = Field $d @('et'); $ehC = Field $d @('eh'); $epC = Field $d @('ep')
            $subT = 'воздух ' + (Num $etC '0.0') + '° · ' + (Num $ehC '0') + '%'
            if ($null -ne $epC) { $subT += ' · ' + [Math]::Round([double]$epC * 0.75006) + ' мм' }
        }
        if ($null -ne $mt -and -not $mt.Fresh) { $tc = $script:CLR.warn }
        Set-Card 't' ((Num $tf '0.0') + ' °C') $subT $tc $txtSub

    } else {

        $subT = if ($null -ne (Get-MeasureMetric $script:measure 't')) { 'нет свежих данных' } else { 'реле не сообщает' }
        Set-Card 't' '—' $subT $script:CLR.dim2 $txtSub

    }



    # ------------------------------------------------ реле

    $r = [bool](Field $d @('r', 'switch') $false)

    if ($r) { Set-Card 'r' 'ВКЛ' 'питание подаётся' '#63D68B' $txtSub }

    else    { Set-Card 'r' 'ВЫКЛ' 'питание снято' '#E85D5D' $txtSub }

    $rc = $script:cards['r']

    if ($null -ne $rc) { Set-Lbl $rc.When '' }   # у реле нет своего времени получения



    # ------------------------------------------------ мощность и ток (P0.1: /measure)
    $mp = Get-MeasureMetric $script:measure 'p'
    $mi = Get-MeasureMetric $script:measure 'i'
    $p = [double](Field $d @('p', 'power') 0)
    if ($null -ne $mp -and $mp.HasValue) { $p = [double]$mp.Value }
    $amp = Field $d @('amp')
    $ampTxt = '—'
    if ($null -ne $mi -and $mi.HasValue) {
        # 0 А — реальное значение, не «нет данных»
        $ampTxt = (Num $mi.Value '0.00') + ' А'
    } elseif ($null -ne $mp -and -not $mp.HasValue) {
        $ampTxt = '—'
    } elseif ($null -ne $amp) {
        $ampTxt = (Num ([double]$amp) '0.00') + ' А'
    } elseif ($v -gt 50) {
        $ampTxt = (Num ($p / $v) '0.00') + ' А (P/V)'
    }
    $pSeen = Field $d @('pSeen')
    $pBad = ($null -ne $mp -and -not $mp.Fresh) -or ([bool]$stale)
    if ($null -ne $mp -and -not $mp.HasValue) {
        Set-Card 'p' '— Вт' 'нет свежих данных' $txtMain $script:CLR.warn
    } elseif ($null -ne $mp -or ($null -eq $pSeen -or [int]$pSeen -ne 0)) {
        $powerSub = if ($pBad) { '⚠ данные могут быть устаревшими · ток ' + $ampTxt } else { 'ток ' + $ampTxt }
        Set-Card 'p' ((Num $p '0.0') + ' Вт') $powerSub $txtMain $(if ($pBad) { $script:CLR.warn } else { $txtSub })
    } else {
        Set-Card 'p' '— Вт' 'ожидание данных реле' $txtMain $script:CLR.warn
    }
    # ------------------------------------------------ сегодня

    $tkwh = Field $d @('tkwh', 'today_kwh')

    $tcost = Field $d @('tcost', 'today_cost')

    if ($null -ne $tkwh) { Set-Card 'e' ((Num $tkwh '0.00') + ' кВт·ч') $src $txtMain $txtGreen }

    else { Set-Card 'e' '—' 'плата не прислала' $script:CLR.dim2 $script:CLR.dim2 }

    $tariff = Field $d @('tariff')

    $tarTxt = if ($null -ne $tariff) { (Num $tariff '0.00') + ' ₽ · ' } else { '' }

    if ($null -ne $tcost) { Set-Card 'cost' ((Num $tcost '0.00') + ' ₽') ($tarTxt + $src) $txtMain $txtGreen }

    else { Set-Card 'cost' '—' 'плата не прислала' $script:CLR.dim2 $script:CLR.dim2 }



    # ------------------------------------------------ вчера и месяц

    $ykwh = Field $d @('ykwh', 'yesterday_kwh')

    $ykost = Field $d @('ykost', 'yesterday_cost')

    $mkwh = Field $d @('mkwh', 'month_kwh')

    $mkost = Field $d @('mkost', 'mcost', 'month_cost')



    if ($null -ne $ykwh) {

        $subY = if ($null -ne $ykost) { (Num $ykost '0.00') + ' ₽ · ' + $src } else { $src }

        Set-Card 'y' ((Num $ykwh '0.00') + ' кВт·ч') $subY $txtMain $txtGreen

    } else {

        Set-Card 'y' '—' 'плата не прислала' $script:CLR.dim2 $script:CLR.dim2

    }

    if ($null -ne $mkwh) {

        $subM = if ($null -ne $mkost) { (Num $mkost '0.00') + ' ₽ · ' + $src } else { $src }

        Set-Card 'm' ((Num $mkwh '0.00') + ' кВт·ч') $subM $txtMain $txtGreen

    } else {

        Set-Card 'm' '—' 'плата не прислала' $script:CLR.dim2 $script:CLR.dim2

    }



    # ------------------------------------------------ счётчик

    $eAll = Field $d @('e', 'energy_kwh')

    if ($null -ne $eAll) { Set-Card 'all' ((Num $eAll '0.00') + ' кВт·ч') 'показание счётчика' $txtMain $txtSub }

    else { Set-Card 'all' '—' 'нет данных' $script:CLR.dim2 $script:CLR.dim2 }



    # ------------------------------------------------ время последнего получения

    # vAt/tAt/pAt/eAt — epoch-секунды от платы; если поля нет или 0, будет «нет данных»

    try {

        foreach ($ck in @($script:cardStamp.Keys)) { Set-CardStamp $ck $script:cardStamp[$ck] }

    } catch {

        Write-AppLog ('время получения: ' + $_.Exception.Message)

    }



    # P0.1: подписи «источник · возраст» по V/P/I/T из /measure
    try { Apply-MeasureStamps } catch { Write-AppLog ('источник измерения: ' + $_.Exception.Message) }

    # ------------------------------------------------ мост для внешних страниц

    # файл пишем не чаще раза в 5 с, чтобы не гонять диск на каждом тике

    if ($null -eq $script:lastState) { $script:lastState = [datetime]::MinValue }

    if (((Get-Date) - $script:lastState).TotalSeconds -ge 5) {

    $script:lastState = Get-Date

    try {

        $state = [ordered]@{

            updated   = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')

            source    = $src

            v         = $v

            p         = $p

            amp       = $amp

            temp      = $t

            r         = $r

            e         = $eAll

            tkwh      = $tkwh

            tcost     = $tcost

            ykwh      = $ykwh

            ykost     = $ykost

            mkwh      = $mkwh

            mkost     = $mkost

            tariff    = $tariff

            rssi      = Field $d @('rssi')

            heap      = Field $d @('heap')

            gaps      = Field $d @('gaps')

            up        = Field $d @('up')

            stale     = $stale

        }

        [System.IO.File]::WriteAllText($script:StateFile, ($state | ConvertTo-Json -Compress),

                                       (New-Object System.Text.UTF8Encoding($false)))

    } catch { }

    }



    # мини-графики карточек — не чаще раза в 5 с

    if ($null -eq $script:lastSpark -or ((Get-Date) - $script:lastSpark).TotalSeconds -ge 5) {

        $script:lastSpark = Get-Date

        foreach ($sk in @('v', 'p', 't', 'e')) {

            $sc = $script:cards[$sk]

            if ($null -ne $sc -and $null -ne $sc.Spark) { $sc.Spark.Invalidate() }

        }

    }



    Update-Lcd $d

}



function Update-Lcd($d) {

    if ($null -eq $d) { return }

    if ($script:lcdOffline) {

        $script:lcdOffline = $false

        $lcd.ForeColor = C $script:CLR.lcdFg

    }

    $inv = [System.Globalization.CultureInfo]::InvariantCulture

    $v = [double](Field $d @('v') 0)

    $p = [double](Field $d @('p') 0)

    $amp = Field $d @('amp')

    $ampTxt = if ($null -ne $amp) { (Num $amp '0.00') } elseif ($v -gt 50) { (Num ($p / $v) '0.00') } else { '--' }

    $e = Field $d @('e')

    $tk = Field $d @('tkwh')

    $tc = Field $d @('tcost')

    $yk = Field $d @('ykwh')

    $yc = Field $d @('ykost')

    $mk = Field $d @('mkwh')

    $mc = Field $d @('mkost', 'mcost')

    $r = [bool](Field $d @('r') $false)

    $rssi = Field $d @('rssi') 0

    $up = Format-Uptime (Field $d @('up'))

    $sd = Field $d @('sdRows')

    $sdTxt = if ($null -ne $sd) { 'SD ' + [int]$sd } else { 'SD --' }

    $fw = '2.0'

    if ($script:fwVal) { $fw = [string]$script:fwVal }

    elseif ($null -ne $script:serialStatus -and $null -ne $script:serialStatus.fw) { $fw = [string]$script:serialStatus.fw }



    $lines = @(

        ('DOM-MONITOR  ' + $fw),

        ('{0,4} V  {1,4} W  {2} A' -f [int]$v, [int]$p, $ampTxt),

        ('{0} kWh  today {1}' -f (Num $e '0.00'), (Num $tk '0.00')),

        ('TODAY {0} RUB' -f (Num $tc '0.00')),

        ('RELAY ' + $(if ($r) { 'ON' } else { 'OFF' })),

        ('YDAY  {0} / {1}' -f (Num $yk '0.00'), (Num $yc '0.00')),

        ('MONTH {0} / {1}' -f (Num $mk '0.00'), (Num $mc '0.00')),

        ('{0} dBm  {1}  {2}' -f [int]$rssi, $up, $sdTxt)

    )

    $text = $lines -join "`n"

    if ($lcd.Text -ne $text) { $lcd.Text = $text }

}



function Update-Footer {

    $now = Get-Date

    $d = Get-Active

    if ($null -eq $d) {

        Set-Lbl $lblFootLeft ('последняя попытка ' + $now.ToString('HH:mm:ss') + ' · ' + $script:boardErr + ' · ошибок ' + $script:pollErr)

    } else {

        $srcTxt = if ((Get-BoardAge) -le 12) { 'HTTP' } elseif ($null -ne $script:serialData) { 'serial' } else { 'нет связи' }

        $msTxt = if ($srcTxt -eq 'HTTP') { ' ' + [int]$script:pollMs + ' мс' } else { '' }

        Set-Lbl $lblFootLeft ($srcTxt + $msTxt + ' · обновлено ' + $now.ToString('HH:mm:ss') + ' · опросов ' + $script:totalPolls + ' · ошибок ' + $script:pollErr)

    }

    if ($null -ne $d) {

        $parts = @()

        $cAge = Field $d @('cAge')

        if ($null -ne $cAge) { $parts += 'облако ' + [int]$cAge + ' с' }

        $heap = Field $d @('heap')

        if ($null -ne $heap) { $parts += 'память ' + [Math]::Round([double]$heap / 1024) + ' КБ' }

        $sd = Field $d @('sdRows')

        if ($null -ne $sd) { $parts += 'SD ' + [int]$sd + ' строк' }

        $gap = Field $d @('gaps')

        if ($null -ne $gap) { $parts += 'пропусков ' + [int]$gap }

        $pg = Field $d @('pageN')

        if ($null -ne $pg) { $parts += 'страниц ' + [int]$pg }

        $up = Field $d @('up')

        if ($null -ne $up) { $parts += 'работает ' + (Format-Uptime $up) }

        $lblFootRight.Text = ($parts -join ' · ')

    } else {

        $lblFootRight.Text = 'нет связи с платой'

    }

}



function Update-Device {

    if (-not $script:uiReady) { return }

    if (-not $pgDev.Visible) { return }

    $d = Get-Active

    $s = $script:serialStatus

    try {

    $rows = New-Object System.Collections.ArrayList

    $online = Board-Online



    $age = Get-BoardAge

    $httpTxt = if ($null -eq $script:board) { 'нет ответа' }

               elseif ($age -le 12) { 'связь есть, ' + [int]$age + ' с назад' }

               else { 'нет связи ' + (Format-Age $age) }

    [void]$rows.Add(@('Источник данных', (Get-SourceName)))

    [void]$rows.Add(@('HTTP ' + $script:cfg.board, $httpTxt))

    $comName = [string]$script:cfg.comPort

    $comTxt = if ($script:port -and $script:port.IsOpen) { 'открыт (монитор)' }

              elseif (-not (Test-ComPresent $comName)) { 'нет устройства' }

              elseif ((Test-PortFree $comName) -eq $false) { 'занят другой программой' }

              else { 'закрыт (свободен)' }

    [void]$rows.Add(@($comName + ' (резерв)', $comTxt))

    $devIp = Get-CfgBoardIp

    [void]$rows.Add(@('Настройка платы', $(if (Test-CfgReady) { 'выполнена · ' + $devIp + ' · мастер сам не открывается' } else { 'не выполнена — нужен мастер' })))

    $fwDev = ''

    if ($script:fwVal) { $fwDev = [string]$script:fwVal }

    elseif ($null -ne $s -and $null -ne $s.fw) { $fwDev = [string]$s.fw }

    [void]$rows.Add(@('Прошивка', $(if ($fwDev) { $fwDev } else { 'не получена (нет /data.fw и serial)' })))

    [void]$rows.Add(@('Ответ /data', $(if ($script:pollMs -gt 0) { [string][int]$script:pollMs + ' мс' } else { '—' })))

    [void]$rows.Add(@('Ошибок опроса', [string]$script:pollErr))

    [void]$rows.Add(@('Время работы платы', (Format-Uptime (Field $d @('up')))))

    [void]$rows.Add(@('Wi-Fi', $(if ($null -ne $d -and (Field $d @('net') $false)) { 'подключено' } else { '—' })))

    [void]$rows.Add(@('Сигнал', $(if ($null -ne $d -and $null -ne (Field $d @('rssi'))) { '{0} dBm' -f (Field $d @('rssi')) } else { '—' })))

    [void]$rows.Add(@('Свободная память', $(if ($null -ne $d -and $null -ne (Field $d @('heap'))) { '{0} КБ' -f [Math]::Round([double](Field $d @('heap')) / 1024) } else { '—' })))

    [void]$rows.Add(@('Карта памяти', $(if ($null -ne $d -and $null -ne (Field $d @('sdRows'))) { 'строк ' + [int](Field $d @('sdRows')) } else { '—' })))

    [void]$rows.Add(@('Свежесть данных', $(if ($null -ne $d) { $va = Field $d @('vAge'); $st = [bool](Field $d @('stale') $false); if ($st) { 'УСТАРЕЛИ' + $(if ($null -ne $va) { ' (' + [int]$va + ' с)' } else { '' }) } elseif ($null -ne $va) { 'свежие (' + [int]$va + ' с)' } else { 'по /data' } } else { '—' })))

    [void]$rows.Add(@('Пропуски записи', $(if ($null -ne $d) { [int](Field $d @('gaps') 0) } else { 0 })))

    [void]$rows.Add(@('Минут в пропусках', $(if ($null -ne $d) { [int](Field $d @('gapMin') 0) } else { 0 })))

    [void]$rows.Add(@('Тариф сейчас', $(if ($null -ne $d -and $null -ne (Field $d @('tariff'))) { (Num (Field $d @('tariff')) '0.00') + ' ₽/кВт·ч' } else { '—' })))

    [void]$rows.Add(@('Время по NTP', $(if ($null -ne $d -and $null -ne (Field $d @('now'))) { [string](Field $d @('now')) } else { '—' })))

    [void]$rows.Add(@('Счётчик всего', $(if ($null -ne $d -and $null -ne (Field $d @('e'))) { (Num (Field $d @('e')) '0.00') + ' кВт·ч' } else { '—' })))

    [void]$rows.Add(@('Реле', $(if ($null -ne $d -and [bool](Field $d @('r') $false)) { 'включено' } else { 'выключено' })))

    [void]$rows.Add(@('Портов в системе', (@(Get-PortList) -join ', ')))



    $gridDev.SuspendLayout()

    $gridDev.Rows.Clear()

    foreach ($row in $rows) {

        $idx = $gridDev.Rows.Add([string]$row[0], [string]$row[1])

        $gridDev.Rows[$idx].Cells[0].Style.ForeColor = C '#8A93A3'

        $gridDev.Rows[$idx].Cells[1].Style.ForeColor = C '#DEE5EF'

    }

    $gridDev.ResumeLayout()



    if (-not $online -and $script:days.Count -eq 0) {

        Set-Lbl $lblFootLeft 'нет связи с платой — проверь Wi-Fi и адрес'

    }

    } catch {

        Write-AppLog ('ошибка таблицы устройства: ' + $_.Exception.Message)

    }

}



# ПИК: когда случился и сколько длился. В /history это pmaxT ("14:23", может

# быть null) и pmaxDur — СЕКУНДЫ (прошивка отдаёт секунды, на экран — минуты).

# Раньше приложение показывало только «Пик Вт», без времени и длительности.

function Format-PeakTime($row) {

    $t = Field $row @('pmaxT')

    if ($null -eq $t) { return '—' }

    $s = [string]$t

    if ($s.Length -lt 4 -or $s -eq 'null') { return '—' }

    return $s

}

function Format-PeakDur($row) {

    $d = 0

    try { $d = [int](Field $row @('pmaxDur') 0) } catch { $d = 0 }

    if ($d -le 0) { return '—' }

    $m = [Math]::Max(1, [int][Math]::Round($d / 60.0))

    return ($m.ToString() + ' мин')

}



function Update-HistoryView {
    if (-not $script:uiReady) { return }
    $date = $script:histSel
    Set-Lbl $lblHistDay (Get-RuDay $date)
    $hours = $null
    if ($script:histCache.ContainsKey($date)) { $hours = @($script:histCache[$date]) }
    $gridHist.SuspendLayout()
    $gridHist.Rows.Clear()
    if ($null -eq $hours -or $hours.Count -eq 0) {
        # Пока ответа нет — «загрузка истории…», а не «нет данных»: плата
        # читает CSV за день 12–65 с, и человек должен видеть, что запрос идёт.
        $msg = 'нет измерений'
        if ($script:histErrDate -eq $date -and $script:histErr) { $msg = $script:histErr }
        elseif (Test-HistLoading $date) { $msg = 'загрузка истории…' }
        Set-Lbl $lblHistSum ($date + ' — ' + $msg)
        $gridHist.ResumeLayout()
        Update-EnergyChart $chartHist $script:histSel
        return
    }
    $map = @{}
    foreach ($r in $hours) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }
    # Суммы — только по реально измеренным значениям: null не прибавляем как 0.
    $sumKwh = 0.0; $sumN = 0; $sumPN = 0.0; $avgN = 0
    $maxPmax = $null; $maxPmaxT = '—'; $maxPmaxDur = '—'
    $vMin = $null; $vMax = $null
    $tSum = 0.0; $tN = 0
    $emptyH = 0; $noKwh = 0
    $now = Get-Date
    $isToday = ($date -eq $now.ToString('yyyy-MM-dd'))
    for ($hh = 0; $hh -lt 24; $hh++) {
        $hhTxt = $hh.ToString('00') + ':00'
        if (-not $map.ContainsKey($hh)) {
            $emptyH++
            $idx = $gridHist.Rows.Add($hhTxt, '—', '—', '—', '—', '—', '—', '—', '0')
            $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#5C6575'
            continue
        }
        $r = $map[$hh]
        $rawK = Field $r @('kwh'); $rawP = Field $r @('p'); $rawPm = Field $r @('pmax')
        $rawV = Field $r @('v'); $rawT = Field $r @('t')
        $nn = [int](Field $r @('n') 0)
        $kwh = if ($null -ne $rawK) { [double]$rawK } else { $null }
        $pp = if ($null -ne $rawP) { [double]$rawP } else { $null }
        $pm = if ($null -ne $rawPm) { [double]$rawPm } else { $null }
        $vm = if ($null -ne $rawV) { [double]$rawV } else { $null }
        $pkT = Format-PeakTime $r
        $pkD = Format-PeakDur $r
        if ($null -ne $kwh) { $sumKwh += $kwh } elseif ($nn -gt 0) { $noKwh++ }
        $sumN += $nn
        if ($null -ne $pp -and $nn -gt 0) { $sumPN += $pp * $nn; $avgN += $nn }
        if ($null -ne $pm -and ($null -eq $maxPmax -or $pm -gt $maxPmax)) { $maxPmax = $pm; $maxPmaxT = $pkT; $maxPmaxDur = $pkD }
        if ($null -ne $vm) {
            if ($null -eq $vMin -or $vm -lt $vMin) { $vMin = $vm }
            if ($null -eq $vMax -or $vm -gt $vMax) { $vMax = $vm }
        }
        if ($null -ne $rawT) { $tSum += [double]$rawT; $tN++ }
        if ($nn -eq 0) { $emptyH++ }
        $idx = $gridHist.Rows.Add(
            $hhTxt,
            (Num $kwh '0.00'),
            (Num $pp '0'),
            (Num $pm '0'),
            $pkT,
            $pkD,
            (Num $vm '0'),
            $(if ($null -ne $rawT) { Num $rawT '0.0' } else { '—' }),
            [string]$nn)
        if ($nn -eq 0) {
            $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#5C6575'
        } elseif ($isToday -and $hh -eq $now.Hour) {
            $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#5AC8FF'
        }
    }
    $avgP = if ($avgN -gt 0) { $sumPN / $avgN } else { 0 }
    $vTxt = if ($null -ne $vMin -and $null -ne $vMax) { (Num $vMin '0') + '–' + (Num $vMax '0') } else { '—' }
    $tTxt = if ($tN -gt 0) { Num ($tSum / $tN) '0.0' } else { '—' }
    $idx = $gridHist.Rows.Add('ИТОГО', (Num $sumKwh '0.00'), (Num $avgP '0'), (Num $maxPmax '0'), $maxPmaxT, $maxPmaxDur, $vTxt, $tTxt, [string]$sumN)
    $gridHist.Rows[$idx].DefaultCellStyle.Font = Fnt 'Segoe UI' 8.5 $true
    $gridHist.Rows[$idx].DefaultCellStyle.BackColor = C (SkinC '#232A36')
    $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#E6ECF5'

    $costTxt = ''
    if ($null -ne $script:board) {
        if ($isToday) {
            $c = Field $script:board @('tcost')
            if ($null -ne $c) { $costTxt = ' · ' + (Num $c '0.00') + ' ₽' }
        } elseif ($date -eq $now.AddDays(-1).ToString('yyyy-MM-dd')) {
            $c = Field $script:board @('ykost')
            if ($null -ne $c) { $costTxt = ' · ' + (Num $c '0.00') + ' ₽' }
        } else {
            $tf = Field $script:board @('tariff')
            if ($null -ne $tf) { $costTxt = ' · ≈' + (Num ($sumKwh * [double]$tf) '0.00') + ' ₽ по тарифу ' + (Num $tf '0.00') }
        }
    }

    $gTxt = ''
    if ($script:daysGaps.ContainsKey($date)) {
        $g = $script:daysGaps[$date]
        if ([int]$g[0] -gt 0) { $gTxt = ' · пропусков ' + [int]$g[0] + ' (' + (Format-Age ([int]$g[1] * 60)) + ')' }
        else { $gTxt = ' · пропусков нет' }
    }
    if ($emptyH -gt 0) { $gTxt += ' · часов без записей ' + $emptyH }
    if ($noKwh -gt 0) { $gTxt += ' · нет измерений кВт·ч: ' + $noKwh + ' ч' }

    # хвост: когда реально получены данные, из которых считается день
    $freshTxt = ''
    try {
        $fs = Get-FreshestStamp
        if ($null -ne $fs) { $freshTxt = ' · данные получены ' + (Format-Stamp $fs).Replace('получено ', '') }
        else { $freshTxt = ' · данные получены —' }
    } catch { }

    Set-Lbl $lblHistSum ($date + ' · ' + (Num $sumKwh '0.00') + ' кВт·ч' + $costTxt + ' · записей ' + $sumN + $gTxt + $freshTxt)
    $gridHist.ResumeLayout()
    # выбранный час — из объекта состояния; обновление данных выбор не сбрасывает
    try {
        if ($null -ne $script:histSelect -and [string]$script:histSelect.Date -eq $date) { Select-HistTableHour $script:histSelect.Hour }
        else { Select-HistTableHour $null }
    } catch { }
    Update-EnergyChart $chartHist $script:histSel
}

$script:refreshHistory = {

    if ($script:uiReady) { Update-HistoryView }

}



function Read-Serial {

    if (-not ($script:port -and $script:port.IsOpen)) { return }

    try {

        $n = $script:port.BytesToRead

        if ($n -le 0) { return }

        $chunk = $script:port.ReadExisting()

        $script:rxBuf += $chunk

        while ($script:rxBuf.Contains("`n")) {

            $i = $script:rxBuf.IndexOf("`n")

            $line = $script:rxBuf.Substring(0, $i)

            $script:rxBuf = $script:rxBuf.Substring($i + 1)

            Handle-Line $line

        }

        if ($script:rxBuf.Length -gt 8192) { $script:rxBuf = '' }

    } catch { }

}



$script:wasOnline = $null



function Log-Beats {

    $now = Get-Date

    $online = Board-Online

    if (-not $online) {

        if ($script:wasOnline -ne $false -and $null -ne $script:board) {

            $script:wasOnline = $false

            $why = $script:boardErr

            if (-not $why) { $why = 'нет ответа' }

            Add-Log ('связь с платой потеряна: ' + $why) 'warn'

        }

        return

    }

    if ($script:wasOnline -ne $true) {

        $script:wasOnline = $true

        Add-Log ('связь с платой есть — ' + (Get-SourceName) + ', ответ ' + [int]$script:pollMs + ' мс') 'ok'

    }

    if (($now - $script:lastLog).TotalSeconds -lt $script:beatSec) { return }

    $script:lastLog = $now



    $d = Get-Active

    if ($null -eq $d) { return }



    # источник данных: HTTP / serial / нет связи

    $src = 'нет связи'

    if ($null -ne $script:board -and (Get-BoardAge) -le 12) { $src = 'HTTP' }

    elseif ($null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) { $src = 'serial' }



    $parts = @()

    if ($src -eq 'HTTP') { $parts += ($src + ' ' + [int]$script:pollMs + ' мс') }

    else { $parts += $src }

    $v = Field $d @('v')

    $p = Field $d @('p')

    if ($null -ne $v) { $parts += ((Num $v '0') + ' В') }

    if ($null -ne $p) { $parts += ((Num $p '0') + ' Вт') }

    $heap = Field $d @('heap')

    if ($null -ne $heap) { $parts += ('плата ' + [Math]::Round([double]$heap / 1024) + ' КБ') }

    $sd = Field $d @('sdRows')

    if ($null -ne $sd) { $parts += ('карта ' + [int]$sd + ' строк') }

    # климат в комнате: AHT20 + BMP280
    $eok = Field $d @('eok')
    if ($null -ne $eok -and [int]$eok -eq 1) {
        $et = Field $d @('et')
        $eh = Field $d @('eh')
        $ep = Field $d @('ep')
        $cl = 'климат '
        if ($null -ne $et) { $cl += (Num $et '0.0') + '°' }
        if ($null -ne $eh) { $cl += ' ' + (Num $eh '0') + '%' }
        if ($null -ne $ep) { $cl += ' ' + [Math]::Round([double]$ep * 0.75006) + ' мм' }
        # погода: сейчас и прогноз на завтра — прямо в журнал, чтобы было видно
        $wt = Field $d @('wT')
        $wd = Field $d @('wDesc')
        if ($null -ne $wt -and [string]$wt -ne '') {
            $cl += ' · улица ' + (Num $wt '0.0') + '°'
            if ($wd) { $cl += ' ' + [string]$wd }
        }
        $w1 = Field $d @('wTminT'); $w2 = Field $d @('wTmaxT'); $wr = Field $d @('wPopT')
        if ((Field $d @('wCodeT') 0) -gt 0 -and $null -ne $w1 -and $null -ne $w2) {
            $cl += ' · завтра ' + (Num $w1 '0') + '…' + (Num $w2 '0') + '°'
            if ($null -ne $wr -and [double]$wr -gt 0) { $cl += ' дождь ' + (Num $wr '0') + '%' }
        }
        $parts += $cl
    }

    # облако: cAge — когда был успешный обмен, cWhy — код причины, cFail — сбои

    $cAge = Field $d @('cAge')

    $cWhy = Field $d @('cWhy')

    $cFail = Field $d @('cFail')

    if ($null -ne $cAge -or $null -ne $cWhy -or $null -ne $cFail) {

        $cloud = 'облако '

        if ($null -ne $cAge) { $cloud += (Format-Age ([double]$cAge)) + ' назад' } else { $cloud += '—' }

        if ($null -ne $cWhy) { $cloud += ' · код ' + [int]$cWhy }

        if ($null -ne $cFail) { $cloud += ' · сбоев ' + [int]$cFail }

        $parts += $cloud

    }

    if ([bool](Field $d @('stale') $false)) { $parts += 'ДАННЫЕ УСТАРЕЛИ' }

    $parts += ('опросов ' + $script:totalPolls + ' · ошибок ' + $script:pollErr)

    Add-LogLine ($parts -join ' · ')

}



$script:tray = $null
$script:exiting = $false
$script:trayAt = [datetime]::MinValue
$script:discovered = @()
$script:alertLast = @{}

$script:alertState = @{}

$script:alertSeen = @{}







# ------------------------------------------------- загрузка периода без заморозки окна
# /range на плате может считаться десятки секунд (202 pending). Раньше запрос
# шёл через Start-Job + DoEvents — окно «жевало» сообщения и подвисало.
# Теперь настоящий async: состояние загрузки лежит в $script:rangeLoad, тики
# On-Tick двигают её, а таблицу заполняем в UI-потоке.

function Stop-RangeLoad {
    $st = $script:rangeLoad
    $script:rangeLoad = $null
    if ($null -eq $st) { return }
    try { if ($null -ne $st.Cts) { $st.Cts.Cancel(); $st.Cts.Dispose() } } catch { }
}

function Start-RangeLoad($grid, $sum, [string]$ip, [string]$from, [string]$to) {
    Stop-RangeLoad
    if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    $chunks = New-Object 'System.Collections.Generic.List[object]'
    $dA = [datetime]$from; $dB = [datetime]$to
    $guard = 0
    while ($dA -le $dB -and $guard -lt 400) {
        $guard++
        $chunkEnd = $dA.AddDays(30)
        if ($chunkEnd -gt $dB) { $chunkEnd = $dB }
        $chunks.Add([pscustomobject]@{ From = $dA.ToString('yyyy-MM-dd'); To = $chunkEnd.ToString('yyyy-MM-dd') })
        $dA = $chunkEnd.AddDays(1)
    }
    $script:rangeLoad = [pscustomobject]@{
        Grid = $grid; Sum = $sum; Ip = $ip; From = $from; To = $to
        Chunks = $chunks; Index = 0; Task = $null; Cts = $null
        TaskAt = [datetime]::MinValue; RetryAt = [datetime]::MinValue; ChunkAt = [datetime]::MinValue; ChunkIndex = -1
        Rows = (New-Object 'System.Collections.Generic.List[object]')
        Kwh = 0.0; Cost = 0.0; DayKwh = 0.0; NightKwh = 0.0; DayCost = 0.0; NightCost = 0.0
        Vmin = 0; Vmax = 0; Pmax = 0; Fail = $null
    }
    if ($null -ne $sum) { try { $sum.Text = 'считаю…' } catch { } }
    if ($null -eq $script:httpSlow) {
        $script:rangeLoad = $null
        if ($null -ne $sum) { try { $sum.Text = 'ошибка: HTTP-клиент не готов' } catch { } }
        return
    }
    Pump-RangeLoad
}

function Pump-RangeLoad {
    $st = $script:rangeLoad
    if ($null -eq $st) { return }
    if ($null -eq $st.Grid -or $st.Grid.IsDisposed) { $script:rangeLoad = $null; return }
    if ($null -ne $st.Task) {
        if (-not $st.Task.IsCompleted) {
            if (((Get-Date) - $st.TaskAt).TotalSeconds -gt 185) {
                try { if ($null -ne $st.Cts) { $st.Cts.Cancel() } } catch { }
            } else { return }
        }
        $task = $st.Task
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        try {
            if ($task.IsCanceled) { throw 'плата не ответила за 180 с' }
            if ($task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw (Get-TaskError $task) }
            $resp = $task.Result
            if ($null -eq $resp -or -not $resp.IsSuccessStatusCode) {
                $code = if ($null -ne $resp) { [int]$resp.StatusCode } else { 0 }
                if ($code -gt 0) { throw ('плата отдала HTTP ' + $code) } else { throw 'плата не ответила' }
            }
            $o = ($resp.Content.ReadAsStringAsync().Result) | ConvertFrom-Json
            if ($o.pending) {
                $ms = 700
                $rawR = Field $o @('retryMs')
                if ($null -ne $rawR) { $ms = [Math]::Max(400, [Math]::Min(4000, [int]$rawR)) }
                $st.RetryAt = (Get-Date).AddMilliseconds($ms)
                return
            }
            if ($o.trunc -or $o.ok -eq $false -or $null -eq $o.PSObject.Properties['rows']) { throw 'неполный ответ периода' }
            foreach ($row in @($o.rows)) { if ($null -ne $row) { $st.Rows.Add($row) } }
            $ct = $o.totals
            if ($null -ne $ct) {
                $st.Kwh += [double](Field $ct @('kwh') 0)
                $st.Cost += [double](Field $ct @('cost') 0)
                $st.DayKwh += [double](Field $ct @('dayKwh') 0)
                $st.NightKwh += [double](Field $ct @('nightKwh') 0)
                $st.DayCost += [double](Field $ct @('dayCost') 0)
                $st.NightCost += [double](Field $ct @('nightCost') 0)
                $vmn = [int](Field $ct @('vmin') 0); $vmx = [int](Field $ct @('vmax') 0); $pmx = [int](Field $ct @('pmax') 0)
                if ($vmn -gt 0 -and ($st.Vmin -eq 0 -or $vmn -lt $st.Vmin)) { $st.Vmin = $vmn }
                if ($vmx -gt $st.Vmax) { $st.Vmax = $vmx }
                if ($pmx -gt $st.Pmax) { $st.Pmax = $pmx }
            }
            $st.Index++
            $st.RetryAt = Get-Date
        } catch {
            $msg = $_.Exception.Message
            $chunkAt = if ($st.ChunkAt -gt [datetime]::MinValue) { $st.ChunkAt } else { $st.TaskAt }
            if (((Get-Date) - $chunkAt).TotalSeconds -lt 170) {
                $st.RetryAt = (Get-Date).AddMilliseconds(700)
                return
            }
            $st.Fail = [string]$msg
        }
    }
    if ($null -ne $st.Fail) {
        if ($null -ne $st.Sum) { try { $st.Sum.Text = 'ошибка: ' + $st.Fail } catch { } }
        Add-Log ('период: ошибка ' + $st.Fail)
        $script:rangeLoad = $null
        return
    }
    if ($st.RetryAt -gt (Get-Date)) { return }
    if ($st.Index -ge $st.Chunks.Count) {
        $script:rangeLoad = $null
        Complete-RangeLoad $st
        return
    }
    $chunk = $st.Chunks[$st.Index]
    if ([int]$st.ChunkIndex -ne [int]$st.Index) { $st.ChunkIndex = [int]$st.Index; $st.ChunkAt = Get-Date }
    try {
        $st.Cts = New-Object System.Threading.CancellationTokenSource
        $st.Task = $script:httpSlow.GetAsync(('http://' + $st.Ip + '/range?from=' + $chunk.From + '&to=' + $chunk.To), $st.Cts.Token)
        $st.TaskAt = Get-Date
        if ($null -ne $st.Sum) {
            try { $st.Sum.Text = 'считаю… ' + [Math]::Min($st.Chunks.Count, $st.Index + 1) + ' из ' + $st.Chunks.Count } catch { }
        }
    } catch {
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        $st.Fail = $_.Exception.Message
    }
}

function Complete-RangeLoad($st) {
    $allRows = $st.Rows.ToArray()   # PS 5.1: @() поверх List из PSCustomObject падает
    $sum = $st.Sum
    $r = [pscustomobject]@{
        rows = $allRows
        totals = [pscustomobject]@{
            kwh = $st.Kwh; cost = $st.Cost; dayKwh = $st.DayKwh; nightKwh = $st.NightKwh
            dayCost = $st.DayCost; nightCost = $st.NightCost
            avgDayKwh = $(if ($allRows.Count -gt 0) { $st.Kwh / $allRows.Count } else { 0 })
            vmin = $st.Vmin; vmax = $st.Vmax; pmax = $st.Pmax
        }
    }
    $script:rangeRows = @($r.rows | Where-Object { $null -ne $_ })
    try {
        $grid = $st.Grid
        $grid.Rows.Clear()
        foreach ($row in $r.rows) {
            $vmaxRow = Field $row @('vmax')
            $tminRow = Field $row @('tmin'); $tmaxRow = Field $row @('tmax')
            $null = $grid.Rows.Add(
                $row.d, [math]::Round($row.kwh, 2), [math]::Round($row.cost, 2),
                [math]::Round($row.dayKwh, 2), [math]::Round($row.nightKwh, 2),
                [math]::Round($row.dayCost, 2), [math]::Round($row.nightCost, 2),
                $(if ((Field $row @('vvalid') $true) -and $null -ne $vmaxRow -and [double]$vmaxRow -gt 0) { [string]$row.vmin + '–' + [string]$row.vmax } else { '—' }),
                $(if ((Field $row @('tvalid') ($null -ne $tminRow -and $null -ne $tmaxRow)) -and [int](Field $row @('rows') 0) -gt 0) { [string]$row.tmin + '–' + [string]$row.tmax } else { '—' }), $row.pmax)
        }
    } catch { }
    try { if ($null -ne $script:rangeChart) { $script:rangeChart.Invalidate() } } catch { }
    try {
        $wLbl = $script:rangeWeather
        if ($null -ne $wLbl) {
            $wDays = 0; $wSum = 0.0; $wMin = 999.0; $wMax = -999.0
            foreach ($row in $r.rows) {
                $a = Field $row @('wtmin'); $b2 = Field $row @('wtmax')
                if (($row.wvalid -eq $true -or ($null -eq $row.wvalid -and [string]$row.w)) -and $null -ne $a -and $null -ne $b2) {
                    $wDays++
                    $wSum += ([double]$a + [double]$b2) / 2
                    if ([double]$a -lt $wMin) { $wMin = [double]$a }
                    if ([double]$b2 -gt $wMax) { $wMax = [double]$b2 }
                }
            }
            if ($wDays -gt 0) {
                $wLbl.Text = ('погода за период: дней с данными ' + $wDays +
                    ' · средняя середина диапазонов ' + [Math]::Round($wSum / $wDays, 1) + '°C' +
                    ' · от ' + [Math]::Round($wMin, 1) + '° до ' + [Math]::Round($wMax, 1) + '°')
            } else {
                $wLbl.Text = 'погода за период: нет данных (плата не отдавала /weather)'
            }
        }
    } catch { }
    $t = $r.totals
    if ($null -ne $sum) {
        try {
            $sum.Text = ('итого ' + [math]::Round($t.kwh, 2) + ' кВт·ч · ' + [math]::Round($t.cost, 2) + ' ₽' +
                         ' · день ' + [math]::Round($t.dayKwh, 2) + ' / ночь ' + [math]::Round($t.nightKwh, 2) +
                         ' · в среднем ' + [math]::Round($t.avgDayKwh, 2) + ' кВт·ч в сутки' +
                         ' · U ' + $t.vmin + '–' + $t.vmax + ' В · пик ' + $t.pmax + ' Вт')
        } catch { }
    }
    Add-Log ('период ' + $st.From + '…' + $st.To + ': ' + [math]::Round($t.kwh, 2) + ' кВт·ч, ' + [math]::Round($t.cost, 2) + ' ₽')
    # сколько дней в периоде и сколько строк пришло — по этой строке видно,
    # совпадает ли число столбиков на графике с числом дней
    $daysInRange = [int]([Math]::Abs(([datetime]$st.To - [datetime]$st.From).TotalDays) + 1)
    $rowsGot = @($script:rangeRows).Count
    $logLine = 'график периода: строк ' + $rowsGot + ', дней ' + $daysInRange
    if ($rowsGot -ne $daysInRange) { $logLine += ' (не совпадает — плата отдала не все дни)' }
    Add-Log $logLine $(if ($rowsGot -eq $daysInRange) { 'ok' } else { 'warn' })
    # P2.1: итог по неполученным дням не выдаём за итог целого периода.
    try {
        $daysWithMeasure = @($script:rangeRows | Where-Object { [int](Field $_ @('rows') 0) -gt 0 }).Count
        if ($daysWithMeasure -lt $daysInRange) {
            $sum.Text += (' · дней с измерениями ' + $daysWithMeasure + ' из ' + $daysInRange + ' — неполное покрытие')
        }
    } catch { }
}

function Update-RangeGrid($grid, $sum, $ip, $from, $to) {
    # Не блокирует окно: только ставит задачу загрузки. Результат заполняет
    # Pump-RangeLoad на тике On-Tick — то есть в UI-потоке.
    if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    # P2.1/P2.2: запоминаем выбранный диапазон; сравнение с предыдущим периодом
    # начинаем заново — старые строки предыдущего периода больше не подходят.
    $script:rangeSpan = @{
        From = [string]$from; To = [string]$to
        Days = [int]([Math]::Abs(([datetime]$to - [datetime]$from).TotalDays) + 1)
    }
    $script:cmpRows = @(); $script:cmpKey = ''; $script:cmpFail = ''
    try { Stop-CmpLoad } catch { }
    Start-RangeLoad $grid $sum $ip $from $to
    return $null
}

# Запрос к плате без заморозки окна: работа идёт в фоновом задании,
# а UI продолжает перерисовываться (Application.DoEvents).
function Invoke-RestUi([string]$url, [int]$timeoutSec = 120) {
    $job = Start-Job -ScriptBlock {
        param($u,$headers,$limit)
        $until=(Get-Date).AddSeconds($limit)
        try {
            do {
                try {$result=Invoke-RestMethod $u -TimeoutSec 20 -Headers $headers}
                catch {
                    if((Get-Date) -ge $until){throw}
                    $status=if($_.Exception.Response){[int]$_.Exception.Response.StatusCode}else{0}
                    if($status -gt 0 -and $status -notin @(408,429,500,502,503,504)){throw}
                    Start-Sleep -Milliseconds 700
                    continue
                }
                if(-not $result.pending){return $result}
                Start-Sleep -Milliseconds 700
            } while((Get-Date) -lt $until)
            throw 'Period calculation timed out'
        } catch { $null }
    } -ArgumentList $url,(Get-ControlHeaders),$timeoutSec
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($job.State -eq 'Running' -and $sw.Elapsed.TotalSeconds -lt $timeoutSec) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 40
    }
    $out = $null
    try { $out = Receive-Job $job -ErrorAction SilentlyContinue } catch { }
    Remove-Job $job -Force -ErrorAction SilentlyContinue
    return $out
}

function Show-Sounds {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Звуки платы — ' + $ip
    $f.Size = New-Object System.Drawing.Size(640, 620)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text
    $font = New-Object System.Drawing.Font('Segoe UI', 9)

    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text = 'Событие → звук. Кнопка ▶ проигрывает выбранный пресет на плате.'
    $lbl.Location = New-Object System.Drawing.Point(14, 10)
    $lbl.Size = New-Object System.Drawing.Size(600, 20)
    $lbl.ForeColor = C $script:CLR.dim2
    $f.Controls.Add($lbl)

    $grid = New-Object System.Windows.Forms.DataGridView
    $grid.Location = New-Object System.Drawing.Point(14, 36)
    $grid.Size = New-Object System.Drawing.Size(600, 300)
    $grid.BackgroundColor = C $script:CLR.panel
    $grid.GridColor = C '#232B39'
    $grid.BorderStyle = 'None'
    $grid.EnableHeadersVisualStyles = $false
    $grid.ColumnHeadersDefaultCellStyle.BackColor = C (SkinC '#151B26')
    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $grid.DefaultCellStyle.BackColor = C $script:CLR.panel
    $grid.DefaultCellStyle.ForeColor = C '#D6E2F5'
    $grid.RowHeadersVisible = $false
    $grid.AllowUserToAddRows = $false
    $grid.AutoSizeColumnsMode = 'Fill'
    $grid.Font = $font
    $null = $grid.Columns.Add('name', 'Событие')
    $null = $grid.Columns.Add('key', 'ключ')
    $grid.Columns['key'].Visible = $false
    $null = $grid.Columns.Add('preset', 'звук')
    $grid.Columns['name'].FillWeight = 140
    $grid.Columns['preset'].FillWeight = 160
    $f.Controls.Add($grid)

    $btnTest = New-Object System.Windows.Forms.Button
    $btnTest.Text = '▶ прослушать'
    $btnTest.Location = New-Object System.Drawing.Point(14, 344)
    $btnTest.Size = New-Object System.Drawing.Size(130, 28)
    $btnTest.FlatStyle = 'Flat'
    $btnTest.BackColor = C (SkinC '#24405A')
    $btnTest.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnTest)

    $lblVol = New-Object System.Windows.Forms.Label
    $lblVol.Text = 'Громкость днём, %:'
    $lblVol.Location = New-Object System.Drawing.Point(160, 350)
    $lblVol.Size = New-Object System.Drawing.Size(130, 20)
    $f.Controls.Add($lblVol)

    $numVol = New-Object System.Windows.Forms.NumericUpDown
    $numVol.Location = New-Object System.Drawing.Point(292, 346)
    $numVol.Size = New-Object System.Drawing.Size(60, 24)
    $numVol.Minimum = 0; $numVol.Maximum = 40; $numVol.Value = 10
    $f.Controls.Add($numVol)

    $lblN = New-Object System.Windows.Forms.Label
    $lblN.Text = 'ночью:'
    $lblN.Location = New-Object System.Drawing.Point(360, 350)
    $lblN.Size = New-Object System.Drawing.Size(60, 20)
    $f.Controls.Add($lblN)

    $numNight = New-Object System.Windows.Forms.NumericUpDown
    $numNight.Location = New-Object System.Drawing.Point(412, 346)
    $numNight.Size = New-Object System.Drawing.Size(60, 24)
    $numNight.Minimum = 0; $numNight.Maximum = 40; $numNight.Value = 5
    $f.Controls.Add($numNight)

    $chkQuiet = New-Object System.Windows.Forms.CheckBox
    $chkQuiet.Text = 'тихие часы 22:00–08:00'
    $chkQuiet.Location = New-Object System.Drawing.Point(14, 380)
    $chkQuiet.Size = New-Object System.Drawing.Size(240, 24)
    $f.Controls.Add($chkQuiet)

    $chkOn = New-Object System.Windows.Forms.CheckBox
    $chkOn.Text = 'звук включён'
    $chkOn.Location = New-Object System.Drawing.Point(270, 380)
    $chkOn.Size = New-Object System.Drawing.Size(140, 24)
    $f.Controls.Add($chkOn)

    $btnMute = New-Object System.Windows.Forms.Button
    $btnMute.Text = 'приглушить 30 мин'
    $btnMute.Location = New-Object System.Drawing.Point(420, 378)
    $btnMute.Size = New-Object System.Drawing.Size(150, 26)
    $btnMute.FlatStyle = 'Flat'
    $btnMute.BackColor = C (SkinC '#2A3240')
    $btnMute.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnMute)

    $st = New-Object System.Windows.Forms.Label
    $st.Text = ''
    $st.Location = New-Object System.Drawing.Point(14, 412)
    $st.Size = New-Object System.Drawing.Size(600, 20)
    $st.ForeColor = C $script:CLR.dim2
    $f.Controls.Add($st)

    $btnSave = New-Object System.Windows.Forms.Button
    $btnSave.Text = 'Сохранить в плату'
    $btnSave.Location = New-Object System.Drawing.Point(14, 440)
    $btnSave.Size = New-Object System.Drawing.Size(170, 30)
    $btnSave.FlatStyle = 'Flat'
    $btnSave.BackColor = C (SkinC '#24405A')
    $btnSave.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnSave)

    $script:sndPresets = @()
    $script:sndEvents = @()

    $load = {
        try {
            $st.Text = 'загружаю с платы…'
            $s = Invoke-RestUi ('http://' + $ip + '/sound') 20
            if ($null -eq $s) { throw 'плата не ответила' }
            $script:sndPresets = @($s.presets)
            $script:sndEvents = @($s.events)
            $chkOn.Checked = ([int]$s.on -eq 1)
            $chkQuiet.Checked = ([int]$s.quiet -eq 1)
            $numVol.Value = [Math]::Min(40, [Math]::Max(0, [int]$s.vol))
            $numNight.Value = [Math]::Min(40, [Math]::Max(0, [int]$s.nvol))
            $grid.Rows.Clear()
            foreach ($ev in $s.events) {
                $i = $grid.Rows.Add($ev.name, $ev.key, '')
                $cell = New-Object System.Windows.Forms.DataGridViewComboBoxCell
                foreach ($p in $s.presets) { [void]$cell.Items.Add($p.name) }
                $cell.Value = $s.presets[[int]$ev.preset].name
                $grid.Rows[$i].Cells[2] = $cell
            }
            $st.Text = 'загружено с платы'
        } catch {
            $st.Text = 'ошибка: ' + $_.Exception.Message
        }
    }

    $btnTest.Add_Click({
        try {
            if ($grid.CurrentRow) {
                $idx = $grid.CurrentRow.Index
                $cell = $grid.Rows[$idx].Cells[2]
                $pname = [string]$cell.Value
                $pid2 = 0
                for ($k = 0; $k -lt $script:sndPresets.Count; $k++) {
                    if ($script:sndPresets[$k].name -eq $pname) { $pid2 = $k; break }
                }
                # правило проекта: слушаем так же, как играет плата по событию
                # (та же громкость и тихие часы), а не «как авария»
                $key = [string]$grid.Rows[$idx].Cells[1].Value
                $saved = -1
                foreach ($ev in @($script:sndEvents)) {
                    if ([string]$ev.key -eq $key) { $saved = [int]$ev.preset; break }
                }
                if ($saved -eq $pid2) {
                    Invoke-RestUi ('http://' + $ip + '/beep?ev=' + $key) 10 | Out-Null
                } else {
                    Invoke-RestUi ('http://' + $ip + '/beep?preset=' + $pid2) 10 | Out-Null
                }
                $st.Text = ('проиграл: ' + $pname)
            }
        } catch { $st.Text = 'ошибка прослушивания' }
    })

    $btnMute.Add_Click({
        try { Invoke-RestUi ('http://' + $ip + '/sound?mute=30') 10 | Out-Null; $st.Text = 'приглушено на 30 минут' }
        catch { $st.Text = 'ошибка' }
    })

    $btnSave.Add_Click({
        try {
            $u = 'http://' + $ip + '/sound?on=' + $(if ($chkOn.Checked) { 1 } else { 0 }) +
                 '&quiet=' + $(if ($chkQuiet.Checked) { 1 } else { 0 }) +
                 '&vol=' + [int]$numVol.Value + '&nvol=' + [int]$numNight.Value
            foreach ($row in $grid.Rows) {
                $key = [string]$row.Cells[1].Value
                $pname = [string]$row.Cells[2].Value
                $pid2 = 0
                for ($k = 0; $k -lt $script:sndPresets.Count; $k++) {
                    if ($script:sndPresets[$k].name -eq $pname) { $pid2 = $k; break }
                }
                $u += '&' + $key + '=' + $pid2
            }
            Invoke-RestUi $u 20 | Out-Null
            $st.Text = 'сохранено в плате'
            Add-Log ('звуки: настройки сохранены (vol ' + [int]$numVol.Value + '%)') 'ok'
        } catch { $st.Text = 'ошибка сохранения: ' + $_.Exception.Message }
    })

    $f.Add_Shown({ & $load })
    [void]$f.ShowDialog()
    $f.Dispose()
}

function Draw-RangeChart($g, $pic) {
    # Столбики расхода по дням периода — то же, что в вебе.
    # ВАЖНО: объект рисования здесь называется $dw, а не $D. PowerShell не
    # различает регистр имён, и прежний $D затирался датой дня ($d): уже на
    # первом столбике MeasureString получал строку вместо кисти и падал с
    # «font = null», поэтому на картинке оставался ровно один столбик
    # (баг «7 дней в таблице — один столбик на графике»).
    try {
        if ($null -eq $g -or $null -eq $pic) { return }
        $rows = @($script:rangeRows | Where-Object { $null -ne $_ })
        $dw = $script:draw
        $w = [int]$pic.Width; $h = [int]$pic.Height
        if ($w -lt 120 -or $h -lt 60) { return }
        $g.Clear((C $script:CLR.card))
        if ($rows.Count -eq 0) {
            $g.DrawString('выбери период — появятся столбики расхода по дням', $dw.fontAxis, $dw.brDim, 8, [float]($h / 2 - 8))
            return
        }
        $padL = 42; $padR = 10; $padT = 18; $padB = 22
        $plotW = $w - $padL - $padR; $plotH = $h - $padT - $padB
        if ($plotW -lt 40 -or $plotH -lt 20) { return }
        $mx = 0.0
        foreach ($r in $rows) { $v = [double](Field $r @('kwh') 0); if ($v -gt $mx) { $mx = $v } }
        if ($mx -le 0) { $mx = 1 }
        $mx = $mx * 1.15
        $n = $rows.Count
        $slot = $plotW / [Math]::Max(1, $n)
        $bw = [Math]::Min(34.0, [Math]::Max(2.0, $slot * 0.62))
        $g.DrawString('РАСХОД ПО ДНЯМ, кВт·ч', $dw.fontAxis, $dw.brDim, [float]$padL, 2)
        for ($k = 0; $k -le 2; $k++) {
            $vy = $padT + $plotH * (1 - $k / 2.0)
            $g.DrawLine($dw.penGrid, [float]$padL, [float]$vy, [float]($padL + $plotW), [float]$vy)
            $g.DrawString(('{0:0.##}' -f ($mx * $k / 2)), $dw.fontAxis, $dw.brDim, 4, [float]($vy - 7))
        }
        # подписи дат: до 16 дней — каждый столбик, 17–31 — через 3 дня,
        # длинные периоды — примерно 12 подписей (30+ дней = раз в 3–5 дней)
        $everyDate = 1
        if ($n -gt 31) { $everyDate = [int][Math]::Max(1, [Math]::Ceiling($n / 12.0)) }
        elseif ($n -gt 16) { $everyDate = 3 }
        $showVal = ($n -le 31)
        for ($i = 0; $i -lt $n; $i++) {
            $v = [double](Field $rows[$i] @('kwh') 0)
            $bh = $plotH * ($v / $mx)
            $bx = $padL + $slot * $i + ($slot - $bw) / 2
            $by = $padT + $plotH - $bh
            $brushBar = if ($v -ge $mx * 0.85 / 1.15) { $dw.brWarn } else { $dw.brAccent }
            $g.FillRectangle($brushBar, [float]$bx, [float]$by, [float]$bw, [float][Math]::Max(1, $bh))
            if ($showVal) {
                $txtVal = ('{0:0.0}' -f $v)
                $szVal = $g.MeasureString($txtVal, $dw.fontSmall)
                if ($szVal.Width -le ($slot + 6)) {
                    $g.DrawString($txtVal, $dw.fontSmall, $dw.brText, [float]($bx + ($bw - $szVal.Width) / 2), [float]($by - 13))
                }
            }
            if (($i % $everyDate) -eq 0) {
                $ds = [string](Field $rows[$i] @('d') '')
                if ($ds.Length -ge 10) { $ds = $ds.Substring(8, 2) + '.' + $ds.Substring(5, 2) }
                if ($ds) {
                    $szDt = $g.MeasureString($ds, $dw.fontAxis)
                    $g.DrawString($ds, $dw.fontAxis, $dw.brDim, [float]($bx + ($bw - $szDt.Width) / 2), [float]($padT + $plotH + 4))
                }
            }
        }
    } catch { Write-AppLog ('rangeChart: ' + $_.Exception.Message) }
}

# ============================================================================
#  P2.1/P2.2 — СРАВНЕНИЕ ПЕРИОДОВ и ПИКИ НАГРУЗКИ (окно «Статистика за период»)
#
#  Считаем только из уже полученных данных: $script:rangeRows (/range по дням)
#  и $script:histCache (/history по часам). Предыдущий период догружается
#  асинхронно в общем контуре (Pump-CmpLoad на тиках On-Tick) — окно не стоит.
#  Разница и проценты — ТОЛЬКО при положительном знаменателе; иначе «—»,
#  никакой бесконечности и NaN.
# ============================================================================

function CmpNum($v, [string]$fmt) {
    # Число для сравнения — с запятой, как в русском интерфейсе; null — «—».
    if ($null -eq $v) { return '—' }
    try {
        return ([double]$v).ToString($fmt, [System.Globalization.CultureInfo]::InvariantCulture).Replace('.', ',')
    } catch { return '—' }
}

function CmpSigned($v, [string]$fmt) {
    # Знаковая разница: «−0,20», «+0,20», ноль без знака.
    if ($null -eq $v) { return '—' }
    try { $d = [double]$v } catch { return '—' }
    $s = CmpNum ([Math]::Abs($d)) $fmt
    if ($d -gt 0) { return '+' + $s }
    if ($d -lt 0) { return '−' + $s }
    return $s
}

function Format-CmpDiff($cur, $prev, [string]$fmt) {
    # Разница в единицах и процентах — только если знаменатель > 0.
    # Прошлый период 0 (или его нет) — честное «—», а не деление на ноль.
    if ($null -eq $cur) { return ' · — (нет текущих данных)' }
    if ($null -eq $prev) { return ' · — (нет прошлых данных)' }
    $c = 0.0; $p = 0.0
    try { $c = [double]$cur; $p = [double]$prev } catch { return ' · — (нет прошлых данных)' }
    if ($p -le 0) { return ' · — (прошлый период 0)' }
    $d = $c - $p
    $pct = $d / $p * 100.0
    return (' · ' + (CmpSigned $d $fmt) + ' · ' + (CmpSigned $pct '0') + ' %')
}

function Format-CmpPair($cur, $prev, [string]$fmt, [string]$unit) {
    # «0,80 против 1,00 кВт·ч · −0,20 · −20 %»
    $a = if ($null -ne $cur) { CmpNum $cur $fmt } else { '—' }
    $b = if ($null -ne $prev) { CmpNum $prev $fmt } else { '—' }
    return ($a + ' против ' + $b + ' ' + $unit + (Format-CmpDiff $cur $prev $fmt))
}

function Get-HistDayAgg([string]$date) {
    # Сводка за день из histCache: кВт·ч, стоимость, часы с данными, полнота.
    # Отсутствующие часы не считаем нулём: сумма только по измеренным кВт·ч.
    $now = Get-Date
    $isToday = ($date -eq $now.ToString('yyyy-MM-dd'))
    $expected = 24
    if ($isToday) { $expected = [Math]::Min(24, [int]$now.Hour + 1) }
    $agg = [pscustomobject]@{
        Date = [string]$date; Kwh = $null; Cost = $null; HasCost = $false; CostApprox = $false
        Hours = 0; Expected = $expected; Partial = $true; Loading = $false; HasData = $false; IsToday = $isToday
    }
    if ($null -eq $script:histCache -or -not $script:histCache.ContainsKey($date)) {
        $agg.Loading = [bool](Test-HistLoading $date)
        return $agg
    }
    $sumK = 0.0; $kN = 0; $hours = 0
    foreach ($r in @($script:histCache[$date])) {
        if ($null -eq $r) { continue }
        $n = [int](Field $r @('n') 0)
        if ($n -gt 0) { $hours++ }
        $rawK = Field $r @('kwh')
        if ($null -ne $rawK) { $sumK += [double]$rawK; $kN++ }
    }
    $agg.Hours = [int]$hours
    $agg.HasData = ($hours -gt 0)
    if ($kN -gt 0) { $agg.Kwh = $sumK }
    $agg.Partial = ($hours -lt $expected)
    try {
        if ($null -ne $script:board) {
            if ($isToday) {
                $c = Field $script:board @('tcost', 'today_cost')
                if ($null -ne $c) { $agg.Cost = [double]$c; $agg.HasCost = $true }
            } elseif ($date -eq $now.AddDays(-1).ToString('yyyy-MM-dd')) {
                $c = Field $script:board @('ykost', 'yesterday_cost')
                if ($null -ne $c) { $agg.Cost = [double]$c; $agg.HasCost = $true }
            }
            if ((-not $agg.HasCost) -and ($null -ne $agg.Kwh)) {
                $tf = Field $script:board @('tariff')
                if ($null -ne $tf) {
                    $agg.Cost = [double]$agg.Kwh * [double]$tf
                    $agg.HasCost = $true
                    $agg.CostApprox = $true
                }
            }
        }
    } catch { }
    return $agg
}

function Get-RangeAgg($rows, [string]$from, [string]$to) {
    # Сводка за диапазон из строк /range (дни). Дни без измерений не нули:
    # DaysWithData < Days => Partial, и такой результат нельзя выдавать за итог.
    $days = 1
    try { $days = [int]([Math]::Abs(([datetime]$to - [datetime]$from).TotalDays) + 1) } catch { $days = 1 }
    $agg = [pscustomobject]@{
        From = $from; To = $to; Days = $days; DaysWithData = 0; Rows = 0
        Kwh = $null; Cost = $null; HasCost = $false; Partial = $true
    }
    if ($null -eq $rows) { return $agg }
    $sumK = 0.0; $kN = 0; $sumC = 0.0; $cN = 0; $dd = 0; $nr = 0
    foreach ($r in @($rows)) {
        if ($null -eq $r) { continue }
        $d = [string](Field $r @('d') '')
        if ((!$d) -or ($d -lt $from) -or ($d -gt $to)) { continue }
        $nr++
        $rn = [int](Field $r @('rows') 0)
        if ($rn -gt 0) { $dd++ }
        $rawK = Field $r @('kwh')
        if ($null -ne $rawK) { $sumK += [double]$rawK; $kN++ }
        $rawC = Field $r @('cost')
        if ($null -ne $rawC) { $sumC += [double]$rawC; $cN++ }
    }
    $agg.Rows = $nr
    $agg.DaysWithData = $dd
    if ($kN -gt 0) { $agg.Kwh = $sumK }
    if ($cN -gt 0) { $agg.Cost = $sumC; $agg.HasCost = $true }
    $agg.Partial = ($dd -lt $days)
    return $agg
}

function Get-HourPeaks([string]$from, [string]$to, [int]$limit = 5) {
    # 5 максимальных НАБЛЮДАВШИХСЯ часовых пиков (pmax часа) + медиана часовой
    # средней (базовая нагрузка) + число часов с данными. Только реальные
    # измерения: null-поля пропускаем, ноль не подставляем.
    $days = 1
    try { $days = [int]([Math]::Abs(([datetime]$to - [datetime]$from).TotalDays) + 1) } catch { $days = 1 }
    $res = [pscustomobject]@{
        From = $from; To = $to; Days = $days; DaysWithHours = 0; Hours = 0
        ExpectedHours = 0; Peaks = @(); Base = $null; Weak = $true
    }
    # Сколько часов вообще должно было быть. Для периода, заканчивающегося
    # сегодня, считаем только уже прошедшие часы — иначе сегодняшний день
    # всегда выглядел бы «неполным».
    $todayKey = (Get-Date).ToString('yyyy-MM-dd')
    $expectedHours = 24 * $days
    if ([string]$to -eq $todayKey) { $expectedHours = 24 * ($days - 1) + [Math]::Min(24, [int](Get-Date).Hour + 1) }
    $res.ExpectedHours = $expectedHours
    if ($null -eq $script:histCache -or $script:histCache.Count -eq 0) { return $res }
    $found = New-Object 'System.Collections.Generic.List[object]'
    $avgs = New-Object 'System.Collections.Generic.List[double]'
    $dates = @($script:histCache.Keys | Where-Object { ([string]$_ -ge $from) -and ([string]$_ -le $to) } | Sort-Object)
    foreach ($date in $dates) {
        $dayHours = 0
        foreach ($r in @($script:histCache[$date])) {
            if ($null -eq $r) { continue }
            $n = [int](Field $r @('n') 0)
            if ($n -le 0) { continue }
            $dayHours++
            $rawP = Field $r @('p')
            if ($null -ne $rawP) { $avgs.Add([double]$rawP) }
            $rawPm = Field $r @('pmax')
            if ($null -ne $rawPm) {
                $hh = [int](Field $r @('h') 0)
                $found.Add([pscustomobject]@{ Date = [string]$date; Hour = $hh; W = [double]$rawPm })
            }
        }
        if ($dayHours -gt 0) { $res.DaysWithHours = [int]$res.DaysWithHours + 1 }
        $res.Hours = [int]$res.Hours + $dayHours
    }
    $sorted = @($found | Sort-Object -Property W -Descending)
    if ($sorted.Count -gt $limit) { $sorted = @($sorted[0..($limit - 1)]) }
    $res.Peaks = $sorted
    if ($avgs.Count -gt 0) {
        $arr = @($avgs | Sort-Object)
        $n2 = [int]$arr.Count
        if (($n2 % 2) -eq 1) {
            $res.Base = [double]$arr[[int](($n2 - 1) / 2)]
        } else {
            $mid = [int]($n2 / 2)
            $res.Base = ([double]$arr[$mid - 1] + [double]$arr[$mid]) / 2.0
        }
    }
    # «максимум среди полученных», если часов меньше ожидаемого или период
    # ещё не закончился (сегодня): за реальный максимум периода это не выдаём.
    $res.Weak = (([int]$res.Hours -lt [int]$res.ExpectedHours) -or ([string]$to -ge $todayKey))
    return $res
}

# ============================================================================
#  P2.5 — КЛИМАТ И ЭНЕРГИЯ (начало функций)
#
#  Окно «Статистика за период»: сводка по климату и расходу за выбранный
#  диапазон. Источники: $script:rangeRows (/range по дням), $script:histCache
#  (/history по часам), текущий /data ($script:board) и /climatehist за период
#  (один асинхронный запрос на смену периода, кэш по from/to — окно не ждёт).
#  Период длиннее 400 дней: /climatehist столько не отдаёт, поэтому окно
#  климата — последние 400 дней (подпись говорит об этом честно), а за весь
#  длинный период плату не просим. Ошибка загрузки кэшируется — рядом
#  кнопка «Повторить»: сброс ошибки периода + один повторный запрос.
#  Пустое значение — «—», не 0. 0 кВт·ч с измерениями — честный ноль.
#  Считать стараемся один раз: Get-ClimateCacheKey + кэш в $script:rangeCmpUi
#  (таймер окна тикает каждые 1,2 с, повторный пересчёт не нужен).
#
#  Что именно лежит в данных (сверено с cydv4.ino и docs/внутренняя заметка):
#   · t в /history и tmin/tmax в /range — температура ВНУТРИ РЕЛЕ (gTemp);
#   · комната (AHT20/BMP280): за период — /climatehist (дни d/n/tmin/tmax/
#     tavg/hmin/hmax/havg/pavg; 0.0 в поле — пропуск, дни без t отсутствуют);
#     текущее значение — поля et/eh/ep в /data («сейчас в комнате»);
#   · wtmin/wtmax в /range — прогноз улицы на день (мин/макс), не измерение.
# ============================================================================

function Get-PearsonR($xs, $ys) {
    # Коэффициент Пирсона по парам без пропусков. N < 5 — «мало данных»,
    # нулевой разброс (деление на ноль) — «нет разброса», а не NaN/бесконечность.
    $res = [pscustomobject]@{ N = 0; R = $null; Reason = 'нет данных' }
    if (($null -eq $xs) -or ($null -eq $ys)) { return $res }
    $xsL = @($xs); $ysL = @($ys)
    $n = [Math]::Min($xsL.Count, $ysL.Count)
    $k = 0; $sx = 0.0; $sy = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        if (($null -eq $xsL[$i]) -or ($null -eq $ysL[$i])) { continue }
        try { $sx += [double]$xsL[$i]; $sy += [double]$ysL[$i]; $k++ } catch { }
    }
    $res.N = $k
    if ($k -le 0) { $res.Reason = 'нет данных'; return $res }
    if ($k -lt 5) { $res.Reason = 'мало данных'; return $res }
    $mx = $sx / $k; $my = $sy / $k
    $sxx = 0.0; $syy = 0.0; $sxy = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        if (($null -eq $xsL[$i]) -or ($null -eq $ysL[$i])) { continue }
        try {
            $dx = [double]$xsL[$i] - $mx
            $dy = [double]$ysL[$i] - $my
            $sxx += $dx * $dx; $syy += $dy * $dy; $sxy += $dx * $dy
        } catch { }
    }
    $den = [Math]::Sqrt($sxx * $syy)
    if (($den -le 0.0) -or [double]::IsNaN($den) -or [double]::IsInfinity($den)) { $res.Reason = 'нет разброса'; return $res }
    $r = $sxy / $den
    if ([double]::IsNaN($r) -or [double]::IsInfinity($r)) { $res.Reason = 'нет разброса'; return $res }
    if ($r -gt 1.0) { $r = 1.0 } elseif ($r -lt -1.0) { $r = -1.0 }
    $res.R = $r
    $res.Reason = 'ok'
    return $res
}

function Format-ClimateCorr([string]$what, $p) {
    # Подпись корреляции. N < 5 — «мало данных»; когда r показан, рядом
    # обязательно «связь не означает причину» (никаких выводов о причинах).
    if ($null -eq $p) { return ('связь ' + $what + ' ↔ расход: нет данных') }
    if ($null -ne $p.R) {
        return ('связь ' + $what + ' ↔ расход дня: r = ' + (CmpSigned $p.R '0.00') + ', N = ' + [int]$p.N + ' · связь не означает причину')
    }
    if ($p.Reason -eq 'мало данных') { return ('связь ' + $what + ' ↔ расход: мало данных (N = ' + [int]$p.N + ', нужно ≥ 5)') }
    if ($p.Reason -eq 'нет разброса') { return ('связь ' + $what + ' ↔ расход: не рассчитать — нет разброса значений') }
    return ('связь ' + $what + ' ↔ расход: нет данных')
}

function Get-ClimateNum($v) {
    # Число из /climatehist. Плата пишет 0.0 вместо отсутствующего измерения,
    # поэтому null/пусто/NaN/Inf и ровно 0.0 — пропуск («—»), а не измерение.
    if ($null -eq $v) { return $null }
    $s = [string]$v
    if ($s -eq '') { return $null }
    $d = [double]0
    if (-not [double]::TryParse($s, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { return $null }
    if ([double]::IsNaN($d) -or [double]::IsInfinity($d)) { return $null }
    if ($d -eq 0.0) { return $null }
    return $d
}

function Get-ClimateWindow([string]$from, [string]$to) {
    # Период комнатного климата. /climatehist отдаёт не больше 400 дней
    # (прошивка, запись в журнал изменений от 17.11), поэтому если выбранный период
    # длиннее, окно климата сдвигается на ПОСЛЕДНИЕ 400 дней: за большее
    # плату не просим, значения не выдумываем, а подпись честно говорит,
    # что климат показан за последние 400 дней.
    # Возвращает: From/To (эффективные), Days (дней в окне), PeriodDays
    # (дней выбрано), Truncated и MaxDays.
    $res = [pscustomobject]@{
        From = [string]$from; To = [string]$to
        Days = 1; PeriodDays = 1; Truncated = $false; MaxDays = 400
    }
    if (([string]$from -eq '') -or ([string]$to -eq '')) { return $res }
    try { $a = [datetime]$from; $b = [datetime]$to } catch { return $res }
    if ($a -gt $b) { $x = $a; $a = $b; $b = $x }
    $periodDays = [int]([Math]::Abs(($b - $a).TotalDays) + 1)
    if ($periodDays -lt 1) { $periodDays = 1 }
    $res.PeriodDays = $periodDays
    $res.To = $b.ToString('yyyy-MM-dd')
    if ($periodDays -le 400) {
        $res.From = $a.ToString('yyyy-MM-dd'); $res.Days = $periodDays
        return $res
    }
    $res.Truncated = $true
    $res.From = $b.AddDays(-399).ToString('yyyy-MM-dd')
    $res.Days = 400
    return $res
}

function Format-ClimateWindowNote($win, $ent, [bool]$loading) {
    # Подпись «период длиннее 400 дней…» — только когда окно реально обрезано.
    # N из 400 — фактически пришедшие дни с валидной t; пока ответа нет, N не
    # выдумываем: пишем «загружаю» либо ничего (причина будет в строке покрытия).
    if ($null -eq $win) { return '' }
    if (-not [bool]$win.Truncated) { return '' }
    $maxD = 400
    try { $maxD = [int]$win.MaxDays } catch { $maxD = 400 }
    if ($maxD -lt 1) { $maxD = 400 }
    $txt = 'период длиннее 400 дней — климат показан за последние ' + $maxD + ' дней'
    if (($null -ne $ent) -and [bool]$ent.Ok) {
        $n = 0
        try { $n = [int]$ent.Count } catch { $n = 0 }
        $txt += ' (' + $n + ' из ' + $maxD + ')'
    } elseif ($loading) { $txt += ' (загружаю)' }
    return $txt
}

function Format-ClimateHours($date, $n, [string]$today) {
    # Ячейка «ч» в таблице климата: сколько часов с валидной t за сутки.
    # Сегодняшний день ещё идёт — показываем «n/24», чтобы он не выглядел
    # полным; за прошедшие дни просто n; нет данных — «—».
    if ($null -eq $n) { return '—' }
    $nn = 0
    try { $nn = [int]$n } catch { return '—' }
    if ($nn -le 0) { return '—' }
    if (([string]$date -ne '') -and ([string]$date -eq [string]$today)) { return ([string]$nn + '/24') }
    return [string]$nn
}

function Convert-ClimateHist($o, [string]$from, [string]$to) {
    # Разбор ответа /climatehist: дни с валидной t и итоги за период.
    # Пустой ответ / ok=false / нет поля days — ошибка, а не выдуманный ноль.
    # День вне диапазона, без валидной t или с n < 1 в расчёт не идёт;
    # дни возвращаются по одному разу и по возрастанию даты, повтор одной
    # даты считается один раз (берётся более полная запись). 0.0 — пропуск.
    $res = [pscustomobject]@{
        Ok = $false; Err = ''; Days = @(); Count = 0
        Totals = [pscustomobject]@{ Days = 0; Hours = 0; TMin = $null; TMax = $null; TAvg = $null
            HMin = $null; HMax = $null; HAvg = $null; PAvg = $null }
    }
    if ($null -eq $o) { $res.Err = 'пустой ответ'; return $res }
    if ($o.ok -eq $false) {
        $e = Field $o @('err', 'error')
        if ($null -ne $e) { $res.Err = [string]$e } else { $res.Err = 'плата ответила ok=false' }
        return $res
    }
    if ($null -eq $o.PSObject.Properties['days']) { $res.Err = 'в ответе нет поля days'; return $res }
    # Дни — по одному разу и по возрастанию даты: порядок в ответе и дубли не
    # важны. Из двух записей одного дня берём более полную (больше часов n),
    # при равенстве — первую, поэтому суммы по дням не задваиваются.
    $days = New-Object 'System.Collections.Generic.List[object]'
    $dayIdx = @{}
    foreach ($rawDay in @($o.days)) {
        if ($null -eq $rawDay) { continue }
        $d = [string](Field $rawDay @('d', 'date') '')
        if ($d -notmatch '^\d{4}-\d{2}-\d{2}$') { continue }
        if (($d -lt $from) -or ($d -gt $to)) { continue }
        $n = 0
        $rn = Field $rawDay @('n')
        if ($null -ne $rn) { try { $n = [int]$rn } catch { $n = 0 } }
        $tavg = Get-ClimateNum (Field $rawDay @('tavg'))
        if (($n -le 0) -or ($null -eq $tavg)) { continue }
        $day = [pscustomobject]@{
            D = $d; N = $n
            TMin = Get-ClimateNum (Field $rawDay @('tmin'))
            TMax = Get-ClimateNum (Field $rawDay @('tmax'))
            TAvg = $tavg
            HMin = Get-ClimateNum (Field $rawDay @('hmin'))
            HMax = Get-ClimateNum (Field $rawDay @('hmax'))
            HAvg = Get-ClimateNum (Field $rawDay @('havg'))
            PAvg = Get-ClimateNum (Field $rawDay @('pavg'))
        }
        if ($dayIdx.ContainsKey($d)) {
            $j = [int]$dayIdx[$d]
            if ($n -gt [int]$days[$j].N) { $days[$j] = $day }
            continue
        }
        $dayIdx[$d] = [int]$days.Count
        $days.Add($day)
    }
    $res.Days = @($days.ToArray() | Sort-Object -Property D)
    $res.Count = @($res.Days).Count
    $t = $res.Totals
    $sumT = 0.0; $sumH = 0.0; $nH = 0; $sumP = 0.0; $nP = 0
    foreach ($day in $res.Days) {
        $t.Days++
        $t.Hours += [int]$day.N
        # средняя t — по часам: дни с большим n весят больше
        $sumT += [double]$day.TAvg * [int]$day.N
        if (($null -ne $day.TMin) -and (($null -eq $t.TMin) -or ([double]$day.TMin -lt [double]$t.TMin))) { $t.TMin = [double]$day.TMin }
        if (($null -ne $day.TMax) -and (($null -eq $t.TMax) -or ([double]$day.TMax -gt [double]$t.TMax))) { $t.TMax = [double]$day.TMax }
        if ($null -ne $day.HMin) { if (($null -eq $t.HMin) -or ([double]$day.HMin -lt [double]$t.HMin)) { $t.HMin = [double]$day.HMin } }
        if ($null -ne $day.HMax) { if (($null -eq $t.HMax) -or ([double]$day.HMax -gt [double]$t.HMax)) { $t.HMax = [double]$day.HMax } }
        if ($null -ne $day.HAvg) { $sumH += [double]$day.HAvg * [int]$day.N; $nH += [int]$day.N }
        if ($null -ne $day.PAvg) { $sumP += [double]$day.PAvg * [int]$day.N; $nP += [int]$day.N }
    }
    if ($t.Hours -gt 0) { $t.TAvg = $sumT / $t.Hours }
    if ($nH -gt 0) { $t.HAvg = $sumH / $nH }
    if ($nP -gt 0) { $t.PAvg = $sumP / $nP }
    $res.Ok = $true
    return $res
}

function Format-ClimateRoomPeriod($ent, [int]$periodDays, [bool]$loading) {
    # Строка «комната за период» для окна периода. Нет ответа — «—», ошибка —
    # «—» с причиной; загрузка — честная надпись, а не ноль. Числа — по n часов.
    if ($null -eq $ent) {
        if ($loading) { return 'комната за период: загружаю /climatehist…' }
        return 'комната за период: — (нет ответа /climatehist)'
    }
    if (-not [bool]$ent.Ok) { return ('комната за период: — (/climatehist: ' + [string]$ent.Err + ')') }
    $n = [int]$ent.Count
    if ($n -le 0) { return ('комната за период: — (/climatehist: за ' + [string]$ent.From + '…' + [string]$ent.To + ' нет ни одного дня с валидной t)') }
    $t = $ent.Totals
    $txt = 'комната за период: t ' + (CmpNum $t.TMin '0.0') + '–' + (CmpNum $t.TMax '0.0') + ' °C · средняя ' + (CmpNum $t.TAvg '0.0') + ' °C'
    $txt += ' · влажность ' + (CmpNum $t.HMin '0') + '–' + (CmpNum $t.HMax '0') + ' % · средняя ' + (CmpNum $t.HAvg '0') + ' %'
    $txt += ' · давление среднее ' + (CmpNum $t.PAvg '0') + ' гПа'
    $partial = 0
    foreach ($day in @($ent.Days)) {
        if ($null -eq $day) { continue }
        $dn = 0
        try { $dn = [int]$day.N } catch { $dn = 0 }
        if (($dn -gt 0) -and ($dn -lt 24)) { $partial++ }
    }
    $txt += ' · покрытие ' + $n + ' дн. из ' + [Math]::Max(1, $periodDays) + ' (' + [int]$t.Hours + ' ч'
    if ($partial -gt 0) { $txt += ', неполных суток ' + $partial }
    $txt += ')'
    return $txt
}

function Get-ClimateHistEntry([string]$from, [string]$to) {
    # Запись кэша /climatehist по ключу from|to (или $null, если ещё нет).
    if ($null -eq $script:climateHistCache) { return $null }
    $key = [string]$from + '|' + [string]$to
    if (-not $script:climateHistCache.ContainsKey($key)) { return $null }
    return $script:climateHistCache[$key]
}

function Set-ClimateHistEntry($entry) {
    # Кладём ответ (в т.ч. ошибку) в кэш по from|to; храним последние 8 периодов.
    if ($null -eq $entry) { return }
    if ($null -eq $script:climateHistCache) { $script:climateHistCache = @{} }
    $key = [string]$entry.Key
    $script:climateHistCache[$key] = $entry
    $lst = New-Object 'System.Collections.Generic.List[string]'
    foreach ($k in @($script:climateHistOrder)) {
        if (($k -ne $key) -and $script:climateHistCache.ContainsKey([string]$k)) { $lst.Add([string]$k) }
    }
    $lst.Insert(0, $key)
    while ($lst.Count -gt 8) {
        $last = $lst[$lst.Count - 1]
        $lst.RemoveAt($lst.Count - 1)
        if ($script:climateHistCache.ContainsKey($last)) { $script:climateHistCache.Remove($last) }
    }
    $script:climateHistOrder = @($lst.ToArray())
}

function Remove-ClimateHistEntry([string]$from, [string]$to) {
    # Сброс закэшированной ошибки /climatehist для периода (кнопка «Повторить»).
    # Кэш и порядок ключей чистятся вместе, чтобы не копить мусор.
    if ($null -eq $script:climateHistCache) { return }
    $key = [string]$from + '|' + [string]$to
    if ($script:climateHistCache.ContainsKey($key)) { $script:climateHistCache.Remove($key) }
    $lst = New-Object 'System.Collections.Generic.List[string]'
    foreach ($k in @($script:climateHistOrder)) { if ([string]$k -ne $key) { $lst.Add([string]$k) } }
    $script:climateHistOrder = @($lst.ToArray())
}

function Set-ClimateHistFail($st, [string]$msg) {
    # Ошибка запроса кэшируется по from|to: окно показывает прочерк, а плату
    # повторно не дёргаем до смены периода.
    if ($null -eq $st) { return }
    Set-ClimateHistEntry ([pscustomobject]@{
        Key = [string]$st.Key; Ip = [string]$st.Ip; From = [string]$st.From; To = [string]$st.To
        Ok = $false; Err = [string]$msg; Days = @(); Count = 0
        Totals = [pscustomobject]@{ Days = 0; Hours = 0; TMin = $null; TMax = $null; TAvg = $null
            HMin = $null; HMax = $null; HAvg = $null; PAvg = $null }
        At = Get-Date
    })
}

function Stop-ClimateLoad {
    $st = $script:climateLoad
    $script:climateLoad = $null
    if ($null -eq $st) { return }
    try { if ($null -ne $st.Cts) { $st.Cts.Cancel(); $st.Cts.Dispose() } } catch { }
}

function Start-ClimateLoad([string]$ip, [string]$from, [string]$to) {
    # Один асинхронный запрос /climatehist на смену периода (не блокирует окно).
    # Ответ забирает Pump-ClimateLoad на тике On-Tick и кэширует по from/to.
    if (-not $ip) { return }
    if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    $key = [string]$from + '|' + [string]$to
    $have = Get-ClimateHistEntry $from $to
    if (($null -ne $have) -and ([string]$have.Ip -eq [string]$ip)) { return }
    $cur = $script:climateLoad
    if (($null -ne $cur) -and ([string]$cur.Key -eq $key)) { return }
    Stop-ClimateLoad
    $script:climateLoad = [pscustomobject]@{
        Ip = $ip; From = $from; To = $to; Key = $key
        Task = $null; Cts = $null; TaskAt = [datetime]::MinValue; RetryAt = [datetime]::MinValue
        Attempts = 0; Deadline = (Get-Date).AddSeconds(25)
    }
    if ($null -eq $script:httpSlow) {
        # клиента ещё нет — не считаем это ошибкой периода, попробуем на следующем тике
        $script:climateLoad = $null
        return
    }
    Pump-ClimateLoad
}

function Pump-ClimateLoad {
    # Догрузка /climatehist на тике On-Tick: окно не ждёт. Плата может ответить
    # pending — повторяем, но не дольше Deadline, чтобы не долбить её вечно.
    $st = $script:climateLoad
    if ($null -eq $st) { return }
    if ($null -eq $script:httpSlow) { $script:climateLoad = $null; return }
    if ($null -ne $st.Task) {
        if (-not $st.Task.IsCompleted) {
            if (((Get-Date) - $st.TaskAt).TotalSeconds -gt 20) {
                try { if ($null -ne $st.Cts) { $st.Cts.Cancel() } } catch { }
            } else { return }
        }
        $task = $st.Task
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        try {
            if ($task.IsCanceled) { throw 'плата не ответила за 20 с' }
            if ($task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw (Get-TaskError $task) }
            $resp = $task.Result
            if ($null -eq $resp -or -not $resp.IsSuccessStatusCode) {
                $code = if ($null -ne $resp) { [int]$resp.StatusCode } else { 0 }
                if ($code -gt 0) { throw ('плата отдала HTTP ' + $code) } else { throw 'плата не ответила' }
            }
            $o = ($resp.Content.ReadAsStringAsync().Result) | ConvertFrom-Json
            if (($null -ne $o) -and $o.pending) {
                if (((Get-Date) -ge $st.Deadline) -or ([int]$st.Attempts -ge 4)) { throw 'плата считает климат слишком долго' }
                $ms = 700
                $rawR = Field $o @('retryMs')
                if ($null -ne $rawR) { $ms = [Math]::Max(400, [Math]::Min(4000, [int]$rawR)) }
                $st.RetryAt = (Get-Date).AddMilliseconds($ms)
                return
            }
            $conv = Convert-ClimateHist $o $st.From $st.To
            if (-not [bool]$conv.Ok) { throw [string]$conv.Err }
            Set-ClimateHistEntry ([pscustomobject]@{
                Key = [string]$st.Key; Ip = [string]$st.Ip; From = [string]$st.From; To = [string]$st.To
                Ok = $true; Err = ''; Days = $conv.Days; Count = [int]$conv.Count; Totals = $conv.Totals
                At = Get-Date
            })
            $script:climateLoad = $null
            Add-Log ('климат за период ' + $st.From + '…' + $st.To + ': дней с данными ' + [int]$conv.Count)
            return
        } catch {
            $msg = [string]$_.Exception.Message
            if (((Get-Date) -lt $st.Deadline) -and ([int]$st.Attempts -lt 3)) {
                $st.RetryAt = (Get-Date).AddMilliseconds(700)
                return
            }
            Set-ClimateHistFail $st $msg
            $script:climateLoad = $null
            Add-Log ('климат за период ' + $st.From + '…' + $st.To + ': ошибка ' + $msg) 'warn'
            return
        }
    }
    if ($st.RetryAt -gt (Get-Date)) { return }
    if ((Get-Date) -ge $st.Deadline) {
        Set-ClimateHistFail $st 'плата не ответила за 25 с'
        $script:climateLoad = $null
        Add-Log ('климат за период ' + $st.From + '…' + $st.To + ': ошибка плата не ответила за 25 с') 'warn'
        return
    }
    try {
        $st.Cts = New-Object System.Threading.CancellationTokenSource
        $st.Task = $script:httpSlow.GetAsync(('http://' + $st.Ip + '/climatehist?from=' + $st.From + '&to=' + $st.To), $st.Cts.Token)
        $st.TaskAt = Get-Date
        $st.Attempts = [int]$st.Attempts + 1
    } catch {
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        Set-ClimateHistFail $st ([string]$_.Exception.Message)
        $script:climateLoad = $null
        Add-Log ('климат за период ' + $st.From + '…' + $st.To + ': ошибка ' + $_.Exception.Message) 'warn'
    }
}

function Get-ClimateCacheKey($rows, $histCache, [string]$from, [string]$to) {
    # Дешёвый ключ кэша сводки: меняется при перезагрузке /range (новый массив)
    # и при любом обновлении histCache (новый день / перезагрузка дня, histAt).
    $rh = 0; $rc = 0
    if ($null -ne $rows) {
        try { $rc = @($rows).Count } catch { }
        try { $rh = $rows.GetHashCode() } catch { }
    }
    $hc = 0; $hs = 0; $ht = 0L
    if ($null -ne $histCache) {
        try {
            $hc = [int]$histCache.Count
            foreach ($k in $histCache.Keys) {
                $ks = [string]$k
                if (($ks -lt $from) -or ($ks -gt $to)) { continue }
                $lst = $histCache[$k]
                if ($null -eq $lst) { continue }
                $hs = [int]$hs + [int]@($lst).Count
                try {
                    if (($null -ne $script:histAt) -and $script:histAt.ContainsKey($k)) {
                        $at = $script:histAt[$k]
                        if ($null -ne $at) { $tk = [int64]$at.Ticks; if ($tk -gt $ht) { $ht = $tk } }
                    }
                } catch { }
            }
        } catch { }
    }
    return ([string]$from + '|' + [string]$to + '|' + [string]$rc + '|' + [string]$rh + '|' + [string]$hc + '|' + [string]$hs + '|' + [string]$ht)
}

function Get-ClimateAgg($rows, $histCache, [string]$from, [string]$to, $climateDays = $null) {
    # Сводка «климат и энергия» за диапазон по уже полученным данным.
    # Дни без измерений не считаются нулями; 0 кВт·ч с измерениями — ноль.
    # Читаем свойства напрямую ($row.kwh): это в разы быстрее Field() на
    # каждом часе, а отсутствующее свойство даёт $null (Set-StrictMode в
    # приложении не включён).
    # Период климата ограничен 400 днями (столько отдаёт /climatehist): если
    # выбранный период длиннее, считаем ПОСЛЕДНИЕ 400 дней, а не первые, и
    # помечаем это в подписи. Значения за пределами окна не выдумываем.
    $win = Get-ClimateWindow ([string]$from) ([string]$to)
    $from = [string]$win.From; $to = [string]$win.To
    $days = [int]$win.Days
    $res = [pscustomobject]@{
        From = $from; To = $to; Days = $days; Cells = @()
        PeriodDays = [int]$win.PeriodDays; Truncated = [bool]$win.Truncated
        WindowFrom = [string]$win.From; WindowTo = [string]$win.To
        MeterDays = 0; CostDays = 0; KwhSum = 0.0; CostSum = 0.0
        KwhPerDay = $null; CostPerDay = $null
        RelayFromHistory = $false; RelayAvg = $null; RelayMin = $null; RelayMax = $null; RelayHours = 0
        TrelayDays = 0; OutDays = 0; OutAvg = $null; OutMin = $null; OutMax = $null
        RoomHasHistory = $false; RoomAvg = $null; RoomMin = $null; RoomMax = $null; RoomHum = $null; RoomHours = 0; RoomDays = 0
        ClimDays = 0; ClimHours = 0
        CorrRelay = $null; CorrOut = $null; CorrRoom = $null
        TableSig = ''
    }
    if ($null -eq $rows) { $rows = @() }

    # --- строки /range по датам: кВт·ч, ₽ и дневные t реле/улица -----------
    $byDate = @{}
    foreach ($r in @($rows)) {
        if ($null -eq $r) { continue }
        $d = [string]$r.d
        if ($d) { $byDate[$d] = $r }
    }

    # --- дни /climatehist по дате: реальный климат комнаты -------------------
    # Ключ — дата: дубли не задваиваются даже если пришли из кэша; дни вне
    # окна климата (последние 400 дней) в расчёт не идут.
    $climByDate = @{}
    if ($null -ne $climateDays) {
        foreach ($cd in @($climateDays)) {
            if ($null -eq $cd) { continue }
            $cdd = [string]$cd.D
            if (-not $cdd) { continue }
            if (($cdd -lt $from) -or ($cdd -gt $to)) { continue }
            $climByDate[$cdd] = $cd
        }
    }

    # --- есть ли в почасовых строках поля комнаты: проверяем одну строку ----
    $hasRoomT = $false; $hasRoomH = $false
    if (($null -ne $histCache) -and ([int]$histCache.Count -gt 0)) {
        $probe = $null
        foreach ($k0 in $histCache.Keys) {
            $lst0 = $histCache[$k0]
            if ($null -eq $lst0) { continue }
            foreach ($hr0 in $lst0) { if ($null -ne $hr0) { $probe = $hr0; break } }
            if ($null -ne $probe) { break }
        }
        if ($null -ne $probe) {
            $pr0 = $probe.PSObject.Properties
            foreach ($nm in @('et', 'roomT', 'troom', 'envT')) { if ($null -ne $pr0[$nm]) { $hasRoomT = $true } }
            foreach ($nm in @('eh', 'roomH', 'hroom', 'envH')) { if ($null -ne $pr0[$nm]) { $hasRoomH = $true } }
        }
    }

    # --- почасовые данные: t реле (и комната, если поля появятся) ----------
    $sumT = 0.0; $nT = 0; $minT = $null; $maxT = $null
    $dayRelay = @{}
    $sumRT = 0.0; $nRT = 0; $minRT = $null; $maxRT = $null
    $sumRH = 0.0; $nRH = 0
    $dayRoom = @{}; $dayHum = @{}
    if (($null -ne $histCache) -and ([int]$histCache.Count -gt 0)) {
        foreach ($k in $histCache.Keys) {
            $dk = [string]$k
            if (($dk -lt $from) -or ($dk -gt $to)) { continue }
            $lst = $histCache[$k]
            if ($null -eq $lst) { continue }
            $s = 0.0; $n = 0
            $rs = 0.0; $rn = 0
            $hs = 0.0; $hn = 0
            foreach ($hr in $lst) {
                if ($null -eq $hr) { continue }
                $tv = $hr.t
                if ($null -ne $tv) {
                    try {
                        $x = [double]$tv
                        $s += $x; $n++
                        $sumT += $x; $nT++
                        if (($null -eq $minT) -or ($x -lt $minT)) { $minT = $x }
                        if (($null -eq $maxT) -or ($x -gt $maxT)) { $maxT = $x }
                    } catch { }
                }
                if ($hasRoomT) {
                    $rtv = $hr.et
                    if ($null -eq $rtv) { $rtv = $hr.roomT }
                    if ($null -eq $rtv) { $rtv = $hr.troom }
                    if ($null -eq $rtv) { $rtv = $hr.envT }
                    if ($null -ne $rtv) {
                        try {
                            $xr = [double]$rtv
                            $rs += $xr; $rn++
                            $sumRT += $xr; $nRT++
                            if (($null -eq $minRT) -or ($xr -lt $minRT)) { $minRT = $xr }
                            if (($null -eq $maxRT) -or ($xr -gt $maxRT)) { $maxRT = $xr }
                        } catch { }
                    }
                }
                if ($hasRoomH) {
                    $rhv = $hr.eh
                    if ($null -eq $rhv) { $rhv = $hr.roomH }
                    if ($null -eq $rhv) { $rhv = $hr.hroom }
                    if ($null -eq $rhv) { $rhv = $hr.envH }
                    if ($null -ne $rhv) { try { $x2 = [double]$rhv; $hs += $x2; $hn++; $sumRH += $x2; $nRH++ } catch { } }
                }
            }
            if ($n -gt 0) { $dayRelay[$dk] = $s / $n }
            if ($rn -gt 0) { $dayRoom[$dk] = $rs / $rn }
            if ($hn -gt 0) { $dayHum[$dk] = $hs / $hn }
        }
    }
    $res.RelayHours = $nT
    if ($nT -gt 0) {
        $res.RelayFromHistory = $true
        $res.RelayAvg = $sumT / $nT; $res.RelayMin = $minT; $res.RelayMax = $maxT
    }

    # --- по дням диапазона -------------------------------------------------
    $cells = New-Object 'System.Collections.Generic.List[object]'
    $xsR = New-Object 'System.Collections.Generic.List[double]'; $ysR = New-Object 'System.Collections.Generic.List[double]'
    $xsO = New-Object 'System.Collections.Generic.List[double]'; $ysO = New-Object 'System.Collections.Generic.List[double]'
    $xsC = New-Object 'System.Collections.Generic.List[double]'; $ysC = New-Object 'System.Collections.Generic.List[double]'
    $rSum = 0.0; $rN = 0; $rMin = $null; $rMax = $null
    $oSum = 0.0; $oN = 0; $oMin = $null; $oMax = $null
    $roomS = 0.0; $roomN = 0; $roomD = 0
    $meterDays = 0; $costDays = 0; $kwhSum = 0.0; $costSum = 0.0; $climHours = 0
    $d0 = [datetime]$from
    for ($i = 0; $i -lt $days; $i++) {
        $date = $d0.AddDays($i).ToString('yyyy-MM-dd')
        $row = $byDate[$date]
        $hasMeter = $false
        $kwh = $null; $cost = $null
        if ($null -ne $row) {
            $rnv = $row.rows
            if ($null -ne $rnv) { try { $hasMeter = ([int]$rnv -gt 0) } catch { $hasMeter = $false } }
            if ($hasMeter) {
                $rawK = $row.kwh
                if ($null -ne $rawK) { try { $kwh = [double]$rawK } catch { $kwh = $null } }
                $rawC = $row.cost
                if ($null -ne $rawC) { try { $cost = [double]$rawC } catch { $cost = $null } }
            }
        }
        # t реле за день: почасовое среднее, иначе середина дневного мин–макс
        $trelay = $null
        if ($dayRelay.ContainsKey($date)) {
            $trelay = [double]$dayRelay[$date]
        } elseif (($null -ne $row) -and $hasMeter) {
            $tvalid = $row.tvalid
            $tOk = $true
            if ($null -ne $tvalid) { $tOk = [bool]$tvalid }
            $tminR = $row.tmin; $tmaxR = $row.tmax
            if ($tOk -and ($null -ne $tminR) -and ($null -ne $tmaxR)) {
                try { $trelay = ([double]$tminR + [double]$tmaxR) / 2.0 } catch { $trelay = $null }
            }
        }
        # улица за день: прогноз платы, середина мин–макс (не измерение)
        $tout = $null
        if ($null -ne $row) {
            $wOk = $false
            $wv = $row.wvalid
            if ($null -ne $wv) { $wOk = [bool]$wv } else { $wOk = ("$($row.w)" -ne '') }
            $wa = $row.wtmin; $wb = $row.wtmax
            if ($wOk -and ($null -ne $wa) -and ($null -ne $wb)) {
                try { $tout = ([double]$wa + [double]$wb) / 2.0 } catch { $tout = $null }
            }
        }
        # t комнаты дня: /climatehist (реальные измерения), иначе — если поля
        # комнаты когда-нибудь появятся в /history. ClimN — часы с валидной t
        # за сутки: по нему в таблице видно неполный день («сегодня n/24»).
        $troom = $null; $climN = $null
        if ($climByDate.ContainsKey($date)) {
            $cdv = $climByDate[$date]
            if ($null -ne $cdv.TAvg) { $troom = [double]$cdv.TAvg }
            try { $climN = [int]$cdv.N } catch { $climN = $null }
        } elseif ($dayRoom.ContainsKey($date)) { $troom = [double]$dayRoom[$date] }
        $hum = $null
        if ($climByDate.ContainsKey($date)) {
            $cdh = $climByDate[$date].HAvg
            if ($null -ne $cdh) { $hum = [double]$cdh }
        } elseif ($dayHum.ContainsKey($date)) { $hum = [double]$dayHum[$date] }
        # ₽ дня: 0 ₽ при израсходованных кВт·ч — это не стоимость, а отсутствие
        # тарифа; такой день в среднее по ₽ не идёт (пусто, а не 0).
        if (($null -ne $cost) -and ($null -ne $kwh) -and ([double]$cost -eq 0.0) -and ([double]$kwh -gt 0.0)) { $cost = $null }
        if ($null -ne $climN) { $climHours += [int]$climN }
        $cells.Add([pscustomobject]@{
            Date = $date; Kwh = $kwh; Cost = $cost; HasMeter = $hasMeter
            Trelay = $trelay; Tout = $tout; Troom = $troom; Hum = $hum; ClimN = $climN
        })
        if ($null -ne $kwh) {
            $meterDays++
            $kwhSum += [double]$kwh
        }
        if ($null -ne $cost) {
            $costDays++
            $costSum += [double]$cost
        }
        if ($null -ne $trelay) {
            $rSum += [double]$trelay; $rN++
            if (($null -eq $rMin) -or ([double]$trelay -lt $rMin)) { $rMin = [double]$trelay }
            if (($null -eq $rMax) -or ([double]$trelay -gt $rMax)) { $rMax = [double]$trelay }
            if ($null -ne $kwh) { $xsR.Add([double]$trelay); $ysR.Add([double]$kwh) }
        }
        if ($null -ne $tout) {
            $oSum += [double]$tout; $oN++
            if (($null -eq $oMin) -or ([double]$tout -lt $oMin)) { $oMin = [double]$tout }
            if (($null -eq $oMax) -or ([double]$tout -gt $oMax)) { $oMax = [double]$tout }
            if ($null -ne $kwh) { $xsO.Add([double]$tout); $ysO.Add([double]$kwh) }
        }
        if ($null -ne $troom) {
            $roomS += [double]$troom; $roomN++; $roomD++
            if ($null -ne $kwh) { $xsC.Add([double]$troom); $ysC.Add([double]$kwh) }
        }
    }
    $res.Days = $days
    $res.ClimDays = $climByDate.Count
    $res.Cells = $cells.ToArray()
    $res.MeterDays = $meterDays; $res.CostDays = $costDays
    $res.KwhSum = $kwhSum; $res.CostSum = $costSum
    $res.TrelayDays = $rN
    if ($meterDays -gt 0) { $res.KwhPerDay = $kwhSum / $meterDays }
    if ($costDays -gt 0) { $res.CostPerDay = $costSum / $costDays }
    if (-not $res.RelayFromHistory) {
        # почасовых данных нет: средняя/мин/макс по серединам дневных мин–макс
        if ($rN -gt 0) { $res.RelayAvg = $rSum / $rN; $res.RelayMin = $rMin; $res.RelayMax = $rMax }
    }
    if ($oN -gt 0) { $res.OutDays = $oN; $res.OutAvg = $oSum / $oN; $res.OutMin = $oMin; $res.OutMax = $oMax }
    if ($roomD -gt 0) {
        $res.RoomHasHistory = $true
        $res.RoomDays = $roomD
        if ($roomN -gt 0) { $res.RoomAvg = $roomS / $roomN }
    }
    $res.RoomHours = $nRT
    if ($nRH -gt 0) { $res.RoomHum = $sumRH / $nRH }
    $res.CorrRelay = Get-PearsonR $xsR $ysR
    $res.CorrOut = Get-PearsonR $xsO $ysO
    $res.CorrRoom = Get-PearsonR $xsC $ysC
    $res.ClimHours = $climHours
    $res.TableSig = ([string]$cells.Count + '|' + [string]$meterDays + '|' + [string]$costDays + '|' +
        [string]$rN + '|' + [string]$oN + '|' + [string]$roomD + '|' + [string]$climByDate.Count + '|' +
        [string]$climHours + '|' +
        (CmpNum $kwhSum '0.000') + '|' + (CmpNum $costSum '0.00') + '|' +
        (CmpNum $rSum '0.00') + '|' + (CmpNum $oSum '0.0') + '|' + (CmpNum $roomS '0.0'))
    return $res
}
#  P2.5 — КЛИМАТ И ЭНЕРГИЯ (конец функций)
# ============================================================================
function Stop-CmpLoad {
    $st = $script:cmpLoad
    $script:cmpLoad = $null
    if ($null -eq $st) { return }
    try { if ($null -ne $st.Cts) { $st.Cts.Cancel(); $st.Cts.Dispose() } } catch { }
}

function Start-CmpLoad([string]$ip, [string]$from, [string]$to) {
    # Асинхронно просит /range за ПРЕДЫДУЩИЙ период — кусками до 31 дня, как
    # основная загрузка периода. Ничего не блокирует: результат забирает
    # Pump-CmpLoad на тике On-Tick.
    Stop-CmpLoad
    if (-not $ip) { return }
    if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    $chunks = New-Object 'System.Collections.Generic.List[object]'
    $dA = [datetime]$from; $dB = [datetime]$to
    $guard = 0
    while ($dA -le $dB -and $guard -lt 400) {
        $guard++
        $chunkEnd = $dA.AddDays(30)
        if ($chunkEnd -gt $dB) { $chunkEnd = $dB }
        $chunks.Add([pscustomobject]@{ From = $dA.ToString('yyyy-MM-dd'); To = $chunkEnd.ToString('yyyy-MM-dd') })
        $dA = $chunkEnd.AddDays(1)
    }
    $script:cmpLoad = [pscustomobject]@{
        Ip = $ip; From = [string]$from; To = [string]$to
        Chunks = $chunks; Index = 0; Task = $null; Cts = $null
        TaskAt = [datetime]::MinValue; RetryAt = [datetime]::MinValue
        ChunkAt = [datetime]::MinValue; ChunkIndex = -1
        Rows = (New-Object 'System.Collections.Generic.List[object]'); Fail = $null
    }
    if ($null -eq $script:httpSlow) {
        $script:cmpFail = 'HTTP-клиент не готов'
        $script:cmpLoad = $null
        return
    }
    Pump-CmpLoad
}

function Pump-CmpLoad {
    # Догрузка предыдущего периода. Повторяет логику Pump-RangeLoad: плата
    # может ответить 202 pending, а неудачный кусок повторяем ~170 с.
    $st = $script:cmpLoad
    if ($null -eq $st) { return }
    if ($null -eq $script:httpSlow) { $script:cmpFail = 'HTTP-клиент не готов'; $script:cmpLoad = $null; return }
    if ($null -ne $st.Task) {
        if (-not $st.Task.IsCompleted) {
            if (((Get-Date) - $st.TaskAt).TotalSeconds -gt 185) {
                try { if ($null -ne $st.Cts) { $st.Cts.Cancel() } } catch { }
            } else { return }
        }
        $task = $st.Task
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        try {
            if ($task.IsCanceled) { throw 'плата не ответила за 180 с' }
            if ($task.Status -ne [Threading.Tasks.TaskStatus]::RanToCompletion) { throw (Get-TaskError $task) }
            $resp = $task.Result
            if ($null -eq $resp -or -not $resp.IsSuccessStatusCode) {
                $code = if ($null -ne $resp) { [int]$resp.StatusCode } else { 0 }
                if ($code -gt 0) { throw ('плата отдала HTTP ' + $code) } else { throw 'плата не ответила' }
            }
            $o = ($resp.Content.ReadAsStringAsync().Result) | ConvertFrom-Json
            if ($o.pending) {
                $ms = 700
                $rawR = Field $o @('retryMs')
                if ($null -ne $rawR) { $ms = [Math]::Max(400, [Math]::Min(4000, [int]$rawR)) }
                $st.RetryAt = (Get-Date).AddMilliseconds($ms)
                return
            }
            if ($o.trunc -or $o.ok -eq $false -or $null -eq $o.PSObject.Properties['rows']) { throw 'неполный ответ периода' }
            foreach ($row in @($o.rows)) { if ($null -ne $row) { $st.Rows.Add($row) } }
            $st.Index++
            $st.RetryAt = Get-Date
        } catch {
            $msg = $_.Exception.Message
            $chunkAt = if ($st.ChunkAt -gt [datetime]::MinValue) { $st.ChunkAt } else { $st.TaskAt }
            if (((Get-Date) - $chunkAt).TotalSeconds -lt 170) {
                $st.RetryAt = (Get-Date).AddMilliseconds(700)
                return
            }
            $st.Fail = [string]$msg
        }
    }
    if ($null -ne $st.Fail) {
        $script:cmpFail = [string]$st.Fail
        $script:cmpLoad = $null
        Add-Log ('сравнение периодов: ошибка ' + $script:cmpFail) 'warn'
        return
    }
    if ($st.RetryAt -gt (Get-Date)) { return }
    if ($st.Index -ge $st.Chunks.Count) {
        $script:cmpRows = @($st.Rows.ToArray() | Where-Object { $null -ne $_ })
        $script:cmpKey = ([string]$st.From + '|' + [string]$st.To)
        $script:cmpFail = ''
        $script:cmpLoad = $null
        Add-Log ('сравнение периодов: предыдущий период ' + $st.From + '…' + $st.To + ' загружен, строк ' + @($script:cmpRows).Count)
        try { Update-RangeAnalytics } catch { }
        return
    }
    $chunk = $st.Chunks[$st.Index]
    if ([int]$st.ChunkIndex -ne [int]$st.Index) { $st.ChunkIndex = [int]$st.Index; $st.ChunkAt = Get-Date }
    try {
        $st.Cts = New-Object System.Threading.CancellationTokenSource
        $st.Task = $script:httpSlow.GetAsync(('http://' + $st.Ip + '/range?from=' + $chunk.From + '&to=' + $chunk.To), $st.Cts.Token)
        $st.TaskAt = Get-Date
    } catch {
        $st.Task = $null
        try { if ($null -ne $st.Cts) { $st.Cts.Dispose() } } catch { }
        $st.Cts = $null
        $st.Fail = $_.Exception.Message
    }
}

function Update-RangeAnalytics {
    # Пересчёт «сравнения периодов» и «пиков нагрузки» из уже полученных данных.
    # Зовётся таймером окна периода. Своих блокирующих запросов нет; для
    # короткого периода недостающие дни ставятся в общую очередь Request-Day.
    $ui = $script:rangeCmpUi
    if ($null -eq $ui) { return }
    $now = Get-Date
    $todayKey = $now.ToString('yyyy-MM-dd')
    $yestKey = $now.AddDays(-1).ToString('yyyy-MM-dd')
    $span = $script:rangeSpan

    # --- 0. недостающие дни короткого периода — в общую очередь /history ------
    if (($null -ne $span) -and ([int]$span.Days -le 7)) {
        try {
            $dd = [datetime]$span.From
            $guard = 0
            while (($dd -le [datetime]$span.To) -and ($guard -lt 10) -and ($dd -le $now)) {
                $guard++
                $k = $dd.ToString('yyyy-MM-dd')
                if (-not $script:histCache.ContainsKey($k)) { Request-Day $k }
                $dd = $dd.AddDays(1)
            }
        } catch { }
    }

    # --- 1. сегодня против вчера ---------------------------------------------
    $aggToday = Get-HistDayAgg $todayKey
    $aggYest = Get-HistDayAgg $yestKey
    $l1 = 'сегодня / вчера: ' + (Format-CmpPair $aggToday.Kwh $aggYest.Kwh '0.00' 'кВт·ч')
    if ($aggToday.HasCost -or $aggYest.HasCost) {
        $costTxt = Format-CmpPair $aggToday.Cost $aggYest.Cost '0.00' '₽'
        if ($aggToday.CostApprox -or $aggYest.CostApprox) { $costTxt += ' (≈ по тарифу)' }
        $l1 += ("`r`n" + 'стоимость: ' + $costTxt)
    } else {
        $l1 += ("`r`n" + 'стоимость: —')
    }
    Set-Lbl $ui.Today $l1

    # --- 2. диапазон против непосредственно предшествующего такой же длины ---
    $curRange = $null; $prevRange = $null
    if ($null -eq $span) {
        Set-Lbl $ui.Range 'диапазон / предыдущий: —'
    } elseif ([int]$span.Days -le 1) {
        $curRange = Get-RangeAgg $script:rangeRows $span.From $span.To
        Set-Lbl $ui.Range ('диапазон / предыдущий: один день — сравнение «сегодня / вчера» выше' + "`r`n" + 'стоимость: —')
    } elseif (($null -eq $script:rangeLoad) -and (@($script:rangeRows).Count -eq 0)) {
        # За период не пришло ни одной строки — не обещаем вечную загрузку.
        Set-Lbl $ui.Range ('диапазон / предыдущий: за период нет данных — сравнивать нечего' + "`r`n" + 'стоимость: —')
    } else {
        $curRange = Get-RangeAgg $script:rangeRows $span.From $span.To
        $prevFrom = ([datetime]$span.From).AddDays(-[int]$span.Days).ToString('yyyy-MM-dd')
        $prevTo = ([datetime]$span.From).AddDays(-1).ToString('yyyy-MM-dd')
        $key = ([string]$span.From + '|' + [string]$span.To)
        if ($script:cmpKey -eq $key) { $prevRange = Get-RangeAgg $script:cmpRows $prevFrom $prevTo }
        $l2 = ''
        if ($null -ne $prevRange) {
            $l2 = 'диапазон / предыдущий: ' + (Format-CmpPair $curRange.Kwh $prevRange.Kwh '0.00' 'кВт·ч')
            if ($curRange.HasCost -or $prevRange.HasCost) {
                $l2 += ("`r`n" + 'стоимость: ' + (Format-CmpPair $curRange.Cost $prevRange.Cost '0.00' '₽'))
            } else {
                $l2 += ("`r`n" + 'стоимость: —')
            }
        } elseif ($script:cmpFail) {
            $l2 = 'диапазон / предыдущий: ошибка — ' + [string]$script:cmpFail
        } elseif ($null -ne $script:cmpLoad) {
            $done = [Math]::Min([int]$script:cmpLoad.Chunks.Count, [int]$script:cmpLoad.Index + 1)
            $l2 = 'диапазон / предыдущий: загружаю предыдущий период (' + $done + ' из ' + [int]$script:cmpLoad.Chunks.Count + ')…'
        } else {
            $l2 = 'диапазон / предыдущий: загружаю предыдущий период…'
        }
        Set-Lbl $ui.Range $l2
    }

    # --- 3. покрытие: что измерено, а что нет (неполное — не итог) -----------
    $cov = @()
    $cov += ('сегодня ' + [int]$aggToday.Hours + '/' + [int]$aggToday.Expected + ' ч' +
             $(if ($aggToday.Loading) { ' (загрузка)' } elseif ($aggToday.Partial) { ' (день не закончился)' } else { '' }))
    $cov += ('вчера ' + [int]$aggYest.Hours + '/' + [int]$aggYest.Expected + ' ч' +
             $(if ($aggYest.Loading) { ' (загрузка)' } elseif ($aggYest.Partial) { ' (неполные данные)' } else { '' }))
    if ($null -ne $curRange) {
        $cov += ('диапазон ' + [int]$curRange.DaysWithData + '/' + [int]$curRange.Days + ' дн.' +
                 $(if ($curRange.Partial) { ' (неполное покрытие — не итог)' } else { '' }))
    }
    if ($null -ne $prevRange) {
        $cov += ('предыдущий ' + [int]$prevRange.DaysWithData + '/' + [int]$prevRange.Days + ' дн.' +
                 $(if ($prevRange.Partial) { ' (неполное покрытие)' } else { '' }))
    }
    Set-Lbl $ui.Coverage ('покрытие: ' + ($cov -join ' · '))

    # --- 4. пики нагрузки ----------------------------------------------------
    if ($null -ne $span) {
        $pk = Get-HourPeaks ([string]$span.From) ([string]$span.To) 5
        $hdr = 'ПИКИ НАГРУЗКИ'
        if (@($pk.Peaks).Count -eq 0) {
            $hdr += ' · почасовых данных за период ещё нет'
        } elseif ([bool]$pk.Weak) {
            $hdr += ' · максимум среди полученных'
        }
        $hdr += ' · клик по строке открывает час'
        Set-Lbl $ui.Header $hdr
        $sig = ''
        try { $sig = (@($pk.Peaks | ForEach-Object { ([string]$_.Date) + ':' + [string]$_.Hour + ':' + [string]$_.W }) -join '|') } catch { }
        if ([string]$ui.Sig -ne $sig) {
            $ui.Sig = $sig
            $ui.PeaksList = @($pk.Peaks)
            $pg = $ui.PeaksGrid
            if ($null -ne $pg) {
                try {
                    $pg.SuspendLayout()
                    $pg.Rows.Clear()
                    foreach ($p in @($pk.Peaks)) {
                        $ds = [string]$p.Date
                        if ($ds.Length -ge 10) { $ds = $ds.Substring(8, 2) + '.' + $ds.Substring(5, 2) }
                        $null = $pg.Rows.Add($ds, ('{0:00}:00' -f [int]$p.Hour), (Num $p.W '0'))
                    }
                    $pg.ResumeLayout()
                } catch { }
            }
        }
        $baseTxt = 'базовая нагрузка (медиана часовой средней): '
        if ($null -ne $pk.Base) { $baseTxt += (Num $pk.Base '0') + ' Вт' } else { $baseTxt += '—' }
        $baseTxt += ' · часов с данными ' + [int]$pk.Hours + ' · дней с почасовыми данными ' + [int]$pk.DaysWithHours + ' из ' + [int]$pk.Days
        $daysMissing = [int]$pk.Days - [int]$pk.DaysWithHours
        if ($daysMissing -gt 0) {
            if ([int]$span.Days -le 7) { $baseTxt += ' · почасовые данные загружаются…' }
            else { $baseTxt += ' · почасовых данных нет за ' + $daysMissing + ' дн. — открой день во вкладке «История»' }
        }
        Set-Lbl $ui.Base $baseTxt
    }

    # --- 5. асинхронная догрузка предыдущего периода -------------------------
    if (($null -ne $span) -and ([int]$span.Days -gt 1) -and (@($script:rangeRows).Count -gt 0)) {
        $key = ([string]$span.From + '|' + [string]$span.To)
        if (($script:cmpKey -ne $key) -and ($null -eq $script:cmpLoad) -and (-not $script:cmpFail) -and ($null -eq $script:rangeLoad)) {
            $prevFrom = ([datetime]$span.From).AddDays(-[int]$span.Days).ToString('yyyy-MM-dd')
            $prevTo = ([datetime]$span.From).AddDays(-1).ToString('yyyy-MM-dd')
            if ($ui.Ip) { Start-CmpLoad ([string]$ui.Ip) $prevFrom $prevTo }
        }
    }

    # --- 6. бюджет месяца: компактная надпись на кнопке окна периода (P2.3) ---
    try {
        if ($null -ne $ui.BudgetBtn) {
            $bud = Get-MonthBudget $script:rangeRows $script:histCache $script:board ([double]$script:cfg.budgetKwh) ([double]$script:cfg.budgetRub) $now
            Set-Lbl $ui.BudgetBtn (Format-BudgetButton $bud)
        }
    } catch { }
    # --- 7. климат и энергия (P2.5) -----------------------------------------
    # Сводка только из уже полученных rangeRows/histCache и текущего /data.
    # Новых запросов нет; при отсутствии данных — «—», а не 0 и не «загрузка».
    # Сводка пересчитывается только при изменении данных (ключ кэша),
    # иначе каждый тик таймера (1,2 с) лишь перерисовывал бы одно и то же.
    try {
        $cu = $script:rangeCmpUi
        if ($null -ne $cu) {
            $ageSec = -1
            if (($null -ne $script:boardAt) -and ([int]$script:boardAt.Year -gt 2000)) { $ageSec = [int]((Get-Date) - $script:boardAt).TotalSeconds }
            $bd = $script:board
            $roomNow = 'в комнате сейчас: нет данных /data'
            if ($null -ne $bd) {
                $eok = [int](Field $bd @('eok') 0)
                $et = Field $bd @('et'); $eh = Field $bd @('eh'); $ep = Field $bd @('ep')
                if (($eok -ne 1) -or (($null -eq $et) -and ($null -eq $eh))) {
                    $roomNow = 'в комнате сейчас: датчик не найден (/data: eok=0)'
                } else {
                    $roomNow = 'в комнате сейчас:'
                    if ($null -ne $et) { $roomNow += ' t ' + (CmpNum $et '0.0') + ' °C' }
                    if ($null -ne $eh) { $roomNow += ' · влажность ' + (CmpNum $eh '0') + ' %' }
                    if ($null -ne $ep) { $roomNow += ' · ' + [string][Math]::Round([double]$ep * 0.75006) + ' мм' }
                    if ($ageSec -ge 0) { $roomNow += ' (/data ' + $ageSec + ' с назад)' }
                }
            }
            if ($null -eq $span) {
                Set-Lbl $cu.ClimateSum ($roomNow + "`r`n" + 'комната за период: выбери период')
                Set-Lbl $cu.ClimateCorr 'связь t ↔ расход: нужно выбрать период'
                Set-Lbl $cu.ClimateNote 'данные: /range, /history и /climatehist'
                if ($null -ne $cu.CliRetry) { try { $cu.CliRetry.Visible = $false } catch { } }
                if ([string]$cu.ClimateSig -ne '') {
                    $cu.ClimateSig = ''
                    $cu.ClimateCacheKey = ''
                    $cu.ClimateCache = $null
                    try { $cu.ClimateGrid.Rows.Clear() } catch { }
                }
            } else {
                # P2.5b: комнатный климат за период — /climatehist. Запрос один
                # на смену периода и асинхронный (Pump-ClimateLoad в On-Tick);
                # ответ кэшируется по from|to, ошибка — прочерк. Период длиннее
                # 400 дней плата не отдаёт: окно климата — последние 400 дней,
                # об этом честно пишет Format-ClimateWindowNote.
                $climWin = Get-ClimateWindow ([string]$span.From) ([string]$span.To)
                $climKey = [string]$climWin.From + '|' + [string]$climWin.To
                $climEnt = Get-ClimateHistEntry ([string]$climWin.From) ([string]$climWin.To)
                if (($null -eq $climEnt) -and $cu.Ip) {
                    $ld0 = $script:climateLoad
                    if (($null -eq $ld0) -or ([string]$ld0.Key -ne $climKey)) {
                        try { Start-ClimateLoad ([string]$cu.Ip) ([string]$climWin.From) ([string]$climWin.To) } catch { }
                    }
                }
                $climEnt = Get-ClimateHistEntry ([string]$climWin.From) ([string]$climWin.To)
                $climLoading = ($null -ne $script:climateLoad) -and ([string]$script:climateLoad.Key -eq $climKey)
                # «Повторить» — только при закэшированной ошибке периода; при
                # успехе/загрузке кнопку не показываем (никакого автоспама).
                if ($null -ne $cu.CliRetry) {
                    try { $cu.CliRetry.Visible = (($null -ne $climEnt) -and (-not [bool]$climEnt.Ok)) } catch { }
                }
                $climDays = $null
                if (($null -ne $climEnt) -and [bool]$climEnt.Ok) { $climDays = @($climEnt.Days) }
                $climTick = 0L
                if ($null -ne $climEnt) { try { $climTick = [int64]$climEnt.At.Ticks } catch { $climTick = 0L } }
                $climMark = if ($climLoading) { 'L' } else { '' }
                $ck = (Get-ClimateCacheKey $script:rangeRows $script:histCache ([string]$climWin.From) ([string]$climWin.To)) + '|' + $climKey + '|' + [string]$climTick + '|' + $climMark + '|' + [string]$climWin.Truncated
                if (([string]$cu.ClimateCacheKey -ne $ck) -or ($null -eq $cu.ClimateCache)) {
                    $cu.ClimateCache = Get-ClimateAgg $script:rangeRows $script:histCache ([string]$climWin.From) ([string]$climWin.To) $climDays
                    $cu.ClimateCacheKey = $ck
                }
                $cl = $cu.ClimateCache
                $lines = New-Object 'System.Collections.Generic.List[string]'
                $lines.Add($roomNow)
                $climWinNote = Format-ClimateWindowNote $climWin $climEnt $climLoading
                if ($climWinNote) { $lines.Add($climWinNote) }
                $lines.Add((Format-ClimateRoomPeriod $climEnt ([int]$climWin.Days) $climLoading))
                if ($cl.RelayFromHistory) {
                    $lines.Add('внутри реле за период: средняя ' + (CmpNum $cl.RelayAvg '0.0') + ' · мин ' + (CmpNum $cl.RelayMin '0.0') + ' · макс ' + (CmpNum $cl.RelayMax '0.0') + ' °C · часов с t ' + [int]$cl.RelayHours + ' · дней ' + [int]$cl.TrelayDays + ' из ' + [int]$cl.Days)
                } elseif ([int]$cl.TrelayDays -gt 0) {
                    $lines.Add('внутри реле за период: мин ' + (CmpNum $cl.RelayMin '0.0') + ' · макс ' + (CmpNum $cl.RelayMax '0.0') + ' °C (середина дневных мин–макс /range; почасовых данных нет) · дней ' + [int]$cl.TrelayDays + ' из ' + [int]$cl.Days)
                } else {
                    $lines.Add('внутри реле за период: — (в /history и /range нет t)')
                }
                if ([int]$cl.OutDays -gt 0) {
                    $lines.Add('улица за период (прогноз платы, середина мин–макс): средняя ' + (CmpNum $cl.OutAvg '0.0') + ' °C · от ' + (CmpNum $cl.OutMin '0.0') + ' до ' + (CmpNum $cl.OutMax '0.0') + ' ° · дней ' + [int]$cl.OutDays + ' из ' + [int]$cl.Days)
                } else {
                    $lines.Add('улица за период: — (плата не отдавала прогноз /weather за эти дни)')
                }
                if ($null -ne $cl.KwhPerDay) {
                    $lines.Add('расход за период: ' + (CmpNum $cl.KwhPerDay '0.00') + ' кВт·ч/сутки · дней с расходом ' + [int]$cl.MeterDays + ' из ' + [int]$cl.Days)
                } else {
                    $lines.Add('расход за период: — (нет измеренных дней)')
                }
                if ($null -ne $cl.CostPerDay) {
                    $lines.Add('стоимость за период: ' + (CmpNum $cl.CostPerDay '0.00') + ' ₽/сутки · дней с ₽ ' + [int]$cl.CostDays + ' из ' + [int]$cl.Days + ' (по /range)')
                } else {
                    $lines.Add('стоимость за период: — (в /range нет ₽)')
                }
                Set-Lbl $cu.ClimateSum ($lines -join "`r`n")
                $corr = New-Object 'System.Collections.Generic.List[string]'
                $corr.Add((Format-ClimateCorr 't реле дня' $cl.CorrRelay))
                $corr.Add((Format-ClimateCorr 't улицы дня' $cl.CorrOut))
                if (($null -ne $climEnt) -and [bool]$climEnt.Ok -and ([int]$climEnt.Count -gt 0)) {
                    $corr.Add((Format-ClimateCorr 't комнаты дня' $cl.CorrRoom))
                } elseif (($null -ne $climEnt) -and [bool]$climEnt.Ok) {
                    $corr.Add('связь t комнаты ↔ расход: — (/climatehist: нет дней с валидной t)')
                } elseif ($null -ne $climEnt) {
                    $corr.Add('связь t комнаты ↔ расход: — (/climatehist: ' + [string]$climEnt.Err + ')')
                } else {
                    $corr.Add('связь t комнаты ↔ расход: — (нет ответа /climatehist)')
                }
                Set-Lbl $cu.ClimateCorr ($corr -join "`r`n")
                Set-Lbl $cu.ClimateNote 'пустое — «—», не 0 · «ч» — часы с данными (сегодня n/24) · климат — /climatehist, сейчас — /data'
                $sig = [string]$cl.TableSig
                if ([string]$cu.ClimateSig -ne $sig) {
                    $cu.ClimateSig = $sig
                    $cg = $cu.ClimateGrid
                    if ($null -ne $cg) {
                        try {
                            $todayStr = (Get-Date).ToString('yyyy-MM-dd')
                            $cg.SuspendLayout()
                            $cg.Rows.Clear()
                            foreach ($c in @($cl.Cells)) {
                                $ds = [string]$c.Date
                                if ($ds.Length -ge 10) { $ds = $ds.Substring(8, 2) + '.' + $ds.Substring(5, 2) }
                                $null = $cg.Rows.Add($ds, (CmpNum $c.Troom '0.0'), (CmpNum $c.Trelay '0.0'), (CmpNum $c.Tout '0.0'), (CmpNum $c.Kwh '0.00'), (CmpNum $c.Cost '0.00'), (Format-ClimateHours $c.Date $c.ClimN $todayStr))
                            }
                            $cg.ResumeLayout()
                        } catch { }
                    }
                }
            }
        }
    } catch { Write-AppLog ('климат/энергия: ' + $_.Exception.Message) }
}

# ============================================================================
#  P2.3 «Бюджет и прогноз месяца» и P2.4 «Экспорт CSV периода»
#  Данные — только из уже полученных rangeRows/histCache и /data.
#  Новых блокирующих запросов нет, окно не замораживается.
# ============================================================================

function Format-CsvNum($v, [string]$fmt = '0.###') {
    # Число для CSV: пропуск (null/пусто/NaN) — пустое поле, а НЕ 0.
    if ($null -eq $v) { return '' }
    $s = [string]$v
    if ($s -eq '') { return '' }
    $d = [double]0
    if (-not [double]::TryParse($s, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { return '' }
    if ([double]::IsNaN($d) -or [double]::IsInfinity($d)) { return '' }
    return $d.ToString($fmt, [System.Globalization.CultureInfo]::InvariantCulture)
}

function Format-CsvField($v) {
    # Точка с запятой / кавычка / перевод строки — поле в кавычках, кавычки удвоены.
    $s = if ($null -eq $v) { '' } else { [string]$v }
    if ($s -match '[;"\r\n]') { return '"' + ($s -replace '"', '""') + '"' }
    return $s
}

function Get-RangeCsvFileName([string]$from, [string]$to) {
    # Имя файла содержит выбранный диапазон: energy-2026-11-01_2026-11-16.csv
    return ('energy-' + $from + '_' + $to + '.csv')
}

function ConvertTo-RangeCsv($rows, $histCache, [string]$from, [string]$to, [datetime]$now) {
    # Одна выгрузка за выбранный диапазон:
    #   раздел «дни»  — все дни диапазона по уже полученному /range
    #                   (нет строки — пустые поля, а не выдуманный 0);
    #   раздел «часы» — только уже загруженные дни histCache (новых запросов нет),
    #                   поэтому там честно указано покрытие.
    # Исходные данные не меняются. Пустой диапазон -> пустая строка (файл не пишем).
    if ($null -ne $from -and $null -ne $to) {
        if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    }
    $map = @{}
    $haveRow = $false
    if ($null -ne $rows) {
        foreach ($r in @($rows)) {
            if ($null -eq $r) { continue }
            $d = [string](Field $r @('d') '')
            if (-not $d) { continue }
            if (($d -lt $from) -or ($d -gt $to)) { continue }
            if (-not $map.ContainsKey($d)) { $map[$d] = $r }
            $haveRow = $true
        }
    }
    $cacheDays = @()
    if ($null -ne $histCache) {
        $cacheDays = @($histCache.Keys | Where-Object {
                ($null -ne $_) -and ([string]$_ -ge $from) -and ([string]$_ -le $to) -and (@($histCache[$_]).Count -gt 0)
            } | Sort-Object)
    }
    if ((-not $haveRow) -and (@($cacheDays).Count -eq 0)) { return '' }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('# DomMonitor / Дом — энергия · экспорт периода ' + $from + '…' + $to + "`r`n")
    [void]$sb.Append('# выгружено ' + $now.ToString('dd.MM.yyyy HH:mm') + ' · время устройства (платы), не браузера' + "`r`n")
    [void]$sb.Append('# пропуски — пустые поля; нули не подставляются; исходные файлы на карте не меняются' + "`r`n")
    [void]$sb.Append('дата;уровень;кВт·ч;₽;день_кВт·ч;ночь_кВт·ч;день_₽;ночь_₽;напряжение_мин_В;напряжение_макс_В;температура_мин_°C;температура_макс_°C;пик_Вт;записей' + "`r`n")

    # --- раздел «дни»: каждый день диапазона, даже если /range его не прислал ---
    try {
        $dd = [datetime]$from
        $guard = 0
        while (($dd -le [datetime]$to) -and ($guard -lt 1000)) {
            $guard++
            $key = $dd.ToString('yyyy-MM-dd')
            $r = if ($map.ContainsKey($key)) { $map[$key] } else { $null }
            $cells = New-Object 'System.Collections.Generic.List[string]'
            $cells.Add((Format-CsvField $key))
            $cells.Add('день')
            if ($null -ne $r) {
                $cells.Add((Format-CsvNum (Field $r @('kwh')) '0.###'))
                $cells.Add((Format-CsvNum (Field $r @('cost')) '0.00'))
                $cells.Add((Format-CsvNum (Field $r @('dayKwh')) '0.###'))
                $cells.Add((Format-CsvNum (Field $r @('nightKwh')) '0.###'))
                $cells.Add((Format-CsvNum (Field $r @('dayCost')) '0.00'))
                $cells.Add((Format-CsvNum (Field $r @('nightCost')) '0.00'))
                $vvalid = Field $r @('vvalid')
                $vmin = Field $r @('vmin'); $vmax = Field $r @('vmax')
                if (($vvalid -ne $false) -and ($null -ne $vmin) -and ($null -ne $vmax)) {
                    $cells.Add((Format-CsvNum $vmin '0')); $cells.Add((Format-CsvNum $vmax '0'))
                } else { $cells.Add(''); $cells.Add('') }
                $tvalid = Field $r @('tvalid')
                $tmin = Field $r @('tmin'); $tmax = Field $r @('tmax')
                if (($tvalid -ne $false) -and ($null -ne $tmin) -and ($null -ne $tmax)) {
                    $cells.Add((Format-CsvNum $tmin '0.0')); $cells.Add((Format-CsvNum $tmax '0.0'))
                } else { $cells.Add(''); $cells.Add('') }
                $cells.Add((Format-CsvNum (Field $r @('pmax')) '0'))
                $cells.Add((Format-CsvNum (Field $r @('rows')) '0'))
            } else {
                for ($i = 0; $i -lt 12; $i++) { $cells.Add('') }
            }
            [void]$sb.Append(($cells -join ';') + "`r`n")
            $dd = $dd.AddDays(1)
        }
    } catch { }

    # --- раздел «часы»: только уже загруженные дни истории ---
    if (@($cacheDays).Count -gt 0) {
        $totalDays = 1
        try { $totalDays = [int]([Math]::Abs(([datetime]$to - [datetime]$from).TotalDays) + 1) } catch { $totalDays = 1 }
        [void]$sb.Append("`r`n")
        [void]$sb.Append('# по часам — только уже загруженные дни истории (' + @($cacheDays).Count + ' из ' + $totalDays + '); новых запросов к плате нет' + "`r`n")
        [void]$sb.Append('дата;время;кВт·ч;средняя_Вт;пик_Вт;напряжение_В;температура_°C;записей;источник' + "`r`n")
        foreach ($date in @($cacheDays)) {
            $hmap = @{}
            foreach ($hr in @($histCache[$date])) {
                if ($null -ne $hr -and $null -ne $hr.h) { $hmap[[int]$hr.h] = $hr }
            }
            for ($hh = 0; $hh -le 23; $hh++) {
                $cells = New-Object 'System.Collections.Generic.List[string]'
                $cells.Add((Format-CsvField ([string]$date)))
                $cells.Add((Format-CsvField ('{0:00}:00' -f $hh)))
                if ($hmap.ContainsKey($hh)) {
                    $hr = $hmap[$hh]
                    $cells.Add((Format-CsvNum (Field $hr @('kwh')) '0.###'))
                    $cells.Add((Format-CsvNum (Field $hr @('p')) '0'))
                    $cells.Add((Format-CsvNum (Field $hr @('pmax')) '0'))
                    $cells.Add((Format-CsvNum (Field $hr @('v')) '0'))
                    $cells.Add((Format-CsvNum (Field $hr @('t')) '0.0'))
                    $cells.Add((Format-CsvNum (Field $hr @('n')) '0'))
                } else {
                    for ($i = 0; $i -lt 6; $i++) { $cells.Add('') }
                }
                $cells.Add('history')
                [void]$sb.Append(($cells -join ';') + "`r`n")
            }
        }
    }
    return $sb.ToString()
}

function Write-RangeCsvFile([string]$path, [string]$text) {
    # BOM обязателен, чтобы Excel не ломал кириллицу. Только запись выбранного
    # файла: файлы на SD-карте платы и кэш приложения не меняются.
    $enc = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($path, $text, $enc)
}

function Get-MonthBudget($rows, $cache, $board, [double]$limitKwh, [double]$limitRub, [datetime]$now) {
    # P2.3: факт текущего месяца и ОЦЕНКА конца месяца по фактическим дням.
    # Отсутствующий день — пропуск, а не 0. 0 кВт·ч с измерениями — валидный ноль.
    # limitKwh/limitRub <= 0 — лимит не задан (0 — тоже «не задан», не «ноль рублей»).
    if ($now -eq [datetime]::MinValue) { $now = Get-Date }
    $mStart = Get-Date -Year $now.Year -Month $now.Month -Day 1 -Hour 0 -Minute 0 -Second 0
    $mEnd = $mStart.AddMonths(1).AddDays(-1)
    $daysInMonth = [int]$mEnd.Day
    $daysElapsed = [int]$now.Day
    $todayKey = $now.ToString('yyyy-MM-dd')
    $yestKey = $now.AddDays(-1).ToString('yyyy-MM-dd')
    $mStartKey = $mStart.ToString('yyyy-MM-dd')

    $days = @{}
    # --- 1. строки /range: полные дни, если текущий месяц попал в диапазон ---
    if ($null -ne $rows) {
        foreach ($r in @($rows)) {
            if ($null -eq $r) { continue }
            $d = [string](Field $r @('d') '')
            if ((-not $d) -or ($d -lt $mStartKey) -or ($d -gt $todayKey)) { continue }
            $rnRaw = Field $r @('rows')
            if (($null -ne $rnRaw) -and ([int]$rnRaw -le 0)) { continue }   # нет измерений — не день с данными
            $k = Field $r @('kwh')
            $c = Field $r @('cost')
            if ($null -eq $k) { continue }
            $rec = [pscustomobject]@{ Date = $d; Kwh = [double]$k; Rub = $null; RubApprox = $false; Source = 'range'; Partial = ($d -eq $todayKey) }
            if ($null -ne $c) { $rec.Rub = [double]$c }
            $days[$d] = $rec
        }
    }
    # --- 2. histCache: дни, которых нет в /range; стоимость из /data или по тарифу ---
    $tcost = $null; $ykost = $null; $tariff = $null
    if ($null -ne $board) {
        $tcost = Field $board @('tcost', 'today_cost')
        $ykost = Field $board @('ykost', 'yesterday_cost')
        $tariff = Field $board @('tariff')
    }
    if ($null -ne $cache) {
        foreach ($date in @($cache.Keys)) {
            $d = [string]$date
            if ((-not $d) -or ($d -lt $mStartKey) -or ($d -gt $todayKey)) { continue }
            $hours = 0; $sumK = 0.0; $kN = 0
            foreach ($hr in @($cache[$date])) {
                if ($null -eq $hr) { continue }
                if ([int](Field $hr @('n') 0) -gt 0) { $hours++ }
                $hk = Field $hr @('kwh')
                if ($null -ne $hk) { $sumK += [double]$hk; $kN++ }
            }
            if ($hours -le 0) { continue }
            $rec = $null
            if ($days.ContainsKey($d)) { $rec = $days[$d] } else {
                $rec = [pscustomobject]@{ Date = $d; Kwh = $null; Rub = $null; RubApprox = $false; Source = 'history'; Partial = ($d -eq $todayKey) }
            }
            if (($null -eq $rec.Kwh) -and ($kN -gt 0)) { $rec.Kwh = $sumK }
            if ($null -eq $rec.Rub) {
                if (($d -eq $todayKey) -and ($null -ne $tcost)) { $rec.Rub = [double]$tcost }
                elseif (($d -eq $yestKey) -and ($null -ne $ykost)) { $rec.Rub = [double]$ykost }
                elseif (($null -ne $rec.Kwh) -and ($null -ne $tariff)) { $rec.Rub = [double]$rec.Kwh * [double]$tariff; $rec.RubApprox = $true }
            }
            $days[$d] = $rec
        }
    }

    $res = [pscustomobject]@{
        Month = $mStart.ToString('yyyy-MM'); MonthStart = $mStartKey; Today = $todayKey
        DaysInMonth = $daysInMonth; DaysElapsed = $daysElapsed
        DaysWithData = 0; DaysCompletedWithData = 0
        FactKwh = $null; AvgKwh = $null; ForecastKwh = $null
        FactRub = $null; AvgRub = $null; ForecastRub = $null; RubDays = 0; RubApprox = $false
        Tariff = $null
        LimitKwh = $(if ($limitKwh -gt 0) { $limitKwh } else { [double]0 })
        LimitRub = $(if ($limitRub -gt 0) { $limitRub } else { [double]0 })
        PercentKwh = $null; PercentRub = $null
        TodayPartial = $false
    }
    if ($null -ne $tariff) { $res.Tariff = [double]$tariff }

    $factK = 0.0; $nFactK = 0; $doneK = 0.0; $nDoneK = 0
    $factC = 0.0; $nFactC = 0; $doneC = 0.0; $nDoneC = 0; $approx = $false
    foreach ($d in @($days.Keys | Sort-Object)) {
        $rec = $days[$d]
        $isToday = ($d -eq $todayKey)
        if ($isToday -and ($null -ne $rec.Kwh)) { $res.TodayPartial = $true }
        if ($null -ne $rec.Kwh) {
            $factK += [double]$rec.Kwh; $nFactK++
            if (-not $isToday) { $doneK += [double]$rec.Kwh; $nDoneK++ }
        }
        if ($null -ne $rec.Rub) {
            $factC += [double]$rec.Rub; $nFactC++
            if ([bool]$rec.RubApprox) { $approx = $true }
            if (-not $isToday) { $doneC += [double]$rec.Rub; $nDoneC++ }
        }
    }
    $res.DaysWithData = $nFactK
    $res.DaysCompletedWithData = $nDoneK
    if ($nFactK -gt 0) { $res.FactKwh = $factK }
    if ($nFactC -gt 0) { $res.FactRub = $factC; $res.RubDays = $nFactC; $res.RubApprox = $approx }
    if ($nDoneK -gt 0) {
        $res.AvgKwh = $doneK / $nDoneK
        $res.ForecastKwh = $res.AvgKwh * $daysInMonth
    }
    if ($nDoneC -gt 0) {
        $res.AvgRub = $doneC / $nDoneC
        $res.ForecastRub = $res.AvgRub * $daysInMonth
    }
    if (([double]$res.LimitKwh -gt 0) -and ($null -ne $res.FactKwh)) { $res.PercentKwh = 100.0 * [double]$res.FactKwh / [double]$res.LimitKwh }
    if (([double]$res.LimitRub -gt 0) -and ($null -ne $res.FactRub)) { $res.PercentRub = 100.0 * [double]$res.FactRub / [double]$res.LimitRub }
    return $res
}

function Format-BudgetSummary($bud) {
    # Тексты окна «Бюджет и прогноз». Числа — только из факта; прогноз помечен
    # «оценка»; при отсутствии лимита — приглашение задать, без выдуманных чисел.
    if ($null -eq $bud) { return @('бюджет: нет данных') }
    $out = New-Object 'System.Collections.Generic.List[string]'
    $scope = $bud.MonthStart + '…' + $bud.Today
    if ($null -ne $bud.FactKwh) {
        $t = 'факт месяца (' + $scope + '): ' + (CmpNum $bud.FactKwh '0.00') + ' кВт·ч'
        if ($null -ne $bud.FactRub) {
            $t += ' · ' + (CmpNum $bud.FactRub '0.00') + ' ₽'
            if ([bool]$bud.RubApprox) { $t += ' (≈ по тарифу)' }
        }
        $out.Add($t)
    } else {
        $out.Add('факт месяца: дней с измерениями пока нет — открой в окне периода «Месяц» (загрузка в фоне)')
    }
    if ([double]$bud.LimitKwh -gt 0) {
        $lim = CmpNum $bud.LimitKwh '0.##'
        if ($null -ne $bud.FactKwh) {
            $rest = [double]$bud.LimitKwh - [double]$bud.FactKwh
            $t = 'лимит ' + $lim + ' кВт·ч: израсходовано ' + (CmpNum $bud.FactKwh '0.00') + ' (' + (CmpNum $bud.PercentKwh '0') + ' %)'
            if ($rest -ge 0) { $t += ' · осталось ' + (CmpNum $rest '0.00') + ' кВт·ч' }
            else { $t += ' · превышен на ' + (CmpNum ([Math]::Abs($rest)) '0.00') + ' кВт·ч' }
            $out.Add($t)
        } else {
            $out.Add('лимит ' + $lim + ' кВт·ч задан — фактических дней пока нет')
        }
    } else {
        $out.Add('лимит кВт·ч не задан — задай добровольный лимит, чтобы видеть прогресс месяца')
    }
    if ([double]$bud.LimitRub -gt 0) {
        $lim = CmpNum $bud.LimitRub '0.00'
        if ($null -ne $bud.FactRub) {
            $rest = [double]$bud.LimitRub - [double]$bud.FactRub
            $t = 'лимит ' + $lim + ' ₽: израсходовано ' + (CmpNum $bud.FactRub '0.00') + ' (' + (CmpNum $bud.PercentRub '0') + ' %)'
            if ($rest -ge 0) { $t += ' · осталось ' + (CmpNum $rest '0.00') + ' ₽' }
            else { $t += ' · превышен на ' + (CmpNum ([Math]::Abs($rest)) '0.00') + ' ₽' }
            $out.Add($t)
        } else {
            $out.Add('лимит ' + $lim + ' ₽ задан — факта пока нет')
        }
    } else {
        $out.Add('лимит в рублях не задан — при желании задай сумму и следи за прогрессом')
    }
    if ($null -ne $bud.ForecastKwh) {
        $t = 'оценка конца месяца: ' + (CmpNum $bud.ForecastKwh '0.00') + ' кВт·ч'
        if ($null -ne $bud.ForecastRub) {
            $t += ' · ≈ ' + (CmpNum $bud.ForecastRub '0.00') + ' ₽'
            if ([bool]$bud.RubApprox) { $t += ' (по тарифу)' }
        }
        $t += ' · это ОЦЕНКА по фактическим дням, а не счёт поставщика'
        $out.Add($t)
    } else {
        $out.Add('оценка конца месяца: пока невозможна — нужен хотя бы один полный день с измерениями (сегодняшний неполный)')
    }
    if ($null -ne $bud.AvgKwh) {
        $m = 'метод: среднее по дням с данными — ' + [string]$bud.DaysCompletedWithData + ' полных дн. × ' + [string]$bud.DaysInMonth + ' дней месяца'
        if ($null -ne $bud.Tariff) { $m += ' · тариф ' + (CmpNum $bud.Tariff '0.00') + ' ₽/кВт·ч (текущий)' }
        $out.Add($m)
    } elseif ([int]$bud.DaysWithData -gt 0) {
        $out.Add('метод: среднее по дням с данными — полных дней ещё нет')
    } else {
        $out.Add('метод: среднее по дням с данными — фактических дней пока нет')
    }
    $cov = 'покрытие: ' + [string]$bud.DaysWithData + ' из ' + [string]$bud.DaysElapsed + ' истёкших дней месяца'
    if ([bool]$bud.TodayPartial) { $cov += ' · сегодняшний день неполный' }
    $out.Add($cov)
    $out.Add('лимит добровольный: приложение только считает и предупреждает, нагрузку не отключает')
    return $out.ToArray()
}

function Format-BudgetButton($bud) {
    # Компактная надпись на кнопке окна периода: без лимита — приглашение.
    if ($null -eq $bud) { return 'Бюджет и прогноз…' }
    if (([double]$bud.LimitKwh -le 0) -and ([double]$bud.LimitRub -le 0)) { return 'Бюджет и прогноз…' }
    if (([double]$bud.LimitKwh -gt 0) -and ($null -ne $bud.FactKwh)) {
        return ('Бюджет: ' + (CmpNum $bud.FactKwh '0.#') + ' / ' + (CmpNum $bud.LimitKwh '0.#') + ' кВт·ч · ' + (CmpNum $bud.PercentKwh '0') + ' %')
    }
    if (([double]$bud.LimitRub -gt 0) -and ($null -ne $bud.FactRub)) {
        return ('Бюджет: ' + (CmpNum $bud.FactRub '0') + ' / ' + (CmpNum $bud.LimitRub '0') + ' ₽ · ' + (CmpNum $bud.PercentRub '0') + ' %')
    }
    return 'Бюджет и прогноз…'
}

function Show-Budget {
    # P2.3: компактное окно «Бюджет и прогноз месяца». Своих сетевых запросов
    # нет: считает по уже полученным rangeRows/histCache и /data; кнопка
    # «Показать месяц» ставит текущий месяц в окно периода (загрузка в фоне).
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Бюджет и прогноз — ' + $ip
    $f.Size = New-Object System.Drawing.Size(700, 580)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text
    $font = New-Object System.Drawing.Font('Segoe UI', 9)

    $lblHdr = New-Lbl $f 'БЮДЖЕТ И ПРОГНОЗ МЕСЯЦА' 10 $script:CLR.accent $true 14 12
    $lblHdr.AutoSize = $false
    $lblHdr.Size = New-Object System.Drawing.Size(660, 20)

    $lblNote = New-Lbl $f ('Лимит добровольный: приложение только считает и предупреждает, нагрузку не отключает.' + "`r`n" + 'Прогноз — оценка по фактическим дням, а не счёт поставщика.') 8.5 $script:CLR.dim2 $false 14 34
    $lblNote.AutoSize = $false
    $lblNote.Size = New-Object System.Drawing.Size(660, 34)

    $lblK = New-Lbl $f 'Лимит кВт·ч:' 9 $script:CLR.dim $false 14 80
    $inpK = New-Object System.Windows.Forms.TextBox
    $inpK.Location = New-Object System.Drawing.Point(112, 76)
    $inpK.Size = New-Object System.Drawing.Size(86, 24)
    $inpK.BackColor = C (SkinC '#12161C')
    $inpK.ForeColor = C $script:CLR.text
    $inpK.Font = $font
    $f.Controls.Add($inpK)

    $lblRub = New-Lbl $f 'Лимит ₽:' 9 $script:CLR.dim $false 214 80
    $inpRub = New-Object System.Windows.Forms.TextBox
    $inpRub.Location = New-Object System.Drawing.Point(282, 76)
    $inpRub.Size = New-Object System.Drawing.Size(86, 24)
    $inpRub.BackColor = C (SkinC '#12161C')
    $inpRub.ForeColor = C $script:CLR.text
    $inpRub.Font = $font
    $f.Controls.Add($inpRub)

    $btnSave = New-Btn $f 'Сохранить лимит' (SkinC '#1F7A46') '#FFFFFF' 160 28
    $btnSave.Location = New-Object System.Drawing.Point(384, 74)
    $btnSave.Font = New-Object System.Drawing.Font('Segoe UI', 9)

    $lblHint = New-Lbl $f 'пусто или 0 — лимит не задан (добровольно)' 8 $script:CLR.dim2 $false 14 106

    $lblBody = New-Lbl $f 'считаю…' 9 $script:CLR.text $false 14 136
    $lblBody.AutoSize = $false
    $lblBody.Size = New-Object System.Drawing.Size(660, 344)

    $btnMonth = New-Btn $f 'Показать текущий месяц в окне периода' (SkinC '#24405A') $script:CLR.text 330 30
    $btnMonth.Location = New-Object System.Drawing.Point(14, 496)
    $btnClose = New-Btn $f 'Закрыть' (SkinC '#2A3240') $script:CLR.text 130 30
    $btnClose.Location = New-Object System.Drawing.Point(544, 496)

    $refresh = {
        try {
            $bud = Get-MonthBudget $script:rangeRows $script:histCache $script:board ([double]$script:cfg.budgetKwh) ([double]$script:cfg.budgetRub) (Get-Date)
            $lblBody.Text = ((Format-BudgetSummary $bud) -join "`r`n")
        } catch { $lblBody.Text = 'не удалось посчитать: ' + $_.Exception.Message }
    }.GetNewClosure()

    $btnSave.Add_Click({
        $ci = [System.Globalization.CultureInfo]::InvariantCulture
        $kt = $inpK.Text.Trim().Replace(',', '.')
        $rt = $inpRub.Text.Trim().Replace(',', '.')
        $kv = [double]0; $rv = [double]0
        $ok = $true
        if ($kt -ne '') { if (-not [double]::TryParse($kt, [System.Globalization.NumberStyles]::Float, $ci, [ref]$kv)) { $ok = $false } }
        if ($rt -ne '') { if (-not [double]::TryParse($rt, [System.Globalization.NumberStyles]::Float, $ci, [ref]$rv)) { $ok = $false } }
        if ((-not $ok) -or ($kv -lt 0) -or ($rv -lt 0)) {
            $lblHint.Text = 'лимит должен быть числом ≥ 0 (пусто — не задан)'
            return
        }
        $script:cfg.budgetKwh = $kv
        $script:cfg.budgetRub = $rv
        Save-Cfg
        $msg = 'бюджет сохранён в settings.json: '
        $msg += $(if ($kv -gt 0) { 'кВт·ч ' + (CmpNum $kv '0.##') } else { 'кВт·ч не задан' })
        $msg += ' · '
        $msg += $(if ($rv -gt 0) { '₽ ' + (CmpNum $rv '0.00') } else { '₽ не задан' })
        Add-Log $msg 'ok'
        $lblHint.Text = 'сохранено в settings.json'
        & $refresh
    })

    $btnMonth.Add_Click({
        try {
            $ctx = $script:rangeCmpUi
            if (($null -eq $ctx) -or ($null -eq $ctx.Grid) -or ($null -eq $ctx.D1) -or ($null -eq $ctx.D2)) {
                $lblHint.Text = 'окно периода закрыто — открой «Статистика за период»'
                return
            }
            $now = Get-Date
            $first = Get-Date -Year $now.Year -Month $now.Month -Day 1 -Hour 0 -Minute 0 -Second 0
            $ctx.D1.Value = $first
            $ctx.D2.Value = $now
            $null = Update-RangeGrid $ctx.Grid $ctx.Sum ([string]$script:cfg.boardIp) $first.ToString('yyyy-MM-dd') $now.ToString('yyyy-MM-dd')
            $lblHint.Text = 'текущий месяц поставлен в окно периода — загрузка идёт в фоне'
            Add-Log ('бюджет: в окно периода поставлен текущий месяц ' + $first.ToString('yyyy-MM-dd') + '…' + $now.ToString('yyyy-MM-dd'))
            & $refresh
        } catch { $lblHint.Text = 'не удалось поставить месяц: ' + $_.Exception.Message }
    })

    $btnClose.Add_Click({ try { $f.Close() } catch { } })

    $t = New-Object System.Windows.Forms.Timer
    $t.Interval = 1500
    $t.Add_Tick($refresh)
    $f.Add_Shown({
        try {
            if ([double]$script:cfg.budgetKwh -gt 0) { $inpK.Text = (CmpNum $script:cfg.budgetKwh '0.##') }
            if ([double]$script:cfg.budgetRub -gt 0) { $inpRub.Text = (CmpNum $script:cfg.budgetRub '0.00') }
        } catch { }
        & $refresh
        $t.Start()
    })
    $f.Add_FormClosed({ try { $t.Stop(); $t.Dispose() } catch { } })
    [void]$f.ShowDialog()
    $f.Dispose()
}

# ============================================================================

function Show-Range {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Статистика за период — ' + $ip
    # P2.5: справа добавлена колонка «КЛИМАТ И ЭНЕРГИЯ» (x = 880, ширина 376),
    # поэтому окно чуть шире. Координаты зафиксированных секций слева не меняются;
    # на маленьком экране окно ужимается и появляется прокрутка вместо обрезанных краёв.
    $clWinW = 1284
    $clWinH = 880
    try {
        $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
        if ([int]$wa.Width -lt ($clWinW + 16)) { $clWinW = [Math]::Max(900, [int]$wa.Width - 16); $f.AutoScroll = $true }
        if ([int]$wa.Height -lt ($clWinH + 16)) { $clWinH = [Math]::Max(620, [int]$wa.Height - 16); $f.AutoScroll = $true }
    } catch { }
    $f.Size = New-Object System.Drawing.Size($clWinW, $clWinH)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text

    $font = New-Object System.Drawing.Font('Segoe UI', 9)
    $presets = @(
        @('Сегодня', 0), @('Вчера', 1), @('7 дней', 2), @('30 дней', 3),
        @('Месяц', 4), @('Прошлый', 5), @('Год', 6), @('С начала года', 7)
    )

    $x = 12
    foreach ($p in $presets) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $p[0]
        $b.Tag = $p[1]
        $b.Size = New-Object System.Drawing.Size(84, 28)
        $b.Location = New-Object System.Drawing.Point($x, 10)
        $b.FlatStyle = 'Flat'
        $b.BackColor = C (SkinC '#24405A')
        $b.ForeColor = C $script:CLR.text
        $b.Font = $font
        $f.Controls.Add($b)
        $x += 88
    }

    # P2.3: компактный вход в «Бюджет и прогноз месяца» — в свободном месте
    # строки пресетов, зафиксированную раскладку окна не двигаем.
    $btnBudget = New-Object System.Windows.Forms.Button
    $btnBudget.Text = 'Бюджет и прогноз…'
    $btnBudget.Location = New-Object System.Drawing.Point(720, 10)
    $btnBudget.Size = New-Object System.Drawing.Size(156, 28)
    $btnBudget.FlatStyle = 'Flat'
    $btnBudget.BackColor = C (SkinC '#1B2E44')
    $btnBudget.ForeColor = C $script:CLR.text
    $btnBudget.Font = New-Object System.Drawing.Font('Segoe UI', 8)
    $btnBudget.AutoEllipsis = $true
    $f.Controls.Add($btnBudget)

    $d1 = New-Object System.Windows.Forms.DateTimePicker
    $d1.Format = 'Short'
    $d1.Location = New-Object System.Drawing.Point(12, 46)
    $d1.Size = New-Object System.Drawing.Size(130, 24)
    $f.Controls.Add($d1)

    $d2 = New-Object System.Windows.Forms.DateTimePicker
    $d2.Format = 'Short'
    $d2.Location = New-Object System.Drawing.Point(150, 46)
    $d2.Size = New-Object System.Drawing.Size(130, 24)
    $f.Controls.Add($d2)

    $btnGo = New-Object System.Windows.Forms.Button
    $btnGo.Text = 'Применить'
    $btnGo.Location = New-Object System.Drawing.Point(292, 44)
    $btnGo.Size = New-Object System.Drawing.Size(110, 28)
    $btnGo.FlatStyle = 'Flat'
    $btnGo.BackColor = C (SkinC '#24405A')
    $btnGo.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnGo)

    $btnFill = New-Object System.Windows.Forms.Button
    $btnFill.Text = 'Заполнить пропуски'
    $btnFill.Location = New-Object System.Drawing.Point(552, 44)
    $btnFill.Size = New-Object System.Drawing.Size(150, 28)
    $btnFill.FlatStyle = 'Flat'
    $btnFill.BackColor = C (SkinC '#24405A')
    $btnFill.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnFill)

    # «Заполнить все пропуски»: плата сама ищет пропуски в облаке Tuya,
    # прогресс — в строке статуса ниже, по завершении таблица перечитывается.
    $btnFillAll = New-Object System.Windows.Forms.Button
    $btnFillAll.Text = 'Заполнить все пропуски'
    $btnFillAll.Location = New-Object System.Drawing.Point(704, 44)
    $btnFillAll.Size = New-Object System.Drawing.Size(176, 28)
    $btnFillAll.FlatStyle = 'Flat'
    $btnFillAll.BackColor = C (SkinC '#1F7A46')
    $btnFillAll.ForeColor = C '#FFFFFF'
    $btnFillAll.Font = $font
    $f.Controls.Add($btnFillAll)

    $btnCsv = New-Object System.Windows.Forms.Button
    $btnCsv.Text = 'CSV за период'
    $btnCsv.Location = New-Object System.Drawing.Point(412, 44)
    $btnCsv.Size = New-Object System.Drawing.Size(130, 28)
    $btnCsv.FlatStyle = 'Flat'
    $btnCsv.BackColor = C (SkinC '#2A3240')
    $btnCsv.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnCsv)

    $sum = New-Object System.Windows.Forms.Label
    $sum.Location = New-Object System.Drawing.Point(12, 80)
    $sum.Size = New-Object System.Drawing.Size(840, 22)
    $sum.ForeColor = C $script:CLR.dim2
    $sum.Text = 'выбери период'
    $f.Controls.Add($sum)

    $grid = New-Object System.Windows.Forms.DataGridView
    $grid.Location = New-Object System.Drawing.Point(12, 96)
    $grid.Size = New-Object System.Drawing.Size(860, 240)
    $grid.BackgroundColor = C $script:CLR.panel
    $grid.GridColor = C '#232B39'
    $grid.BorderStyle = 'None'
    $grid.EnableHeadersVisualStyles = $false
    $grid.ColumnHeadersDefaultCellStyle.BackColor = C (SkinC '#151B26')
    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $grid.DefaultCellStyle.BackColor = C $script:CLR.panel
    $grid.DefaultCellStyle.ForeColor = C '#D6E2F5'
    $grid.DefaultCellStyle.SelectionBackColor = C (SkinC '#24405A')
    $grid.DefaultCellStyle.SelectionForeColor = C $script:CLR.text
    $grid.RowHeadersVisible = $false
    $grid.AllowUserToAddRows = $false
    $grid.ReadOnly = $true
    $grid.AutoSizeColumnsMode = 'Fill'
    $grid.Font = $font
    foreach ($col in @(
        @('d', 'День', 90), @('kwh', 'кВт·ч', 70), @('cost', '₽', 70),
        @('dayKwh', 'день', 70), @('nightKwh', 'ночь', 70),
        @('dayCost', '₽ день', 70), @('nightCost', '₽ ночь', 70),
        @('v', 'U мин–макс', 100), @('t', 't мин–макс', 90), @('pmax', 'пик Вт', 70)
    )) {
        $null = $grid.Columns.Add($col[0], $col[1])
        $grid.Columns[$col[0]].FillWeight = $col[2]
    }
    $f.Controls.Add($grid)

    # графики периода и сводка погоды — как на веб-странице
    $rangeChart = New-Object System.Windows.Forms.PictureBox
    $rangeChart.Location = New-Object System.Drawing.Point(12, 344)
    $rangeChart.Size = New-Object System.Drawing.Size(860, 160)
    $rangeChart.BackColor = C $script:CLR.panel
    $rangeChart.Add_Paint({ param($s, $e) Draw-RangeChart $e.Graphics $s })
    $f.Controls.Add($rangeChart)

    $lblRngWeather = New-Lbl $f 'погода: —' 9 $script:CLR.dim2 $false 12 508
    $lblRngWeather.Size = New-Object System.Drawing.Size(860, 22)
    $f.Controls.Add($lblRngWeather)

    # --- P2.1 «Сравнение периодов»: числа из уже полученных rangeRows/histCache
    $lblCmpHdr = New-Lbl $f 'СРАВНЕНИЕ ПЕРИОДОВ' 9 $script:CLR.accent $true 12 534
    $lblCmpHdr.AutoSize = $false
    $lblCmpHdr.Size = New-Object System.Drawing.Size(860, 16)
    $lblCmpToday = New-Lbl $f 'сегодня / вчера: загрузка…' 8.5 $script:CLR.dim $false 12 552
    $lblCmpToday.AutoSize = $false
    $lblCmpToday.Size = New-Object System.Drawing.Size(860, 36)
    $lblCmpRange = New-Lbl $f 'диапазон / предыдущий: загрузка…' 8.5 $script:CLR.dim $false 12 590
    $lblCmpRange.AutoSize = $false
    $lblCmpRange.Size = New-Object System.Drawing.Size(860, 36)
    $lblCmpCov = New-Lbl $f 'покрытие: —' 8.5 $script:CLR.dim2 $false 12 628
    $lblCmpCov.AutoSize = $false
    $lblCmpCov.Size = New-Object System.Drawing.Size(860, 18)

    # --- P2.2 «Пики нагрузки»: 5 наблюдавшихся часовых пиков -----------------
    $lblPeakHdr = New-Lbl $f 'ПИКИ НАГРУЗКИ' 9 $script:CLR.accent $true 12 650
    $lblPeakHdr.AutoSize = $false
    $lblPeakHdr.Size = New-Object System.Drawing.Size(860, 16)
    $peaksGrid = New-Object System.Windows.Forms.DataGridView
    $peaksGrid.Location = New-Object System.Drawing.Point(12, 668)
    $peaksGrid.Size = New-Object System.Drawing.Size(860, 118)
    $peaksGrid.BackgroundColor = C $script:CLR.panel
    $peaksGrid.GridColor = C '#232B39'
    $peaksGrid.BorderStyle = 'None'
    $peaksGrid.EnableHeadersVisualStyles = $false
    $peaksGrid.ColumnHeadersDefaultCellStyle.BackColor = C (SkinC '#151B26')
    $peaksGrid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $peaksGrid.DefaultCellStyle.BackColor = C $script:CLR.panel
    $peaksGrid.DefaultCellStyle.ForeColor = C '#D6E2F5'
    $peaksGrid.DefaultCellStyle.SelectionBackColor = C (SkinC '#24405A')
    $peaksGrid.DefaultCellStyle.SelectionForeColor = C $script:CLR.text
    $peaksGrid.RowHeadersVisible = $false
    $peaksGrid.AllowUserToAddRows = $false
    $peaksGrid.ReadOnly = $true
    $peaksGrid.SelectionMode = 'FullRowSelect'
    $peaksGrid.MultiSelect = $false
    $peaksGrid.AutoSizeColumnsMode = 'Fill'
    $peaksGrid.Font = $font
    $peaksGrid.RowTemplate.Height = 18
    $null = $peaksGrid.Columns.Add('pd', 'дата')
    $null = $peaksGrid.Columns.Add('ph', 'час')
    $null = $peaksGrid.Columns.Add('pw', 'Вт (наблюдавшийся пик часа)')
    $peaksGrid.Columns['pd'].FillWeight = 80
    $peaksGrid.Columns['ph'].FillWeight = 100
    $peaksGrid.Columns['pw'].FillWeight = 240
    $f.Controls.Add($peaksGrid)
    $lblPeakBase = New-Lbl $f 'базовая нагрузка: —' 8.5 $script:CLR.dim2 $false 12 788
    $lblPeakBase.AutoSize = $false
    $lblPeakBase.Size = New-Object System.Drawing.Size(860, 20)

    # клик по пику — выбор этого часа существующим механизмом Set-HistSelectHour
    $peaksGrid.Add_CellClick({
        param($sender, $e)
        try {
            $ui = $script:rangeCmpUi
            if ($null -eq $ui -or $null -eq $e) { return }
            $plist = @($ui.PeaksList)
            $i = [int]$e.RowIndex
            if ($i -lt 0 -or $i -ge $plist.Count) { return }
            $p = $plist[$i]
            $script:histMetric = 'p'
            $null = Set-HistSelectHour ([string]$p.Date) ([int]$p.Hour) 'p'
            Add-Log ('пики: выбран час ' + [string]$p.Date + ' ' + ('{0:00}:00' -f [int]$p.Hour))
            try { Show-Page 'hist' } catch { }
            try { $f.Close() } catch { }
        } catch { Write-AppLog ('клик по пику: ' + $_.Exception.Message) }
    })

    # строка статуса сбора пропусков (ниже новых секций)
    $lblFill = New-Lbl $f 'заполнение пропусков: не запущено' 9 $script:CLR.dim2 $false 12 812
    $lblFill.AutoSize = $false
    $lblFill.Size = New-Object System.Drawing.Size(860, 20)


    # --- P2.5 «КЛИМАТ И ЭНЕРГИЯ» — компактная колонка справа от секций
    #     сравнения/пиков. Координаты зафиксированных слева не меняются, окно
    #     лишь чуть шире. Комнатный климат за период — /climatehist: один
    #     асинхронный запрос на смену периода, ответ кэшируется по from/to.
    $clX = 880
    $clW = 376
    $lblCliHdr = New-Lbl $f 'КЛИМАТ И ЭНЕРГИЯ' 9 $script:CLR.accent $true $clX 82
    $lblCliHdr.AutoSize = $false
    $lblCliHdr.Size = New-Object System.Drawing.Size($clW, 16)
    $lblCliSum = New-Lbl $f 'климат: выбери период' 8.5 $script:CLR.dim $false $clX 100
    $lblCliSum.AutoSize = $false
    $lblCliSum.Size = New-Object System.Drawing.Size($clW, 186)
    $lblCliCorr = New-Lbl $f 'связь: —' 8.5 $script:CLR.dim2 $false $clX 290
    $lblCliCorr.AutoSize = $false
    $lblCliCorr.Size = New-Object System.Drawing.Size($clW, 94)
    $lblCliCap = New-Lbl $f 'таблица по дням · t реле — почасовое среднее, иначе середина дневных мин–макс; t ул — прогноз платы' 8 $script:CLR.dim2 $false $clX 386
    $lblCliCap.AutoSize = $false
    $lblCliCap.Size = New-Object System.Drawing.Size($clW, 32)
    $climateGrid = New-Object System.Windows.Forms.DataGridView
    $climateGrid.Location = New-Object System.Drawing.Point($clX, 422)
    $climateGrid.Size = New-Object System.Drawing.Size($clW, 232)
    $climateGrid.BackgroundColor = C $script:CLR.panel
    $climateGrid.GridColor = C '#232B39'
    $climateGrid.BorderStyle = 'None'
    $climateGrid.EnableHeadersVisualStyles = $false
    $climateGrid.ColumnHeadersDefaultCellStyle.BackColor = C (SkinC '#151B26')
    $climateGrid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $climateGrid.DefaultCellStyle.BackColor = C $script:CLR.panel
    $climateGrid.DefaultCellStyle.ForeColor = C '#D6E2F5'
    $climateGrid.DefaultCellStyle.SelectionBackColor = C (SkinC '#24405A')
    $climateGrid.DefaultCellStyle.SelectionForeColor = C $script:CLR.text
    $climateGrid.RowHeadersVisible = $false
    $climateGrid.AllowUserToAddRows = $false
    $climateGrid.ReadOnly = $true
    $climateGrid.AutoSizeColumnsMode = 'Fill'
    $climateGrid.RowTemplate.Height = 18
    $climateGrid.Font = New-Object System.Drawing.Font('Segoe UI', 8)
    $null = $climateGrid.Columns.Add('cd', 'день')
    $null = $climateGrid.Columns.Add('ct', 't комн')
    $null = $climateGrid.Columns.Add('cr', 't реле')
    $null = $climateGrid.Columns.Add('cw', 't ул')
    $null = $climateGrid.Columns.Add('ck', 'кВт·ч')
    $null = $climateGrid.Columns.Add('cu', '₽')
    $null = $climateGrid.Columns.Add('cn', 'ч')
    $climateGrid.Columns['cd'].FillWeight = 64
    $climateGrid.Columns['ct'].FillWeight = 60
    $climateGrid.Columns['cr'].FillWeight = 60
    $climateGrid.Columns['cw'].FillWeight = 56
    $climateGrid.Columns['ck'].FillWeight = 80
    $climateGrid.Columns['cu'].FillWeight = 64
    $climateGrid.Columns['cn'].FillWeight = 34
    $f.Controls.Add($climateGrid)
    $lblCliNote = New-Lbl $f 'пустое — «—», не 0 · «ч» — часы с данными (сегодня n/24) · климат — /climatehist, сейчас — /data' 8 $script:CLR.dim2 $false $clX 660
    $lblCliNote.AutoSize = $false
    $lblCliNote.Size = New-Object System.Drawing.Size($clW, 40)
    $f.Controls.Add($lblCliNote)
    # P2.5b-MINOR: «Повторить» при закэшированной ошибке загрузки климата.
    # Маленькая кнопка в правой колонке (зафиксированные секции слева не двигаются):
    # сбрасывает ошибку периода и делает ОДИН повторный запрос, без автоспама.
    $btnCliRetry = New-Object System.Windows.Forms.Button
    $btnCliRetry.Text = 'Повторить'
    $btnCliRetry.Location = New-Object System.Drawing.Point($clX, 704)
    $btnCliRetry.Size = New-Object System.Drawing.Size(96, 22)
    $btnCliRetry.FlatStyle = 'Flat'
    $btnCliRetry.BackColor = C (SkinC '#24405A')
    $btnCliRetry.ForeColor = C $script:CLR.text
    $btnCliRetry.Font = New-Object System.Drawing.Font('Segoe UI', 8)
    $btnCliRetry.Visible = $false
    $btnCliRetry.Add_Click({
        try {
            $spanR = $script:rangeSpan
            $uiR = $script:rangeCmpUi
            if (($null -eq $spanR) -or ($null -eq $uiR)) { return }
            $winR = Get-ClimateWindow ([string]$spanR.From) ([string]$spanR.To)
            # сброс закэшированной ошибки и ровно один повторный запрос
            Remove-ClimateHistEntry ([string]$winR.From) ([string]$winR.To)
            if ($null -ne $uiR.CliRetry) { try { $uiR.CliRetry.Visible = $false } catch { } }
            Stop-ClimateLoad
            if ($uiR.Ip) { Start-ClimateLoad ([string]$uiR.Ip) ([string]$winR.From) ([string]$winR.To) }
            Add-Log ('климат за период ' + $winR.From + '…' + $winR.To + ': повтор по кнопке «Повторить»')
            try { Update-RangeAnalytics } catch { }
        } catch { Add-Log ('климат: повтор не удался — ' + $_.Exception.Message) 'warn' }
    })
    $f.Controls.Add($btnCliRetry)

    $script:rangeRows = @()
    $script:rangeWeather = $lblRngWeather
    $script:rangeChart = $rangeChart
    $script:rangeCmpUi = @{
        Ip = $ip; Today = $lblCmpToday; Range = $lblCmpRange; Coverage = $lblCmpCov
        Header = $lblPeakHdr; PeaksGrid = $peaksGrid; Base = $lblPeakBase
        PeaksList = @(); Sig = ''
        BudgetBtn = $btnBudget; Grid = $grid; Sum = $sum; D1 = $d1; D2 = $d2
        ClimateSum = $lblCliSum; ClimateCorr = $lblCliCorr; ClimateGrid = $climateGrid
        ClimateNote = $lblCliNote; ClimateSig = ''; CliRetry = $btnCliRetry
        ClimateCache = $null; ClimateCacheKey = ''
    }
    $script:rangeCmpTimer = New-Object System.Windows.Forms.Timer
    $script:rangeCmpTimer.Interval = 1200
    $script:rangeCmpTimer.Add_Tick({ try { Update-RangeAnalytics } catch { } })
    $script:rangeCmpTimer.Start()
    $script:gapPollTimer = New-Object System.Windows.Forms.Timer
    $script:gapPollTimer.Interval = 2000
    $script:gapPollTimer.Add_Tick({ try { Step-GapFillPoll } catch { } })
    $script:gapUi = @{ ip = $ip; lbl = $lblFill; grid = $grid; sum = $sum; d1 = $d1; d2 = $d2; btn = $btnFillAll; busy = $false; sw = $null }


    foreach ($c in $f.Controls) {
        if ($c -is [System.Windows.Forms.Button] -and $null -ne $c.Tag) {
            $tag = [int]$c.Tag
            $c.Add_Click({
                $now = Get-Date
                switch ($tag) {
                    0 { $a = $now;              $b = $now }
                    1 { $a = $now.AddDays(-1);  $b = $a }
                    2 { $a = $now.AddDays(-6);  $b = $now }
                    3 { $a = $now.AddDays(-29); $b = $now }
                    4 { $a = (Get-Date -Year $now.Year -Month $now.Month -Day 1); $b = $now }
                    5 { $a = (Get-Date -Year $now.Year -Month $now.Month -Day 1).AddMonths(-1)
                        $b = (Get-Date -Year $now.Year -Month $now.Month -Day 1).AddDays(-1) }
                    6 { $a = (Get-Date -Year $now.Year -Month 1 -Day 1); $b = $now }
                    7 { $a = (Get-Date -Year $now.Year -Month 1 -Day 1); $b = $now }
                }
                $d1.Value = $a
                $d2.Value = $b
                $null = Update-RangeGrid $grid $sum $ip $a.ToString('yyyy-MM-dd') $b.ToString('yyyy-MM-dd')
            }.GetNewClosure())
        }
    }

    $btnGo.Add_Click({
        $null = Update-RangeGrid $grid $sum $ip $d1.Value.ToString('yyyy-MM-dd') $d2.Value.ToString('yyyy-MM-dd')
    })

    $btnFill.Add_Click({
        $a = $d1.Value.ToString('yyyy-MM-dd')
        $b = $d2.Value.ToString('yyyy-MM-dd')
        Fill-DayGaps $a $b
        Add-Log ('заполнение за ' + $a + '…' + $b + ': идёт в фоне на плате')
    })

    $btnFillAll.Add_Click({
        if ($null -ne $script:gapUi -and [bool]$script:gapUi.busy) {
            $lblFill.Text = 'заполнение уже идёт — ждём прогресс'
            return
        }
        $lblFill.ForeColor = C $script:CLR.dim2
        $lblFill.Text = 'заполнение пропусков: отправляю запрос плате…'
        [System.Windows.Forms.Application]::DoEvents()
        if ($null -ne $script:gapUi) {
            $script:gapUi.busy = $true
            $script:gapUi.sw = [System.Diagnostics.Stopwatch]::StartNew()
            $script:gapUi.btn.Enabled = $false
        }
        try {
            $res = Start-GapFillAll $ip
            if ($null -ne $res -and [bool](Field $res @('ok') $false)) {
                $lblFill.Text = 'заполнение пропусков: запущено на плате, жду прогресс…'
                Add-Log 'заполнение всех пропусков: запущено на плате' 'ok'
                $script:gapPollTimer.Start()
            } else {
                $err = if ($null -ne $res) { [string](Field $res @('err') 'плата отказала') } else { 'нет ответа' }
                Stop-GapFillPoll ('заполнение: плата не запустила — ' + $err)
                Add-Log ('заполнение всех пропусков: отказ — ' + $err) 'warn'
            }
        } catch {
            Stop-GapFillPoll ('заполнение: ошибка — ' + $_.Exception.Message)
            Add-Log ('заполнение всех пропусков: ' + $_.Exception.Message) 'err'
        }
    })

    $btnCsv.Add_Click({
        # P2.4: CSV за выбранный диапазон. Одна кнопка, диалог сохранения с
        # выбором папки; BOM для Excel, заголовок с единицами, дата+время,
        # пропуски — пустые поля; исходные данные и файлы на SD не меняются.
        $from = $d1.Value.ToString('yyyy-MM-dd')
        $to = $d2.Value.ToString('yyyy-MM-dd')
        $csv = ConvertTo-RangeCsv $script:rangeRows $script:histCache $from $to (Get-Date)
        if (-not $csv) {
            Add-Log 'CSV периода: за выбранный диапазон нет данных — файл не создан' 'warn'
            return
        }
        $dlg = New-Object System.Windows.Forms.SaveFileDialog
        $dlg.Filter = 'CSV для Excel (*.csv)|*.csv|Все файлы (*.*)|*.*'
        $dlg.DefaultExt = 'csv'
        $dlg.AddExtension = $true
        $dlg.FileName = Get-RangeCsvFileName $from $to
        $dir = [string]$script:cfg.csvDir
        try {
            if ($dir -and (Test-Path $dir)) { $dlg.InitialDirectory = $dir }
            else { $dlg.InitialDirectory = [Environment]::GetFolderPath('MyDocuments') }
        } catch { }
        if ($dlg.ShowDialog() -ne 'OK') { return }
        try {
            Write-RangeCsvFile $dlg.FileName $csv
            $script:cfg.csvDir = [System.IO.Path]::GetDirectoryName($dlg.FileName)
            Save-Cfg
            $rowsN = @($script:rangeRows).Count
            Add-Log ('CSV периода: сохранено ' + $dlg.FileName + ' · ' + $from + '…' + $to + ' · дней /range ' + $rowsN) 'ok'
        } catch {
            Add-Log ('CSV периода: не удалось сохранить — ' + $_.Exception.Message) 'err'
        }
    })

    $btnBudget.Add_Click({
        try { Show-Budget } catch { Add-Log ('бюджет: ' + $_.Exception.Message) 'err' }
    })

    $d1.Value = (Get-Date).AddDays(-6)
    $d2.Value = (Get-Date)
    $f.Add_FormClosed({
        try { $script:gapPollTimer.Stop() } catch { }
        try { if ($null -ne $script:gapCts) { $script:gapCts.Cancel(); $script:gapCts.Dispose() } } catch { }
        $script:gapCts = $null; $script:gapTask = $null
        try { if ($null -ne $script:rangeCmpTimer) { $script:rangeCmpTimer.Stop(); $script:rangeCmpTimer.Dispose() } } catch { }
        $script:rangeCmpTimer = $null
        $script:rangeCmpUi = $null
        try { Stop-CmpLoad } catch { }
        try { Stop-ClimateLoad } catch { }
        $script:cmpRows = @(); $script:cmpKey = ''; $script:cmpFail = ''; $script:rangeSpan = $null
        try { Stop-RangeLoad } catch { }
        $script:gapUi = $null
        $script:rangeWeather = $null
        $script:rangeChart = $null
    })

    $f.Add_Shown({
        $null = Update-RangeGrid $grid $sum $ip $d1.Value.ToString('yyyy-MM-dd') $d2.Value.ToString('yyyy-MM-dd')
        try { Update-RangeAnalytics } catch { }
    })
    [void]$f.ShowDialog()
    $f.Dispose()
}

function Show-Scheduler {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Планировщик реле — ' + $ip
    $f.Size = New-Object System.Drawing.Size(560, 440)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text

    $list = New-Object System.Windows.Forms.ListBox
    $list.Location = New-Object System.Drawing.Point(14, 14)
    $list.Size = New-Object System.Drawing.Size(516, 210)
    $list.BackColor = C $script:CLR.panel
    $list.ForeColor = C '#D6E2F5'
    $list.Font = New-Object System.Drawing.Font('Consolas', 10)
    $f.Controls.Add($list)

    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Location = New-Object System.Drawing.Point(14, 234)
    $lbl.Size = New-Object System.Drawing.Size(516, 20)
    $lbl.Text = 'Время (ЧЧ:ММ)   действие   маска дней (127 = все, 31 = пн-пт)'
    $lbl.ForeColor = C $script:CLR.dim2
    $f.Controls.Add($lbl)

    $tbTime = New-Object System.Windows.Forms.TextBox
    $tbTime.Location = New-Object System.Drawing.Point(14, 258)
    $tbTime.Size = New-Object System.Drawing.Size(70, 24)
    $tbTime.Text = '07:30'
    $f.Controls.Add($tbTime)

    $cbAct = New-Object System.Windows.Forms.ComboBox
    $cbAct.Location = New-Object System.Drawing.Point(92, 258)
    $cbAct.Size = New-Object System.Drawing.Size(120, 24)
    [void]$cbAct.Items.Add('включить')
    [void]$cbAct.Items.Add('выключить')
    $cbAct.SelectedIndex = 0
    $f.Controls.Add($cbAct)

    $tbDays = New-Object System.Windows.Forms.TextBox
    $tbDays.Location = New-Object System.Drawing.Point(220, 258)
    $tbDays.Size = New-Object System.Drawing.Size(60, 24)
    $tbDays.Text = '127'
    $f.Controls.Add($tbDays)

    $btnAdd = New-Object System.Windows.Forms.Button
    $btnAdd.Location = New-Object System.Drawing.Point(290, 256)
    $btnAdd.Size = New-Object System.Drawing.Size(110, 28)
    $btnAdd.Text = 'Добавить'
    $f.Controls.Add($btnAdd)

    $btnDel = New-Object System.Windows.Forms.Button
    $btnDel.Location = New-Object System.Drawing.Point(408, 256)
    $btnDel.Size = New-Object System.Drawing.Size(110, 28)
    $btnDel.Text = 'Удалить'
    $f.Controls.Add($btnDel)

    $chkAllow = New-Object System.Windows.Forms.CheckBox
    $chkAllow.Location = New-Object System.Drawing.Point(14, 296)
    $chkAllow.Size = New-Object System.Drawing.Size(300, 24)
    $chkAllow.Text = 'Разрешить выключение реле'
    $chkAllow.ForeColor = C '#F8B4B4'
    $f.Controls.Add($chkAllow)

    $btnSave = New-Object System.Windows.Forms.Button
    $btnSave.Location = New-Object System.Drawing.Point(330, 330)
    $btnSave.Size = New-Object System.Drawing.Size(196, 30)
    $btnSave.Text = 'Сохранить на плату'
    $f.Controls.Add($btnSave)

    $script:schRules = New-Object System.Collections.ArrayList
    $script:schAllow = $false

    function Refresh-Sch {
        $list.Items.Clear()
        foreach ($r in $script:schRules) {
            $hh = '{0:00}:{1:00}' -f [int]$r.h, [int]$r.m
            $act = if ([int]$r.on -eq 1) { 'включить  ' } else { 'выключить ' }
            [void]$list.Items.Add(("{0}  {1}  дни {2}  {3}" -f $hh, $act, $r.days, $(if ([int]$r.en -eq 1) { 'вкл' } else { 'выкл' })))
        }
        $chkAllow.Checked = [bool]$script:schAllow
    }

    function Load-Sch {
        try {
            $j = Invoke-RestMethod ("http://" + $ip + "/sched") -TimeoutSec 8
            $script:schRules = New-Object System.Collections.ArrayList
            foreach ($r in @($j.rules)) { [void]$script:schRules.Add($r) }
            $script:schAllow = [bool]$j.allowOff
            Refresh-Sch
            Add-Log ("планировщик: правил " + $script:schRules.Count) 'ok'
        } catch {
            Add-Log ('планировщик: не удалось прочитать — ' + $_.Exception.Message) 'err'
        }
    }

    function Save-Sch {
        $parts = @()
        foreach ($r in $script:schRules) {
            $parts += ('{0:00}:{1:00},{2},{3},{4}' -f [int]$r.h, [int]$r.m, $(if ([int]$r.on -eq 1) { 'on' } else { 'off' }), [int]$r.days, $(if ([int]$r.en -eq 1) { 1 } else { 0 }))
        }
        $q = "/sched?set=" + [uri]::EscapeDataString(($parts -join ';'))
        try {
            Invoke-RestMethod ("http://" + $ip + $q) -TimeoutSec 8 -Headers (Get-ControlHeaders) | Out-Null
            Invoke-RestMethod ("http://" + $ip + "/sched?allow=" + $(if ($chkAllow.Checked) { 1 } else { 0 })) -TimeoutSec 8 -Headers (Get-ControlHeaders) | Out-Null
            Add-Log 'планировщик: сохранено на плату' 'ok'
            Load-Sch
        } catch {
            Add-Log ('планировщик: ошибка сохранения — ' + $_.Exception.Message) 'err'
        }
    }

    $btnAdd.Add_Click({
        $t = ($tbTime.Text).Trim()
        if ($t -notmatch '^(\d{1,2}):(\d{2})$') { [System.Windows.Forms.MessageBox]::Show('Время в формате ЧЧ:ММ'); return }
        $h = [int]$Matches[1]; $m = [int]$Matches[2]
        if ($h -gt 23 -or $m -gt 59) { [System.Windows.Forms.MessageBox]::Show('Неверное время'); return }
        $on = if ($cbAct.SelectedIndex -eq 0) { 1 } else { 0 }
        if ($on -eq 0 -and -not $chkAllow.Checked) {
            $res = [System.Windows.Forms.MessageBox]::Show('Выключение реле не разрешено. Разрешить?', 'Планировщик', 'YesNo', 'Warning')
            if ($res -ne 'Yes') { return }
            $chkAllow.Checked = $true
        }
        $days = 127
        [void][int]::TryParse($tbDays.Text, [ref]$days)
        [void]$script:schRules.Add([pscustomobject]@{ h = $h; m = $m; on = $on; days = $days; en = 1 })
        Refresh-Sch
    })
    $btnDel.Add_Click({
        if ($list.SelectedIndex -ge 0) { $script:schRules.RemoveAt($list.SelectedIndex); Refresh-Sch }
    })
    $btnSave.Add_Click({ Save-Sch })
    $f.Add_Shown({ Load-Sch })
    [void]$f.ShowDialog()
}

function Show-Tariff {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Тариф — ' + $ip
    $f.Size = New-Object System.Drawing.Size(420, 300)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text

    $cur = $null
    try { $cur = Invoke-RestUi ('http://' + $ip + '/tariff') 15 } catch { }

    $defs = @(
        @{ id = 'tarDayIn';  text = 'День, ₽/кВт·ч';       val = 8.39; min = 0; max = 99; step = '0.01' },
        @{ id = 'tarNightIn';text = 'Ночь, ₽/кВт·ч';       val = 4.49; min = 0; max = 99; step = '0.01' },
        @{ id = 'tarFromIn'; text = 'Ночь начинается, ч';  val = 23;   min = 0; max = 23; step = '1' },
        @{ id = 'tarToIn';   text = 'Ночь кончается, ч';   val = 6;    min = 0; max = 23; step = '1' }
    )
    if ($cur) {
        $defs[0].val = [double]$cur.day
        $defs[1].val = [double]$cur.night
        $h1 = Field $cur @('h1'); $h2 = Field $cur @('h2')
        if ($null -eq $h1) { $h1 = Field $cur @('from') }
        if ($null -eq $h2) { $h2 = Field $cur @('to') }
        if ($null -ne $h2) { $defs[2].val = [double]$h2 }   # ночь начинается
        if ($null -ne $h1) { $defs[3].val = [double]$h1 }   # ночь кончается
    }

    $y = 16
    foreach ($d in $defs) {
        $l = New-Lbl $f $d.text 9 $script:CLR.dim $false 16 $y
        $l.Size = New-Object System.Drawing.Size(190, 20)
        $n = New-Object System.Windows.Forms.NumericUpDown
        $n.Location = New-Object System.Drawing.Point(214, ($y - 2))
        $n.Size = New-Object System.Drawing.Size(90, 24)
        $n.Minimum = $d.min; $n.Maximum = $d.max
        $n.DecimalPlaces = $(if ($d.step -eq '0.01') { 2 } else { 0 })
        $n.Increment = [decimal]$d.step
        $n.Value = [decimal]$d.val
        $n.BackColor = C (SkinC '#12161C')
        $n.ForeColor = C $script:CLR.text
        Set-Variable -Name ('num_' + $d.id) -Value $n -Scope Script
        $f.Controls.Add($l); $f.Controls.Add($n)
        $y += 38
    }

    $st = New-Lbl $f '—' 9 $script:CLR.dim $false 16 ($y + 4)
    $st.Size = New-Object System.Drawing.Size(370, 20)

    $b = New-Btn $f 'Сохранить в плату' (SkinC '#24405A') $script:CLR.text 170 30
    $b.Location = New-Object System.Drawing.Point(16, ($y + 30))
    $b.Add_Click({
        try {
            $u = 'http://' + $ip + '/tariff?set=' + $script:num_tarDayIn.Value + ',' + $script:num_tarNightIn.Value +
                 ',' + $script:num_tarToIn.Value + ',' + $script:num_tarFromIn.Value
            $r = Invoke-RestUi $u 15
            $st.Text = 'тариф сохранён'
            Add-Log ('тариф: день ' + $script:num_tarDayIn.Value + ', ночь ' + $script:num_tarNightIn.Value)
            if ($r) { }
        } catch { $st.Text = 'не удалось сохранить' }
    })
    $f.Controls.Add($b)
    [void]$f.ShowDialog()
}

function Ensure-Tray {
    if ($script:tray) { return }
    try {
        $script:tray = New-Object System.Windows.Forms.NotifyIcon
        $script:tray.Icon = [System.Drawing.SystemIcons]::Information
        $script:tray.Text = 'DOM MONITOR'
        $script:tray.Visible = $true
        $menu = New-Object System.Windows.Forms.ContextMenuStrip
        $miOpen = $menu.Items.Add('Открыть окно')
        $miOpen.Add_Click({ try { $form.Show(); $form.WindowState = 'Normal'; $form.Activate() } catch { } })
        $miWeb = $menu.Items.Add('Веб-страница платы')
        $miWeb.Add_Click({ try { Start-Process (Get-ControlWebUrl $script:cfg.boardIp) } catch { } })
        $miFind = $menu.Items.Add('Найти плату в сети')
        $miFind.Add_Click({ try { Find-Board -Quiet:$false } catch { } })
        $menu.Items.Add('-') | Out-Null
        $miExit = $menu.Items.Add('Выход')
        $miExit.Add_Click({ try { $script:exiting = $true; $form.Close() } catch { } })
        $miTariff = $menu.Items.Add('Тариф платы')
        $miTariff.Add_Click({ try { Show-Tariff } catch { } })
        $miSound = $menu.Items.Add('Звуки платы')
        $miSound.Add_Click({ try { Show-Sounds } catch { } })
        $script:tray.ContextMenuStrip = $menu
        $script:tray.Add_MouseDoubleClick({ try { $form.Show(); $form.WindowState = 'Normal'; $form.Activate() } catch { } })
        Add-Log 'трей: значок создан' 'ok'
    } catch { Add-Log ('трей: ' + $_.Exception.Message) 'err' }
}

function Update-Tray {
    if (-not $script:tray) { return }
    $now = Get-Date
    if (($now - $script:trayAt).TotalSeconds -lt 5) { return }
    $script:trayAt = $now
    $d = $script:board
    $txt = 'DOM MONITOR'
    if ($d) {
        $v = ''; $p = ''
        try { if ($d.v) { $v = [string][int]$d.v } } catch { }
        try { if ($d.p -ne $null) { $p = [string][int]$d.p } } catch { }
        if ($v) { $txt = "DOM MONITOR · $v В · $p Вт" }
    } else { $txt = 'DOM MONITOR · нет связи' }
    if ($txt.Length -gt 63) { $txt = $txt.Substring(0, 63) }
    try { $script:tray.Text = $txt } catch { }
}

function Find-Board {
    param([switch]$Quiet)
    $base = [string]$script:cfg.boardIp
    if (-not $base) { $base = '192.168.1.51' }
    $parts = $base.Split('.')
    if ($parts.Count -ne 4) { return @() }
    $prefix = "$($parts[0]).$($parts[1]).$($parts[2])"
    if (-not $Quiet) { Add-Log "поиск платы в сети $prefix.0/24 …" }
    $found = New-Object System.Collections.ArrayList
    $client = New-Object System.Net.Http.HttpClient
    $client.Timeout = [TimeSpan]::FromMilliseconds(700)
    $tasks = @{}
    foreach ($n in 1..254) {
        $ip = "$prefix.$n"
        try {
            $tasks[$ip] = $client.GetStringAsync("http://$ip/data")
        } catch { }
        if ($tasks.Count -ge 48) { break }
    }
    Start-Sleep -Milliseconds 900
    foreach ($ip in @($tasks.Keys)) {
        $task = $tasks[$ip]
        try {
            if ($task.IsCompleted -and -not $task.IsFaulted -and -not $task.IsCanceled) {
                $j = $task.Result | ConvertFrom-Json
                if ($null -ne $j.v -or $null -ne $j.e) {
                    [void]$found.Add([pscustomobject]@{ ip = $ip; v = $j.v; e = $j.e })
                }
            }
        } catch { }
    }
    $client.Dispose()
    $script:discovered = @($found)
    if ($found.Count -gt 0) {
        Add-Log ("найдено плат: " + $found.Count + " — " + (($found | ForEach-Object { $_.ip }) -join ', ')) 'ok'
    } elseif (-not $Quiet) {
        Add-Log 'плата в сети не найдена' 'err'
    }
    return $found
}

function Get-AlertCfg($name, $def) {

    try {

        $v = $script:cfg[$name]

        if ($null -eq $v -or $v -eq '') { return $def }

        return [double]$v

    } catch { return $def }

}



function Show-Toast($title, $text, $kind) {

    try {

        if ($null -eq $script:tray) {

            $script:tray = New-Object System.Windows.Forms.NotifyIcon

            $script:tray.Icon = [System.Drawing.SystemIcons]::Information

            $script:tray.Text = 'DOM MONITOR'

            $script:tray.Visible = $true

        }

        $ico = if ($kind -eq 'err') { [System.Windows.Forms.ToolTipIcon]::Error }

               elseif ($kind -eq 'warn') { [System.Windows.Forms.ToolTipIcon]::Warning }

               else { [System.Windows.Forms.ToolTipIcon]::Info }

        $script:tray.ShowBalloonTip(8000, $title, $text, $ico)

    } catch { }

}



function Send-TgRelay($text) {

    try {

        if (-not $script:cfg.tgRelay) { Add-Log 'telegram: реле выключено (tgRelay=false)' 'warn'; return }

        # токен GitHub: сначала переменная окружения, потом файл secrets.env рядом с настройками,
    # и только потом старое поле в settings.json (для совместимости)
    $token = [string]$env:DOM_GITHUB_TOKEN
    if (-not $token) {
        $envf = Join-Path (Split-Path -Parent $script:cfgPath) 'secrets.env'
        if (-not (Test-Path $envf)) { $envf = Join-Path $env:APPDATA 'DomMonitor\secrets.env' }
        if (Test-Path $envf) {
            foreach ($ln in [System.IO.File]::ReadAllLines($envf)) {
                if ($ln -match '^DOM_GITHUB_TOKEN=(.+)$') { $token = $Matches[1].Trim() }
            }
        }
    }
    if (-not $token) { $token = [string]$script:cfg.githubToken }

        if (-not $token) { Add-Log 'telegram: нет githubToken в настройках' 'warn'; return }

        $relay = Join-Path $script:AppDir 'tg-relay.ps1'

        if (-not (Test-Path $relay)) { Add-Log ('нет tg-relay.ps1: ' + $relay) 'err'; return }

        # не чаще одной отправки в 3 секунды; БЕЗ Start-Sleep — иначе замирает интерфейс
    $nowTg = Get-Date
    if ($script:tgLastAt -and ($nowTg - $script:tgLastAt).TotalSeconds -lt 3) {
        Add-Log 'telegram: пропуск (слишком часто)' 'warn'
        return
    }
    $script:tgLastAt = $nowTg

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)

        $b64 = [Convert]::ToBase64String($bytes)

        Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @(

            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $relay,

            '-TextBase64', $b64, '-Token', $token, '-Repo', [string]$script:cfg.tgRepo) | Out-Null

    } catch { }

}



# cooldown: одно и то же уведомление не чаще раза в 15 минут

function Alert-State($key, $bad, $title, $text, $kind, $okTitle, $okText) {
    # Уведомляем о СМЕНЕ состояния, а не каждые 15 минут: пока напряжение
    # висит за порогом, сообщений больше не будет; вернулось в норму — придёт
    # одно короткое «восстановилось».
    $prev = [bool]$script:alertState[$key]
    if ($bad -eq $prev) { return }
    $script:alertState[$key] = $bad
    if ($bad) {
        Add-Log ('УВЕДОМЛЕНИЕ: ' + $title + ' — ' + $text) $kind
        Show-Toast $title $text $kind
        Send-TgRelay ($title + ': ' + $text)
    } elseif ($okTitle) {
        Add-Log ('восстановилось: ' + $okTitle) 'ok'
        Show-Toast $okTitle $okText 'ok'
        Send-TgRelay ($okTitle + ': ' + $okText)
    }
}

function Alert-Once($key, $title, $text, $kind) {

    $now = Get-Date

    $last = $script:alertLast[$key]

    if ($last -and ($now - $last).TotalMinutes -lt 15) { return }

    $script:alertLast[$key] = $now

    Add-Log ('УВЕДОМЛЕНИЕ: ' + $title + ' — ' + $text) $kind

    Show-Toast $title $text $kind

    Send-TgRelay ($title + ': ' + $text)

}



function Maybe-DailyReport {
    # Ежедневная сводка в Telegram. Включается в settings.json:
    #   "tgDaily": true, "tgDailyAt": "23:55". По умолчанию выключено.
    if (-not [bool]$script:cfg.tgDaily) { return }
    $now = Get-Date
    $today = $now.ToString('yyyy-MM-dd')
    if ([string]$script:cfg.tgDailyDay -eq $today) { return }
    $at = [string]$script:cfg.tgDailyAt
    if (-not $at) { $at = '23:55' }
    $atTime = [datetime]::ParseExact($at, 'H:mm', $null)
    if ($now.TimeOfDay -lt $atTime.TimeOfDay) { return }
    $script:cfg.tgDailyDay = $today
    try { Save-Cfg } catch { }
    $cand = @()
    if ($script:cfg.tgScript) { $cand += [string]$script:cfg.tgScript }
    $cand += (Join-Path $script:AppDir 'energy-telegram.ps1')
    $cand += 'D:\DomEnergy\timelapse\watchdog\energy-telegram.ps1'
    $scr = $cand | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $scr) { Add-Log 'ежедневный отчёт: не найден energy-telegram.ps1' 'err'; return }
    try {
        Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scr) | Out-Null
        Add-Log 'ежедневный отчёт: отправлен' 'ok'
    } catch {
        Add-Log ('ежедневный отчёт: ошибка ' + $_.Exception.Message) 'err'
    }
}

function Log-AlertConfig {
    if ($script:alertCfgLogged) { return }
    $script:alertCfgLogged = $true
    $on = $(if ([bool]$script:cfg.alertOn) { 'вкл' } else { 'выкл' })
    $tg = $(if ([bool]$script:cfg.tgRelay) { 'через реле' } else { 'выкл' })
    $dl = $(if ([bool]$script:cfg.tgDaily) { [string]$script:cfg.tgDailyAt } else { 'выкл' })
    Add-Log ('уведомления: ' + $on + ', U ' + (Get-AlertCfg 'alertVMin' 200) + '–' + (Get-AlertCfg 'alertVMax' 250) +
        ' В, t ' + (Get-AlertCfg 'alertTMax' 55) + ' °C, скачок ' + (Get-AlertCfg 'alertSpikeW' 500) +
        ' Вт, офлайн ' + (Get-AlertCfg 'alertOffMin' 3) + ' мин, telegram ' + $tg + ', сводка ' + $dl)
}

function Check-ProtAlerts {
    $pf=$null
    if ($null -ne $script:board -and (Get-BoardAge) -le 12 -and $null -ne (Field $script:board @('protectN'))) {
        $pf=[pscustomobject]@{trig=$script:board.protectN;on=$script:board.protectOn;why=$script:board.protectWhy}
    } elseif ($null -ne $script:protTask) {
        if (-not $script:protTask.IsCompleted) { return }
        try { if($script:protTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion){$pf=$script:protTask.Result|ConvertFrom-Json} } catch {}
        $script:protTask=$null
    } else {
        if (-not $script:cfg.boardIp -or $null -eq $script:httpSlow) { return }
        if ($script:protAt -and ((Get-Date)-$script:protAt).TotalSeconds -lt 60) { return }
        $script:protAt=Get-Date
        try { $script:protTask=$script:httpSlow.GetStringAsync('http://'+$script:cfg.boardIp+'/protect') } catch {}
        return
    }
    if ($null -eq $pf) { return }
    $trig=[int]$pf.trig
    if ($null -ne $script:protTrigPrev -and $trig -gt $script:protTrigPrev) {
        $text='отключений: '+$trig+' · '+[string]$pf.why
        Alert-Once 'prot' 'Защита сработала' $text 'err'
    }
    $script:protTrigPrev=$trig
}

function Check-Alerts {

    if ($script:otaProc -or ($script:otaQuietUntil -and (Get-Date) -lt $script:otaQuietUntil)) { return }

    if (-not [bool]$script:cfg.alertOn) { return }

    $now = Get-Date

    if ($script:alertCheckAt -and ($now - $script:alertCheckAt).TotalSeconds -lt 5) { return }

    $script:alertCheckAt = $now



    $vmin = Get-AlertCfg 'alertVMin' 200

    $vmax = Get-AlertCfg 'alertVMax' 250

    $tmax = Get-AlertCfg 'alertTMax' 55

    $offm = Get-AlertCfg 'alertOffMin' 3

    $spkW = Get-AlertCfg 'alertSpikeW' 500

    $spkX = Get-AlertCfg 'alertSpikeX' 2.0



    $d = $script:board

    if (-not $script:startedAt) { $script:startedAt = $now }

    # «плата молчит» проверяем только через 2 минуты после запуска программы

    $graceOver = (($now - $script:startedAt).TotalMinutes -ge 2)

    $age = 999

    if ($script:boardAt -and $script:boardAt.Year -gt 2000) { $age = ($now - $script:boardAt).TotalMinutes }



    # 1. пропажа связи

    if ($age -ge $offm -and $graceOver) {

        $script:alertSeen['wasOffline'] = $true
        if (-not $script:discoverTried -and -not $script:discovered -or ($script:discovered.Count -eq 0)) {
            if (-not $script:discoverAt) { $script:discoverAt = $now }
            elseif (($now - $script:discoverAt).TotalSeconds -gt 40 -and -not $script:discoverTried) {
                $script:discoverTried = $true
                try {
                    $found = Find-Board -Quiet
                    if ($found.Count -gt 0) {
                        $script:cfg.boardIp = $found[0].ip
                        $script:cfg.board = 'http://' + $found[0].ip
                        Save-Cfg
                        Add-Log ('плата найдена: ' + $found[0].ip + ' — переключаюсь') 'ok'
                    }
                } catch { }
            }
        }

        Alert-State 'offline' $true 'Плата не отвечает' ('нет данных ' + [Math]::Round($age, 1) + ' мин · ' + $script:cfg.boardIp) 'err' 'Плата на связи' ('данные снова идут · ' + $script:cfg.boardIp)

        return

    } elseif ($graceOver) {
        Alert-State 'offline' $false '' '' 'info'
    }

    if ($null -eq $d) { return }



    # 2. напряжение

    $v = 0.0; try { $v = [double]$d.v } catch { }

    if ($v -gt 0) {

        # Гистерезис: вошли в «плохо» за порогом, выходим только вернувшись на 3 В назад.
        # Так на самой границе нет пар «в норме / высокое».
        $wasLow  = [bool]$script:alertState['vlow']
        $wasHigh = [bool]$script:alertState['vhigh']
        $badLow  = if ($wasLow)  { $v -lt ($vmin + 3) } else { $v -lt $vmin }
        $badHigh = if ($wasHigh) { $v -gt ($vmax - 3) } else { $v -gt $vmax }
        if (-not $badHigh) {
            Alert-State 'vlow' $badLow 'Низкое напряжение' ($v.ToString('0') + ' В · порог ' + $vmin + ' В') 'warn' 'Напряжение в норме' ($v.ToString('0') + ' В')
        }
        if (-not $badLow) {
            Alert-State 'vhigh' $badHigh 'Высокое напряжение' ($v.ToString('0') + ' В · порог ' + $vmax + ' В') 'warn' 'Напряжение в норме' ($v.ToString('0') + ' В')
        }

        $script:alertSeen['v'] = $v

    }



    # 3. температура реле

    $tp = 0.0; try { $tp = [double]$d.temp } catch { }

    if ($tp -gt 0) {
        $wasTemp = [bool]$script:alertState['temp']
        $badTemp = if ($wasTemp) { $tp -ge ($tmax - 2) } else { $tp -ge $tmax }
        Alert-State 'temp' $badTemp 'Перегрев реле' ($tp.ToString('0.0') + ' °C · порог ' + $tmax + ' °C') 'err' 'Реле остыло' ($tp.ToString('0.0') + ' °C')
    }



    # 4. скачок мощности

    $p = 0.0; try { $p = [double]$d.p } catch { }

    if ($p -gt 0) {

        if (-not $script:alertSeen['pavg']) { $script:alertSeen['pavg'] = $p }

        $avg = [double]$script:alertSeen['pavg']

        if ($p -ge $spkW -and $avg -gt 0 -and $p -ge ($avg * $spkX)) {

            Alert-Once 'spike' 'Скачок мощности' ($p.ToString('0') + ' Вт при среднем ' + $avg.ToString('0') + ' Вт') 'warn'

        }

        $script:alertSeen['pavg'] = ($avg * 0.95) + ($p * 0.05)

    }

}



function On-Tick {

    try {

        Pump-Ota
        if ($script:otaProc -or $script:usbFlashBusy) { return }

        Pump-Board

        Pump-Measure

        Pump-Firmware

        Pump-Days

        # Дневной график и спарклайны карточек читают histCache за СЕГОДНЯ,
        # а «История» по умолчанию открыта на вчера — раньше сегодняшний день
        # не запрашивался вовсе, и карточки писали «нет измерений за сегодня».
        # Request-Day сам следит за свежестью (2 мин) и не создаёт шторма.
        try {
            $todayKey = (Get-Date).ToString('yyyy-MM-dd')
            $hasToday = ($null -ne $script:histCache) -and $script:histCache.ContainsKey($todayKey)
            if (-not $hasToday) { Request-Day $todayKey }
        } catch { }

        Pump-History

        Read-Serial



        Log-AlertConfig

        Check-ProtAlerts

        Check-Alerts

        Maybe-DailyReport



        $now = Get-Date

        if (($now - $script:lastUi).TotalMilliseconds -ge 450) {

            $script:lastUi = $now

            Update-Header

            Update-Cards

            Update-Footer

            if ($pgScreen.Visible) { Update-EnergyChart $chartScreen (Get-Date).ToString('yyyy-MM-dd') }

            if ((($now - $script:lastDev).TotalSeconds) -ge 3) {

                $script:lastDev = $now

                Update-Device

            }

        }

        Pump-ChartRange
        Pump-RangeLoad

        # P2.1: предыдущий период для «сравнения периодов» — тот же асинхронный
        # контур, что и основная загрузка /range.
        Pump-CmpLoad

        # P2.5b: комнатный климат за период (/climatehist) — один запрос на
        # смену периода, ответ забираем здесь и кэшируем по from/to.
        Pump-ClimateLoad


        Pump-Terminal

        # Архив и диагностика (P1.4/P1.5): прогресс снимка, теста,
        # статус задачи schtasks и обезличенный отчёт — без блокировки UI.
        Pump-Archive

        # История: сегодня просим, только когда оно открыто — иначе плата
        # читает CSV по 12–65 с, и нужный день ждёт в очереди.
        $todayStr = $now.ToString('yyyy-MM-dd')
        $yestStr  = $now.AddDays(-1).ToString('yyyy-MM-dd')

        if ($script:histSel -eq $todayStr) { Request-Day $todayStr }

        # Вчера — один раз: для линии «вчера» и мгновенного открытия вкладки.
        if (-not $script:histCache.ContainsKey($yestStr)) { Request-Day $yestStr }

        if ($script:histSel -ne $todayStr -and $script:histSel -ne $yestStr) { Request-Day $script:histSel }

        # ------------------------------------------- плата не отвечает 30 с

        # Мастер при этом НЕ открываем: если плата уже настроена (setupDone=true

        # и есть boardIp), он доступен только кнопкой на вкладке «Устройство».

        # Автозапуск мастера — только когда настроек ещё нет.

        if (-not $script:autoWizAsked -and -not $script:wizOpen -and -not $script:W.busy -and

            $script:uiReady -and ($script:boardBad -ge 3) -and -not (Board-Online) -and

            (((Get-Date) - $script:appStart).TotalSeconds -gt 30)) {

            $script:autoWizAsked = $true

            if (Test-CfgReady) {

                $bip = Get-CfgBoardIp

                Add-Log ('плата не отвечает: ' + $bip + ' — настройки уже есть, мастер не нужен') 'warn'

                Add-Log '  проверьте питание и Wi-Fi · «Мастер настройки» — вкладка «Устройство»' 'warn'

            } else {

                Add-Log 'плата не отвечает, адрес ещё не настроен — «Мастер настройки» на вкладке «Устройство»' 'warn'

            }

        }



        Log-Beats

    } catch {

        Write-AppLog ('ошибка в таймере: ' + $_.Exception.Message)

    }

}



# ============================================================================

#  ПРОШИВКА

# ============================================================================

$flashTimer = New-Object System.Windows.Forms.Timer

$flashTimer.Interval = 400

$flashTimer.Add_Tick({

    if (Test-Path $script:FlashLog) {

        try {

            $all = Get-Content $script:FlashLog -Raw -Encoding UTF8 -ErrorAction SilentlyContinue

            if ($null -ne $all -and $all.Length -gt $script:flashPos) {

                $new = $all.Substring($script:flashPos)

                $script:flashPos = $all.Length

                $txtFlashLog.AppendText($new)

                $txtFlashLog.SelectionStart = $txtFlashLog.TextLength

                $txtFlashLog.ScrollToCaret()

            }

        } catch { }

    }



    if ($script:job -and $script:job.State -ne 'Running') {

        $flashTimer.Stop()

        $okFlash = ($script:job.State -eq 'Completed') -and ($script:job.ChildJobs[0].JobStateInfo.State -eq 'Completed')

        Set-Lbl $lblFlashState $(if ($okFlash) { 'готово' } else { 'ошибка — смотри журнал сборки' })

        $lblFlashState.ForeColor = if ($okFlash) { C '#63D68B' } else { C '#E85D5D' }

        $btnFlash.Enabled = $true

        $btnCompile.Enabled = $true

        $btnOta.Enabled = $true
        $script:usbFlashBusy=$false
        $script:otaQuietUntil=(Get-Date).AddSeconds(60)

        $script:job = $null

        Start-Sleep -Milliseconds 400

        Connect-Serial

    }

})



function Get-SketchInfo([string]$sketch) {

    # что именно собираемся прошивать: имя .ino и FW_VER из него

    $r = @{ ok = $false; ino = ''; name = ''; fw = ''; label = ''; msg = '' }

    if (!(Test-Path $sketch)) {

        $r.msg = "Папка скетча не найдена:`n$sketch"

        return $r

    }

    $inos = @(Get-ChildItem -Path $sketch -Filter '*.ino' -File -ErrorAction SilentlyContinue)

    if ($inos.Count -eq 0) {

        $r.msg = "В папке нет ни одного .ino:`n$sketch"

        return $r

    }

    $leaf = Split-Path $sketch -Leaf

    $main = $inos | Where-Object { $_.BaseName -eq $leaf } | Select-Object -First 1

    if ($null -eq $main) { $main = $inos | Select-Object -First 1 }

    $r.ino = $main.FullName

    $r.name = $main.BaseName

    try {

        $t = Get-Content $main.FullName -Raw -Encoding UTF8

        $m = [regex]::Match($t, 'FW_VER\s*=\s*"([^"]+)"')

        if ($m.Success) { $r.fw = $m.Groups[1].Value }

    } catch { }

    if ($main.BaseName -eq 'cydv4') {

        $r.label = 'CYD-' + $(if ($r.fw) { $r.fw } else { '?' })

    } elseif ($r.fw) {

        $r.label = $main.BaseName + ' ' + $r.fw

    } else {

        $r.label = $main.BaseName

    }

    if ($main.BaseName -ne $leaf) {

        $r.msg = "В папке нет $leaf.ino (нашёл $($main.Name))"

    }

    $r.ok = $true

    return $r

}



function Start-Flash($doUpload) {

    if($script:otaProc -or $script:usbFlashBusy){return}

    $sketch = $txtSketch.Text.Trim()

    if (!(Test-Path $sketch)) { [System.Windows.Forms.MessageBox]::Show('Папка скетча не найдена', 'Ошибка'); return }

    if (!(Test-Path $script:ArduinoCli)) { [System.Windows.Forms.MessageBox]::Show("Не найден arduino-cli:`n$($script:ArduinoCli)", 'Ошибка'); return }



    $fqbn = $txtFqbn.Text.Trim()

    if ($fqbn -notmatch '^esp32:esp32:') {

        [System.Windows.Forms.MessageBox]::Show("Неверный FQBN: $fqbn`nНужен, например:`nesp32:esp32:esp32:PartitionScheme=min_spiffs", 'Ошибка')

        return

    }



    # проверка перед прошивкой: что за прошивка и откуда

    $info = Get-SketchInfo $sketch

    if (-not $info.ok) { [System.Windows.Forms.MessageBox]::Show($info.msg, 'Ошибка'); return }

    $what = 'Прошиваю: ' + $info.label + ' из ' + $sketch

    if ($info.msg) { $what = $what + '  ! ' + $info.msg }

    if ($info.ino -notmatch 'cydv4\.ino$' -and $sketch -match 'cydv4$') {

        [System.Windows.Forms.MessageBox]::Show("В папке $sketch нет cydv4.ino — проверьте путь.", 'Внимание')

    }



    Disconnect-Serial

    $script:cfg.sketch = $sketch

    $script:cfg.fqbn = $fqbn

    Save-Cfg



    $txtFlashLog.Text = ''

    $script:flashPos = 0

    if (Test-Path $script:FlashLog) { Remove-Item $script:FlashLog -Force -ErrorAction SilentlyContinue }



    $btnFlash.Enabled = $false

    $btnCompile.Enabled = $false

    Set-Lbl $lblFlashState $what

    $lblFlashState.ForeColor = C '#F2B44B'

    Add-Log $what 'warn'

    if ($info.msg) { Add-Log ('внимание: ' + $info.msg) 'warn' }

    Add-Log ('FQBN: ' + $fqbn)



    $buildPath = Join-Path $env:TEMP ('dommon-usb-'+[Guid]::NewGuid().ToString('N'))
    $script:usbFlashBusy=$true
    $btnOta.Enabled=$false



    try { $script:job = Start-Job -ScriptBlock {

        param($cli, $libs, $sketch, $buildPath, $doUpload, $com, $log, $fqbn, $what)



        function Emit($t) { Add-Content -Path $log -Value $t -Encoding UTF8 }



        Emit '=== СБОРКА ==='

        Emit $what

        Emit "скетч: $sketch"

        Emit "fqbn:  $fqbn"

        $generator=Join-Path $sketch 'web\build_page.py'
        if(Test-Path -LiteralPath $generator){
            $generated=& python $generator 2>&1
            foreach($line in $generated){Emit ([string]$line)}
            if($LASTEXITCODE -ne 0){throw 'Web asset generation failed'}
        }

        $args1 = @('compile', '--fqbn', $fqbn, '--libraries', $libs,

                   '--build-property', 'build.extra_flags=-DESP32 -DCORE_DEBUG_LEVEL=0',

                   '--build-path', $buildPath)
        $extra=Join-Path $env:USERPROFILE 'OneDrive\Документы\Arduino\libraries'
        if(Test-Path -LiteralPath $extra){$args1+=@('--libraries',$extra)}
        $args1+=$sketch

        $out = & $cli @args1 2>&1

        foreach ($l in $out) { Emit ([string]$l) }

        if ($LASTEXITCODE -ne 0) { Emit ''; Emit "=== СБОРКА НЕ УДАЛАСЬ (код $LASTEXITCODE) ==="; throw 'USB compilation failed' }



        Emit ''

        Emit '=== СБОРКА УСПЕШНА ==='



        if ($doUpload) {

            Emit ''

            Emit "=== ЗАЛИВКА В $com ==="

            $args2 = @('upload', '-p', $com, '--fqbn', $fqbn,

                       '--upload-property', 'upload.speed=921600',

                       '--input-dir', $buildPath, $sketch)

            $out2 = & $cli @args2 2>&1

            foreach ($l in $out2) { Emit ([string]$l) }

            if ($LASTEXITCODE -ne 0) { Emit ''; Emit "=== ЗАЛИВКА НЕ УДАЛАСЬ (код $LASTEXITCODE) ==="; throw 'USB upload failed' }

            Emit ''

            Emit '=== ЗАЛИВКА УСПЕШНА ==='

        }

    } -ArgumentList $script:ArduinoCli, $script:LibsDir, $sketch, $buildPath, $doUpload, $script:cfg.comPort, $script:FlashLog, $fqbn, $what
    } catch {
        $script:usbFlashBusy=$false;$btnFlash.Enabled=$true;$btnCompile.Enabled=$true;$btnOta.Enabled=$true
        Add-Log ('не удалось запустить сборку: '+$_.Exception.Message) 'err'
        Connect-Serial;return
    }



    $flashTimer.Start()

}



# ============================================================================

#  РАСКЛАДКА

# ============================================================================

function Layout-Header {
    if (-not $script:uiReady) { return }
    $w = $top.Width
    $x = $w - 14
    foreach ($b in @($script:btnTheme, $btnSerial, $btnReboot, $btnPult, $btnSnd)) {
        if ($null -eq $b) { continue }
        $b.Location = New-Object System.Drawing.Point(($x - $b.Width), 12)
        $x -= ($b.Width + 8)
    }
    if ($null -ne $script:trkBright) {
        $script:trkBright.Location = New-Object System.Drawing.Point(($w - 196), 50)
    }
    if ($null -ne $script:lblBright) {
        $script:lblBright.Location = New-Object System.Drawing.Point(($w - 58), 56)
    }
    if ($null -ne $lblFwChip) { $lblFwChip.Location = New-Object System.Drawing.Point(200, 48) }
    if ($null -ne $lblFresh) { $lblFresh.Location = New-Object System.Drawing.Point(326, 50) }
    if ($null -ne $script:lblClimate) { $script:lblClimate.Location = New-Object System.Drawing.Point(428, 51) }
    if ($null -ne $script:dotCom) { $script:dotCom.Location = New-Object System.Drawing.Point(580, 55) }
    if ($null -ne $lblCom) { $lblCom.Location = New-Object System.Drawing.Point(594, 47) }

    # узкое окно: шильдик прошивки уступает место статусу связи
    if ($null -ne $lblFwChip) { $lblFwChip.Visible = ($x -gt 300) }

    $lblFootRight.Size = New-Object System.Drawing.Size(($w - 420), 18)
    $lblFootRight.Location = New-Object System.Drawing.Point(400, 5)

    # Вкладка «Архив» — 8-я. На узком окне поджимаем кнопки навигации,
    # чтобы все вкладки оставались доступны; при ширине по умолчанию
    # позиции прежние (10, 146, 282, 418, 554, 690, 826, 962).
    if ($null -ne $script:navBtn -and $script:navBtn.Count -gt 0 -and $null -ne $navBar -and $null -ne $script:navOrder) {
        $n = $script:navOrder.Count
        $gap = 4
        $bw = 132
        $need = 10 + $n * $bw + ($n - 1) * $gap + 10
        if ($navBar.Width -lt $need) {
            $bw = [int][Math]::Floor(($navBar.Width - 20 - ($n - 1) * $gap) / $n)
            if ($bw -lt 92) { $bw = 92 }
        }
        $nx = 10
        foreach ($k in @($script:navOrder)) {
            $b = $script:navBtn[$k]
            if ($null -eq $b) { continue }
            if ($b.Width -ne $bw) { $b.Width = $bw }
            $b.Location = New-Object System.Drawing.Point($nx, 3)
            $nx += ($bw + $gap)
        }
    }
}



function Layout-All {

    Layout-Header

    Layout-Screen

    Layout-History

    Layout-Device

    Layout-Flash

    Layout-Log

    Layout-Term

    Layout-Arch

}



# ============================================================================

#  ТЕМЫ: применение скина на лету (без перезапуска)

# ============================================================================

function Map-Hex([string]$h, $map) {
    if ([string]::IsNullOrEmpty($h)) { return $null }
    $u = $h.ToUpper()
    if ($map.ContainsKey($u)) { return $map[$u] }
    return $null
}

function Recolor-Ctl($c, $map) {
    if ($null -eq $c) { return }
    try {
        # сначала дети: их наследуемый цвет ещё «старый», потом родитель
        foreach ($ch in @($c.Controls)) { Recolor-Ctl $ch $map }

        if (-not $c.BackColor.IsEmpty) {
            $u = (HexOf $c.BackColor).ToUpper()
            if ($map.ContainsKey($u)) { $c.BackColor = C $map[$u] }
        }
        if (-not $c.ForeColor.IsEmpty) {
            $v = (HexOf $c.ForeColor).ToUpper()
            if ($map.ContainsKey($v)) { $c.ForeColor = C $map[$v] }
        }

        if ($c -is [System.Windows.Forms.Button]) {
            if ($c.Tag -is [string] -and ([string]$c.Tag) -match '^#[0-9A-Fa-f]{6}$') {
                $tb = ([string]$c.Tag).ToUpper()
                if ($map.ContainsKey($tb)) { $c.Tag = $map[$tb] }
                try { $c.FlatAppearance.MouseOverBackColor = C (Get-HoverC ([string]$c.Tag)) } catch { }
                try { $c.FlatAppearance.MouseDownBackColor = C (Get-HoverC ([string]$c.Tag)) } catch { }
            } else {
                $mo = (HexOf $c.FlatAppearance.MouseOverBackColor).ToUpper()
                if ($map.ContainsKey($mo)) { $c.FlatAppearance.MouseOverBackColor = C $map[$mo] }
            }
        }

        if ($c -is [System.Windows.Forms.DataGridView]) {
            $bgm = Map-Hex (HexOf $c.BackgroundColor) $map
            if ($bgm) { $c.BackgroundColor = C $bgm }
            $gm = Map-Hex (HexOf $c.GridColor) $map
            if ($gm) { $c.GridColor = C $gm }
            $s1 = $c.ColumnHeadersDefaultCellStyle
            $m1 = Map-Hex (HexOf $s1.BackColor) $map
            if ($m1) { $s1.BackColor = C $m1 }
            $m2 = Map-Hex (HexOf $s1.ForeColor) $map
            if ($m2) { $s1.ForeColor = C $m2 }
            $m3 = Map-Hex (HexOf $s1.SelectionBackColor) $map
            if ($m3) { $s1.SelectionBackColor = C $m3 }
            $d1 = $c.DefaultCellStyle
            $n1 = Map-Hex (HexOf $d1.BackColor) $map
            if ($n1) { $d1.BackColor = C $n1 }
            $n2 = Map-Hex (HexOf $d1.ForeColor) $map
            if ($n2) { $d1.ForeColor = C $n2 }
            $n3 = Map-Hex (HexOf $d1.SelectionBackColor) $map
            if ($n3) { $d1.SelectionBackColor = C $n3 }
            $n4 = Map-Hex (HexOf $d1.SelectionForeColor) $map
            if ($n4) { $d1.SelectionForeColor = C $n4 }
            $a1 = $c.AlternatingRowsDefaultCellStyle
            $k1 = Map-Hex (HexOf $a1.BackColor) $map
            if ($k1) { $a1.BackColor = C $k1 }
        }

    } catch { }
}

function Apply-Theme([string]$name) {
    try {
        $old = $script:CLR
        $new = Get-Skin $name
        $map = @{}

        # сначала «ручные» цвета из кода…
        foreach ($k in @($script:LegacyRole.Keys)) {
            $role = [string]$script:LegacyRole[$k]
            if ($new.Contains($role)) { $map[$k.ToUpper()] = [string]$new[$role] }
        }
        # …потом текущая палитра — она главнее; роли в порядке важности,
        # иначе в ЧБ-теме белый цвет «схлопывается» в один (последний) токен
        $prio = @('card', 'bg', 'panel', 'text', 'dim', 'dim2', 'line', 'accent', 'header', 'nav',
                  'footer', 'card2', 'ok', 'warn', 'err', 'lcdBg', 'lcdFg', 'temp', 'power',
                  'money', 'good', 'bad', 'btnA', 'btnN', 'btnD', 'btnOk', 'shadow')
        $roleMap = @{}
        foreach ($role in $prio) {
            if (-not $new.Contains($role) -or -not $old.Contains($role)) { continue }
            $ov = [string]$old[$role]
            if ($ov -notmatch '^#[0-9A-Fa-f]{6}$') { continue }
            $key = $ov.ToUpper()
            if (-not $roleMap.ContainsKey($key)) { $roleMap[$key] = [string]$new[$role] }
        }
        foreach ($key in @($roleMap.Keys)) { $map[$key] = $roleMap[$key] }

        $form.SuspendLayout()
        try { Recolor-Ctl $form $map } finally { $form.ResumeLayout($false) }

        $script:CLR = $new
        $script:cfg.theme = $new.skin

        $script:draw = New-Draw

        $form.BackColor = C $new.bg
        $form.ForeColor = C $new.text
        try { $chartScreen.BackColor = C $new.bg } catch { }
        try { $chartHist.BackColor = C $new.bg } catch { }

        try { Update-Header } catch { }
        try { Update-ComBadge } catch { }
        try { $form.Invalidate($true) } catch { }
        try { $chartScreen.Invalidate() } catch { }
        try { Update-EnergyChart $chartHist $script:histSel } catch { }
        try { Enable-DoubleBufferAll $form } catch { }

        if ($null -ne $script:tipMain -and $null -ne $script:btnTheme) {
            $script:tipMain.SetToolTip($script:btnTheme, ('тема: ' + $script:SkinRu[$new.skin] + ' · клик — следующая'))
        }

        Save-Cfg
        Add-Log ('тема интерфейса: ' + $script:SkinRu[$new.skin]) 'ok'
    } catch {
        Write-AppLog ('тема: ошибка ' + $_.Exception.Message)
    }
}

function Switch-Theme {
    try {
        $cur = [string]$script:cfg.theme
        $idx = [Array]::IndexOf($script:SkinOrder, $cur)
        if ($idx -lt 0) { $idx = 1 }
        $next = $script:SkinOrder[($idx + 1) % $script:SkinOrder.Count]
        Apply-Theme $next
    } catch {
        Write-AppLog ('тема: ' + $_.Exception.Message)
    }
}

# ============================================================================

#  ИСТОРИЯ — ПЕРЕКЛЮЧЕНИЕ ДНЕЙ

# ============================================================================

function Step-Day([int]$delta) {

    $list = @($script:days)

    if ($list.Count -eq 0) {

        Set-HistDay ((Get-Date).AddDays(-1).ToString('yyyy-MM-dd'))  # обычно вчера уже в кэше

        Request-Day $script:histSel

        Update-HistoryView

        return

    }

    $idx = $list.IndexOf($script:histSel)

    if ($idx -lt 0) { $idx = 0 } else { $idx += $delta }

    if ($idx -lt 0) { $idx = 0 }

    if ($idx -ge $list.Count) { $idx = $list.Count - 1 }

    Set-HistDay ([string]$list[$idx])

    Request-Day $script:histSel

    Update-HistoryView

}



# ============================================================================

#  КНОПКИ И СОБЫТИЯ

# ============================================================================

$btnPult.Add_Click({

    # HTTP: работает без COM-порта. Читаем /data, чтобы знать текущее состояние.

    try {

        $j = $script:http.GetStringAsync("$($script:cfg.board)/data").Result | ConvertFrom-Json

        $on = ($j.disp -eq 1)

        $cmd = if ($on) { 'on=0' } else { 'on=1' }

        $null = $script:http.GetStringAsync("$($script:cfg.board)/disp?$cmd").Result

        Add-Log ("кнопка «Экран платы»: подсветка " + $(if ($on) { 'выключена' } else { 'включена' }))

        if ($cmd -eq 'on=1' -and $null -ne $script:trkBright) {

            $v = [int]$script:trkBright.Value

            $null = $script:http.GetStringAsync("$($script:cfg.board)/disp?bright=$v").Result

        }

    } catch { Add-Log 'кнопка «Экран платы»: плата не ответила' 'warn' }

})



$btnReboot.Add_Click({

    $r = [System.Windows.Forms.MessageBox]::Show('Перезагрузить плату?', 'Подтверждение', 'YesNo', 'Question')

    if ($r -ne 'Yes') { return }

    # Раньше команда уходила только по serial, а плата работает по Wi-Fi —
    # кнопка молча ничего не делала. Теперь сначала HTTP, serial — запасной путь.
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.1.51' }
    $ok = $false
    try {
        $null = Invoke-RestUi ('http://' + $ip + '/reboot') 15
        $ok = $true
        Add-Log ('перезагрузка платы по HTTP (' + $ip + ')')
    } catch {
        Add-Log ('HTTP-перезагрузка не удалась: ' + $_.Exception.Message) 'warn'
    }
    if (-not $ok) {
        if (Send-Serial 'reboot') { Add-Log 'команда: перезагрузка платы (serial)' }
        else { Add-Log 'перезагрузка не отправлена: нет ни HTTP, ни порта' 'err' }
    }

})



$btnSerial.Add_Click({

    if ($script:port -and $script:port.IsOpen) { Disconnect-Serial; Add-Log 'порт закрыт' }

    else { Connect-Serial -Force | Out-Null }

})



$null = New-NavButton 'screen' 'Экран'      10

$null = New-NavButton 'dev'    'Устройство' 146

$null = New-NavButton 'hist'   'История'    282

$null = New-NavButton 'flash'  'Прошивка'   418

$null = New-NavButton 'log'    'Журнал'     554

$null = New-NavButton 'prot'   'Защита'     690

$null = New-NavButton 'term'   'Терминал'   826

$null = New-NavButton 'arch'   'Архив'      962

$script:navOrder = @('screen', 'dev', 'hist', 'flash', 'log', 'prot', 'term', 'arch')



$script:navBtn['screen'].Add_Click({ Show-Page 'screen'; Add-Log 'вкладка: Экран' })

$script:navBtn['dev'].Add_Click({ Show-Page 'dev'; Update-Device; Add-Log 'вкладка: Устройство' })

$script:navBtn['hist'].Add_Click({ Show-Page 'hist'; Request-Day $script:histSel; Update-HistoryView; Add-Log 'вкладка: История' })

$script:navBtn['flash'].Add_Click({ Show-Page 'flash'; Add-Log 'вкладка: Прошивка' })

$script:navBtn['log'].Add_Click({ Show-Page 'log'; Add-Log 'вкладка: Журнал' })

$script:navBtn['prot'].Add_Click({ Show-Page 'prot'; Add-Log 'вкладка: Защита' })

$script:navBtn['term'].Add_Click({ Show-Page 'term'; Refresh-Terminal; Add-Log 'вкладка: Терминал' })

$script:navBtn['arch'].Add_Click({ Show-Page 'arch'; Refresh-Archive; Add-Log 'вкладка: Архив' })



$btnPrev.Add_Click({ Step-Day 1; Add-Log ('история: предыдущий день — ' + $script:histSel) })

$btnNext.Add_Click({ Step-Day -1; Add-Log ('история: следующий день — ' + $script:histSel) })

$btnToday.Add_Click({

    Set-HistDay ((Get-Date).ToString('yyyy-MM-dd'))

    Request-Day $script:histSel

    Update-HistoryView

    Add-Log 'история: выбран сегодняшний день'

})

$btnReload.Add_Click({

    if ($script:histCache.ContainsKey($script:histSel)) { $script:histCache.Remove($script:histSel) }

    if ($script:histAt.ContainsKey($script:histSel)) { $script:histAt.Remove($script:histSel) }

    Request-Day $script:histSel

    Add-Log ('запрошена история за ' + $script:histSel)

})



$btnHistCsv.Add_Click({

    $date = $script:histSel

    if (-not $script:histCache.ContainsKey($date)) { Add-Log 'история за этот день ещё не загружена' 'warn'; return }

    $dlg = New-Object System.Windows.Forms.SaveFileDialog

    $dlg.Filter = 'CSV файл (*.csv)|*.csv'

    $dlg.FileName = "energy-$date.csv"

    if ($dlg.ShowDialog() -ne 'OK') { return }

    $lines = @('дата;час;кВтч;средняя_Вт;пик_Вт;напряжение_В;температура;записей')

    foreach ($r in @($script:histCache[$date])) {

        $lines += ('{0};{1};{2};{3};{4};{5};{6};{7}' -f $date, [int]$r.h, $r.kwh, $r.p, $r.pmax, $r.v, $r.t, $r.n)

    }

    [System.IO.File]::WriteAllLines($dlg.FileName, $lines, [System.Text.Encoding]::UTF8)

    Add-Log ('история сохранена: ' + $dlg.FileName) 'ok'

})



$btnHistTg.Add_Click({

    # путь: из настроек, иначе ищем в известных местах

    $cand = @()

    if ($script:cfg.tgScript) { $cand += [string]$script:cfg.tgScript }

    $cand += (Join-Path $script:AppDir 'energy-telegram.ps1')

    $cand += 'D:\DomEnergy\timelapse\watchdog\energy-telegram.ps1'

    $script:TgScript = $cand | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1

    if (-not $script:TgScript) {

        Add-Log 'не найден energy-telegram.ps1 — укажи путь в settings.json (tgScript)' 'err'

        return

    }

    Add-Log ('telegram-скрипт: ' + $script:TgScript)

    Add-Log 'отправляю отчёт в Telegram…'

    try {

        Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @(

            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:TgScript, '-Mode', 'report')

        Add-Log 'отчёт запущен — результат в energy-telegram.log' 'ok'

    } catch {

        Add-Log ('ошибка отправки: ' + $_.Exception.Message) 'err'

    }

})



$btnPortScan.Add_Click({ Update-PortList; Add-Log 'список портов обновлён' })

$btnPortApply.Add_Click({

    if (-not $cmbPort.SelectedItem) { Add-Log 'порт не выбран' 'warn'; return }

    $newPort = [string]$cmbPort.SelectedItem

    if ($newPort -eq $script:cfg.comPort -and $script:port -and $script:port.IsOpen) { Add-Log "порт $newPort уже открыт"; return }

    Disconnect-Serial

    Start-Sleep -Milliseconds 300

    $script:cfg.comPort = $newPort

    Save-Cfg

    Add-Log "порт изменён на $newPort"

    Connect-Serial | Out-Null

    Update-Device

})



$btnBoardApply.Add_Click({

    $addr = $txtBoard.Text.Trim().TrimEnd('/')

    if ($addr -notmatch '^https?://') { $addr = 'http://' + $addr }

    if ($addr -notmatch '^https?://[^\s]+$') { Add-Log 'неверный адрес платы' 'err'; return }

    $script:cfg.board = $addr

    Save-Cfg

    $script:board = $null

    $script:boardAt = [datetime]::MinValue

    $script:boardErr = ''

    $script:boardBad = 0

    $script:daysAt = [datetime]::MinValue

    $script:histCache = @{}

    $script:histAt = @{}

    $script:histPending = $null; $script:histRequests = @{}; $script:histFirstAt = @{}

    Set-Lbl $lblSub ('ESP32 · ' + $addr.Replace('http://', '') + ' · обновление 5 с')

    # другой адрес — другая плата: старые запросы истории/периода отменяем,
    # выбранный час сбрасываем (он относился к прежней плате)
    Reset-HistSelection $script:histSel
    Cancel-HistRequests -KeepDate ''
    Stop-RangeLoad


    Add-Log ('адрес платы: ' + $addr) 'ok'

    Request-Day $script:histSel

})



function Update-PortList {

    $ports = @(Get-PortList)

    $cmbPort.Items.Clear()

    foreach ($p in $ports) { [void]$cmbPort.Items.Add($p) }

    if ($ports.Count -gt 0) {

        $idx = $ports.IndexOf($script:cfg.comPort)

        if ($idx -ge 0) { $cmbPort.SelectedIndex = $idx } else { $cmbPort.SelectedIndex = 0 }

    }

}



$btnBrowse.Add_Click({

    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog

    $dlg.SelectedPath = $txtSketch.Text

    if ($dlg.ShowDialog() -eq 'OK') { $txtSketch.Text = $dlg.SelectedPath }

})

$btnSched = New-Btn $pgDevice 'Планировщик реле' '#2E4A6B' '#FFFFFF' 168 30
$btnSched.Add_Click({ Show-Scheduler })
$btnFlash.Add_Click({ Start-Flash $true })

$btnCompile.Add_Click({ Start-Flash $false })

function Pump-Ota {
    if (-not $script:otaProc) { return }
    foreach ($stream in @('out','err')) {
        $path = $script:otaFiles[$stream]
        if (Test-Path -LiteralPath $path) {
            $lines = @(Get-Content -LiteralPath $path -Encoding UTF8 -ErrorAction SilentlyContinue)
            for ($i=$script:otaRead[$stream]; $i -lt $lines.Count; $i++) {
                if ($lines[$i].Trim()) { Add-Log $lines[$i] $(if($stream -eq 'err'){'warn'}else{'info'}) }
            }
            $script:otaRead[$stream] = $lines.Count
        }
    }
    if (Test-Path -LiteralPath $script:otaFiles.progress) {
        try {
            $stream=[IO.File]::Open($script:otaFiles.progress,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $reader=New-Object IO.StreamReader($stream)
            try { $progress=$reader.ReadToEnd() } finally { $reader.Dispose() }
            $matches = [regex]::Matches($progress,'([0-9.]+)%')
            if ($matches.Count) { $lblFlashState.Text = 'Передача: ' + $matches[$matches.Count-1].Value }
        } catch { } # curl may hold the progress file exclusively for a moment
    }
    if (-not $script:otaProc.HasExited) { return }
    $script:otaProc.WaitForExit()
    $code = $script:otaProc.ExitCode
    $script:otaProc.Dispose(); $script:otaProc = $null
    $script:otaQuietUntil = (Get-Date).AddSeconds(60)
    $btnOta.Enabled = $true; $btnFlash.Enabled = $true; $btnCompile.Enabled = $true
    $lblFlashState.Text = if($code -eq 0){'Прошивка и сборка на плате проверены'}else{'Ошибка OTA · см. журнал'}
    Add-Log $lblFlashState.Text $(if($code -eq 0){'ok'}else{'err'})
    $script:boardBad = 0
}

$btnOta.Add_Click({

    $otaScript = Join-Path $script:AppDir 'flash-http.ps1'

    if (-not (Test-Path $otaScript)) { Add-Log ('не найден flash-http.ps1: ' + $otaScript) 'err'; return }

    $ip = $script:cfg.boardIp

    if (-not $ip) { $ip = '192.168.1.51' }

    Add-Log ('прошивка по Wi-Fi: ' + $ip + ' — собираю и отправляю…')

    Add-Log ('скрипт: ' + $otaScript)

    if ($script:otaProc -or $script:usbFlashBusy -or $script:W.busy) { return }
    $sketch = $txtSketch.Text.Trim()
    $fqbn = $txtFqbn.Text.Trim()
    if (-not (Test-Path -LiteralPath (Join-Path $sketch 'cydv4.ino'))) { Add-Log 'Выберите папку скетча cydv4' 'err'; return }
    $script:otaFiles = @{out=(Join-Path $script:LogDir 'ota-last.txt');err=(Join-Path $script:LogDir 'ota-last.err.txt');progress=(Join-Path $script:LogDir 'ota-progress.txt')}
    $script:otaRead = @{out=0;err=0}
    foreach($path in $script:otaFiles.Values){[IO.File]::WriteAllText($path,'')}

    $btnOta.Enabled = $false
    $btnFlash.Enabled = $false; $btnCompile.Enabled = $false
    $lblFlashState.Text = 'Сборка выбранного скетча…'

    try {

        # Start-Process joins arguments on Windows: quote paths explicitly.
        foreach($value in @($otaScript,$sketch,$fqbn,$script:ArduinoCli,$script:otaFiles.progress)) {
            if ($value -match '["\r\n]') { throw 'Недопустимый символ в параметрах сборки' }
        }
        $script:otaProc = Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -PassThru -ArgumentList @(
            '-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$otaScript+'"'),'-Ip',$ip,
            '-Build','-Sketch',('"'+$sketch+'"'),'-Cli',('"'+$script:ArduinoCli+'"'),'-Fqbn',('"'+$fqbn+'"'),
            '-ProgressPath',('"'+$script:otaFiles.progress+'"')) -RedirectStandardOutput $script:otaFiles.out -RedirectStandardError $script:otaFiles.err
        $null = $script:otaProc.Handle # retain handle before process exits (Windows PowerShell 5.1)

    } catch {

        Add-Log ('ошибка прошивки по Wi-Fi: ' + $_.Exception.Message) 'err'

        $btnOta.Enabled = $true; $btnFlash.Enabled = $true; $btnCompile.Enabled = $true
    }

})

$btnClearLog.Add_Click({

    $script:txtLog.Text = ''

    Add-Log 'журнал очищен пользователем'

})



# ============================================================================

#  МАСТЕР ПЕРВОНАЧАЛЬНОЙ НАСТРОЙКИ

# ============================================================================

# ============================================================================

#  МАСТЕР ПЕРВОНАЧАЛЬНОЙ НАСТРОЙКИ

# ----------------------------------------------------------------------------

#  Пять шагов: поиск платы по COM -> версия и прошивка -> Wi-Fi -> проверка

#  по сети -> готово. Конфиг: %APPDATA%\DomMonitor\settings.json

#

#  Логика Wi-Fi: если ПК подключён по Wi-Fi — берём SSID текущей сети и

#  подставляем его; если по кабелю — списка сетей нет, просим ввести имя

#  сети вручную.

# ============================================================================



# esptool не зашит в код: разные версии Arduino IDE/CLI кладут его в разные

# папки, а профиль пользователя у каждого свой. Ищем в PATH, затем в пакетах

# Arduino15 (LOCALAPPDATA у Arduino IDE, APPDATA у установки для всех).

function Find-Esptool {

    foreach ($n in @('esptool.exe', 'esptool')) {

        $c = Get-Command $n -ErrorAction SilentlyContinue

        if ($c -and $c.Source) { return $c.Source }

    }

    foreach ($base in @(

            (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'),

            (Join-Path $env:APPDATA 'Arduino15\packages\esp32\tools\esptool_py'))) {

        if (!(Test-Path $base)) { continue }

        $hit = Get-ChildItem -Path $base -Filter 'esptool.exe' -Recurse -ErrorAction SilentlyContinue |

               Sort-Object FullName -Descending | Select-Object -First 1

        if ($hit) { return $hit.FullName }

    }

    return ''

}



$script:WizJson    = $script:CfgFile       # один файл: %APPDATA%\DomMonitor\settings.json

$script:WizSketch  = [string]$script:cfg.sketch

$script:WizImage   = Join-Path $script:ArchiveDir 'flash-dumps\cydv4-full.bin'

$script:WizEsptool = Find-Esptool

$script:W = @{}                 # состояние мастера

$script:W.step = 1

$script:W.busy = $false

$script:W.lines = @()

$script:wizOpen = $false

$script:autoWizAsked = $false

$script:appStart = Get-Date

# '' — обычный режим; '1' | 'cable' — заглушки для скриншотов без платы

$script:WizDemo = [string]$env:DOMMON_WIZ_DEMO



# --------------------------------------------------------------- конфиг JSON

# Читаем и пишем ОДИН файл — settings.json. Функции возвращают/пишут те же

# значения, что нужны мастеру (boardIp/port/setupDone), но живут в общей модели.

function Read-WizCfg {

    if (!(Test-Path $script:CfgFile)) { return $null }

    $h = @{

        boardIp   = [string]$script:cfg.boardIp

        boardPort = 80

        port      = [string]$script:cfg.port

        setupDone = ($script:cfg.setupDone -eq $true)

        wifi      = $script:cfg.wifi

        lastKnown = [string]$script:cfg.lastKnown

    }

    if ($h.boardIp -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { return $null }

    return $h

}



function Write-WizCfg([string]$ip, [string]$ssid, [string]$port) {

    try {

        $script:cfg.boardIp   = $ip

        $script:cfg.port      = $port

        $script:cfg.comPort   = $port

        $script:cfg.board     = 'http://' + $ip

        $script:cfg.setupDone = $true

        $script:cfg.wifi      = @{ ssid = $ssid }

        $script:cfg.lastKnown = (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss')

        Save-Cfg

        Write-AppLog ("мастер: настройки записаны $($script:CfgFile): ip=$ip ssid=$ssid port=$port")

        return $true

    } catch {

        Write-AppLog ('мастер: настройки не записались: ' + $_.Exception.Message)

        return $false

    }

}



# --------------------------------------------------------------- процессы

function Invoke-Native([string]$exe, [string[]]$argv, [int]$timeoutMs) {

    $res = @{ code = -1; out = ''; err = ''; text = ''; timedout = $false }

    if (!(Test-Path $exe)) { $res.err = "нет файла $exe"; $res.text = $res.err; return $res }

    try {

        $psi = New-Object System.Diagnostics.ProcessStartInfo

        $psi.FileName = $exe

        $psi.Arguments = (($argv | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }) -join ' ')

        $psi.UseShellExecute = $false

        $psi.RedirectStandardOutput = $true

        $psi.RedirectStandardError = $true

        $psi.CreateNoWindow = $true

        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8

        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

        $p = New-Object System.Diagnostics.Process

        $p.StartInfo = $psi

        [void]$p.Start()

        $so = $p.StandardOutput.ReadToEndAsync()

        $se = $p.StandardError.ReadToEndAsync()

        $t0 = Get-Date

        while (-not $p.HasExited) {

            [System.Windows.Forms.Application]::DoEvents()

            Start-Sleep -Milliseconds 60

            if ($timeoutMs -gt 0 -and ((Get-Date) - $t0).TotalMilliseconds -gt $timeoutMs) {

                $res.timedout = $true

                try { $p.Kill() } catch { }

                break

            }

        }

        [void]$p.WaitForExit(1500)

        try { $res.out = $so.Result } catch { }

        try { $res.err = $se.Result } catch { }

        try { $res.code = $p.ExitCode } catch { }

        $res.text = ($res.out + "`n" + $res.err)

    } catch {

        $res.err = $_.Exception.Message

        $res.text = $res.err

    }

    return $res

}



# --------------------------------------------------------------- COM-порты

function Get-WizPorts {

    $all = @()

    try { $all = @([System.IO.Ports.SerialPort]::GetPortNames()) } catch { }

    if ($all.Count -eq 0) { try { $all = @(Get-PortList) } catch { } }

    return @($all | Sort-Object)

}



function Get-WizPortInfo([string]$port) {

    $r = @{ port = $port; vid = ''; pid = ''; desc = ''; kind = 'COM' }

    try {

        $rows = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |

                Where-Object { [string]$_.Name -match ('\b' + [regex]::Escape($port) + '\b') }

        foreach ($x in $rows) {

            $r.desc = [string]$x.Name

            $m = [regex]::Match([string]$x.DeviceID, 'VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})')

            if ($m.Success) {

                $r.vid = $m.Groups[1].Value.ToUpper()

                $r.pid = $m.Groups[2].Value.ToUpper()

            }

        }

    } catch { }

    if ($r.vid -eq '1A86') { $r.kind = 'CH340' }

    elseif ($r.vid -eq '303A') { $r.kind = 'нативный USB (ESP32-C3/S3)' }

    return $r

}



# --------------------------------------------------------------- serial

function Wiz-OpenPort([string]$port) {

    if ($script:WizDemo) {          # демо: порт не трогаем вообще

        Wiz-Log ('  (демо: порт ' + $port + ' не открываю)')

        $script:W.port = $port

        return $null

    }

    Wiz-ClosePort

    try {

        $sp = New-Object System.IO.Ports.SerialPort $port, 115200, 'None', 8, 'One'

        $sp.ReadTimeout = 400

        $sp.Encoding = [System.Text.Encoding]::UTF8     # плата пишет по-русски

        # DTR/RTS не поднимаем: на CH340 это держит плату в сбросе

        $sp.DtrEnable = $false

        $sp.RtsEnable = $false

        $sp.Open()

        Start-Sleep -Milliseconds 250

        try { $sp.DiscardInBuffer() } catch { }

        $script:W.sp = $sp

        return $sp

    } catch {

        Wiz-Log ('! порт ' + $port + ' не открылся: ' + $_.Exception.Message)

        $script:W.sp = $null

        return $null

    }

}



function Wiz-ClosePort {

    try { if ($script:W.sp -and $script:W.sp.IsOpen) { $script:W.sp.Close() } } catch { }

    $script:W.sp = $null

}



# Читает порт до совпадения с pattern (или таймаута). Строки пишет в консоль

# мастера и в $script:W.lines. Служебный шум ESP-IDF («E (1234) wifi:...»)

# не показываем, но проверять pattern по нему всё равно можно.

function Wiz-Pump([string]$pattern, [int]$timeoutMs) {

    $sp = $script:W.sp

    if (-not $sp) { return $false }

    $t0 = Get-Date

    $line = ''

    while (((Get-Date) - $t0).TotalMilliseconds -lt $timeoutMs) {

        $chunk = ''

        try { $chunk = $sp.ReadExisting() } catch { }

        if ($chunk) {

            $line += $chunk

            while ($line.Contains("`n")) {

                $i = $line.IndexOf("`n")

                $one = $line.Substring(0, $i).TrimEnd("`r")

                $line = $line.Substring($i + 1)

                if ($one.Trim()) {

                    $script:W.lines += $one

                    # служебный шум ESP-IDF и сырой JSON статуса в консоль не льём —

                    # статус разбирается и показывается короткими строками

                    if ($one -notmatch '^[EWID] \(\d+\) ' -and $one -notmatch '"t"\s*:\s*"status"') { Wiz-Log ('  ' + $one) }

                    if ($pattern -and ($one -match $pattern)) { return $true }

                }

            }

        }

        [System.Windows.Forms.Application]::DoEvents()

        Start-Sleep -Milliseconds 100

    }

    return $false

}



function Wiz-Find([string]$pattern) {

    foreach ($l in @($script:W.lines)) { if ($l -match $pattern) { return $true } }

    return $false

}



function Wiz-FindVal([string]$pattern) {

    $v = ''

    foreach ($l in @($script:W.lines)) {

        if ($l -match $pattern) { $v = $Matches[1] }

    }

    return $v

}



# Версия прошивки на плате: команда STATUS по serial

function Get-BoardFw([string]$port, [int]$timeoutMs = 8000) {

    if (-not (Wiz-OpenPort $port)) { return '' }

    try { $script:W.sp.Write("STATUS`n") } catch { }

    Wiz-Pump '"t"\s*:\s*"status"' $timeoutMs | Out-Null

    $fw = Wiz-FindVal '"fw"\s*:\s*"([^"]+)"'

    Wiz-ClosePort

    return $fw

}



# Разобранный ответ на STATUS: версия, IP, Wi-Fi. Открывает порт на несколько

# секунд и закрывает обратно — плату это не сбрасывает (в отличие от esptool).

function Get-WizBoardStatus([string]$port, [int]$timeoutMs = 6000) {

    $r = @{ fw = ''; ip = ''; wifi = $false; rssi = $null; sdRows = $null; raw = '' }

    $start = @($script:W.lines).Count

    if (-not (Wiz-OpenPort $port)) { return $null }

    try { $script:W.sp.Write("STATUS`n") } catch { }

    try { Wiz-Pump '"t"\s*:\s*"status"' $timeoutMs | Out-Null } catch { }

    Wiz-ClosePort

    $line = ''

    $all = @($script:W.lines)

    for ($i = $start; $i -lt $all.Count; $i++) {

        if ("$($all[$i])" -match '"t"\s*:\s*"status"') { $line = [string]$all[$i]; break }

    }

    if (-not $line) { return $r }

    $r.raw = $line

    $m = [regex]::Match($line, '"fw"\s*:\s*"([^"]+)"')

    if ($m.Success) { $r.fw = $m.Groups[1].Value }

    $m = [regex]::Match($line, '"ip"\s*:\s*"([^"]+)"')

    if ($m.Success) { $r.ip = $m.Groups[1].Value }

    $m = [regex]::Match($line, '"wifi"\s*:\s*(true|false)')

    if ($m.Success) { $r.wifi = ($m.Groups[1].Value -eq 'true') }

    $m = [regex]::Match($line, '"rssi"\s*:\s*(-?\d+)')

    if ($m.Success) { try { $r.rssi = [int]$m.Groups[1].Value } catch { } }

    $m = [regex]::Match($line, '"sdRows"\s*:\s*(\d+)')

    if ($m.Success) { try { $r.sdRows = [int]$m.Groups[1].Value } catch { } }

    return $r

}



# --------------------------------------------------------------- Wi-Fi ПК

function Get-WifiIface {

    if ($env:DOMMON_CABLE -eq '1') {

        # демонстрационный режим «ПК по кабелю» (проверка логики без выключения Wi-Fi)

        return @{ adapter = $false; wifi = $false; ssid = ''; state = ''; raw = '' }

    }

    $out = ''

    try { $out = (& netsh wlan show interfaces 2>&1) | Out-String } catch { $out = '' }

    $r = @{ adapter = $false; wifi = $false; ssid = ''; state = ''; raw = $out }

    if ($out -match '(?im)^\s*(Имя|Name)\s*:\s*\S') { $r.adapter = $true }

    $m = [regex]::Match($out, '(?im)^\s*SSID\s*:\s*(\S.*)$')

    if ($m.Success) { $r.ssid = $m.Groups[1].Value.Trim(); $r.wifi = $true }

    $m = [regex]::Match($out, '(?im)^\s*(Состояние|State)\s*:\s*(.+)$')

    if ($m.Success) { $r.state = $m.Groups[2].Value.Trim() }

    return $r

}



function Get-WifiNets {

    $out = ''

    try { $out = (& netsh wlan show networks mode=bssid 2>&1) | Out-String } catch { $out = '' }

    $list = New-Object System.Collections.ArrayList

    $cur = $null

    foreach ($ln in ($out -split "`r?`n")) {

        $m = [regex]::Match($ln, '^SSID\s+\d+\s*:\s*(.*)$')

        if ($m.Success) {

            if ($null -ne $cur) { [void]$list.Add($cur) }

            $cur = @{ ssid = $m.Groups[1].Value.Trim(); chans = @(); sigs = @() }

            continue

        }

        if ($null -eq $cur) { continue }

        $m = [regex]::Match($ln, '(?i)^\s*(Channel|Канал)\s*:\s*(\d+)')

        if ($m.Success) { $cur.chans += [int]$m.Groups[2].Value; continue }

        $m = [regex]::Match($ln, '(?i)^\s*(Signal|Сигнал)\s*:\s*(\d+)')

        if ($m.Success) { $cur.sigs += [int]$m.Groups[2].Value; continue }

    }

    if ($null -ne $cur) { [void]$list.Add($cur) }



    $res = New-Object System.Collections.ArrayList

    foreach ($n in $list) {

        if (-not $n.ssid) { continue }

        $ch24 = @($n.chans | Where-Object { $_ -ge 1 -and $_ -le 13 })

        $ch5  = @($n.chans | Where-Object { $_ -ge 32 })

        $pct = 0

        if (@($n.sigs).Count -gt 0) { $pct = ($n.sigs | Measure-Object -Maximum).Maximum }

        $dbm = [int]([math]::Round($pct / 2 - 100))

        [void]$res.Add([pscustomobject]@{

            ssid   = $n.ssid

            band24 = ($ch24.Count -gt 0)

            band5  = (($ch5.Count -gt 0) -and ($ch24.Count -eq 0))

            chans  = ($n.chans -join ',')

            pct    = $pct

            dbm    = $dbm

        })

    }

    return @($res | Sort-Object -Property @{ Expression = 'band24'; Descending = $true },

                                            @{ Expression = 'dbm'; Descending = $true })

}



function Get-WifiPass([string]$ssid) {

    if (!$ssid) { return '' }

    $out = ''

    try { $out = (& netsh wlan show profile "name=`"$ssid`"" key=clear 2>&1) | Out-String } catch { return '' }

    $m = [regex]::Match($out, '(?im)^\s*(Key Content|Содержимое ключа)\s*:\s*(.+)$')

    if ($m.Success) { return $m.Groups[2].Value.Trim() }

    return ''

}



# --------------------------------------------------------------- прошивка

function Get-WizAppBin {

    $cands = @()

    foreach ($d in @('build2', 'build')) {

        $p = Join-Path (Join-Path $script:WizSketch $d) 'cydv4.ino.bin'

        if (Test-Path $p) { $cands += (Get-Item $p) }

    }

    if ($cands.Count -eq 0) { return $null }

    return ($cands | Sort-Object LastWriteTime -Descending | Select-Object -First 1)

}



function Get-WizSrcVer {

    $f = Join-Path $script:WizSketch 'cydv4.ino'

    if (!(Test-Path $f)) { return '' }

    try {

        $t = Get-Content $f -Raw -Encoding UTF8

        $m = [regex]::Match($t, 'FW_VER\s*=\s*"([^"]+)"')

        if ($m.Success) { return 'CYD-' + $m.Groups[1].Value }

    } catch { }

    return ''

}



# Заливка через esptool. Вывод читаем построчно, проценты — в прогресс-бар.

function Invoke-WizFlash([string]$offset, [string]$image) {

    if ($script:WizDemo) {          # демо: esptool не запускается вообще

        Wiz-Log ('запуск: esptool --port ' + $script:W.port + ' --baud 460800 write_flash ' + $offset + ' <образ>')

        Wiz-Log '  (демо: прошивка не выполняется)'

        foreach ($pct in 0, 20, 40, 60, 80, 100) {

            Wiz-SetBar $pct

            $script:W.barLbl.Text = "$pct %"

            Wiz-Log "  прошивка: $pct %"

            [System.Windows.Forms.Application]::DoEvents()

            Start-Sleep -Milliseconds 250

        }

        Wiz-Log 'прошивка: OK'

        return $true

    }

    if (!$script:WizEsptool -or !(Test-Path $script:WizEsptool)) {

        Wiz-Log '! esptool не найден — установите Arduino CLI (arduino-cli) или добавьте esptool в PATH'

        return $false

    }

    if (!(Test-Path $image)) { Wiz-Log "! не найден образ: $image"; return $false }

    $port = $script:W.port

    if (!$port) { $port = $script:cfg.comPort }

    Wiz-Log ('запуск: esptool --port ' + $port + ' --baud 460800 write_flash ' + $offset + ' <образ>')

    Wiz-Log ('образ: ' + $image)

    Wiz-Log ('размер: ' + [math]::Round((Get-Item $image).Length / 1MB, 2) + ' МБ')



    $argv = @('--port', $port, '--baud', '460800', 'write_flash',

              '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '4MB',

              $offset, $image)

    $psi = New-Object System.Diagnostics.ProcessStartInfo

    $psi.FileName = $script:WizEsptool

    $psi.Arguments = (($argv | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }) -join ' ')

    $psi.UseShellExecute = $false

    $psi.CreateNoWindow = $true

    $psi.RedirectStandardOutput = $true

    $psi.RedirectStandardError = $true

    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8

    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $p = New-Object System.Diagnostics.Process

    $p.StartInfo = $psi

    [void]$p.Start()

    $so = $p.StandardOutput.ReadToEndAsync()

    $se = $p.StandardError.ReadToEndAsync()

    $t0 = Get-Date

    $lastPct = -10

    while (-not $p.HasExited) {

        [System.Windows.Forms.Application]::DoEvents()

        Start-Sleep -Milliseconds 120

        $pct = -1

        try {

            # .Result блокирует до конца потока — берём только готовый снимок

            $txt = ''

            if ($so.Wait(0)) { $txt = $so.Result }

            if ($txt) {

                $mm = [regex]::Matches($txt, '\((\d+)\s*%\)')

                if ($mm.Count -gt 0) { $pct = [int]$mm[$mm.Count - 1].Groups[1].Value }

            }

        } catch { }

        if ($pct -ge 0 -and $pct -ne $lastPct) {

            $lastPct = $pct

            Wiz-SetBar $pct

            $script:W.barLbl.Text = "$pct %"

            if ($pct % 10 -eq 0) { Wiz-Log ("  прошивка: $pct %") }

        }

        if (((Get-Date) - $t0).TotalSeconds -gt 240) {

            try { $p.Kill() } catch { }

            Wiz-Log '! прошивка не уложилась в 240 с — прервано'

            break

        }

    }

    [void]$p.WaitForExit(3000)

    $out = ''

    try { $out = $so.Result } catch { }

    $err = ''

    try { $err = $se.Result } catch { }

    $code = -1

    try { $code = $p.ExitCode } catch { }

    foreach ($ln in (($out + "`n" + $err) -split "`r?`n")) {

        $t = "$ln".Trim()

        if (!$t) { continue }

        if ($t -match '^Writing at' ) { continue }

        Wiz-Log ('  ' + $t)

    }

    $ok = ($code -eq 0) -and (($out + $err) -notmatch '(?i)\b(failed|error)\b')

    Wiz-SetBar $(if ($ok) { 100 } else { 0 })

    if ($ok) { Wiz-Log 'прошивка: OK' } else { Wiz-Log "! прошивка: код $code" }

    return $ok

}



# --------------------------------------------------------------- HTTP

function Test-BoardHttp([string]$ip, [int]$tries = 3) {

    for ($i = 1; $i -le $tries; $i++) {

        try {

            return (Invoke-RestMethod ("http://" + $ip + "/data") -TimeoutSec 4)

        } catch {

            Wiz-Log ("  попытка $i из $tries — " + $_.Exception.Message)

            [System.Windows.Forms.Application]::DoEvents()

            Start-Sleep -Milliseconds 700

        }

    }

    return $null

}



# --------------------------------------------------------------- вывод мастера

function Wiz-Log([string]$t) {

    if ($null -eq $script:W.txt) { return }

    try {

        $script:W.txt.AppendText($t + "`r`n")

        $script:W.txt.SelectionStart = $script:W.txt.TextLength

        $script:W.txt.ScrollToCaret()

        [System.Windows.Forms.Application]::DoEvents()

    } catch { }

}



function Wiz-Clear {

    if ($null -eq $script:W.txt) { return }

    $script:W.txt.Text = ''

    $script:W.lines = @()

}



function Wiz-SetBar($pct) {

    if ($null -eq $script:W.bar) { return }

    if ($pct -lt 0) { $pct = 0 }

    if ($pct -gt 100) { $pct = 100 }

    try { $script:W.bar.Value = [int]$pct } catch { }

}



function Wiz-ShowCtl($name, [bool]$on) {

    $c = $script:W[$name]

    if ($null -ne $c) { $c.Visible = $on }

}



# Кнопка шага 2 зависит от выбора: «Оставить как есть» → «Далее» (активна),

# любой вариант прошивки → «Прошить» + предупреждение, что настройки затрутся.

function Wiz-FlashChoice {

    if ($null -eq $script:W.btnNext) { return }

    $none = [bool]$script:W.rFlashNone.Checked

    Set-Lbl $script:W.btnNext $(if ($none) { 'Далее' } else { 'Прошить' })

    $script:W.btnNext.Enabled = $true

    if ($null -ne $script:W.hint) {

        if ($none) {

            $script:W.hint.Text = 'прошивка не нужна — жмите «Далее», настройки платы сохранятся'

            $script:W.hint.ForeColor = C $script:CLR.ok

        } else {

            $script:W.hint.Text = 'внимание: прошивка затрёт настройки платы (Wi-Fi, калибровка)'

            $script:W.hint.ForeColor = C $script:CLR.warn

        }

    }

}



# Показать/скрыть управление под текущий шаг

function Wiz-ApplyStep {

    foreach ($k in @('lblSsidL', 'cmbSsid', 'lblPassL', 'txtPass', 'chkShow',

                     'rFlashNone', 'rFlashApp', 'rFlashFull', 'hint')) {

        Wiz-ShowCtl $k $false

    }

    $script:W.btnBack.Visible = ($script:W.step -gt 1)

    $script:W.btnNext.Enabled = $true

    $script:W.btnAlt.Visible = $false

    $script:W.bar.Visible = ($script:W.step -eq 2)

    $script:W.barLbl.Visible = ($script:W.step -eq 2)

    $script:W.hint.Visible = $true



    switch ($script:W.step) {

        1 {

            $script:W.hdr.Text = '[ ПОИСК УСТРОЙСТВА ]'

            $script:W.sub.Text = 'Шаг 1 из 5 · USB-кабель'

            $script:W.hint.Text = 'Ищу плату по COM-портам: CH340 (VID 1A86) — наша, нативный USB (303A) — ESP32-C3.'

            $script:W.btnNext.Text = 'Далее'

            $script:W.btnAlt.Text = 'Повторить'

            $script:W.btnAlt.Visible = $true

            $script:W.btnNext.Enabled = [bool]$script:W.found

        }

        2 {

            $script:W.hdr.Text = '[ ПРОШИВКА ]'

            $script:W.sub.Text = 'Шаг 2 из 5 · версия прошивки'

            $script:W.btnAlt.Text = 'Пропустить'

            $script:W.btnAlt.Visible = $true

            foreach ($k in @('rFlashNone', 'rFlashApp', 'rFlashFull')) { Wiz-ShowCtl $k $true }

            Wiz-FlashChoice

        }

        3 {

            $script:W.hdr.Text = '[ НАСТРОЙКА WI-FI ]'

            $script:W.sub.Text = 'Шаг 3 из 5 · сеть и пароль'

            $script:W.btnNext.Text = 'Подключить'

            $script:W.btnAlt.Text = 'Список сетей'

            $script:W.btnAlt.Visible = $true

            foreach ($k in @('lblSsidL', 'cmbSsid', 'lblPassL', 'txtPass', 'chkShow')) { Wiz-ShowCtl $k $true }

            if ($script:W.needManual) {

                $script:W.hint.Text = 'кабель: введите SSID вручную — плата видит только 2.4 ГГц, регистр важен'

                $script:W.hint.ForeColor = C $script:CLR.warn

            } else {

                $script:W.hint.Text = 'ESP32 видит только 2.4 ГГц (каналы 1–13). Пароль уходит на плату по USB.'

                $script:W.hint.ForeColor = C $script:CLR.dim2

            }

        }

        4 {

            $script:W.hdr.Text = '[ ПОДКЛЮЧЕНИЕ ПО СЕТИ ]'

            $script:W.sub.Text = 'Шаг 4 из 5 · проверка IP'

            $script:W.hint.Text = 'USB можно отключать: приложение работает по сети.'

            $script:W.btnNext.Text = 'Готово'

            $script:W.btnAlt.Text = 'Проверить снова'

            $script:W.btnAlt.Visible = $true

        }

        5 {

            $script:W.hdr.Text = '[ ГОТОВО ]'

            $script:W.sub.Text = 'Шаг 5 из 5 · настройка завершена'

            $script:W.hint.Text = 'Приложение уже переключено на новый адрес платы.'

            $script:W.btnNext.Text = 'Закрыть'

            $script:W.btnAlt.Visible = $false

        }

    }

    # на шагах без полей ввода консоль занимает всю высоту

    $tall = ($script:W.step -eq 1) -or ($script:W.step -eq 4) -or ($script:W.step -eq 5)

    if ($tall) {

        $script:W.txt.Height = 436

        $script:W.hint.Location = New-Object System.Drawing.Point(20, 512)

    } else {

        $script:W.txt.Height = 288

        $script:W.hint.Location = New-Object System.Drawing.Point(20, 364)

    }

    if ($script:W.btnNext.Enabled) { $script:W.btnNext.BackColor = C (SkinC '#1F7A46') }

    else { $script:W.btnNext.BackColor = C (SkinC '#2A3240') }

    $script:W.dots.Text = ((1..5 | ForEach-Object { if ($_ -eq $script:W.step) { '●' } else { '○' } }) -join ' ')

}



function Wiz-Goto([int]$n) {

    if ($n -lt 1) { $n = 1 }

    if ($n -gt 5) { $n = 5 }

    $script:W.step = $n

    Wiz-Clear

    Wiz-SetBar 0

    $script:W.barLbl.Text = '0 %'

    Wiz-ApplyStep

    [System.Windows.Forms.Application]::DoEvents()

    switch ($n) {

        1 { Wiz-Step1 }

        2 { Wiz-Step2 }

        3 { Wiz-Step3 }

        4 { Wiz-Step4 }

        5 { Wiz-Step5 }

    }

    Wiz-ApplyStep

}



# --------------------------------------------------------------- шаг 1: поиск

# Порядок проверки: сначала STATUS по serial — он не сбрасывает ESP32 и сразу

# говорит, наша ли это плата и какая на ней прошивка. esptool (flash_id) —

# только запасной вариант: он переводит плату в загрузчик и на живой прошивке

# часто «не видит» чип, из-за чего шаг 1 раньше вставал на «CH340, VID 1A86».

function Wiz-Step1 {

    Wiz-Log 'Перебираю COM-порты…'

    $script:W.found = $false

    $script:W.port = ''

    $script:W.chip = ''

    $script:W.flash = ''

    $script:W.fwBoard = ''

    $script:W.sameFw = $false

    if ($script:WizDemo) {

        Wiz-Log '  COM1 — не плата'

        Wiz-Log '  COM3 — нативный USB — ESP32-C3/S3, не CYD, пропускаю'

        Wiz-Log '  COM5 — CH340, VID 1A86 PID 7523'

        Wiz-Log '    {"t":"status","fw":"CYD-4.4","name":"CYD-DASH","wifi":true,"ip":"192.168.1.51"}'

        Wiz-Log ''

        Wiz-Log 'COM5 — CH340, ESP32 ✓'

        Wiz-Log '  прошивка: CYD-4.4'

        Wiz-Log '  плата уже настроена (192.168.1.51)'

        Wiz-Log '  → прошивка не нужна'

        $script:W.found = $true

        $script:W.port = 'COM5'

        $script:W.chip = 'ESP32'

        $script:W.fwBoard = 'CYD-4.4'

        $script:W.ip = '192.168.1.51'

        $script:W.btnNext.Enabled = $true

        return

    }

    $ports = @(Get-WizPorts)

    if ($ports.Count -eq 0) { Wiz-Log 'портов нет вовсе' }

    $probed = 0

    foreach ($p in $ports) {

        $info = Get-WizPortInfo $p

        # не-USB порты (COM1 материнской платы) — сразу мимо: STATUS там ждать нечего

        $usbLike = ($info.vid -ne '') -or ("$($info.desc)" -match '(?i)USB|CH340|CP210|FTDI|Silicon')

        if (-not $usbLike -and $p -ne $script:cfg.comPort) {

            Wiz-Log ('  ' + $p + ' — не плата')

            continue

        }

        $label = '  ' + $p

        if ($info.vid) { $label += (" — $($info.kind), VID $($info.vid) PID $($info.pid)") }

        elseif ($info.desc) { $label += ' — ' + $info.kind }

        if ($info.vid -eq '303A') {

            Wiz-Log ($label + ' — не CYD, пропускаю')

            continue

        }

        Wiz-Log $label



        # --- 1) STATUS по serial: быстро, без сброса платы

        Disconnect-Serial                     # порт может занимать приложение

        $st = Get-WizBoardStatus $p 6000

        if ($null -ne $st -and $st.fw) {

            $kind = ''

            if ($info.kind -and $info.kind -ne 'COM') { $kind = $info.kind + ', ' }

            $liveIp = ''

            if ($st.ip -match '^\d{1,3}(\.\d{1,3}){3}$' -and $st.ip -ne '0.0.0.0') { $liveIp = $st.ip }

            $cfgIp = ''

            if (Test-CfgReady) { $cfgIp = Get-CfgBoardIp }

            $script:W.found = $true

            $script:W.port = $p

            $script:W.chip = 'ESP32'

            $script:W.fwBoard = [string]$st.fw

            if ($cfgIp) { $script:W.ip = $cfgIp } elseif ($liveIp) { $script:W.ip = $liveIp }

            Wiz-Log ''

            Wiz-Log ($p + ' — ' + $kind + 'ESP32 ✓')

            Wiz-Log ('  прошивка: ' + $st.fw)

            if ($cfgIp) {

                Wiz-Log ('  плата уже настроена (' + $cfgIp + ')')

                Wiz-Log '  → прошивка не нужна'

            } elseif ($liveIp) {

                Wiz-Log ('  плата в сети: ' + $liveIp)

                Wiz-Log '  → прошивка не нужна'

            } else {

                Wiz-Log '  плата ещё не настроена: дальше — Wi-Fi'

            }

            $script:cfg.comPort = $p

            Save-Cfg

            break

        }



        # --- 2) по serial молчит — запасной вариант: esptool flash_id

        $chip = ''

        $fl = ''

        if ($script:WizEsptool -and (Test-Path $script:WizEsptool)) {

            Disconnect-Serial

            $probed++

            $r = Invoke-Native $script:WizEsptool @('--port', $p, 'flash_id') 9000

            $txt = [string]$r.text

            $m = [regex]::Match($txt, '(?im)^\s*(?:Chip is|Detected chip:)\s*([^\r\n\(]+)')

            if ($m.Success) { $chip = $m.Groups[1].Value.Trim() }

            $m = [regex]::Match($txt, '(?i)(?:Detected flash size|Flash size)\s*:\s*([0-9.]+\s*[KMG]?B)')

            if ($m.Success) { $fl = $m.Groups[1].Value.Trim() }

        }

        if (!$chip) {

            if ($info.vid -eq '1A86') { Wiz-Log '    CH340 есть, но плата не ответила (ни STATUS, ни esptool)' }

            else { Wiz-Log '    не плата' }

            continue

        }

        if ($chip -match '(?i)C3|C6|S2|S3|C2') {

            Wiz-Log ("    $chip — это не CYD, пропускаю")

            continue

        }

        $script:W.found = $true

        $script:W.port = $p

        $script:W.chip = $chip

        $script:W.flash = $fl

        Wiz-Log ("    $chip — чип есть, прошивка не ответила (пустая плата?)")

        break

    }

    if ($probed -gt 0) { Wiz-Log '  (esptool при опросе перезагружает плату — это нормально)' }



    Wiz-Log ''

    if ($script:W.found) {

        if (-not $script:W.fwBoard) {

            Wiz-Log ('Плата: ' + $script:W.chip + $(if ($script:W.flash) { ', ' + $script:W.flash + ' flash' } else { '' }))

            Wiz-Log ('Порт: ' + $script:W.port)

            Wiz-Log 'Прошивка не ответила — на шаге 2 предложу прошить.'

        }

        $script:W.btnNext.Enabled = $true

        $script:cfg.comPort = $script:W.port

        Save-Cfg

    } else {

        Wiz-Log 'Плата не найдена.'

        Wiz-Log 'Подключите USB-кабель и нажмите «Повторить».'

        $script:W.btnNext.Enabled = $false

    }

}



# --------------------------------------------------------------- шаг 2: прошивка

function Wiz-Step2 {

    $port = $script:W.port

    if (!$port) { $port = $script:cfg.comPort }

    $script:W.port = $port

    $src = Get-WizSrcVer

    $fw = [string]$script:W.fwBoard

    if ($fw) {

        Wiz-Log ('Версия на плате: ' + $fw + ' (прочитана по STATUS на шаге 1)')

    } else {

        Wiz-Log 'Определяю версию на плате (команда STATUS по serial)…'

        if ($script:WizDemo) {

            Wiz-Log '  {"t":"status","fw":"CYD-4.4","name":"CYD-DASH","wifi":true,"ip":"192.168.1.51"}'

            $fw = 'CYD-4.4'

        } else {

            Disconnect-Serial

            $fw = Get-BoardFw $port 25000

        }

        $script:W.fwBoard = $fw

    }

    Wiz-Log ''

    Wiz-Log ('Текущая версия на плате: ' + $(if ($fw) { $fw } else { 'не определена' }))

    Wiz-Log ('Версия в установщике:     ' + $(if ($src) { $src } else { 'нет исходника' }))



    $bin = Get-WizAppBin

    $imgOk = Test-Path $script:WizImage

    $script:W.binPath = ''

    if ($null -ne $bin) { $script:W.binPath = $bin.FullName }

    Wiz-Log ''

    if ($bin) {

        Wiz-Log ('свежая сборка: ' + $bin.FullName)

        Wiz-Log ('  ' + [math]::Round($bin.Length / 1MB, 2) + ' МБ, ' + $bin.LastWriteTime.ToString('dd.MM.yyyy HH:mm'))

    } else {

        Wiz-Log ('свежей сборки нет (' + (Join-Path $script:WizSketch 'build2') + ') — только полный образ')

    }

    if (!$imgOk) { Wiz-Log ('! нет полного образа: ' + $script:WizImage) }

    Wiz-Log ''

    $script:W.rFlashApp.Enabled = ($null -ne $bin)

    $script:W.rFlashFull.Enabled = $imgOk



    # Версии совпадают — версия с платы равна FW_VER из cydv4.ino:

    # по умолчанию «Оставить как есть», перепрошивка только по явному выбору.

    $script:W.sameFw = ($fw -and $src -and ($fw -eq $src))

    if ($script:W.sameFw) {

        $script:W.rFlashNone.Checked = $true

        Wiz-Log ('версии совпадают (' + $fw + ') — прошивка не нужна')

        Wiz-Log '  по умолчанию выбрано «Оставить как есть»; «Прошить заново» затрёт настройки'

    } elseif ($fw -and $src) {

        $script:W.rFlashApp.Checked = $true

        Wiz-Log ('версии разные: на плате ' + $fw + ', в установщике ' + $src)

        Wiz-Log '  предлагаю обновить — выбрано «Прошить только прошивку»'

    } else {

        $script:W.rFlashNone.Checked = $true

        Wiz-Log 'версия на плате не определена — безопасный вариант «Оставить как есть»'

    }

    $script:W.btnNext.Enabled = $true

    Wiz-FlashChoice

}



# --------------------------------------------------------------- шаг 3: Wi-Fi

function Wiz-Step3 {

    $script:W.needManual = $false

    Wiz-Log 'Смотрю, как подключён компьютер…'

    if ($script:WizDemo) {

        if ($script:WizDemo -eq 'cable') {

            $ifc = @{ adapter = $false; wifi = $false; ssid = '' }

        } else {

            $ifc = @{ adapter = $true; wifi = $true; ssid = 'MyWiFi' }

        }

    } else {

        $ifc = Get-WifiIface

    }

    if ($ifc.wifi) {

        Wiz-Log ('Компьютер в сети:  ' + $ifc.ssid + ' (Wi-Fi)')

        Wiz-Log '  беру эту сеть и подставляю её в поле «Сеть»'

        $script:W.pcSsid = $ifc.ssid

    } elseif ($ifc.adapter) {

        Wiz-Log 'Wi-Fi адаптер есть, но ни к одной сети не подключён'

        $script:W.pcSsid = ''

    } else {

        Wiz-Log 'Компьютер подключён по кабелю — Wi-Fi сетей у него нет.'

        Wiz-Log ''

        Wiz-Log 'Введите имя сети вручную — той, где будет жить плата:'

        Wiz-Log '  • плата видит ТОЛЬКО 2.4 ГГц (каналы 1–13), 5 ГГц не подойдёт;'

        Wiz-Log '  • имя должно совпадать точь-в-точь, с учётом регистра;'

        Wiz-Log '  • список сетей удобно подсмотреть на телефоне (настройки Wi-Fi).'

        $script:W.pcSsid = ''

        $script:W.needManual = $true

    }



    Wiz-Log ''

    $nets = @()

    if (-not $script:W.needManual) {

        if ($script:WizDemo) {

            $nets = @(

                [pscustomobject]@{ ssid = 'MyWiFi';   band24 = $true;  band5 = $false; dbm = -45 },

                [pscustomobject]@{ ssid = 'Neighbor'; band24 = $true;  band5 = $false; dbm = -72 },

                [pscustomobject]@{ ssid = 'IoT-2.4';  band24 = $true;  band5 = $false; dbm = -80 },

                [pscustomobject]@{ ssid = 'Home_5G';  band24 = $false; band5 = $true;  dbm = -55 }

            )

        } else {

            try { $nets = @(Get-WifiNets) } catch { $nets = @() }

        }

    }

    $n24 = @($nets | Where-Object { $_.band24 })

    $n5 = @($nets | Where-Object { $_.band5 })

    if (-not $script:W.needManual) {

        $tail = ''

        if ($n5.Count -gt 0) { $tail = ', 5 ГГц: ' + $n5.Count + ' (ESP32 их не видит)' }

        Wiz-Log ('Доступных сетей 2.4 ГГц: ' + $n24.Count + $tail)

        foreach ($n in $n24) {

            $mark = ''

            if ($n.ssid -eq $script:W.pcSsid) { $mark = '   <- текущая сеть ПК' }

            Wiz-Log ('  • ' + $n.ssid.PadRight(20) + ' ' + $n.dbm + ' dBm' + $mark)

        }

        foreach ($n in $n5) { Wiz-Log ('  • ' + $n.ssid.PadRight(20) + ' 5 ГГц — не подойдёт') }

    }



    $script:W.nets = $nets

    $script:W.cmbSsid.Items.Clear()

    foreach ($n in $n24) { [void]$script:W.cmbSsid.Items.Add($n.ssid) }

    $pick = $script:W.pcSsid

    if (!$pick -and $n24.Count -gt 0) { $pick = $n24[0].ssid }

    $script:W.cmbSsid.Text = $pick



    $script:W.txtPass.Text = ''

    if ($script:WizDemo) {

        if ($pick) {

            $script:W.txtPass.Text = 'demo-password'

            Wiz-Log ''

            Wiz-Log ('Пароль для "' + $pick + '" взят из настроек Windows (13 симв.)')

        }

    } elseif ($pick) {

        $pw = Get-WifiPass $pick

        if ($pw) {

            $script:W.txtPass.Text = $pw

            Wiz-Log ''

            Wiz-Log ('Пароль для "' + $pick + '" взят из настроек Windows (' + $pw.Length + ' симв.)')

        } else {

            Wiz-Log ''

            Wiz-Log ('Пароля для "' + $pick + '" в Windows нет — введите вручную')

        }

    }

    $script:W.btnNext.Enabled = $true

}



# --------------------------------------------------------------- шаг 4: сеть

function Wiz-Step4 {

    $ip = $script:W.ip

    if ($script:WizDemo -and !$ip) {

        Wiz-Log 'Беру IP с платы по serial (команда IP)…'

        Wiz-Log '  IP: 192.168.1.51 ssid=MyWiFi rssi=-45'

        Wiz-Log ''

        Wiz-Log 'IP платы: 192.168.1.51  (получен по serial)'

        $ip = '192.168.1.51'

    }

    if (!$ip) {

        Wiz-Log 'Беру IP с платы по serial (команда IP)…'

        $port = $script:W.port

        if (!$port) { $port = $script:cfg.comPort }

        Disconnect-Serial

        if (Wiz-OpenPort $port) {

            try { $script:W.sp.Write("IP`n") } catch { }

            Wiz-Pump '^IP:\s*[0-9]' 8000 | Out-Null

            Wiz-ClosePort

            $ip = Wiz-FindVal '^IP:\s*([0-9]{1,3}(\.[0-9]{1,3}){3})'

            if ($ip) { Wiz-Log ''; Wiz-Log ('IP платы: ' + $ip + '  (получен по serial)') }

        }

    }

    if (!$ip) {

        Wiz-Log '! IP не получен — плата не в сети или порт занят'

        $script:W.btnNext.Enabled = $false

        return

    }

    $script:W.ip = $ip

    Wiz-Log ''

    Wiz-Log ('Проверяю http://' + $ip + '/data …')

    if ($script:WizDemo) {

        $d = [pscustomobject]@{ v = 237; p = 18; sdRows = 11212 }

    } else {

        $d = Test-BoardHttp $ip 3

    }

    if ($null -eq $d) {

        Wiz-Log '! плата не ответила по HTTP — проверьте сеть и пароль'

        $script:W.btnNext.Enabled = $false

        return

    }

    $rows = 0

    try { $rows = [int]$d.sdRows } catch { }

    Wiz-Log 'OK'

    Wiz-Log ('  Напряжение ' + $d.v + ' В · Мощность ' + $d.p + ' Вт · Карта ' + $rows + ' строк')

    $script:W.rows = $rows

    $script:W.data = $d

    $script:W.btnNext.Enabled = $true

    Wiz-Log ''

    Wiz-Log 'mDNS: http://cyd.local — можно пользоваться и по имени'

}



# --------------------------------------------------------------- шаг 5: готово

function Wiz-Step5 {

    $ip = $script:W.ip

    if ($script:WizDemo) {

        if (!$ip) { $ip = '192.168.1.51' }

        if (!$script:W.chip) { $script:W.chip = 'ESP32-D0WD-V3' }

        if (!$script:W.ssid) { $script:W.ssid = 'MyWiFi' }

        if ($script:W.rows -le 0) { $script:W.rows = 11212 }

        Wiz-Log '(демо-режим: конфиг не записывается, адрес платы не меняется)'

        Wiz-Log ''

        Wiz-Log 'Настройка завершена.'

        Wiz-Log ("Плата: $($script:W.chip) · $ip · $($script:W.rows) строк на карте")

        Wiz-Log ''

        Wiz-Log 'Дальше приложение работает по сети. USB больше не нужен.'

        return

    }

    $ok = Write-WizCfg $ip $script:W.ssid $script:W.port

    Wiz-Log 'Настройка завершена.'

    Wiz-Log ("Плата: $($script:W.chip) · $ip · $($script:W.rows) строк на карте")

    Wiz-Log ''

    if ($ok) { Wiz-Log ('Конфиг записан: ' + $script:WizJson) }

    else { Wiz-Log '! конфиг записать не удалось (см. журнал)' }

    Wiz-Log ''

    Wiz-Log 'Дальше приложение работает по сети. USB больше не нужен.'



    # приложение переключаем на новый адрес

    $addr = 'http://' + $ip

    $script:cfg.board = $addr

    $script:board = $null

    $script:boardAt = [datetime]::MinValue

    $script:boardErr = ''

    $script:boardBad = 0

    $script:daysAt = [datetime]::MinValue

    $script:histCache = @{}

    $script:histAt = @{}

    $script:histPending = $null; $script:histRequests = @{}; $script:histFirstAt = @{}

    try {

        $txtBoard.Text = $addr

        Set-Lbl $lblSub ('ESP32 · ' + $ip + ' · обновление 5 с')

        # другой адрес — другая плата: старые запросы истории/периода отменяем
        Reset-HistSelection $script:histSel
        Cancel-HistRequests -KeepDate ''
        Stop-RangeLoad


    } catch { }

    Save-Cfg

    Request-Day $script:histSel

    $script:wizNeeded = $false

}



# --------------------------------------------------------------- «Далее»

function Wiz-Next {

    if ($script:W.busy) { return }

    $script:W.busy = $true

    try {

        switch ($script:W.step) {

            1 {

                if (-not $script:W.found) { return }

                Wiz-Goto 2

            }

            2 {

                if ($script:W.rFlashNone.Checked) { Wiz-Goto 3; return }

                Disconnect-Serial

                Wiz-Log ''

                $ok = $false

                if ($script:W.rFlashFull.Checked) {

                    $msg = "Восстановить полный образ 4 МБ?`r`n`r`nЗатирает MAC-адрес, калибровку тача`r`nи настройки Wi-Fi платы."

                    if ([System.Windows.Forms.MessageBox]::Show($msg, 'Прошивка', 'YesNo', 'Warning') -ne 'Yes') { return }

                    $ok = Invoke-WizFlash '0x0' $script:WizImage

                } else {

                    if (-not $script:W.binPath) {

                        Wiz-Log ('! нет файла прошивки (' + (Join-Path $script:WizSketch 'build2') + ') — прошивать нечем')

                        Wiz-Goto 3

                        return

                    }

                    $msg = "Прошить заново?`r`n`r`nПрошивка затрёт настройки платы:`r`nWi-Fi и калибровку тача."

                    if ([System.Windows.Forms.MessageBox]::Show($msg, 'Прошивка', 'YesNo', 'Warning') -ne 'Yes') { return }

                    $ok = Invoke-WizFlash '0x10000' $script:W.binPath

                }

                if ($ok) {

                    Wiz-Log 'плата перезагружается, жду 10 с…'

                    $tw = Get-Date

                    while (((Get-Date) - $tw).TotalSeconds -lt 10) {

                        [System.Windows.Forms.Application]::DoEvents()

                        Start-Sleep -Milliseconds 200

                    }

                    Connect-Serial | Out-Null

                    $fw = Get-BoardFw $script:W.port 25000

                    if ($fw) { Wiz-Log ("версия после прошивки: " + $fw) }

                }

                Wiz-Goto 3

            }

            3 {

                Wiz-DoWifiConnect

            }

            4 {

                Wiz-Goto 5

            }

            5 {

                $script:W.Form.Close()

            }

        }

    } catch {

        Wiz-Log ('! ошибка шага: ' + $_.Exception.Message)

        Write-AppLog ('мастер: ошибка шага ' + $script:W.step + ': ' + $_.Exception.Message)

    } finally {

        $script:W.busy = $false

        Wiz-ApplyStep

    }

}



# Проверка имени сети и пароля до отправки на плату.

# $true — можно отправлять; иначе причина пишется в консоль мастера.

function Test-WizWifi([string]$ssid, [string]$pass) {

    if (!$ssid) { Wiz-Log '! введите имя сети (SSID) — поле «Сеть» пустое'; return $false }

    if ($ssid.Length -gt 32) {

        Wiz-Log ("! имя сети длиннее 32 символов ($($ssid.Length)) — плата такое не примет"); return $false

    }

    if ($ssid.Contains('"') -or $pass.Contains('"')) {

        Wiz-Log '! кавычки в имени сети или пароле недопустимы (плата разбирает команду по кавычкам)'; return $false

    }

    if ($ssid -ne $ssid.Trim()) {

        Wiz-Log '! в имени сети пробелы по краям — уберите их'; return $false

    }

    if ($pass.Length -gt 0 -and $pass.Length -lt 8) {

        Wiz-Log ("  внимание: пароль короче 8 символов ($($pass.Length)) — WPA2 такое не принимает, пробую всё равно")

    }

    if ($script:W.nets) {

        $only5 = @($script:W.nets | Where-Object { ($_.ssid -eq $ssid) -and $_.band5 })

        if ($only5.Count -gt 0) {

            Wiz-Log '  внимание: по сканированию эта сеть только 5 ГГц — плата её не увидит, нужна сеть 2.4 ГГц'

        }

    }

    return $true

}



function Wiz-DoWifiConnect {

    $ssid = $script:W.cmbSsid.Text.Trim()

    $pass = $script:W.txtPass.Text

    if (-not (Test-WizWifi $ssid $pass)) { return }

    $port = $script:W.port

    if (!$port) { $port = $script:cfg.comPort }

    if (!$port) { Wiz-Log '! порт не выбран — шаг 1 не пройден'; return }



    Wiz-Log ''

    Wiz-Log ('Отправляю: WIFISET "' + $ssid + '" (пароль ' + $pass.Length + ' симв., в лог не пишется)')

    if ($script:WizDemo) {          # демо: на плату ничего не отправляем

        Wiz-Log ('  WIFISET: OK ssid=' + $ssid)

        Wiz-Log '  веб: http://192.168.1.51/'

        Wiz-Log ''

        Wiz-Log 'Wi-Fi: подключено · IP платы 192.168.1.51'

        $script:W.ssid = $ssid

        $script:W.ip = '192.168.1.51'

        Wiz-Goto 4

        return

    }

    Disconnect-Serial

    if (-not (Wiz-OpenPort $port)) { return }

    try {

        $script:W.sp.Write('WIFISET "' + $ssid + '" "' + $pass + '"' + "`n")

        $hit = Wiz-Pump 'WIFISET:\s*(OK|ERR)' 22000

        if (-not $hit) {

            Wiz-Log '! плата не ответила за 22 с'

            Wiz-ClosePort

            Connect-Serial | Out-Null

            return

        }

        if (Wiz-Find 'WIFISET:\s*OK') {

            $script:W.ssid = $ssid

            Wiz-Pump 'веб: http://' 5000 | Out-Null

            $ip = Wiz-FindVal 'веб: http://([0-9]{1,3}(\.[0-9]{1,3}){3})'

            if (-not $ip) {

                try { $script:W.sp.Write("IP`n") } catch { }

                Wiz-Pump '^IP:\s*[0-9]' 5000 | Out-Null

                $ip = Wiz-FindVal '^IP:\s*([0-9]{1,3}(\.[0-9]{1,3}){3})'

            }

            Wiz-ClosePort

            if ($ip) {

                $script:W.ip = $ip

                Wiz-Log ''

                Wiz-Log ('Wi-Fi: подключено · IP платы ' + $ip)

                Connect-Serial | Out-Null

                Wiz-Goto 4

            } else {

                Wiz-Log '! подключилось, но IP не пришёл — проверьте шаг 4'

                Connect-Serial | Out-Null

            }

        } else {

            Wiz-Log '! WIFISET вернул ошибку (неверный пароль или сеть 5 ГГц)'

            Wiz-Log '  плата осталась в прежней сети'

            Wiz-ClosePort

            Connect-Serial | Out-Null

        }

    } catch {

        Wiz-Log ('! ошибка Wi-Fi: ' + $_.Exception.Message)

        Wiz-ClosePort

        Connect-Serial | Out-Null

    }

}



# --------------------------------------------------------------- сам диалог

function Show-SetupWizard([int]$step = 0) {

    if ($script:wizOpen) { return }

    $script:wizOpen = $true

    $script:W = @{}

    $script:W.step = 1

    $script:W.busy = $false

    $script:W.lines = @()

    $script:W.found = $false

    $script:W.port = ''

    $script:W.ip = ''

    $script:W.ssid = ''

    $script:W.rows = 0

    $script:W.needManual = $false



    $f = New-Object System.Windows.Forms.Form

    $f.Text = 'НАСТРОЙКА — DOM MONITOR'

    $f.ClientSize = New-Object System.Drawing.Size(820, 606)

    $f.StartPosition = 'CenterParent'

    $f.BackColor = C $script:CLR.bg

    $f.ForeColor = C $script:CLR.text

    $f.Font = Fnt 'Segoe UI' 9 $false

    $f.FormBorderStyle = 'FixedDialog'

    $f.MaximizeBox = $false

    $f.MinimizeBox = $false

    $f.ShowInTaskbar = $false

    $script:W.Form = $f



    $script:W.hdr = New-Lbl $f '[ ПОИСК УСТРОЙСТВА ]' 13.5 $script:CLR.accent $true 18 14

    $script:W.sub = New-Lbl $f 'Шаг 1 из 5' 9 $script:CLR.dim $false 20 44

    $script:W.dots = New-Lbl $f '● ○ ○ ○ ○' 12 $script:CLR.dim2 $false 690 16



    $txt = New-Object System.Windows.Forms.TextBox

    $txt.Multiline = $true

    $txt.ReadOnly = $true

    $txt.ScrollBars = 'Vertical'

    $txt.WordWrap = $false

    $txt.BackColor = C (SkinC '#05070A')

    $txt.ForeColor = C '#9BE3B4'

    $txt.Font = Fnt 'Consolas' 9.5 $false

    $txt.BorderStyle = 'FixedSingle'

    $txt.Location = New-Object System.Drawing.Point(18, 70)

    $txt.Size = New-Object System.Drawing.Size(784, 288)

    $f.Controls.Add($txt)

    $script:W.txt = $txt



    # --- шаг 3: сеть и пароль

    $script:W.lblSsidL = New-Lbl $f 'Сеть:' 9.5 $script:CLR.dim $false 20 394

    $cmb = New-Object System.Windows.Forms.ComboBox

    $cmb.DropDownStyle = 'DropDown'

    $cmb.FlatStyle = 'Flat'

    $cmb.BackColor = C (SkinC '#12161C')

    $cmb.ForeColor = C $script:CLR.text

    $cmb.Font = Fnt 'Consolas' 9.5 $false

    $cmb.Location = New-Object System.Drawing.Point(104, 390)

    $cmb.Size = New-Object System.Drawing.Size(320, 26)

    $cmb.IntegralHeight = $false

    $f.Controls.Add($cmb)

    $script:W.cmbSsid = $cmb



    $script:W.lblPassL = New-Lbl $f 'Пароль:' 9.5 $script:CLR.dim $false 20 430

    $tp = New-Object System.Windows.Forms.TextBox

    $tp.UseSystemPasswordChar = $true

    $tp.BackColor = C (SkinC '#12161C')

    $tp.ForeColor = C $script:CLR.text

    $tp.BorderStyle = 'FixedSingle'

    $tp.Font = Fnt 'Consolas' 9.5 $false

    $tp.Location = New-Object System.Drawing.Point(104, 426)

    $tp.Size = New-Object System.Drawing.Size(320, 26)

    $f.Controls.Add($tp)

    $script:W.txtPass = $tp



    $chk = New-Object System.Windows.Forms.CheckBox

    $chk.Text = 'показать'

    $chk.ForeColor = C $script:CLR.dim

    $chk.BackColor = C $script:CLR.bg

    $chk.FlatStyle = 'Flat'

    $chk.Font = Fnt 'Segoe UI' 8.5 $false

    $chk.AutoSize = $true

    $chk.Location = New-Object System.Drawing.Point(434, 430)

    $f.Controls.Add($chk)

    $script:W.chkShow = $chk

    $chk.Add_CheckedChanged({ $script:W.txtPass.UseSystemPasswordChar = -not $script:W.chkShow.Checked })



    # --- шаг 2: варианты прошивки

    function New-WizRadio([string]$text, [int]$y, [string]$color) {

        $r = New-Object System.Windows.Forms.RadioButton

        $r.Text = $text

        $r.ForeColor = C $color

        $r.BackColor = C $script:CLR.bg

        $r.FlatStyle = 'Flat'

        $r.Font = Fnt 'Segoe UI' 9 $false

        $r.AutoSize = $true

        $r.Location = New-Object System.Drawing.Point(22, $y)

        $f.Controls.Add($r)

        return $r

    }

    $r1 = New-WizRadio 'Оставить как есть' 390 $script:CLR.text

    $r2 = New-WizRadio 'Прошить заново — затрёт настройки платы (Wi-Fi, калибровка)' 412 $script:CLR.warn

    $r3 = New-WizRadio 'Восстановить полный образ 4 МБ (затирает MAC и настройки платы)' 434 $script:CLR.warn

    $script:W.rFlashNone = $r1

    $script:W.rFlashApp = $r2

    $script:W.rFlashFull = $r3

    $handler = { Wiz-FlashChoice }

    $r1.Add_CheckedChanged($handler)

    $r2.Add_CheckedChanged($handler)

    $r3.Add_CheckedChanged($handler)



    # --- подсказка под элементами

    $script:W.hint = New-Lbl $f '' 8.5 $script:CLR.dim2 $false 20 364



    # --- прогресс (шаг 2)

    $bar = New-Object System.Windows.Forms.ProgressBar

    $bar.Location = New-Object System.Drawing.Point(22, 472)

    $bar.Size = New-Object System.Drawing.Size(600, 12)

    $bar.Minimum = 0

    $bar.Maximum = 100

    $bar.Value = 0

    $f.Controls.Add($bar)

    $script:W.bar = $bar

    $script:W.barLbl = New-Lbl $f '0 %' 8.5 $script:CLR.dim $false 634 469



    # --- кнопки

    $bBack = New-Btn $f 'Назад' '#2A3240' $script:CLR.text 110 34

    $bBack.Location = New-Object System.Drawing.Point(20, 546)

    $bBack.Add_Click({ if ($script:W.busy) { return }; Wiz-Goto ($script:W.step - 1) })

    $script:W.btnBack = $bBack



    $bCancel = New-Btn $f 'Отмена' '#2A3240' $script:CLR.dim 110 34

    $bCancel.Location = New-Object System.Drawing.Point(140, 546)

    $bCancel.Add_Click({ if ($script:W.busy) { return }; $script:W.Form.Close() })



    $bAlt = New-Btn $f 'Повторить' '#24405A' $script:CLR.text 150 34

    $bAlt.Location = New-Object System.Drawing.Point(500, 546)

    $bAlt.Add_Click({

        if ($script:W.busy) { return }

        switch ($script:W.step) {

            1 { Wiz-Goto 1 }

            2 { Wiz-Goto 3 }

            3 {

                if ($script:W.needManual) {

                    Wiz-Log ''

                    Wiz-Log '! у компьютера нет Wi-Fi адаптера — сканировать нечем.'

                    Wiz-Log '  Посмотрите список сетей 2.4 ГГц на телефоне и введите имя сюда.'

                } else {

                    Wiz-Goto 3

                }

            }

            4 { Wiz-Goto 4 }

        }

    })

    $script:W.btnAlt = $bAlt



    $bNext = New-Btn $f 'Далее' '#1F7A46' '#FFFFFF' 150 34

    $bNext.Location = New-Object System.Drawing.Point(660, 546)

    $bNext.Add_Click({ Wiz-Next })

    $script:W.btnNext = $bNext



    $f.Add_FormClosed({

        $script:wizOpen = $false

        $script:autoWizAsked = $true       # в этой сессии мастер больше не всплывает

        try { Wiz-ClosePort } catch { }

        try { $timer.Start() } catch { }

        if (-not $script:WizDemo) { try { Connect-Serial | Out-Null } catch { } }

        Write-AppLog 'мастер: окно закрыто (программа продолжает работу)'

    })



    # таймер приложения останавливаем: мастер сам читает порт и HTTP.

    # Шапку обновляем сразу после отключения порта — иначе она замирает на

    # «РЕЗЕРВ COM5» при уже закрытом порте (взаимоисключающие надписи).

    try { $timer.Stop() } catch { }

    try { Disconnect-Serial } catch { }

    try { Update-Header } catch { }



    $script:W.initialStep = $step

    $f.Add_Shown({

        if ($script:W.initialStep -gt 0) { Wiz-Goto $script:W.initialStep }

        else { Wiz-Goto 1 }

    })

    Write-AppLog 'мастер: окно открыто'

    [void]$f.ShowDialog($form)

}



# --------------------------------------------------------- кнопка мастера

# Мастер при готовых настройках сам не открывается — только этой кнопкой.

$script:btnWizard = New-Btn $pgDev 'Мастер настройки' '#24405A' $script:CLR.text 176 28

$script:btnWizard.Location = New-Object System.Drawing.Point(600, 500)

$script:btnWizard.Add_Click({ Show-SetupWizard 0 })

$script:btnSounds = New-Btn $pgDev 'Звуки платы' '#24405A' $script:CLR.text 176 28

$script:btnSounds.Location = New-Object System.Drawing.Point(440, 500)

$script:btnSounds.Add_Click({ Show-Sounds })



# --------------------------------------------------------------- конфиг при старте

$script:wizCfg = Read-WizCfg

if ($null -ne $script:wizCfg) {

    $bip = [string]$script:wizCfg['boardIp']

    if ($bip -match '^\d{1,3}(\.\d{1,3}){3}$') {

        $script:cfg.board = 'http://' + $bip

        if ($null -ne $txtBoard) { $txtBoard.Text = $script:cfg.board }

    }

    $wp = [string]$script:wizCfg['port']

    if ($wp -match '^COM\d+$') { $script:cfg.comPort = $wp }

    Write-AppLog ('настройки: ' + $bip + ' / ' + $wp + ' / setupDone=' + [string]$script:wizCfg['setupDone'])

}



# Нужен ли мастер при старте: только если нет настроек или setupDone = false.

# settings.json — единственный источник правды, поэтому после прохождения

# мастера (setupDone = true) окно больше не открывается.

$script:wizNeeded = $false

if ($null -eq $script:wizCfg) { $script:wizNeeded = $true }

elseif ($script:wizCfg['setupDone'] -ne $true) { $script:wizNeeded = $true }

if ($env:DOMMON_WIZARD -eq '1') { $script:wizNeeded = $true }

$script:wizSay = if ($script:wizNeeded) { 'мастер нужен' } else { 'мастер не нужен' }





# ============================================================================

#  ТАЙМЕР

# ============================================================================

$timer = New-Object System.Windows.Forms.Timer

$timer.Interval = 400

$timer.Add_Tick({ On-Tick })



$form.Add_FormClosed({ try { if ($script:tray) { $script:tray.Visible = $false; $script:tray.Dispose() } } catch { } })



# ============================================================================

#  ЗАПУСК

# ============================================================================

$form.Add_Shown({

    $script:uiReady = $true



    Init-Http

    Load-ConfigPort

    $null = Auto-PickPort

    Update-PortList



    Layout-All

    Show-Page 'screen'

    Update-Header

    Update-Cards

    Update-Footer



    Add-Log 'программа запущена'

    Add-Log ('источник данных: HTTP ' + $script:cfg.board) 'ok'

    $setupIp = [string]$script:cfg.boardIp

    if ($setupIp -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { $setupIp = [string]$script:cfg.board -replace '^https?://', '' }

    $setupTxt = 'настройки: ' + $setupIp + ' / ' + $script:cfg.comPort + ' / ' + $script:wizSay

    Add-Log $setupTxt $(if ($script:wizNeeded) { 'warn' } else { 'ok' })

    if ($script:CfgMigrated) { Add-Log 'старый DomMonitor.json перенесён в settings.json и удалён' 'warn' }

    if ($script:CfgUpgraded) { Add-Log 'настройки уже были — добавил признак setupDone, мастер не нужен' 'ok' }

    # демо-режим (скриншоты без платы) порт не открывает

    if (-not $script:WizDemo) { Connect-Serial | Out-Null }



    # мастер настройки: при первом запуске (нет конфига) открываем после

    # появления окна — иначе диалог встанет поверх неразложенной формы

    $script:wizTimer = New-Object System.Windows.Forms.Timer

    $script:wizTimer.Interval = 1200

    $script:wizTimer.Add_Tick({

        $script:wizTimer.Stop()

        if ($script:wizNeeded) {

            $n = 0

            if ($env:DOMMON_WIZ_STEP) { try { $n = [int]$env:DOMMON_WIZ_STEP } catch { $n = 0 } }

            Show-SetupWizard $n

        }

    })

    $script:wizTimer.Start()



    $timer.Start()

    # Открываем вчерашний день: он предзагружается один раз и потом
    # показывается из кэша мгновенно (сегодня плата читает 12–65 с).
    $script:histSel = (Get-Date).AddDays(-1).ToString('yyyy-MM-dd')

    Request-Day $script:histSel

    Update-HistoryView



    # --- демо-хуки для скриншотов (в обычной работе не заданы) ---------------

    if ($env:DOMMON_BEAT_SEC -match '^\d+$') { $script:beatSec = [Math]::Max(5, [int]$env:DOMMON_BEAT_SEC) }

    if ($env:DOMMON_HOVER_HOUR -match '^\d+$') { $script:pinScr = [Math]::Min(23, [int]$env:DOMMON_HOVER_HOUR) }

    if ($env:DOMMON_HIST_HOUR -match '^\d+$') { $script:pinHist = [Math]::Min(23, [int]$env:DOMMON_HIST_HOUR) }

    if ($env:DOMMON_PAGE -match '^(screen|hist|dev|flash|log|prot|term|arch)$') {

        Show-Page ([string]$env:DOMMON_PAGE)

        Add-Log ('демо: открыта вкладка ' + $env:DOMMON_PAGE)

    }



    # страницы получают настоящий размер только после показа окна —

    # поэтому раскладку повторяем коротким таймером (иначе карточки остаются в (0,0))

    $script:firstLayout = New-Object System.Windows.Forms.Timer

    $script:firstLayout.Interval = 150

    Ensure-Tray

$script:firstLayout.Add_Tick({

        $script:firstLayout.Stop()

        try {

            $form.PerformLayout()

            Layout-All

            $chartScreen.Invalidate()

            Update-EnergyChart $chartHist $script:histSel

            Write-AppLog ('раскладка: экран ' + $pgScreen.ClientSize.Width + 'x' + $pgScreen.ClientSize.Height +

                          ', карточка ' + $script:cards['v'].Panel.Bounds)

        } catch {

            Write-AppLog ('ошибка первой раскладки: ' + $_.Exception.Message)

        }

    })

    $script:firstLayout.Start()

})



function Load-ConfigPort {

    # старые настройки могли хранить чужой порт (COM3/COM4) — берём USB-переходник

    if ("$($script:cfg.comPort)" -notmatch '^COM\d+$') { $script:cfg.comPort = 'COM5' }

}



$form.Add_Resize({

    try { Layout-All } catch { Write-AppLog ('ошибка раскладки: ' + $_.Exception.Message) }

})



$form.Add_FormClosing({

    param($s, $e)

    # Крестик = свернуться в трей (чтобы не терять связь с платой).
    # Полный выход — через меню значка в трее.
    if (-not $script:exiting) {
        $e.Cancel = $true
        $form.Hide()
        try {
            $script:tray.Visible = $true
            $script:tray.ShowBalloonTip(2000, 'DOM MONITOR', 'Программа свернулась в трей и продолжает опрос платы', 'Info')
        } catch { }
        Write-AppLog 'свернулось в трей (выход — через меню значка)'
        return
    }

    Write-AppLog 'форма закрывается'

    try { if ($script:job) { Stop-Job $script:job -ErrorAction SilentlyContinue; Remove-Job $script:job -Force -ErrorAction SilentlyContinue } } catch { }

    $timer.Stop()

    $flashTimer.Stop()

    Disconnect-Serial

    Save-Cfg

    try { [System.Windows.Forms.Application]::Exit() } catch { }

})



[System.Windows.Forms.Application]::add_ThreadException({

    param($sender, $e)

    Write-AppLog '!!! ОШИБКА В ИНТЕРФЕЙСЕ !!!'

    Write-AppLog ('тип: ' + $e.Exception.GetType().FullName)

    Write-AppLog ('текст: ' + $e.Exception.Message)

    Write-AppLog ('стек: ' + $e.Exception.StackTrace)

})



Write-AppLog 'форма готова, показываю'
try {
    Ensure-Tray
    Show-Toast 'DOM MONITOR' ('работает в трее · плата ' + [string]$script:cfg.boardIp) 'ok'
    Write-AppLog 'трей: значок показан, приветственный шарик отправлен'
} catch { Write-AppLog ('трей: ' + $_.Exception.Message) }

# двойная буферизация формы и панелей — без мерцания при перерисовке
try { Enable-DoubleBufferAll $form } catch { Write-AppLog ('двойная буферизация: ' + $_.Exception.Message) }

# Показываем форму модально (как раньше), но после скрытия в трей не даём скрипту
# завершиться: крутим очередь сообщений, пока пользователь не выйдет через меню значка.
[void]$form.ShowDialog()
while (-not $script:exiting) {
    try { [System.Windows.Forms.Application]::DoEvents() } catch { }
    Start-Sleep -Milliseconds 100
}

Write-AppLog '=== ШТАТНОЕ ЗАВЕРШЕНИЕ ==='

