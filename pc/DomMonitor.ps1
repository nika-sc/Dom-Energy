# ============================================================================

#  DOM MONITOR 2.0 — пульт управления устройством на ESP32 (CYD, Wi-Fi)

# ----------------------------------------------------------------------------

#  Источник данных №1 — HTTP  http://192.168.0.51/data    (обновление 5 с)

#  Источник данных №2 — COM5 (резерв, только если HTTP недоступен)

#

#  Вкладки: Экран · Устройство · История · Прошивка · Журнал

#  ВАЖНО: файл хранится в UTF-8 С BOM (иначе PowerShell 5.1 покажет кракозябры).

# ============================================================================



Add-Type -AssemblyName System.Windows.Forms

Add-Type -AssemblyName System.Drawing

Add-Type -AssemblyName System.Net.Http

[System.Windows.Forms.Application]::EnableVisualStyles()



$ErrorActionPreference = 'Stop'



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

    board     = 'http://192.168.0.51'

    fqbn      = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'

    boardIp   = '192.168.0.51'

    port      = 'COM5'

    setupDone = $false

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

    try { $script:cfg | ConvertTo-Json -Depth 5 | Set-Content $script:CfgFile -Encoding UTF8 } catch { }

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

$script:histSel      = (Get-Date).ToString('yyyy-MM-dd')



$script:lastUi       = [datetime]::MinValue

$script:lastDev      = [datetime]::MinValue

$script:lastLog      = [datetime]::MinValue

$script:uiReady      = $false

$script:totalPolls   = 0

$script:pollMs       = 0

$script:pollStartAt  = [datetime]::MinValue

$script:pollErr      = 0              # неудачных опросов /data всего

$script:httpErrSeen  = ''             # последняя ошибка HTTP (чтобы не дублировать в журнал)

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

    $b.Text = $text

    $b.Size = New-Object System.Drawing.Size($w, $h)

    $b.FlatStyle = 'Flat'

    $b.BackColor = C $bg

    $b.ForeColor = C $fg

    $b.FlatAppearance.BorderSize = 0

    $b.FlatAppearance.MouseOverBackColor = C '#2C3442'

    $b.Font = Fnt 'Segoe UI' 9 $true

    $b.Cursor = 'Hand'

    if ($null -ne $parent) { $parent.Controls.Add($b) }

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

        }

        if ($null -eq $script:httpSlow) {

            $script:httpSlow = New-Object System.Net.Http.HttpClient

            $script:httpSlow.Timeout = [TimeSpan]::FromSeconds(20)

            $script:httpSlow.DefaultRequestHeaders.ConnectionClose = $true

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

                if ($null -ne $o -and $null -ne $o.v -and [double]$o.v -gt 0) {

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



    if (((Get-Date) - $script:pollAt).TotalSeconds -lt 5) { return }

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

            } catch { }

        }

        return

    }



    if (((Get-Date) - $script:daysAt).TotalSeconds -lt 120) { return }

    $script:daysAt = Get-Date

    try { $script:daysTask = $script:httpSlow.GetStringAsync("$($script:cfg.board)/days") }

    catch { $script:daysTask = $null }

}



# ------------------------------------------------------- история за один день

function Request-Day([string]$date) {

    if ("$date" -notmatch '^\d{4}-\d{2}-\d{2}$') { return }

    if ($script:histCache.ContainsKey($date)) {

        $at = $script:histAt[$date]

        if ($null -ne $at -and (((Get-Date) - $at).TotalSeconds) -lt 120) { return }

    }

    $script:histPending = $date

    if ($script:uiReady) { Update-HistoryView }

}



function Fill-DayGaps($from, $to) {
    # Плата умеет добирать пропущенные строки из облака Tuya: /gapfill?run=1.
    # Запускаем и не ждём: заполнение идёт в фоне на самой плате, история
    # перечитается обычным опросом.
    $ip = [string]$script:cfg.boardIp
    if (-not $ip -or -not $from) { return }
    if (-not $to) { $to = $from }
    try {
        $r = Invoke-RestMethod ('http://' + $ip + '/gapfill?run=1&from=' + $from + '&to=' + $to) -TimeoutSec 12
        if ($r.ok) {
            Add-Log ('заполнение пропусков из облака за ' + $from + '…' + $to + ': запущено') 'info'
        } else {
            Add-Log 'заполнение пропусков: плата не приняла запрос' 'warn'
        }
    } catch {
        Add-Log ('заполнение пропусков: ' + $_.Exception.Message) 'warn'
    }
}

function Pump-History {

    if ($null -eq $script:httpSlow) { return }



    if ($null -ne $script:histTask) {

        if (-not $script:histTask.IsCompleted) { return }

        $task = $script:histTask

        $date = $script:histTaskDate

        $script:histTask = $null

        $script:histTaskDate = ''

        if ($task.Status -eq [System.Threading.Tasks.TaskStatus]::RanToCompletion) {

            try {

                $o = $task.Result | ConvertFrom-Json

                $script:histCache[$date] = @($o.hours)

                $script:histAt[$date] = Get-Date

                if ($script:histErrDate -eq $date) { $script:histErr = '' }

            } catch {

                $script:histErr = 'ответ /history не разобран'

                $script:histErrDate = $date

            }

        } else {

            $script:histErr = 'плата не отдала историю'

            $script:histErrDate = $date

        }

        if ($null -ne $script:refreshHistory) { & $script:refreshHistory }

        return

    }



    if ($null -eq $script:histPending) { return }

    $date = $script:histPending

    $script:histPending = $null

    $script:histTaskDate = $date

    try { $script:histTask = $script:httpSlow.GetStringAsync("$($script:cfg.board)/history?date=$date") }

    catch {

        $script:histTask = $null

        $script:histTaskDate = ''

        $script:histErr = 'запрос истории не отправлен'

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

    if ($script:port -and $script:port.IsOpen) {

        Set-Lbl $lblCom ('● ' + $com + ' — открыт (монитор)')

        $lblCom.ForeColor = C '#63D68B'

    } elseif (-not (Test-ComPresent $com)) {

        Set-Lbl $lblCom ('● ' + $com + ' — нет устройства')

        $lblCom.ForeColor = C '#E85D5D'

    } elseif ((Test-PortFree $com) -eq $false) {

        Set-Lbl $lblCom ('● ' + $com + ' — занят другой программой')

        $lblCom.ForeColor = C '#F2B44B'

    } else {

        Set-Lbl $lblCom ('● ' + $com + ' — закрыт')

        $lblCom.ForeColor = C '#8B94A5'

    }

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

        if ($v -gt 0) {

            $script:serialData = $obj

            $script:serialAt = Get-Date

            $script:serialZero = $false

        } elseif (-not $script:serialZero) {

            $script:serialZero = $true

            Add-Log 'serial присылает напряжение 0 — данные берём только по HTTP' 'warn'

        }

    }

}



# ============================================================================

#  ПАЛИТРА И ФОРМА

# ============================================================================

$script:CLR = @{

    bg     = '#0E1015'

    header = '#12151B'

    nav    = '#151920'

    footer = '#101318'

    panel  = '#171B22'

    card   = '#1B2029'

    card2  = '#232A36'

    line   = '#252C38'

    text   = '#E6ECF5'

    dim    = '#8B94A5'

    dim2   = '#68707F'

    accent = '#5AC8FF'

    ok     = '#63D68B'

    warn   = '#F2B44B'

    err    = '#E85D5D'

    lcdBg  = '#05070A'

    lcdFg  = '#7FE0A0'

}



$form                = New-Object System.Windows.Forms.Form

$form.Text           = 'DOM MONITOR — пульт управления'

$form.Size           = New-Object System.Drawing.Size(1140, 872)

$form.MinimumSize    = New-Object System.Drawing.Size(980, 740)

$form.StartPosition  = 'CenterScreen'

$form.BackColor      = C $script:CLR.bg

$form.ForeColor      = C $script:CLR.text

$form.Font           = Fnt 'Segoe UI' 9 $false



# ------------------------------------------------------------------ шапка

$top = New-Object System.Windows.Forms.Panel

$top.Dock = 'Top'

$top.Height = 62

$top.BackColor = C $script:CLR.header

$form.Controls.Add($top)



$lblTitle = New-Lbl $top 'DOM MONITOR' 15.5 $script:CLR.accent $true 16 8

$lblSub   = New-Lbl $top 'ESP32 · 192.168.0.51 · обновление 5 с' 8 $script:CLR.dim2 $false 18 38



$lblFwChip = New-Object System.Windows.Forms.Label

$lblFwChip.Text = 'прошивка —'

$lblFwChip.Font = Fnt 'Segoe UI' 8 $false

$lblFwChip.ForeColor = C $script:CLR.dim

$lblFwChip.BackColor = C $script:CLR.card

$lblFwChip.TextAlign = 'MiddleCenter'

