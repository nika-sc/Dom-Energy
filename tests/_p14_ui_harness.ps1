# ============================================================================
#  _p14_ui_harness.ps1 — стенд без запуска приложения: создаёт РЕАЛЬНУЮ
#  страницу «Архив» из C:\esp32\tools\DomMonitor.ps1 в невидимой форме,
#  прогоняет Layout-Arch на широком и узком окне и обновление карточки.
#  Окно не показывается, плата не опрашивается, приложение не запускается.
# ============================================================================

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$PASS = 0
$FAIL = 0
function Ok([string]$name, [bool]$cond, [string]$detail = '') {
    if ($cond) { $script:PASS++; Write-Host ('PASS  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' })) }
    else { $script:FAIL++; Write-Host ('FAIL  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' })) -ForegroundColor Red }
}

$SRC = 'C:\esp32\tools\DomMonitor.ps1'
$src = [IO.File]::ReadAllText($SRC, [Text.Encoding]::UTF8)
$errs = $null
$toks = $null
$ast = [System.Management.Automation.Language.Parser]::ParseInput($src, [ref]$toks, [ref]$errs)
Ok 'Parser: файл без ошибок' ($errs.Count -eq 0) ([string]$errs.Count)

$wanted = @(
    'C', 'Fnt', 'HexOf', 'MixC', 'SkinC', 'Get-HoverC', 'Set-Round', 'Field', 'Set-Lbl',
    'Format-Uptime', 'Format-Age', 'Format-TermAge', 'Format-TermSize', 'Format-TermKB',
    'Get-CloudWhyRu', 'New-Lbl', 'New-Btn', 'Get-Skin', 'New-TermBox', 'Set-TermLines',
    'New-TabPage', 'Add-Page', 'Layout-Arch', 'Update-ArchiveCard', 'Format-ArchiveStatusLines',
    'Get-AnonDiagnosticsReport', 'Get-MeasureField', 'Get-MeasureMetric', 'Format-MeasureSource',
    'Get-SanitizedText', 'Convert-ArchiveManifestText', 'Convert-StorageJson',
    'Convert-SchTasksListText', 'Get-SchTaskResultRu', 'Get-SchTaskStateRu', 'Convert-SchTaskDate',
    'Format-SchTaskHex', 'Pump-ArchiveDiagRender', 'Start-ArchiveRestore', 'Start-ArchiveSnapshot',
    'Pump-ArchiveRestore', 'Pump-ArchiveSnapshot', 'Refresh-Archive', 'Pump-Archive',
    'Get-ArchiveState', 'Get-ArchiveSnapshotManifestFile', 'Test-ArchiveManifestObjects',
    'Find-ArchivePython', 'Get-ArchiveDriveRoot'
)
$defs = @{}
foreach ($fd in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
    if ($wanted -contains $fd.Name) { $defs[$fd.Name] = $fd }
}
$missing = @($wanted | Where-Object { -not $defs.ContainsKey($_) })
Ok 'все функции для стенда найдены' ($missing.Count -eq 0) ($missing -join ', ')

