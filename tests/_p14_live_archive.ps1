# ============================================================================
#  _p14_live_archive.ps1 — живая проверка карточки архива БЕЗ запуска
#  приложения: реальный schtasks-опрос в фоне и реальный ручной снимок через
#  те же функции DomMonitor.ps1 (Start-ArchiveSnapshot + Pump-ArchiveSnapshot),
#  проверка «второй снимок не запускается», затем проверка SHA-256 новой копии.
#  Архив только пополняется, ничего не удаляется.
# ============================================================================

$ErrorActionPreference = 'Stop'
$PASS = 0
$FAIL = 0
function Ok([string]$name, [bool]$cond, [string]$detail = '') {
    if ($cond) { $script:PASS++; Write-Host ('PASS  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' })) }
    else { $script:FAIL++; Write-Host ('FAIL  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' })) -ForegroundColor Red }
}

$SRC = 'C:\esp32\tools\DomMonitor.ps1'
$src = [IO.File]::ReadAllText($SRC, [Text.Encoding]::UTF8)
$ast = [System.Management.Automation.Language.Parser]::ParseInput($src, [ref]$null, [ref]$null)
$defs = @{}
foreach ($fd in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) { $defs[$fd.Name] = $fd }
$wanted = @('Start-ArchiveSnapshot', 'Pump-ArchiveSnapshot', 'Get-ArchivePythonLogLines',
            'Start-ArchiveTaskQuery', 'Pump-ArchiveTaskQuery', 'Convert-SchTasksListText',
            'Get-SchTaskResultRu', 'Get-SchTaskStateRu', 'Convert-SchTaskDate', 'Format-SchTaskHex',
            'Get-ArchiveState', 'Get-ArchiveSnapshotManifestFile', 'Test-ArchiveManifestObjects',
            'Convert-ArchiveManifestText', 'Find-ArchivePython', 'Format-TermSize', 'Field',
            'Get-SanitizedText', 'Get-ArchiveDriveRoot')
foreach ($n in $wanted) {
    if (-not $defs.ContainsKey($n)) { Ok ('функция ' + $n) $false; continue }
    . ([scriptblock]::Create($defs[$n].Extent.Text))
}
function Update-ArchiveCard { }
function Add-Log { param($text, $color = 'normal') }
function Write-AppLog { param($msg) }

$script:LogDir = Join-Path $env:TEMP 'p14-live-logs'
if (-not (Test-Path $script:LogDir)) { New-Item -ItemType Directory -Path $script:LogDir -Force | Out-Null }
$script:ArchRoot = 'D:\DomEnergy\DomEnergy-Archive'
$script:ArchRunner = 'C:\esp32\tools\archive-run.ps1'
$script:ArchSd = 'C:\esp32\tools\archive-sd.py'
$script:ArchTaskName = 'DomEnergy-DailyArchive'
$script:cfg = [pscustomobject]@{ board = 'http://192.168.1.51'; boardIp = '192.168.1.51' }
$script:archState = $null
$script:archStateAt = [datetime]::MinValue
$script:archVerifyAt = [datetime]::MinValue
$script:archLinesKey = ''
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
$script:archRunOkAt = [datetime]::MinValue
$script:archRestoreProc = $null
$script:archRestoreState = 'тест восстановления не запускался'

# --- 1. статус задачи планировщика в фоне ------------------------------------
Start-ArchiveTaskQuery
$deadline = (Get-Date).AddSeconds(25)
while ($null -ne $script:archTaskProc -and (Get-Date) -lt $deadline) {
    Pump-ArchiveTaskQuery
    if ($null -ne $script:archTaskProc) { Start-Sleep -Milliseconds 100 }
}
Ok 'schtasks: опрос завершился' ($null -eq $script:archTaskProc) ''
Ok 'schtasks: задача найдена' ($null -ne $script:archTask -and $script:archTask.Found) ([string]$script:archTaskErr)
Ok 'schtasks: состояние прочитано' ($null -ne $script:archTask -and $script:archTask.Status) ([string]$script:archTask.Status)
Ok 'schtasks: код последнего запуска прочитан' ($null -ne $script:archTask -and $script:archTask.LastResult -ne '') ([string]$script:archTask.LastResult)

# --- 2. реальный ручной снимок ----------------------------------------------
$before = Get-ArchiveState
Ok 'архив до снимка прочитан' ([bool]$before.Exists) $before.Root
$prevUtc = $null
if ($null -ne $before.Latest) { $prevUtc = $before.Latest.CreatedUtc }
$script:archState = $before

$msg = Start-ArchiveSnapshot
Ok 'снимок запущен (нет отказа)' ([string]::IsNullOrEmpty($msg)) $msg
if (-not [string]::IsNullOrEmpty($msg)) { Write-Host ('  причина: ' + $msg) }

if ($null -ne $script:archRunProc) {
    $msg2 = Start-ArchiveSnapshot
    Ok 'второй снимок одновременно не запускается' ($msg2 -match 'уже идёт') $msg2
    $deadline = (Get-Date).AddSeconds(240)
    while ($null -ne $script:archRunProc -and (Get-Date) -lt $deadline) {
        Pump-ArchiveSnapshot
        if ($null -ne $script:archRunProc) { Start-Sleep -Milliseconds 300 }
    }
    Ok 'снимок завершился' ($null -eq $script:archRunProc) ''
    Ok 'итог снимка: успех' ($script:archRunState -match 'снимок готов') $script:archRunState
} else {
    Ok 'снимок завершился' $false 'процесс не стартовал'
}

# --- 3. новая копия и её SHA-256 --------------------------------------------
$after = Get-ArchiveState -Verify
Ok 'архив после снимка: latest.json есть' ($null -ne $after.Latest -and $after.Latest.Ok) ''
if ($null -ne $after.Latest -and $null -ne $after.Latest.CreatedUtc -and $null -ne $prevUtc) {
    Ok 'появилась более новая проверенная копия' ($after.Latest.CreatedUtc -gt $prevUtc) ($prevUtc.ToString('HH:mm:ss') + ' -> ' + $after.Latest.CreatedUtc.ToString('HH:mm:ss'))
} else {
    Ok 'появилась более новая проверенная копия' $false 'нет времени копии'
}
Ok 'SHA-256 новой копии: все объекты совпали' ($after.VerifyOk -eq $after.Latest.FileCount -and $after.VerifyBad -eq 0) ('ok=' + $after.VerifyOk + ' bad=' + $after.VerifyBad + ' ms=' + $after.VerifyMs)
Ok 'снимки не удаляются (число не уменьшилось)' ($after.Snapshots -ge $before.Snapshots) ($before.Snapshots.ToString() + ' -> ' + $after.Snapshots.ToString())

try { [IO.Directory]::Delete($script:LogDir, $true) } catch { }
Write-Host ''
Write-Host ('ИТОГО: PASS ' + $PASS + ' · FAIL ' + $FAIL)
if ($FAIL -gt 0) { exit 1 } else { exit 0 }