$lblFwChip.Size = New-Object System.Drawing.Size(130, 26)

$lblFwChip.Location = New-Object System.Drawing.Point(196, 20)

$top.Controls.Add($lblFwChip)



$lblHttp = New-Object System.Windows.Forms.Label

$lblHttp.Text = '● HTTP —'

$lblHttp.Font = Fnt 'Segoe UI' 8 $true

$lblHttp.ForeColor = C $script:CLR.err

$lblHttp.BackColor = C $script:CLR.card

$lblHttp.TextAlign = 'MiddleCenter'

$lblHttp.Size = New-Object System.Drawing.Size(112, 26)

$lblHttp.Location = New-Object System.Drawing.Point(600, 20)

$top.Controls.Add($lblHttp)



# свежесть самих показателей (vAt/tAt/pAt/eAt), а не факта ответа платы

$lblFresh = New-Object System.Windows.Forms.Label

$lblFresh.Text = 'данные —'

$lblFresh.Font = Fnt 'Segoe UI' 8 $false

$lblFresh.ForeColor = C $script:CLR.dim2

$lblFresh.BackColor = C $script:CLR.card

$lblFresh.TextAlign = 'MiddleCenter'

$lblFresh.Size = New-Object System.Drawing.Size(92, 26)

$lblFresh.Location = New-Object System.Drawing.Point(456, 20)

$top.Controls.Add($lblFresh)



$lblCom = New-Object System.Windows.Forms.Label

$lblCom.Text = '● COM5 — закрыт'

$lblCom.Font = Fnt 'Segoe UI' 8 $false

$lblCom.ForeColor = C $script:CLR.dim2

$lblCom.BackColor = C $script:CLR.card

$lblCom.TextAlign = 'MiddleCenter'

$lblCom.Size = New-Object System.Drawing.Size(195, 26)

$lblCom.Location = New-Object System.Drawing.Point(798, 20)

$top.Controls.Add($lblCom)



$btnPult    = New-Btn $top 'Экран платы' '#2A3240' $script:CLR.text 112 32

$btnReboot  = New-Btn $top 'Перезагрузить' '#4A3138' $script:CLR.text 120 32

$btnSerial  = New-Btn $top 'Подключить'   '#24405A' $script:CLR.text 100 32

# --- яркость экрана платы: ползунок в шапке, работает по HTTP ---------------

$script:trkBright = New-Object System.Windows.Forms.TrackBar

$script:trkBright.Minimum = 5

$script:trkBright.Maximum = 100

$script:trkBright.TickFrequency = 25

$script:trkBright.Value = 100

$script:trkBright.Size = New-Object System.Drawing.Size(130, 28)

$script:trkBright.Location = New-Object System.Drawing.Point(600, 17)

$top.Controls.Add($script:trkBright)



$script:lblBright = New-Lbl $top '100 %' 8.5 $script:CLR.dim2 $true 736 22



$script:trkBright.Add_Scroll({

    $v = [int]$script:trkBright.Value

    if ($null -ne $script:lblBright) { $script:lblBright.Text = "$v %" }

    try {

        $null = $script:http.GetStringAsync("$($script:cfg.board)/disp?bright=$v").Result

        Add-Log ("яркость экрана: $v %")

    } catch { Add-Log "яркость: плата не ответила" 'warn' }

})

if ($null -ne $script:tipMain) {

    $script:tipMain.SetToolTip($script:trkBright, 'яркость подсветки экрана платы (5-100 %), применяется сразу по HTTP')

}



$btnPult.Location    = New-Object System.Drawing.Point(760, 15)

$btnReboot.Location  = New-Object System.Drawing.Point(886, 15)

$btnSerial.Location  = New-Object System.Drawing.Point(1020, 15)



# подсказки в шапке

$script:tipMain = New-Object System.Windows.Forms.ToolTip

$script:tipMain.AutoPopDelay = 20000

$script:tipMain.InitialDelay = 350

$script:tipMain.ReshowDelay = 200

$script:tipMain.ShowAlways = $true

$script:tipMain.SetToolTip($btnPult, 'включить/выключить подсветку экрана платы (команда disp toggle по serial; нужен открытый порт)')

$script:tipMain.SetToolTip($btnReboot, 'перезагрузить плату (команда reboot по serial; нужен открытый порт)')

$script:tipMain.SetToolTip($btnSerial, 'открыть/закрыть COM-порт для команд Пульт и Перезагрузить; данные идут по HTTP')

$script:tipMain.SetToolTip($lblFwChip, 'версия прошивки: из /data, из serial или со страницы платы (CYD-4.4)')



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

    $b.FlatAppearance.MouseOverBackColor = C '#222836'

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

    if ($key -eq 'screen') { $chartScreen.Invalidate() }

    if ($key -eq 'hist')   { Layout-History; $chartHist.Invalidate() }

    if ($key -eq 'dev')    { Layout-Device }

    if ($key -eq 'flash')  { Layout-Flash }

    if ($key -eq 'log')    { Layout-Log }

    if ($key -eq 'prot')   { Layout-Prot; Load-Prot }

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



$lcd = New-Object System.Windows.Forms.Label

$lcd.Font = Fnt 'Consolas' 12.5 $true

$lcd.BackColor = C $script:CLR.lcdBg

$lcd.ForeColor = C $script:CLR.lcdFg

$lcd.Location = New-Object System.Drawing.Point(0, 20)

$lcd.Size = New-Object System.Drawing.Size(356, 236)

$lcd.Padding = New-Object System.Windows.Forms.Padding(12, 10, 10, 8)

$lcd.Text = "DOM-MONITOR`n`n  ожидание данных…"

$lcdWrap.Controls.Add($lcd)



$lblLcdHint = New-Lbl $lcdWrap 'зеркало OLED-экрана платы' 7.5 $script:CLR.dim2 $false 2 260



function New-Card($parent, [string]$caption, [float]$valSize) {

    $p = New-Object System.Windows.Forms.Panel

    $p.BackColor = C $script:CLR.card

    $p.Size = New-Object System.Drawing.Size(200, 132)



    $cap = New-Lbl $p $caption 7.5 $script:CLR.dim $true 11 9



    $val = New-Object System.Windows.Forms.Label

    $val.Text = '—'

    $val.AutoSize = $false

    $val.Font = Fnt 'Segoe UI' $valSize $true

    $val.ForeColor = C $script:CLR.text

    $val.Location = New-Object System.Drawing.Point(10, 24)

    $val.Size = New-Object System.Drawing.Size(180, 34)

    $val.TextAlign = 'MiddleLeft'

    $p.Controls.Add($val)



    $sub = New-Object System.Windows.Forms.Label

    $sub.Text = ''

    $sub.AutoSize = $false

    $sub.AutoEllipsis = $true

    $sub.Font = Fnt 'Segoe UI' 8 $false

    $sub.ForeColor = C $script:CLR.dim2

    $sub.Location = New-Object System.Drawing.Point(11, 62)

    $sub.Size = New-Object System.Drawing.Size(180, 17)

    $p.Controls.Add($sub)



    # третья строка: когда показатель реально получен («получено 12:31»)

    $when = New-Object System.Windows.Forms.Label

    $when.Text = 'получено —'

    $when.AutoSize = $false

    $when.AutoEllipsis = $true

    $when.Font = Fnt 'Segoe UI' 7.5 $false

    $when.ForeColor = C $script:CLR.dim2

    $when.Location = New-Object System.Drawing.Point(11, 80)

    $when.Size = New-Object System.Drawing.Size(180, 16)

    $p.Controls.Add($when)



    # мини-график за сутки внизу карточки (рисуется по /history)

    $spark = New-Object System.Windows.Forms.PictureBox

    $spark.BackColor = C $script:CLR.card

    $spark.Location = New-Object System.Drawing.Point(10, 99)

    $spark.Size = New-Object System.Drawing.Size(180, 27)

    $spark.Cursor = 'Hand'

    $p.Controls.Add($spark)



    $parent.Controls.Add($p)

    return [pscustomobject]@{ Panel = $p; Cap = $cap; Val = $val; Sub = $sub; When = $when; Spark = $spark }

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



$chartScreen = New-Object System.Windows.Forms.PictureBox

$chartScreen.BackColor = C '#0F1218'

$chartScreen.Location = New-Object System.Drawing.Point(14, 300)

$chartScreen.Size = New-Object System.Drawing.Size(1000, 380)

$pgScreen.Controls.Add($chartScreen)



# --- общие объекты рисования (создаются один раз, чтобы не течь GDI) --------

$script:draw = @{

    fontAxis  = Fnt 'Segoe UI' 8 $false

    fontSmall = Fnt 'Segoe UI' 7.5 $false

    fontTitle = Fnt 'Segoe UI' 10 $true

    fontVal   = Fnt 'Segoe UI' 8.5 $true

    brDim     = New-Object System.Drawing.SolidBrush (C '#7D879B')

    brDim2    = New-Object System.Drawing.SolidBrush (C '#5C6575')

    brText    = New-Object System.Drawing.SolidBrush (C '#C3CCDA')

    brAccent  = New-Object System.Drawing.SolidBrush (C '#5AC8FF')

    brWarn    = New-Object System.Drawing.SolidBrush (C '#F2B44B')

    brOk      = New-Object System.Drawing.SolidBrush (C '#63D68B')

    brVolt    = New-Object System.Drawing.SolidBrush (C '#A78BFA')

    brBar     = New-Object System.Drawing.SolidBrush (C '#3E7FB8')

    brBarHi   = New-Object System.Drawing.SolidBrush (C '#5AC8FF')

    penGrid   = New-Object System.Drawing.Pen ((C '#1F2530'), 1)

    penGrid2  = New-Object System.Drawing.Pen ((C '#2A3240'), 1)

    penVolt   = New-Object System.Drawing.Pen ((C '#5AC8FF'), 2)

    penViolet = New-Object System.Drawing.Pen ((C '#A78BFA'), 1.6)

    penNow    = New-Object System.Drawing.Pen ((C '#F2B44B'), 1)

    brGap     = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(26, 242, 180, 75))

    brFill    = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(22, 90, 200, 255))

    # --- интерактивные подсказки и мини-графики

    brTipBg   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(244, 13, 16, 21))

    brTipText = New-Object System.Drawing.SolidBrush (C '#F2F6FC')

    brTipDim  = New-Object System.Drawing.SolidBrush (C '#9AA6B8')

    penTip    = New-Object System.Drawing.Pen ((C '#46536A'), 1)

    brHover   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(22, 90, 200, 255))

    penHover  = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(150, 90, 200, 255), 1)

    brBarDim  = New-Object System.Drawing.SolidBrush (C '#2B4A66')

    penTemp   = New-Object System.Drawing.Pen ((C '#F472B6'), 1.6)

    brTemp    = New-Object System.Drawing.SolidBrush (C '#F472B6')

    penVoltHi    = New-Object System.Drawing.Pen ((C '#5AC8FF'), 3)

    penTempHi    = New-Object System.Drawing.Pen ((C '#F472B6'), 3)

    penVioletDim = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(90, 167, 139, 250), 1.2)

    penTempDim   = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(90, 244, 114, 182), 1.2)

    penSparkV = New-Object System.Drawing.Pen ((C '#5AC8FF'), 1.5)

    penSparkP = New-Object System.Drawing.Pen ((C '#63D68B'), 1.5)

    penSparkT = New-Object System.Drawing.Pen ((C '#F472B6'), 1.5)

    penSparkE = New-Object System.Drawing.Pen ((C '#F2B44B'), 1.5)

    brSparkV  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(30, 90, 200, 255))

    brSparkP  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(30, 99, 214, 139))

    brSparkT  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(30, 244, 114, 182))

    brSparkE  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(30, 242, 180, 75))

}