# --- окружение, как у настоящего приложения (минимально нужное) -------------
$form = New-Object System.Windows.Forms.Form
$form.ClientSize = New-Object System.Drawing.Size(1140, 872)
$pageHost = New-Object System.Windows.Forms.Panel
$pageHost.Dock = 'None'
$pageHost.Size = New-Object System.Drawing.Size(1124, 689)
$form.Controls.Add($pageHost)
$script:pages = [ordered]@{}
$script:navBtn = @{}
$script:navOrder = @('screen', 'dev', 'hist', 'flash', 'log', 'prot', 'term', 'arch')
$script:uiReady = $false
$script:SkinData = @{
    dark = @{ bg = '#0B111B'; surf = '#141C2A'; elev = '#1C2636'; text = '#E8EEFC'; muted = '#93A4BE'
              line = '#293D54'; accent = '#22D3EE'; temp = '#F472B6'; power = '#FBBF24'
              money = '#F472B6'; good = '#34D399'; bad = '#F87171' }
}
$script:cfg = [pscustomobject]@{ board = 'http://192.168.1.51'; boardIp = '192.168.1.51' }
$script:tipMain = $null
$script:LogDir = Join-Path $env:TEMP 'p14-harness-logs'
if (-not (Test-Path $script:LogDir)) { New-Item -ItemType Directory -Path $script:LogDir -Force | Out-Null }
$script:ArchRoot = 'D:\DomEnergy\DomEnergy-Archive'
$script:ArchRunner = 'C:\esp32\tools\archive-run.ps1'
$script:ArchSd = 'C:\esp32\tools\archive-sd.py'
$script:ArchTaskName = 'DomEnergy-DailyArchive'
$script:appStart = (Get-Date).AddMinutes(-30)
$script:board = $null
$script:boardAt = [datetime]::MinValue
$script:boardErr = ''
$script:measure = $null
$script:measureAt = [datetime]::MinValue
$script:measureErr = ''
$script:measMs = 0
$script:measTotal = 0
$script:archState = $null
$script:archStateAt = [datetime]::MinValue
$script:archVerifyAt = [datetime]::MinValue
$script:archTask = $null
$script:archTaskAt = [datetime]::MinValue
$script:archTaskProc = $null
$script:archTaskFiles = $null
$script:archTaskStart = [datetime]::MinValue
$script:archTaskErr = ''
$script:archRunProc = $null
$script:archRunStart = [datetime]::MinValue
$script:archRunOut = ''
$script:archRunErr = ''
$script:archRunState = 'снимок ещё не запускался из приложения'
$script:archRestoreProc = $null
$script:archRestoreStart = [datetime]::MinValue
$script:archRestoreTmp = ''
$script:archRestoreManifest = ''
$script:archRestoreState = 'тест восстановления не запускался'
$script:archRestoreLast = ''
$script:archRestoreLog = Join-Path $script:LogDir 'archive-restore.log'
$script:archStore = $null
$script:archStoreAt = [datetime]::MinValue
$script:archGapfill = $null
$script:archGapAt = [datetime]::MinValue
$script:archDiagTask = $null
$script:archDiagName = ''
$script:archDiagStart = [datetime]::MinValue
$script:archDiagAt = [datetime]::MinValue
$script:archDiagText = ''
$script:archLinesKey = ''

foreach ($n in $wanted) {
    if (-not $defs.ContainsKey($n)) { continue }
    . ([scriptblock]::Create($defs[$n].Extent.Text))
}
# заглушки вместо журнала/уведомлений (на стенде UI не нужен)
function Add-Log { param($text, $color = 'normal') }
function Write-AppLog { param($msg) }
function Init-Http { }

$script:CLR = Get-Skin 'dark'

# --- исполняем ровно тот код создания страницы, что в файле ------------------
$startMark = '#  Страница «Архив»: карточка архива + центр обезличенной диагностики.'
$endMark = 'function Layout-Arch {'
$si = $src.IndexOf($startMark)
$ei = $src.IndexOf($endMark)
Ok 'код создания страницы найден' ($si -gt 0 -and $ei -gt $si) ('start=' + $si + ' end=' + $ei)
$creation = $src.Substring($si, $ei - $si)
$creation = $creation.Substring($creation.IndexOf('# ---'))
try {
    . ([scriptblock]::Create($creation))
    Ok 'страница «Архив» создана без исключений' $true
} catch {
    Ok 'страница «Архив» создана без исключений' $false $_.Exception.Message
}

Ok 'страница зарегистрирована в $script:pages' ($null -ne $script:pages['arch']) ''
Ok 'две колонки: журнал архива и предпросмотр отчёта' (($null -ne $script:txtArch) -and ($null -ne $script:txtDiag)) ''
Ok 'кнопки: снимок / тест / обновить / копировать' (($null -ne $btnArchSnap) -and ($null -ne $btnArchRestore) -and ($null -ne $btnArchRefresh) -and ($null -ne $btnDiagCopy)) ''
Ok 'подсказка: вторая копия не запускается' ($btnArchSnap.Text -match 'Снимок') ''
Ok 'подписи карточки и отчёта' (($lblArchCap.Text -match 'АРХИВ') -and ($lblDiagCap.Text -match 'ДИАГНОСТИКА')) ''
Ok 'заголовок честно про автоотправки' ($lblArchHead.Text -match 'Автоматических отправок нет') ''

