# ============================================================================
#  _p14_e2e_restore.ps1 — сквозная проверка «Теста восстановления» БЕЗ запуска
#  приложения: берёт реальные функции из C:\esp32\tools\DomMonitor.ps1,
#  запускает archive-sd.py --restore во временную папку, проверяет SHA-256
#  и убеждается, что удалена ТОЛЬКО эта временная папка.
#  Плата не опрашивается, приложение не запускается.
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
$wanted = @('Start-ArchiveRestore', 'Pump-ArchiveRestore', 'Remove-ArchiveRestoreTemp',
            'Get-ArchiveSnapshotManifestFile', 'Convert-ArchiveManifestText', 'Get-SanitizedText',
            'Format-TermSize', 'Format-TermKB', 'Find-ArchivePython', 'Field', 'Get-ArchiveState',
            'Test-ArchiveManifestObjects', 'Get-ArchiveDriveRoot', 'Start-ArchiveSnapshot')
foreach ($n in $wanted) {
    if (-not $defs.ContainsKey($n)) { Ok ('функция ' + $n) $false; continue }
    . ([scriptblock]::Create($defs[$n].Extent.Text))
}

# заглушки вместо UI и общего журнала
function Update-ArchiveCard { }
function Add-Log { param($text, $color = 'normal') }

$script:LogDir = Join-Path $env:TEMP 'p14-logs'
if (-not (Test-Path $script:LogDir)) { New-Item -ItemType Directory -Path $script:LogDir -Force | Out-Null }
$script:ArchRoot = 'D:\DomEnergy\DomEnergy-Archive'
$script:ArchSd = 'C:\esp32\tools\archive-sd.py'
$script:ArchTaskName = 'DomEnergy-DailyArchive'
$script:archRestoreLog = Join-Path $script:LogDir 'archive-restore.log'
$script:archState = $null
$script:archRunProc = $null
$script:archRestoreProc = $null
$script:archRestoreState = 'тест восстановления не запускался'
$script:archRestoreLast = ''
$script:archRestoreTmp = ''
$script:archRestoreManifest = ''
$script:archRunState = ''

# --- 0. защита от двух операций одновременно ---------------------------------
$script:archRunProc = 'dummy'   # как будто идёт снимок
$m1 = Start-ArchiveRestore
Ok 'защита: при идущем снимке тест не запускается' ($m1 -match 'уже идёт|Идёт снимок') $m1
$script:archRestoreProc = 'dummy'
$m2 = Start-ArchiveSnapshot
Ok 'защита: при идущем тесте снимок не запускается' ($m2 -match 'Тест восстановления|уже идёт') $m2
$script:archRunProc = $null
$script:archRestoreProc = $null

# --- 1. запуск теста восстановления -----------------------------------------
$beforeRestoreDirs = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'DomEnergy-Restore-*' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$msg = Start-ArchiveRestore
Ok 'тест восстановления запущен' ([string]::IsNullOrEmpty($msg)) $msg
$tmpDir = [string]$script:archRestoreTmp
Ok 'временная папка наша (DomEnergy-Restore-*)' ($tmpDir -match 'DomEnergy-Restore-\d{8}-\d{6}-[0-9a-f]{4}$') $tmpDir
Ok 'временная папка внутри %TEMP%' ($tmpDir.StartsWith($env:TEMP, [StringComparison]::OrdinalIgnoreCase)) $tmpDir
Ok 'манифест — файл из snapshots/' ($script:archRestoreManifest -match 'snapshots\\\d{4}-\d{2}\\') $script:archRestoreManifest

# --- 2. ждём завершения (без блокировки: опрос по 200 мс) --------------------
$deadline = (Get-Date).AddSeconds(120)
while ($null -ne $script:archRestoreProc -and (Get-Date) -lt $deadline) {
    Pump-ArchiveRestore
    if ($null -ne $script:archRestoreProc) { Start-Sleep -Milliseconds 200 }
}
Ok 'процесс теста завершился' ($null -eq $script:archRestoreProc) ''
Ok 'итог: тест УСПЕШЕН' ($script:archRestoreState -match 'УСПЕШЕН') $script:archRestoreState

# --- 3. временная папка удалена, и удалена ТОЛЬКО она ------------------------
Ok 'временная папка удалена' (-not (Test-Path -LiteralPath $tmpDir)) $tmpDir
$afterRestoreDirs = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'DomEnergy-Restore-*' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
Ok 'других папок DomEnergy-Restore-* не появилось' (($afterRestoreDirs | Where-Object { $beforeRestoreDirs -notcontains $_ }).Count -eq 0) (($afterRestoreDirs | Where-Object { $beforeRestoreDirs -notcontains $_ }) -join '; ')

# --- 4. запись в журнал тестов восстановления -------------------------------
$logLast = ''
if (Test-Path -LiteralPath $script:archRestoreLog) { $logLast = @(Get-Content -LiteralPath $script:archRestoreLog -Tail 1 -Encoding UTF8)[0] }
Ok 'журнал тестов восстановления обновлён' ($logLast -match 'OK files=') $logLast

# --- 5. защита удаления: чужую папку функция не трогает ---------------------
$foreign = Join-Path $env:TEMP 'p14-foreign-folder'
if (-not (Test-Path $foreign)) { New-Item -ItemType Directory -Path $foreign -Force | Out-Null }
$note1 = Remove-ArchiveRestoreTemp $foreign
Ok 'защита удаления: чужое имя не удаляется' ($note1 -match 'отказ') $note1
Ok 'защита удаления: чужая папка на месте' (Test-Path -LiteralPath $foreign) $foreign
$note2 = Remove-ArchiveRestoreTemp (Join-Path $script:ArchRoot 'snapshots')
Ok 'защита удаления: папка архива не удаляется' ($note2 -match 'отказ') $note2
Ok 'защита удаления: папка архива на месте' (Test-Path -LiteralPath (Join-Path $script:ArchRoot 'snapshots')) ''
try { [IO.Directory]::Delete($foreign, $true) } catch { }
try { [IO.Directory]::Delete($script:LogDir, $true) } catch { }

Write-Host ''
Write-Host ('ИТОГО: PASS ' + $PASS + ' · FAIL ' + $FAIL)
if ($FAIL -gt 0) { exit 1 } else { exit 0 }