$script:draw.penNow.DashStyle = 'Dash'



# ============================================================================

#  ПОДСКАЗКИ, МИНИ-ГРАФИКИ, ВЫБОР ПОКАЗАТЕЛЯ

# ============================================================================

# Рисуем подсказку сами: полупрозрачный прямоугольник + текст рядом с курсором.

# Первая строка — крупнее и светлее, остальные — мельче и серые.

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

function Get-HourMap([string]$date) {

    $map = @{}

    if ($script:histCache.ContainsKey($date)) {

        foreach ($r in @($script:histCache[$date])) {

            if ($null -ne $r.h) { $map[[int]$r.h] = $r }

        }

    }

    return $map

}



# строки подсказки для графика «Экран»: час, В, Вт, кВт·ч, n

function Get-ScreenHourLines([int]$hour) {

    $now = Get-Date

    $date = $now.ToString('yyyy-MM-dd')

    $map = Get-HourMap $date

    $hh = [Math]::Max(0, [Math]::Min(23, $hour))

    $head = ('{0:00}:00' -f $hh)

    if ($hh -eq $now.Hour) { $head += ' (час не закончился)' }

    if (-not $map.ContainsKey($hh)) { return @($head, 'нет данных за этот час') }

    $r = $map[$hh]

    $v = [double](Field $r @('v') 0)

    $p = [double](Field $r @('p') 0)

    $k = [double](Field $r @('kwh') 0)

    $n = [int](Field $r @('n') 0)

    $l1 = $head + ' · ' + (Num $v '0') + ' В · ' + (Num $p '0') + ' Вт · ' + (Num $k '0.00') + ' кВт·ч'

    $l2 = 'измерений: ' + $n

    return @($l1, $l2)

}



# часовые значения для спарклайна карточки (24 ячейки, $null = нет данных)

function Get-SparkValues([string]$metric) {

    $date = (Get-Date).ToString('yyyy-MM-dd')

    $out = New-Object 'object[]' 24

    for ($i = 0; $i -lt 24; $i++) { $out[$i] = $null }

    if (-not $script:histCache.ContainsKey($date)) { return ,$out }

    foreach ($r in @($script:histCache[$date])) {

        if ($null -eq $r.h) { continue }

        $hh = [int]$r.h

        if ($hh -lt 0 -or $hh -gt 23) { continue }

        $val = $null

        switch ($metric) {

            'v' { $x = [double](Field $r @('v') 0); if ($x -gt 0) { $val = $x } }

            'p' { $x = [double](Field $r @('p') 0); if ($x -gt 0) { $val = $x } }

            't' { $x = Field $r @('t'); if ($null -ne $x) { $val = [double]$x } }

            'e' { $x = [double](Field $r @('kwh') 0); if ($x -gt 0) { $val = $x } }

        }

        $out[$hh] = $val

    }

    return ,$out

}



function Draw-Sparkline($g, $pic, [string]$metric) {

    try {

        if ($null -eq $g -or $null -eq $pic) { return }

        $D = $script:draw

        $w = [int]$pic.Width; $h = [int]$pic.Height

        if ($w -lt 20 -or $h -lt 10) { return }

        $vals = Get-SparkValues $metric

        $pen = $D.penSparkE; $fill = $D.brSparkE

        switch ($metric) {

            'v' { $pen = $D.penSparkV; $fill = $D.brSparkV }

            'p' { $pen = $D.penSparkP; $fill = $D.brSparkP }

            't' { $pen = $D.penSparkT; $fill = $D.brSparkT }

            'e' { $pen = $D.penSparkE; $fill = $D.brSparkE }

        }

        $n = 0

        $min = 1.0e12; $max = -1.0e12

        for ($i = 0; $i -lt 24; $i++) {

            $vv = $vals[$i]

            if ($null -eq $vv) { continue }

            $n++

            if ($vv -lt $min) { $min = $vv }

            if ($vv -gt $max) { $max = $vv }

        }

        if ($n -lt 2) {

            $g.DrawString('история за сегодня недоступна', $D.fontSmall, $D.brDim2, 0, [float](($h - 12) / 2))

            return

        }

        if (($max - $min) -lt 0.001) { $min = $min - 0.5; $max = $max + 0.5 }

        $pad = 2

        $pts = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'

        for ($i = 0; $i -lt 24; $i++) {

            $vv = $vals[$i]

            if ($null -eq $vv) { continue }

            $px = [float]($pad + ($w - 2 * $pad) * $i / 23.0)

            $py = [float]($h - $pad - ($h - 2 * $pad) * (($vv - $min) / ($max - $min)))

            $pts.Add([System.Drawing.PointF]::new([single]$px, [single]$py))

        }

        if ($pts.Count -lt 2) { return }

        $poly = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'

        $poly.Add([System.Drawing.PointF]::new([single]$pts[0].X, [single]($h - 1)))

        foreach ($pt in $pts) { $poly.Add($pt) }

        $poly.Add([System.Drawing.PointF]::new([single]$pts[$pts.Count - 1].X, [single]($h - 1)))

        $g.FillPolygon($fill, $poly.ToArray())

        $g.DrawLines($pen, $pts.ToArray())

        $g.FillEllipse($pen.Brush, [float]($pts[$pts.Count - 1].X - 2), [float]($pts[$pts.Count - 1].Y - 2), 4, 4)

    } catch { Write-AppLog ('spark: ошибка ' + $_.Exception.Message) }

}



# клик по карточке: «История» с выбранным показателем

function Open-CardHistory([string]$metric) {

    if (-not $script:cardMetric.Contains($metric)) { return }

    $script:histMetric = $metric

    $script:histSel = (Get-Date).ToString('yyyy-MM-dd')

    Request-Day $script:histSel

    Show-Page 'hist'

    Update-HistoryView

    Add-Log ('карточка «' + $script:cardSparkTitle[$metric] + '» → вкладка «История»')

}