# --- раскладка: широкое окно ------------------------------------------------
$script:uiReady = $true
$pgArch.Visible = $true
$pgArch.Dock = 'Fill'
$pageHost.PerformLayout()
$form.PerformLayout()
$pgArch.PerformLayout()
Ok 'страница получила размер' ($pgArch.ClientSize.Width -gt 900 -and $pgArch.ClientSize.Height -gt 500) ([string]$pgArch.ClientSize)
try { Layout-Arch; Ok 'Layout-Arch (широкое окно) без исключений' $true } catch { Ok 'Layout-Arch (широкое окно) без исключений' $false $_.Exception.Message }
$pw = $pgArch.ClientSize.Width
$ph = $pgArch.ClientSize.Height
Ok 'журнал архива виден и не нулевой' ($script:txtArch.Width -gt 300 -and $script:txtArch.Height -gt 100) ([string]$script:txtArch.Size)
Ok 'предпросмотр отчёта виден и не нулевой' ($script:txtDiag.Width -gt 250 -and $script:txtDiag.Height -gt 100) ([string]$script:txtDiag.Size)
Ok 'колонки не пересекаются' (($script:txtArch.Right + 10) -le $script:txtDiag.Left) ('arch.Right=' + $script:txtArch.Right + ' diag.Left=' + $script:txtDiag.Left)
Ok 'кнопки архива внутри страницы' (($btnArchRefresh.Right -le $pw) -and ($btnArchSnap.Top -lt $ph)) ('right=' + $btnArchRefresh.Right + ' pw=' + $pw)
Ok 'кнопки диагностики внутри страницы' (($btnDiagCopy.Right -le $pw) -and ($btnDiagRefresh.Top -lt $ph)) ('right=' + $btnDiagCopy.Right + ' pw=' + $pw)
Ok 'кнопки не налезают на предпросмотр' (($btnArchSnap.Top -gt $script:txtArch.Bottom) -and ($btnDiagRefresh.Top -gt $script:txtDiag.Bottom)) ('btn.Top=' + $btnArchSnap.Top + ' box.Bottom=' + $script:txtArch.Bottom)

# --- раскладка: минимальное окно (980x760) ----------------------------------
$pageHost.Size = New-Object System.Drawing.Size(964, 624)
$pageHost.PerformLayout()
$pgArch.PerformLayout()
try { Layout-Arch; Ok 'Layout-Arch (узкое окно) без исключений' $true } catch { Ok 'Layout-Arch (узкое окно) без исключений' $false $_.Exception.Message }
$pw2 = $pgArch.ClientSize.Width
Ok 'узкое окно: кнопка «Обновить» влезает' ($btnArchRefresh.Right -le $pw2) ('right=' + $btnArchRefresh.Right + ' pw=' + $pw2)
Ok 'узкое окно: предпросмотр не нулевой' ($script:txtDiag.Width -gt 250) ([string]$script:txtDiag.Width)
Ok 'узкое окно: подпись статуса не отрицательной ширины' ($lblArchHint.Width -gt 0 -and $lblDiagState.Width -gt 0) ([string]$lblArchHint.Width + '/' + [string]$lblDiagState.Width)

# --- карточка и отчёт: реальные данные (фикстуры) ---------------------------
$script:board = [pscustomobject]@{ fw = '4.14'; build = '6acd775a4f217e710854'; up = 559; rssi = -55; r = $true
    net = $true; cTry = 2; cOk = 1; cFail = 0; cWhy = 0; cAge = 503; heap = 108412; free = 108412
    maxAlloc = 42996; gaps = 0; gapN = 2; gapAct = 0; sdRows = 43516; histMs = 0; sdMs = 10
    pageMs = 4; loopMs = 1; loopMaxMs = 139; loopMaxAll = 3079; blk = 'pwrfill'; blkMs = 2431
    msDns = 126; msTls = 993; msCloud = 1766; msTuya = 2; msWrite = 15; cliSt = 0; cliHttp = 200
    cliLen = 4864; apiN = 0; pageN = 5; pageSend = 297; pageCut = 0; redraw = 9; now = '2026-11-15 19:39:33' }
$script:boardAt = (Get-Date).AddSeconds(-2)
$script:measure = [pscustomobject]@{ clockOk = $true
    ttl = [pscustomobject]@{ vpSec = 90; tSec = 300 }
    voltage = [pscustomobject]@{ source = 'local'; ageSec = 2; valid = $true }
    power = [pscustomobject]@{ source = 'local'; ageSec = 2; valid = $true }
    current = [pscustomobject]@{ source = 'local'; ageSec = 2; valid = $true }
    temperature = [pscustomobject]@{ source = 'local'; ageSec = 7; valid = $true } }