function Layout-Screen {

    if (-not $script:uiReady) { return }

    $pw = $pgScreen.ClientSize.Width

    $ph = $pgScreen.ClientSize.Height

    if ($pw -lt 500 -or $ph -lt 380) { return }



    $m = 14

    $lcdW = 356

    $lcdWrap.Location = New-Object System.Drawing.Point($m, 12)

    $lcdWrap.Size = New-Object System.Drawing.Size($lcdW, 278)

    $lcd.Size = New-Object System.Drawing.Size($lcdW, 236)

    $lblLcdHint.Location = New-Object System.Drawing.Point(2, 258)



    $cx = $m + $lcdW + $m

    $cw = $pw - $cx - $m

    $gap = 10

    $cardH = 132

    $cardW = [int](($cw - 2 * $gap) / 3)

    if ($cardW -lt 120) { $cardW = 120 }



    $i = 0

    foreach ($k in @($script:cards.Keys)) {

        $c = $script:cards[$k]

        $col = $i % 3

        $row = [Math]::Floor($i / 3)

        $c.Panel.Size = New-Object System.Drawing.Size($cardW, $cardH)

        $c.Panel.Location = New-Object System.Drawing.Point(($cx + $col * ($cardW + $gap)), ($m + $row * ($cardH + $gap)))

        $c.Val.Width = $cardW - 20

        $c.Sub.Width = $cardW - 20

        $c.When.Width = $cardW - 20

        if ($null -ne $c.Spark) {

            $c.Spark.Location = New-Object System.Drawing.Point(10, ($cardH - 33))

            $c.Spark.Size = New-Object System.Drawing.Size(($cardW - 20), 27)

        }

        $i++

    }



    $chartTop = $m + 3 * $cardH + 2 * $gap + $m

    $chartH = $ph - $chartTop - $m

    if ($chartH -lt 110) { $chartH = 110 }

    $chartScreen.Location = New-Object System.Drawing.Point($m, $chartTop)

    $chartScreen.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $chartH)

    $chartScreen.Invalidate()

}



# ----------------------------------------------------------------- график дня