$script:measureAt = (Get-Date).AddSeconds(-1)
$script:measMs = 120
$script:measTotal = 3
$script:archStore = Convert-StorageJson '{"ok":true,"policy":"monthly-keep-all","autoDelete":false,"totalBytes":125798350848,"usedBytes":5210112,"freeBytes":125793140736,"archiveError":false,"lowSpace":false,"writeOk":17,"writeFail":0,"writeFailRun":0,"lastWriteAgeSec":20,"writeOkHack":0,"freeBytes":1239089152}'
$script:archGapfill = [pscustomobject]@{ commitStage = 'check_rows'; commitCode = 'no_cloud_data'; noData = $true; active = 0; gaps = 2; todayN = 0; todayMin = 0; sdFault = 0; msg = 'в облаке нет записей' }
$script:archState = Get-ArchiveState -Verify
$script:archStateAt = Get-Date
$script:archVerifyAt = Get-Date
$script:archTask = Convert-SchTasksListText "TaskName: \DomEnergy-DailyArchive`r`nStatus: Ready`r`nLast Run Time: 15.11.2026 9:49:42`r`nLast Result: 0`r`nScheduled Task State: Enabled`r`nNext Run Time: 16.11.2026 5:30:00"
$script:archLinesKey = ''
try { Update-ArchiveCard; Ok 'Update-ArchiveCard без исключений' $true } catch { Ok 'Update-ArchiveCard без исключений' $false $_.Exception.Message }
Ok 'карточка заполнена текстом' ($script:txtArch.Text.Length -gt 400) ([string]$script:txtArch.Text.Length)
Ok 'карточка: задача прочитана' ($script:txtArch.Text -match 'ЗАДАЧА DomEnergy-DailyArchive') ''
Ok 'карточка: подпись про включённый ПК' ($script:txtArch.Text -match 'Копия с ПК требует включённого ПК') ''
Ok 'карточка: про два снимка' ($script:txtArch.Text -match 'Два снимка одновременно не запускаются') ''
Ok 'карточка: последняя проверенная копия' ($script:txtArch.Text -match 'ПОСЛЕДНЯЯ ПРОВЕРЕННАЯ КОПИЯ') ''
try { Pump-ArchiveDiagRender; Ok 'Pump-ArchiveDiagRender без исключений' $true } catch { Ok 'Pump-ArchiveDiagRender без исключений' $false $_.Exception.Message }
Ok 'предпросмотр отчёта заполнен' ($script:txtDiag.Text -match 'DOM ENERGY') ''
Ok 'в предпросмотре нет IP' ($script:txtDiag.Text -notmatch '192\.168\.') ''
Ok 'в предпросмотре есть архив и SHA-256' (($script:txtDiag.Text -match 'АРХИВ НА ПК') -and ($script:txtDiag.Text -match 'SHA-256')) ''
Ok 'кнопки не заблокированы без операций' ($btnArchSnap.Enabled -and $btnArchRestore.Enabled) ''

# --- состояние «идёт снимок»: кнопки ждут -----------------------------------
$script:archRunProc = New-Object psobject
$script:archLinesKey = ''
Update-ArchiveCard
Ok 'во время снимка кнопки недоступны' ((-not $btnArchSnap.Enabled) -and (-not $btnArchRestore.Enabled)) ''
Ok 'во время снимка подсказка это пишет' ($lblArchHint.Text -match 'идёт операция') $lblArchHint.Text
$script:archRunProc = $null
$script:archLinesKey = ''
Update-ArchiveCard
Ok 'после снимка кнопки снова доступны' ($btnArchSnap.Enabled -and $btnArchRestore.Enabled) ''

# --- уборка ----------------------------------------------------------------
Write-Host ''
Write-Host '----- КАРТОЧКА «АРХИВ» (живое состояние) -----'
Write-Host $script:txtArch.Text
Write-Host ''
Write-Host '----- ПРЕДПРОСМОТР ОТЧЁТА (первые 28 строк) -----'
Write-Host ((($script:txtDiag.Text -split "`r?`n") | Select-Object -First 28) -join "`r`n")
Write-Host ''
try { $pgArch.Dispose() } catch { }
try { $form.Dispose() } catch { }
try { [IO.Directory]::Delete($script:LogDir, $true) } catch { }

Write-Host ''
Write-Host ''
Write-Host ('ИТОГО: PASS ' + $PASS + ' · FAIL ' + $FAIL)
if ($FAIL -gt 0) { exit 1 } else { exit 0 }