$chartScreen.Add_Paint({

    param($sender, $e)

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

        $g.DrawString('В', $D.fontAxis, $D.brDim, 12, [float]($padT - 14))



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

        for ($hh = 0; $hh -lt 24; $hh++) {

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



        # линия (с разрывом на пропусках)

        $prev = $null

        foreach ($p in ($pts | Sort-Object x)) {

            if ($null -ne $prev -and ($p.x - $prev.x) -lt 6) {

                $g.DrawLine($D.penVolt, [float](& $xOf $prev.x), [float](& $yOf $prev.v),

                            [float](& $xOf $p.x), [float](& $yOf $p.v))

            }

            $prev = $p

        }



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



        # подпись справа: мин / сред / макс

        $stat = 'мин ' + (Num $minV '0') + ' · сред ' + (Num $avgV '0') + ' · макс ' + (Num $maxV '0') + ' В'

        $sz2 = $g.MeasureString($stat, $D.fontAxis)

        $g.DrawString($stat, $D.fontAxis, $D.brDim, [float]($padL + $plotW - $sz2.Width), 13)



        # пометка о пропусках

        if ($null -ne $gapsToday -and [int]$gapsToday[0] -gt 0) {

            $gm = [int]$gapsToday[1]

            $gt = 'пропуски: ' + [int]$gapsToday[0] + ' (' + (Format-Age ($gm * 60)) + ')'

            $g.DrawString($gt, $D.fontAxis, $D.brWarn, [float]$padL, 30)

        } elseif ($null -ne $gapsToday) {

            $g.DrawString('пропусков нет — данные за сутки полные', $D.fontAxis, $D.brOk, [float]$padL, 30)

        }



        # --- интерактив: подсветка часа и подсказка (наведение или клик)

        $hhTip = $script:hoverScr

        if ($hhTip -lt 0) { $hhTip = $script:pinScr }

        if ($hhTip -ge 0 -and $hhTip -le 23) {

            $x1 = & $xOf $hhTip

            $x2 = & $xOf ($hhTip + 1)

            $g.FillRectangle($D.brHover, [float]$x1, [float]$padT, [float]($x2 - $x1), [float]$plotH)

            $g.DrawLine($D.penHover, [float]$x1, [float]$padT, [float]$x1, [float]($padT + $plotH))

            $g.DrawLine($D.penHover, [float]$x2, [float]$padT, [float]$x2, [float]($padT + $plotH))

            $tipLines = Get-ScreenHourLines $hhTip

            # при наведении — у курсора, при закреплённом клике — у выбранного часа

            $mx = if ($script:hoverScr -ge 0 -and $script:mouseScr.X -ge 0) { [int]$script:mouseScr.X } else { [int]($x1 + $plotW / 24.0 * 0.5) }

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

    $grid.BackgroundColor = C '#12161C'

    $grid.BorderStyle = 'None'

    $grid.EnableHeadersVisualStyles = $false

    $grid.ColumnHeadersDefaultCellStyle.BackColor = C '#232A36'

    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C '#E6ECF5'

    $grid.ColumnHeadersDefaultCellStyle.SelectionBackColor = C '#232A36'

    $grid.ColumnHeadersDefaultCellStyle.Font = Fnt 'Segoe UI' 8.5 $true

    $grid.ColumnHeadersHeight = 28

    $grid.DefaultCellStyle.BackColor = C '#171B22'

    $grid.DefaultCellStyle.ForeColor = C '#D5DCE7'

    $grid.DefaultCellStyle.SelectionBackColor = C '#2A3444'

    $grid.DefaultCellStyle.SelectionForeColor = C '#FFFFFF'

    $grid.AlternatingRowsDefaultCellStyle.BackColor = C '#14181E'

    $grid.GridColor = C '#252C38'

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

$btnReload  = New-Btn $pgHist 'Обновить' '#2A3240' $script:CLR.text 100 30

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



$chartHist = New-Object System.Windows.Forms.PictureBox

$chartHist.BackColor = C '#0F1218'

$pgHist.Controls.Add($chartHist)



$btnHistTg = New-Btn $pgHist 'Отчёт в Telegram' '#24405A' $script:CLR.text 160 30

$btnHistCsv = New-Btn $pgHist 'CSV дня' '#2A4034' $script:CLR.text 100 30



$chartHist.Add_Paint({

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

            if ($script:histTaskDate -eq $date -or $script:histPending -eq $date) { $msg = 'загрузка…' }

            elseif ($script:histErrDate -eq $date -and $script:histErr) { $msg = $script:histErr }

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



        # линия напряжения

        if ($hasVolt) {

            $vPen = $D.penViolet

            if ($metric -eq 'v') { $vPen = $D.penVoltHi }

            elseif ($metric -eq 't' -or $metric -eq 'e') { $vPen = $D.penVioletDim }

            $prev = $null

            for ($hh = 0; $hh -lt 24; $hh++) {

                if (-not $map.ContainsKey($hh)) { $prev = $null; continue }

                $vm = [double](Field $map[$hh] @('v') 0)

                if ($vm -le 0) { $prev = $null; continue }

                $px = [float]($padL + $slot * ($hh + 0.5))

                $py = [float]($padT + $plotH - $plotH * (($vm - $vLo) / ($vHi - $vLo)))

                if ($null -ne $prev) {

                    $g.DrawLine($vPen, [float]$prev.x, [float]$prev.y, $px, $py)

                }

                $g.FillEllipse($D.brVolt, [float]($px - 2), [float]($py - 2), 4, 4)

                $prev = [pscustomobject]@{ x = $px; y = $py }

            }

        }



        # линия температуры (своя шкала, подписи — в легенде и подсказке)

        if ($hasTemp) {

            $tPen = $D.penTemp

            if ($metric -eq 't') { $tPen = $D.penTempHi }

            elseif ($metric -eq 'v' -or $metric -eq 'e') { $tPen = $D.penTempDim }

            $prevT = $null

            for ($hh = 0; $hh -lt 24; $hh++) {

                if (-not $map.ContainsKey($hh)) { $prevT = $null; continue }

                $tm = Field $map[$hh] @('t')

                if ($null -eq $tm) { $prevT = $null; continue }

                $tf = [double]$tm

                $px = [float]($padL + $slot * ($hh + 0.5))

                $py = [float]($padT + $plotH - $plotH * (($tf - $tMin) / ($tMax - $tMin)))

                if ($null -ne $prevT) {

                    $g.DrawLine($tPen, [float]$prevT.x, [float]$prevT.y, $px, $py)

                }

                $g.FillEllipse($D.brTemp, [float]($px - 2), [float]($py - 2), 4, 4)

                $prevT = [pscustomobject]@{ x = $px; y = $py }

            }

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

    $hh = [Math]::Max(0, [Math]::Min(23, $hour))

    $head = ('{0:00}:00–{1:00}:00' -f $hh, ($hh + 1))

    $now = Get-Date

    $partial = ($date -eq $now.ToString('yyyy-MM-dd') -and $hh -eq $now.Hour)

    $map = Get-HourMap $date

    if (-not $map.ContainsKey($hh)) {

        if ($partial) { $head += ' (час не закончился)' }

        return @($head, 'нет записей за этот час')

    }

    $r = $map[$hh]

    $n = [int](Field $r @('n') 0)

    $k = [double](Field $r @('kwh') 0)

    $p = [double](Field $r @('p') 0)

    $pm = [double](Field $r @('pmax') 0)

    # Время и длительность пика — прямо в подсказке: «пик 1840 Вт в 14:23, 3 мин».

    $pkT = Format-PeakTime $r

    $pkD = Format-PeakDur $r

    $pkTxt = (Num $pm '0') + ' Вт'

    if ($pkT -ne '—') { $pkTxt += ' в ' + $pkT }

    if ($pkD -ne '—') { $pkTxt += ', ' + $pkD }

    $vals = (Num $k '0.00') + ' кВт·ч · средняя ' + (Num $p '0') + ' Вт · пик ' + $pkTxt + ' · ' + (Format-Records $n)

    if ($partial) { return @(($head + ' (час не закончился)'), $vals) }

    return @(($head + ' · ' + (Num $k '0.00') + ' кВт·ч'), ('средняя ' + (Num $p '0') + ' Вт · пик ' + $pkTxt + ' · ' + (Format-Records $n)))

}



$chartHist.Add_MouseMove({

    param($sender, $e)

    $hr = Get-HistChartHour $e.X $e.Y $sender.Width $sender.Height

    $script:mouseHist = New-Object System.Drawing.Point -ArgumentList $e.X, $e.Y

    if ($hr -ne $script:hoverHist) {

        $script:hoverHist = $hr

        $sender.Invalidate()

    }

})



$chartHist.Add_MouseDown({

    param($sender, $e)

    $hr = Get-HistChartHour $e.X $e.Y $sender.Width $sender.Height

    if ($hr -ge 0) {

        $script:pinHist = if ($script:pinHist -eq $hr) { -1 } else { $hr }

        Add-Log ('история: выбран час ' + ('{0:00}:00–{1:00}:00' -f $hr, ($hr + 1)))

        $sender.Invalidate()

    } else {

        $script:pinHist = -1

        $sender.Invalidate()

    }

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
    $btnHistCsv.Location = New-Object System.Drawing.Point(($pw - $m - 100), 12)

    $btnHistTg.Location = New-Object System.Drawing.Point(($pw - $m - 100 - 8 - 160), 12)

    $lblHistSum.TextAlign = 'MiddleLeft'

    $lblHistSum.Size = New-Object System.Drawing.Size(($pw - 2 * $m), 22)

    $lblHistSum.Location = New-Object System.Drawing.Point($m, 50)



    $top2 = 78

    $availH = $ph - $top2 - $m



    if ($pw -ge 1020) {

        $tableW = [int](($pw - 3 * $m) * 0.55)

        $gridHist.Location = New-Object System.Drawing.Point($m, $top2)

        $gridHist.Size = New-Object System.Drawing.Size($tableW, $availH)

        $chartHist.Location = New-Object System.Drawing.Point(($m + $tableW + $m), $top2)

        $chartHist.Size = New-Object System.Drawing.Size(($pw - $tableW - 3 * $m), $availH)

    } else {

        $tableH = [int]($availH * 0.52)

        $gridHist.Location = New-Object System.Drawing.Point($m, $top2)

        $gridHist.Size = New-Object System.Drawing.Size(($pw - 2 * $m), $tableH)

        $chartHist.Location = New-Object System.Drawing.Point($m, ($top2 + $tableH + $m))

        $chartHist.Size = New-Object System.Drawing.Size(($pw - 2 * $m), ($availH - $tableH - $m))

    }

    $chartHist.Invalidate()

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



# состояние связи с платой: «плата не отвечает: 192.168.0.51» + кнопка мастера

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

$txtSketch.BackColor = C '#12161C'

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

$txtFqbn.BackColor = C '#12161C'

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

$txtFlashLog.BackColor = C '#0A0C10'

$txtFlashLog.ForeColor = C '#9BE3B4'

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
    $tb.BackColor = C '#0F1218'
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
        $r = Invoke-RestMethod ("http://" + $ip + $q) -TimeoutSec 90
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

$script:txtLog.BackColor = C '#0A0C10'

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

    $age = Get-BoardAge

    $comOpen = ($null -ne $script:port -and $script:port.IsOpen)

    if ($null -ne $script:board -and $age -le 12) {

        Set-Lbl $lblHttp ('● HTTP ' + [int]$age + ' с · ' + (Num (Field $script:board @('p') 0) '0') + ' Вт')

        $lblHttp.ForeColor = C $script:CLR.ok

    } elseif ($comOpen -and $null -ne $script:serialData -and (((Get-Date) - $script:serialAt).TotalSeconds) -le 90) {

        # резерв — только когда порт действительно открыт монитором,

        # иначе в шапке одновременно стояли «РЕЗЕРВ COM5» и «COM5 закрыт»

        Set-Lbl $lblHttp '● РЕЗЕРВ COM5'

        $lblHttp.ForeColor = C $script:CLR.warn

    } else {

        $txt = '● НЕТ СВЯЗИ'

        if ($null -ne $script:board -and $age -lt 86400) { $txt += ' · ' + [int]$age + ' с' }

        Set-Lbl $lblHttp $txt

        $lblHttp.ForeColor = C $script:CLR.err

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



function Set-Card($key, [string]$value, [string]$sub, [string]$valueColor, [string]$subColor) {

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

    Set-Card 'v' ((Num $v '0') + ' В') $subV $txtMain $colV



    # ------------------------------------------------ температура

    $t = Field $d @('temp', 'temperature')

    if ($null -ne $t -and [double]$t -gt 0) {

        $tf = [double]$t

        $tc = $txtMain

        if ($tf -ge 60) { $tc = '#E85D5D' } elseif ($tf -ge 45) { $tc = '#F2B44B' }

        Set-Card 't' ((Num $tf '0.0') + ' °C') 'корпус реле' $tc $txtSub

    } else {

        Set-Card 't' '—' 'реле не сообщает' $script:CLR.dim2 $txtSub

    }



    # ------------------------------------------------ реле

    $r = [bool](Field $d @('r', 'switch') $false)

    if ($r) { Set-Card 'r' 'ВКЛ' 'питание подаётся' '#63D68B' $txtSub }

    else    { Set-Card 'r' 'ВЫКЛ' 'питание снято' '#E85D5D' $txtSub }

    $rc = $script:cards['r']

    if ($null -ne $rc) { Set-Lbl $rc.When '' }   # у реле нет своего времени получения



    # ------------------------------------------------ мощность и ток

    $p = [double](Field $d @('p', 'power') 0)

    $amp = Field $d @('amp')

    $ampTxt = '—'

    if ($null -ne $amp -and [double]$amp -gt 0) { $ampTxt = (Num ([double]$amp) '0.00') + ' А' }

    elseif ($v -gt 50) { $ampTxt = (Num ($p / $v) '0.00') + ' А (P/V)' }

    Set-Card 'p' ((Num $p '0.0') + ' Вт') ('ток ' + $ampTxt) $txtMain $txtSub



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

        $msg = 'нет данных'

        if ($script:histTaskDate -eq $date -or $script:histPending -eq $date) { $msg = 'загрузка…' }

        elseif ($script:histErrDate -eq $date -and $script:histErr) { $msg = $script:histErr }

        Set-Lbl $lblHistSum ($date + ' — ' + $msg)

        $gridHist.ResumeLayout()

        $chartHist.Invalidate()

        return

    }



    $map = @{}

    foreach ($r in $hours) { if ($null -ne $r.h) { $map[[int]$r.h] = $r } }



    $sumKwh = 0.0

    $sumN = 0

    $sumPN = 0.0

    $maxPmax = 0.0

    $maxPmaxT = '—'

    $maxPmaxDur = '—'

    $vMin = 9999.0

    $vMax = 0.0

    $tSum = 0.0

    $tN = 0

    $emptyH = 0

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

        $kwh = [double](Field $r @('kwh') 0)

        $pp  = [double](Field $r @('p') 0)

        $pm  = [double](Field $r @('pmax') 0)

        $vm  = [double](Field $r @('v') 0)

        $tm  = Field $r @('t')

        $nn  = [int](Field $r @('n') 0)

        $pkT = Format-PeakTime $r

        $pkD = Format-PeakDur $r



        $sumKwh += $kwh

        $sumN += $nn

        $sumPN += $pp * $nn

        if ($pm -gt $maxPmax) { $maxPmax = $pm; $maxPmaxT = $pkT; $maxPmaxDur = $pkD }

        if ($vm -gt 0) { if ($vm -lt $vMin) { $vMin = $vm }; if ($vm -gt $vMax) { $vMax = $vm } }

        if ($null -ne $tm) { $tSum += [double]$tm; $tN++ }

        if ($nn -eq 0) { $emptyH++ }



        $idx = $gridHist.Rows.Add(

            $hhTxt,

            (Num $kwh '0.00'),

            (Num $pp '0'),

            (Num $pm '0'),

            $pkT,

            $pkD,

            $(if ($vm -gt 0) { Num $vm '0' } else { '—' }),

            $(if ($null -ne $tm) { Num $tm '0.0' } else { '—' }),

            [string]$nn)



        if ($nn -eq 0) {

            $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#5C6575'

        } elseif ($isToday -and $hh -eq $now.Hour) {

            $gridHist.Rows[$idx].DefaultCellStyle.ForeColor = C '#5AC8FF'

        }

    }



    $avgP = if ($sumN -gt 0) { $sumPN / $sumN } else { 0 }

    $vTxt = if ($vMax -gt 0) { (Num $vMin '0') + '–' + (Num $vMax '0') } else { '—' }

    $tTxt = if ($tN -gt 0) { Num ($tSum / $tN) '0.0' } else { '—' }

    $idx = $gridHist.Rows.Add('ИТОГО', (Num $sumKwh '0.00'), (Num $avgP '0'), (Num $maxPmax '0'), $maxPmaxT, $maxPmaxDur, $vTxt, $tTxt, [string]$sumN)

    $gridHist.Rows[$idx].DefaultCellStyle.Font = Fnt 'Segoe UI' 8.5 $true

    $gridHist.Rows[$idx].DefaultCellStyle.BackColor = C '#232A36'

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



    # хвост: когда реально получены данные, из которых считается день

    $freshTxt = ''

    try {

        $fs = Get-FreshestStamp

        if ($null -ne $fs) { $freshTxt = ' · данные получены ' + (Format-Stamp $fs).Replace('получено ', '') }

        else { $freshTxt = ' · данные получены —' }

    } catch { }



    Set-Lbl $lblHistSum ($date + ' · ' + (Num $sumKwh '0.00') + ' кВт·ч' + $costTxt + ' · записей ' + $sumN + $gTxt + $freshTxt)



    $gridHist.ResumeLayout()

    $chartHist.Invalidate()

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







function Update-RangeGrid($grid, $sum, $ip, $from, $to) {
    if ([datetime]$from -gt [datetime]$to) { $x = $from; $from = $to; $to = $x }
    $sum.Text = 'считаю…'
    try {
        # Плата отдаёт максимум 31 день за запрос — длинные периоды режем на куски
        $dA = [datetime]$from
        $dB = [datetime]$to
        $allRows = @()
        $sumK = 0.0; $sumC = 0.0; $sumD = 0.0; $sumN = 0.0; $sumDC = 0.0; $sumNC = 0.0
        $vminAll = 0; $vmaxAll = 0; $pmaxAll = 0
        while ($dA -le $dB) {
            $chunkEnd = $dA.AddDays(30)
            if ($chunkEnd -gt $dB) { $chunkEnd = $dB }
            $ca = $dA.ToString('yyyy-MM-dd')
            $cb = $chunkEnd.ToString('yyyy-MM-dd')
            $part = Invoke-RestMethod ("http://" + $ip + "/range?from=" + $ca + "&to=" + $cb) -TimeoutSec 120
            $allRows += @($part.rows)
            $ct = $part.totals
            $sumK += [double]$ct.kwh; $sumC += [double]$ct.cost
            $sumD += [double]$ct.dayKwh; $sumN += [double]$ct.nightKwh
            $sumDC += [double]$ct.dayCost; $sumNC += [double]$ct.nightCost
            if ([int]$ct.vmin -gt 0 -and ($vminAll -eq 0 -or [int]$ct.vmin -lt $vminAll)) { $vminAll = [int]$ct.vmin }
            if ([int]$ct.vmax -gt $vmaxAll) { $vmaxAll = [int]$ct.vmax }
            if ([int]$ct.pmax -gt $pmaxAll) { $pmaxAll = [int]$ct.pmax }
            $dA = $chunkEnd.AddDays(1)
        }
        $r = [pscustomobject]@{
            rows = $allRows
            totals = [pscustomobject]@{
                kwh = $sumK; cost = $sumC; dayKwh = $sumD; nightKwh = $sumN
                dayCost = $sumDC; nightCost = $sumNC
                avgDayKwh = $(if ($allRows.Count -gt 0) { $sumK / $allRows.Count } else { 0 })
                vmin = $vminAll; vmax = $vmaxAll; pmax = $pmaxAll
            }
        }
        $script:rangeRows = @($r.rows)
        $grid.Rows.Clear()
        foreach ($row in $r.rows) {
            $null = $grid.Rows.Add(
                $row.d, [math]::Round($row.kwh, 2), [math]::Round($row.cost, 2),
                [math]::Round($row.dayKwh, 2), [math]::Round($row.nightKwh, 2),
                [math]::Round($row.dayCost, 2), [math]::Round($row.nightCost, 2),
                ([string]$row.vmin + '–' + [string]$row.vmax),
                ([string]$row.tmin + '–' + [string]$row.tmax), $row.pmax)
        }
        $t = $r.totals
        $sum.Text = ('итого ' + [math]::Round($t.kwh, 2) + ' кВт·ч · ' + [math]::Round($t.cost, 2) + ' ₽' +
                     ' · день ' + [math]::Round($t.dayKwh, 2) + ' / ночь ' + [math]::Round($t.nightKwh, 2) +
                     ' · в среднем ' + [math]::Round($t.avgDayKwh, 2) + ' кВт·ч в сутки' +
                     ' · U ' + $t.vmin + '–' + $t.vmax + ' В · пик ' + $t.pmax + ' Вт')
        Add-Log ('период ' + $from + '…' + $to + ': ' + [math]::Round($t.kwh, 2) + ' кВт·ч, ' + [math]::Round($t.cost, 2) + ' ₽')
        return $r
    } catch {
        $sum.Text = 'ошибка: ' + $_.Exception.Message
        Add-Log ('период: ошибка ' + $_.Exception.Message)
        return $null
    }
}

function Show-Sounds {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.0.51' }
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
    $grid.BackgroundColor = C '#0F1218'
    $grid.GridColor = C '#232B39'
    $grid.BorderStyle = 'None'
    $grid.EnableHeadersVisualStyles = $false
    $grid.ColumnHeadersDefaultCellStyle.BackColor = C '#151B26'
    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $grid.DefaultCellStyle.BackColor = C '#0F1218'
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
    $btnTest.BackColor = C '#24405A'
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
    $btnMute.BackColor = C '#2A3240'
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
    $btnSave.BackColor = C '#24405A'
    $btnSave.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnSave)

    $script:sndPresets = @()
    $script:sndEvents = @()

    $load = {
        try {
            $s = Invoke-RestMethod ('http://' + $ip + '/sound') -TimeoutSec 15
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
                Invoke-RestMethod ('http://' + $ip + '/beep?preset=' + $pid2) -TimeoutSec 10 | Out-Null
                $st.Text = ('проиграл: ' + $pname)
            }
        } catch { $st.Text = 'ошибка прослушивания' }
    })

    $btnMute.Add_Click({
        try { Invoke-RestMethod ('http://' + $ip + '/sound?mute=30') -TimeoutSec 10 | Out-Null; $st.Text = 'приглушено на 30 минут' }
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
            Invoke-RestMethod $u -TimeoutSec 15 | Out-Null
            $st.Text = 'сохранено в плате'
            Add-Log ('звуки: настройки сохранены (vol ' + [int]$numVol.Value + '%)') 'ok'
        } catch { $st.Text = 'ошибка сохранения: ' + $_.Exception.Message }
    })

    $f.Add_Shown({ & $load })
    [void]$f.ShowDialog()
    $f.Dispose()
}

function Show-Range {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.0.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Статистика за период — ' + $ip
    $f.Size = New-Object System.Drawing.Size(880, 560)
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
        $b.BackColor = C '#24405A'
        $b.ForeColor = C $script:CLR.text
        $b.Font = $font
        $f.Controls.Add($b)
        $x += 88
    }

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
    $btnGo.BackColor = C '#24405A'
    $btnGo.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnGo)

    $btnFill = New-Object System.Windows.Forms.Button
    $btnFill.Text = 'Заполнить пропуски'
    $btnFill.Location = New-Object System.Drawing.Point(552, 44)
    $btnFill.Size = New-Object System.Drawing.Size(150, 28)
    $btnFill.FlatStyle = 'Flat'
    $btnFill.BackColor = C '#24405A'
    $btnFill.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnFill)

    $btnCsv = New-Object System.Windows.Forms.Button
    $btnCsv.Text = 'CSV за период'
    $btnCsv.Location = New-Object System.Drawing.Point(412, 44)
    $btnCsv.Size = New-Object System.Drawing.Size(130, 28)
    $btnCsv.FlatStyle = 'Flat'
    $btnCsv.BackColor = C '#2A3240'
    $btnCsv.ForeColor = C $script:CLR.text
    $f.Controls.Add($btnCsv)

    $sum = New-Object System.Windows.Forms.Label
    $sum.Location = New-Object System.Drawing.Point(12, 80)
    $sum.Size = New-Object System.Drawing.Size(840, 22)
    $sum.ForeColor = C $script:CLR.dim2
    $sum.Text = 'выбери период'
    $f.Controls.Add($sum)

    $grid = New-Object System.Windows.Forms.DataGridView
    $grid.Location = New-Object System.Drawing.Point(12, 108)
    $grid.Size = New-Object System.Drawing.Size(840, 400)
    $grid.BackgroundColor = C '#0F1218'
    $grid.GridColor = C '#232B39'
    $grid.BorderStyle = 'None'
    $grid.EnableHeadersVisualStyles = $false
    $grid.ColumnHeadersDefaultCellStyle.BackColor = C '#151B26'
    $grid.ColumnHeadersDefaultCellStyle.ForeColor = C '#8FA3BF'
    $grid.DefaultCellStyle.BackColor = C '#0F1218'
    $grid.DefaultCellStyle.ForeColor = C '#D6E2F5'
    $grid.DefaultCellStyle.SelectionBackColor = C '#24405A'
    $grid.DefaultCellStyle.SelectionForeColor = C '#FFFFFF'
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

    $script:rangeRows = @()


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

    $btnCsv.Add_Click({
        if (-not $script:rangeRows -or $script:rangeRows.Count -eq 0) { return }
        $dlg = New-Object System.Windows.Forms.SaveFileDialog
        $dlg.Filter = 'CSV (*.csv)|*.csv'
        $dlg.FileName = 'energy-' + $d1.Value.ToString('yyyy-MM-dd') + '_' + $d2.Value.ToString('yyyy-MM-dd') + '.csv'
        if ($dlg.ShowDialog() -eq 'OK') {
            $sb = New-Object System.Text.StringBuilder
            $inv = [System.Globalization.CultureInfo]::InvariantCulture
            [void]$sb.AppendLine('date;kwh;cost;day_kwh;night_kwh;day_cost;night_cost;vmin;vmax;tmin;tmax;pmax')
            foreach ($row in $script:rangeRows) {
                $vals = @($row.d,
                    ([double]$row.kwh).ToString('0.###', $inv),
                    ([double]$row.cost).ToString('0.##', $inv),
                    ([double]$row.dayKwh).ToString('0.###', $inv),
                    ([double]$row.nightKwh).ToString('0.###', $inv),
                    ([double]$row.dayCost).ToString('0.##', $inv),
                    ([double]$row.nightCost).ToString('0.##', $inv),
                    [string]$row.vmin, [string]$row.vmax,
                    ([double]$row.tmin).ToString('0.#', $inv),
                    ([double]$row.tmax).ToString('0.#', $inv),
                    [string]$row.pmax)
                [void]$sb.AppendLine($vals -join ';')
            }
            [System.IO.File]::WriteAllText($dlg.FileName, $sb.ToString(), [System.Text.Encoding]::UTF8)
            Add-Log ('CSV за период: ' + $dlg.FileName)
        }
    })

    $d1.Value = (Get-Date).AddDays(-6)
    $d2.Value = (Get-Date)
    $f.Add_Shown({
        $null = Update-RangeGrid $grid $sum $ip $d1.Value.ToString('yyyy-MM-dd') $d2.Value.ToString('yyyy-MM-dd')
    })
    [void]$f.ShowDialog()
    $f.Dispose()
}

function Show-Scheduler {
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { $ip = '192.168.0.51' }
    $f = New-Object System.Windows.Forms.Form
    $f.Text = 'Планировщик реле — ' + $ip
    $f.Size = New-Object System.Drawing.Size(560, 440)
    $f.StartPosition = 'CenterParent'
    $f.BackColor = C $script:CLR.bg
    $f.ForeColor = C $script:CLR.text

    $list = New-Object System.Windows.Forms.ListBox
    $list.Location = New-Object System.Drawing.Point(14, 14)
    $list.Size = New-Object System.Drawing.Size(516, 210)
    $list.BackColor = C '#0F1218'
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
            Invoke-RestMethod ("http://" + $ip + $q) -TimeoutSec 8 | Out-Null
            Invoke-RestMethod ("http://" + $ip + "/sched?allow=" + $(if ($chkAllow.Checked) { 1 } else { 0 })) -TimeoutSec 8 | Out-Null
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
        $miWeb.Add_Click({ try { Start-Process ('http://' + $script:cfg.boardIp + '/') } catch { } })
        $miFind = $menu.Items.Add('Найти плату в сети')
        $miFind.Add_Click({ try { Find-Board -Quiet:$false } catch { } })
        $menu.Items.Add('-') | Out-Null
        $miExit = $menu.Items.Add('Выход')
        $miExit.Add_Click({ try { $script:exiting = $true; $form.Close() } catch { } })
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
    if (-not $base) { $base = '192.168.0.51' }
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

        $token = [string]$script:cfg.githubToken

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
    $cand += 'D:\DSH-Workspace\timelapse\watchdog\energy-telegram.ps1'
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
    # /data не отдаёт счётчик срабатываний — раз в минуту спрашиваем /protect.
    $ip = [string]$script:cfg.boardIp
    if (-not $ip) { return }
    $now = Get-Date
    if ($script:protAt -and ($now - $script:protAt).TotalSeconds -lt 60) { return }
    $script:protAt = $now
    try { $pf = Invoke-RestMethod ('http://' + $ip + '/protect') -TimeoutSec 8 } catch { return }
    $trig = 0; try { $trig = [int]$pf.trig } catch { }
    if ($null -ne $script:protTrigPrev -and $trig -gt $script:protTrigPrev) {
        $txt = 'отключений: ' + $trig + ' · U ' + $pf.vmin + '–' + $pf.vmax + ' В, t ' + $pf.tmax + ' °C'
        Alert-Once 'prot' 'Защита сработала' $txt 'err'
        if ([int]$pf.on -eq 0) { Alert-Once 'protoff' 'Защита выключена' 'в автомате защита не активна' 'warn' }
    }
    $script:protTrigPrev = $trig
}

function Check-Alerts {

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

        Pump-Board

        Pump-Firmware

        Pump-Days

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

            if ($pgScreen.Visible) { $chartScreen.Invalidate() }

            if ((($now - $script:lastDev).TotalSeconds) -ge 3) {

                $script:lastDev = $now

                Update-Device

            }

        }

        Request-Day($now.ToString('yyyy-MM-dd'))

        if ($script:histSel -ne $now.ToString('yyyy-MM-dd')) { Request-Day($script:histSel) }

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



    $buildPath = Join-Path $env:TEMP 'dommon-build-esp32'

    if (Test-Path $buildPath) { Remove-Item $buildPath -Recurse -Force -ErrorAction SilentlyContinue }



    $script:job = Start-Job -ScriptBlock {

        param($cli, $libs, $sketch, $buildPath, $doUpload, $com, $log, $fqbn, $what)



        function Emit($t) { Add-Content -Path $log -Value $t -Encoding UTF8 }



        Emit '=== СБОРКА ==='

        Emit $what

        Emit "скетч: $sketch"

        Emit "fqbn:  $fqbn"

        $args1 = @('compile', '--fqbn', $fqbn, '--libraries', $libs,

                   '--build-property', 'build.extra_flags=-DESP32 -DCORE_DEBUG_LEVEL=0',

                   '--build-path', $buildPath, $sketch)

        $out = & $cli @args1 2>&1

        foreach ($l in $out) { Emit ([string]$l) }

        if ($LASTEXITCODE -ne 0) { Emit ''; Emit "=== СБОРКА НЕ УДАЛАСЬ (код $LASTEXITCODE) ==="; return }



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

            if ($LASTEXITCODE -ne 0) { Emit ''; Emit "=== ЗАЛИВКА НЕ УДАЛАСЬ (код $LASTEXITCODE) ==="; return }

            Emit ''

            Emit '=== ЗАЛИВКА УСПЕШНА ==='

        }

    } -ArgumentList $script:ArduinoCli, $script:LibsDir, $sketch, $buildPath, $doUpload, $script:cfg.comPort, $script:FlashLog, $fqbn, $what



    $flashTimer.Start()

}



# ============================================================================

#  РАСКЛАДКА

# ============================================================================

function Layout-Header {

    if (-not $script:uiReady) { return }

    $w = $top.Width

    $x = $w - 14

    $btnSerial.Location = New-Object System.Drawing.Point(($x - $btnSerial.Width), 15)

    $x -= $btnSerial.Width + 8

    $btnReboot.Location = New-Object System.Drawing.Point(($x - $btnReboot.Width), 15)

    $x -= $btnReboot.Width + 8

    $btnPult.Location = New-Object System.Drawing.Point(($x - $btnPult.Width), 15)

    $x -= $btnPult.Width + 12

    $lblCom.Location = New-Object System.Drawing.Point(($x - $lblCom.Width), 20)

    $x -= $lblCom.Width + 8

    $lblHttp.Location = New-Object System.Drawing.Point(($x - $lblHttp.Width), 20)

    $x -= $lblHttp.Width + 8

    $lblFresh.Location = New-Object System.Drawing.Point(($x - $lblFresh.Width), 20)



    # шильдик прошивки прижимаем к левому краю, а если места нет — убираем,

    # чтобы он не наезжал на новый индикатор свежести данных

    $avail = $lblFresh.Left - 8 - $lblFwChip.Left

    if ($avail -lt 60) {

        $lblFwChip.Visible = $false

    } else {

        $lblFwChip.Visible = $true

        $fwW = [Math]::Min(130, $avail)

        if ($lblFwChip.Width -ne $fwW) { $lblFwChip.Width = $fwW }

    }



    $lblFootRight.Size = New-Object System.Drawing.Size(($w - 420), 18)

    $lblFootRight.Location = New-Object System.Drawing.Point(400, 5)

}



function Layout-All {

    Layout-Header

    Layout-Screen

    Layout-History

    Layout-Device

    Layout-Flash

    Layout-Log

}



# ============================================================================

#  ИСТОРИЯ — ПЕРЕКЛЮЧЕНИЕ ДНЕЙ

# ============================================================================

function Step-Day([int]$delta) {

    $list = @($script:days)

    if ($list.Count -eq 0) {

        $script:histSel = (Get-Date).ToString('yyyy-MM-dd')

        Request-Day $script:histSel

        Update-HistoryView

        return

    }

    $idx = $list.IndexOf($script:histSel)

    if ($idx -lt 0) { $idx = 0 } else { $idx += $delta }

    if ($idx -lt 0) { $idx = 0 }

    if ($idx -ge $list.Count) { $idx = $list.Count - 1 }

    $script:histSel = [string]$list[$idx]

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

    if (Send-Serial 'reboot') { Add-Log 'команда: перезагрузка платы' }

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



$script:navBtn['screen'].Add_Click({ Show-Page 'screen'; Add-Log 'вкладка: Экран' })

$script:navBtn['dev'].Add_Click({ Show-Page 'dev'; Update-Device; Add-Log 'вкладка: Устройство' })

$script:navBtn['hist'].Add_Click({ Show-Page 'hist'; Request-Day $script:histSel; Update-HistoryView; Add-Log 'вкладка: История' })

$script:navBtn['flash'].Add_Click({ Show-Page 'flash'; Add-Log 'вкладка: Прошивка' })

$script:navBtn['log'].Add_Click({ Show-Page 'log'; Add-Log 'вкладка: Журнал' })

$script:navBtn['prot'].Add_Click({ Show-Page 'prot'; Add-Log 'вкладка: Защита' })



$btnPrev.Add_Click({ Step-Day 1; Add-Log ('история: предыдущий день — ' + $script:histSel) })

$btnNext.Add_Click({ Step-Day -1; Add-Log ('история: следующий день — ' + $script:histSel) })

$btnToday.Add_Click({

    $script:histSel = (Get-Date).ToString('yyyy-MM-dd')

    Request-Day $script:histSel

    Update-HistoryView

    Add-Log 'история: выбран сегодняшний день'

})

$btnReload.Add_Click({

    Fill-DayGaps $script:histSel $script:histSel

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

    $cand += 'D:\DSH-Workspace\timelapse\watchdog\energy-telegram.ps1'

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

    Set-Lbl $lblSub ('ESP32 · ' + $addr.Replace('http://', '') + ' · обновление 5 с')

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

$btnOta.Add_Click({

    $script:http = Join-Path $script:AppDir 'flash-http.ps1'

    if (-not (Test-Path $script:http)) { Add-Log ('не найден flash-http.ps1: ' + $script:http) 'err'; return }

    $ip = $script:cfg.boardIp

    if (-not $ip) { $ip = '192.168.0.51' }

    Add-Log ('прошивка по Wi-Fi: ' + $ip + ' — собираю и отправляю…')

    Add-Log ('скрипт: ' + $script:http)

    $out = Join-Path $script:LogDir 'ota-last.txt'

    $btnOta.Enabled = $false

    try {

        Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -Wait -ArgumentList @(

            '-NoProfile','-ExecutionPolicy','Bypass','-File',$script:http,'-Ip',$ip) -RedirectStandardOutput $out

        if (Test-Path $out) {

            foreach ($ln in (Get-Content $out -Encoding UTF8)) { if ($ln.Trim()) { Add-Log $ln 'info' } }

        }

        Add-Log 'прошивка по Wi-Fi завершена' 'ok'

    } catch {

        Add-Log ('ошибка прошивки по Wi-Fi: ' + $_.Exception.Message) 'err'

    } finally {

        $btnOta.Enabled = $true

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

    if ($script:W.btnNext.Enabled) { $script:W.btnNext.BackColor = C '#1F7A46' }

    else { $script:W.btnNext.BackColor = C '#2A3240' }

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

        Wiz-Log '    {"t":"status","fw":"CYD-4.4","name":"CYD-DASH","wifi":true,"ip":"192.168.0.51"}'

        Wiz-Log ''

        Wiz-Log 'COM5 — CH340, ESP32 ✓'

        Wiz-Log '  прошивка: CYD-4.4'

        Wiz-Log '  плата уже настроена (192.168.0.51)'

        Wiz-Log '  → прошивка не нужна'

        $script:W.found = $true

        $script:W.port = 'COM5'

        $script:W.chip = 'ESP32'

        $script:W.fwBoard = 'CYD-4.4'

        $script:W.ip = '192.168.0.51'

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

            Wiz-Log '  {"t":"status","fw":"CYD-4.4","name":"CYD-DASH","wifi":true,"ip":"192.168.0.51"}'

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

        Wiz-Log '  IP: 192.168.0.51 ssid=MyWiFi rssi=-45'

        Wiz-Log ''

        Wiz-Log 'IP платы: 192.168.0.51  (получен по serial)'

        $ip = '192.168.0.51'

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

        if (!$ip) { $ip = '192.168.0.51' }

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

    try {

        $txtBoard.Text = $addr

        Set-Lbl $lblSub ('ESP32 · ' + $ip + ' · обновление 5 с')

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

        Wiz-Log '  веб: http://192.168.0.51/'

        Wiz-Log ''

        Wiz-Log 'Wi-Fi: подключено · IP платы 192.168.0.51'

        $script:W.ssid = $ssid

        $script:W.ip = '192.168.0.51'

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

    $txt.BackColor = C '#05070A'

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

    $cmb.BackColor = C '#12161C'

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

    $tp.BackColor = C '#12161C'

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

    Request-Day((Get-Date).ToString('yyyy-MM-dd'))

    $script:histSel = (Get-Date).ToString('yyyy-MM-dd')

    Update-HistoryView



    # --- демо-хуки для скриншотов (в обычной работе не заданы) ---------------

    if ($env:DOMMON_BEAT_SEC -match '^\d+$') { $script:beatSec = [Math]::Max(5, [int]$env:DOMMON_BEAT_SEC) }

    if ($env:DOMMON_HOVER_HOUR -match '^\d+$') { $script:pinScr = [Math]::Min(23, [int]$env:DOMMON_HOVER_HOUR) }

    if ($env:DOMMON_HIST_HOUR -match '^\d+$') { $script:pinHist = [Math]::Min(23, [int]$env:DOMMON_HIST_HOUR) }

    if ($env:DOMMON_PAGE -match '^(screen|hist|dev|flash|log)$') {

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

            $chartHist.Invalidate()

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

})



[System.Windows.Forms.Application]::add_ThreadException({

    param($sender, $e)

    Write-AppLog '!!! ОШИБКА В ИНТЕРФЕЙСЕ !!!'

    Write-AppLog ('тип: ' + $e.Exception.GetType().FullName)

    Write-AppLog ('текст: ' + $e.Exception.Message)

    Write-AppLog ('стек: ' + $e.Exception.StackTrace)

})



Write-AppLog 'форма готова, показываю'

[void]$form.ShowDialog()

Write-AppLog '=== ШТАТНОЕ ЗАВЕРШЕНИЕ ==='

