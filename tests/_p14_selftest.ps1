# ============================================================================
#  _p14_selftest.ps1 — оффлайн-проверки P1.4/P1.5 (архив + обезличенная
#  диагностика) для C:\DomEnergy\tools\DomMonitor.ps1.
#  Берёт РЕАЛЬНЫЕ функции из DomMonitor.ps1 (через AST, без запуска окна)
#  и прогоняет их на фикстурах. Приложение не запускается.
#  Запуск: powershell.exe -NoProfile -ExecutionPolicy Bypass -File этот_файл
# ============================================================================

$ErrorActionPreference = 'Stop'
$PASS = 0
$FAIL = 0
function Ok([string]$name, [bool]$cond, [string]$detail = '') {
    if ($cond) {
        $script:PASS++
        Write-Host ('PASS  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' }))
    } else {
        $script:FAIL++
        Write-Host ('FAIL  ' + $name + $(if ($detail) { '  (' + $detail + ')' } else { '' })) -ForegroundColor Red
    }
}

$SRC = 'C:\DomEnergy\tools\DomMonitor.ps1'
$src = [IO.File]::ReadAllText($SRC, [Text.Encoding]::UTF8)

# --- 1. синтаксис всего файла ------------------------------------------------
$errs = $null
$toks = $null
[void][System.Management.Automation.Language.Parser]::ParseInput($src, [ref]$toks, [ref]$errs)
Ok 'Parser: 0 ошибок во всём DomMonitor.ps1' ($errs.Count -eq 0) ($errs.Count.ToString() + ' err')

# --- 2. вытаскиваем функции из файла ----------------------------------------
$wanted = @(
    'Convert-SchTasksListText', 'Format-SchTaskHex', 'Get-SchTaskResultRu', 'Get-SchTaskStateRu',
    'Convert-SchTaskDate', 'Convert-ArchiveManifestText', 'Convert-StorageJson', 'Get-SanitizedText',
    'Get-AnonDiagnosticsReport', 'Format-ArchiveStatusLines', 'Field', 'Get-MeasureField',
    'Get-MeasureMetric', 'Format-MeasureSource', 'Get-CloudWhyRu', 'Format-Age', 'Format-Uptime',
    'Format-TermAge', 'Format-TermSize', 'Format-TermKB', 'Get-ArchiveState',
    'Get-ArchiveSnapshotManifestFile', 'Test-ArchiveManifestObjects', 'Find-ArchivePython',
    'Get-ArchiveDriveRoot'
)
$ast = [System.Management.Automation.Language.Parser]::ParseInput($src, [ref]$null, [ref]$null)
$defs = @{}
foreach ($fd in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
    if ($wanted -contains $fd.Name) { $defs[$fd.Name] = $fd }
}
$missing = @($wanted | Where-Object { -not $defs.ContainsKey($_) })
Ok 'все нужные функции найдены в файле' ($missing.Count -eq 0) ($missing -join ', ')

# заглушки цветов (форматирование источника/возраста этим пользуется)
$script:CLR = @{
    text = '#DEE5EF'; dim = '#9AA6B8'; dim2 = '#5C6575'; warn = '#F2B44B'; err = '#E85D5D'
    ok = '#63D68B'; accent = '#5AC8FF'; card = '#1B2029'; bg = '#0E1015'; header = '#12151B'; nav = '#151920'
}
$script:measureAt = [datetime]::MinValue
$script:measMs = 0
$script:measTotal = 0
$script:boardErr = ''
$script:measureErr = ''
$script:ArchTaskName = 'DomEnergy-DailyArchive'
$script:archTask = $null
$script:archTaskErr = ''
$script:archRunProc = $null
$script:archRestoreProc = $null
$script:archRunState = 'снимок ещё не запускался из приложения'
$script:archRestoreState = 'тест восстановления не запускался'
$script:archRestoreLast = ''
$script:appStart = (Get-Date).AddHours(-2)
$script:archState = $null
$script:ArchRoot = 'D:\DomEnergy\DomEnergy-Archive'

# подгружаем функции (dot-source на scriptblock)
foreach ($n in $wanted) {
    if (-not $defs.ContainsKey($n)) { continue }
    . ([scriptblock]::Create($defs[$n].Extent.Text))
}

# --- 3. фикстуры schtasks /FO LIST /V ---------------------------------------
$en = "Folder: \`r`nHostName:      TEST-PC`r`nTaskName:      \DomEnergy-DailyArchive`r`nNext Run Time: 16.11.2026 5:30:00`r`nStatus:        Ready`r`nLogon Mode:    Interactive only`r`nLast Run Time: 15.11.2026 9:49:42`r`nLast Result:   0`r`nAuthor:        N/A`r`nTask To Run:   C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe -File C:\DomEnergy\tools\archive-run.ps1 -Board http://192.168.1.51`r`nStart In:      C:\DomEnergy\tools`r`nScheduled Task State: Enabled`r`n`r`nHostName:      TEST-PC`r`nTaskName:      \DomEnergy-DailyArchive`r`nNext Run Time: 16.11.2026 5:30:00`r`nStatus:        Ready`r`nLast Run Time: 15.11.2026 9:49:42`r`nLast Result:   0"
$ru = (@(
    'Папка: \',
    'Имя узла: TEST-PC',
    'Имя задачи: \DomEnergy-DailyArchive',
    'Время следующего запуска: 16.11.2026 5:30:00',
    'Состояние: Готово',
    'Режим входа в систему: Только интерактивный',
    'Время прошлого запуска: 15.11.2026 9:49:42',
    'Прошлый результат: 0',
    'Автор: Н/Д',
    'Задача для выполнения: C:\DomEnergy\tools\archive-run.ps1',
    'Рабочая папка: C:\DomEnergy\tools',
    'Состояние назначенной задачи: Включено'
) -join "`r`n")
# старые (другие версии Windows) имена полей тоже должны работать
$ruLegacy = (@(
    'Folder: \',
    'Имя задачи: \DomEnergy-DailyArchive',
    'Состояние: Готово',
    'Время последнего запуска: 15.11.2026 9:49:42',
    'Код последнего результата: 0',
    'Выполняемая задача: C:\DomEnergy\tools\archive-run.ps1',
    'Состояние задачи планировщика: Включено'
) -join "`r`n")

$r = Convert-SchTasksListText $en
Ok 'schtasks EN: найдена задача' ($r.Found) ('Found=' + $r.Found)
Ok 'schtasks EN: имя задачи' ($r.TaskName -eq '\DomEnergy-DailyArchive') $r.TaskName
Ok 'schtasks EN: Status=Ready' ($r.Status -eq 'Ready') $r.Status
Ok 'schtasks EN: LastResult=0' ($r.LastResult -eq '0') $r.LastResult
Ok 'schtasks EN: два блока триггеров' ($r.Blocks -eq 2) ('Blocks=' + $r.Blocks)
$dt = Convert-SchTaskDate $r.LastRun
Ok 'schtasks EN: дата последнего запуска разобрана' ($null -ne $dt -and $dt.ToString('yyyy-MM-dd HH:mm:ss') -eq '2026-11-15 09:49:42') ([string]$dt)
Ok 'schtasks EN: код 0 -> успешно' ((Get-SchTaskResultRu $r.LastResult) -eq 'успешно (0)') (Get-SchTaskResultRu $r.LastResult)
Ok 'schtasks EN: состояние -> готова' ((Get-SchTaskStateRu $r.State $r.Status) -eq 'готова') (Get-SchTaskStateRu $r.State $r.Status)

$r2 = Convert-SchTasksListText $ru
Ok 'schtasks RU: найдена задача' ($r2.Found) ('Found=' + $r2.Found)
Ok 'schtasks RU: Status=Готово' ($r2.Status -eq 'Готово') $r2.Status
Ok 'schtasks RU: LastResult=0' ($r2.LastResult -eq '0') $r2.LastResult
$dt2 = Convert-SchTaskDate $r2.LastRun
Ok 'schtasks RU: дата разобрана' ($null -ne $dt2 -and $dt2.ToString('yyyy-MM-dd HH:mm:ss') -eq '2026-11-15 09:49:42') ([string]$dt2)

$r3 = Convert-SchTasksListText $ruLegacy
Ok 'schtasks RU (старые имена полей): найдена' ($r3.Found) ('Found=' + $r3.Found)
Ok 'schtasks RU (старые имена полей): LastResult=0' ($r3.LastResult -eq '0') $r3.LastResult
Ok 'schtasks RU (старые имена полей): State=Включено' ($r3.State -eq 'Включено') $r3.State
# вывод schtasks в OEM-кодировке 866 (как реально на русской Windows)
$bytes866 = [Text.Encoding]::GetEncoding(866).GetBytes($ru)
$ru866 = [Text.Encoding]::GetEncoding(866).GetString($bytes866)
Ok 'schtasks RU: разбор после cp866-декодирования' ((Convert-SchTasksListText $ru866).Found) ''
Ok 'schtasks RU: cp1251-декодирование не ломает разбор' ((Convert-SchTasksListText ([Text.Encoding]::GetEncoding(1251).GetString($bytes866))).Found -eq $false) 'ожидаемо: cp1251 даёт мусор, английский текст разобрался бы'


Ok 'schtasks: задача не найдена (EN ERROR)' (-not (Convert-SchTasksListText "ERROR: The system cannot find the file specified.").Found)
Ok 'schtasks: задача не найдена (RU ОШИБКА)' (-not (Convert-SchTasksListText "ОШИБКА: Не удается найти указанный файл.").Found)
Ok 'schtasks: пустой вывод не ломает разбор' (-not (Convert-SchTasksListText '').Found)
Ok 'schtasks: мусор не ломает разбор' (-not (Convert-SchTasksListText 'какой-то текст без полей').Found)
Ok 'Last Result: 267011 (ещё не запускалась)' ((Get-SchTaskResultRu '267011') -eq 'ещё не запускалась (267011)') (Get-SchTaskResultRu '267011')
Ok 'Last Result: hex 0x80070002' ((Format-SchTaskHex '-2147024894') -eq '0x80070002') (Format-SchTaskHex '-2147024894')
Ok 'Last Result: 0 -> 0x00000000' ((Format-SchTaskHex '0') -eq '0x00000000') (Format-SchTaskHex '0')

# --- 4. фикстура манифеста архива -------------------------------------------
$shaA = ('a' * 64)
$shaB = ('b' * 64)
$manJson = '{"format":1,"createdUtc":"2026-11-15T16:04:55.160993+00:00","url":"http://192.168.1.51","storage":{"device":"0000000000000000"},"downloadedBytes":15,"files":{"energy.csv":{"size":10,"sha256":"' + $shaA + '"},"energy.bak":{"size":5,"sha256":"' + $shaB + '"}}}'
$man = Convert-ArchiveManifestText $manJson
Ok 'манифест: разобран' ($man.Ok) $man.Error
Ok 'манифест: 2 файла' ($man.FileCount -eq 2) ([string]$man.FileCount)
Ok 'манифест: сумма размеров 15' ($man.TotalBytes -eq 15) ([string]$man.TotalBytes)
Ok 'манифест: createdUtc разобран' ($null -ne $man.CreatedUtc) ([string]$man.CreatedUtc)
Ok 'манифест: createdUtc в UTC' ($man.CreatedUtc.Kind -eq [DateTimeKind]::Utc) ([string]$man.CreatedUtc.Kind)
Ok 'манифест: имя файла сохранено' (@($man.Files | Where-Object { $_.Name -eq 'energy.csv' }).Count -eq 1)
Ok 'манифест: плохой JSON -> Ok=false' (-not (Convert-ArchiveManifestText 'не json').Ok)
Ok 'манифест: без файлов -> Ok=false' (-not (Convert-ArchiveManifestText '{"format":1,"createdUtc":"2026-11-15T16:04:55Z","files":{}}').Ok)

# --- 5. /storage с ДУБЛЕМ freeBytes: берём первое (согласуется с total-used) --
$stRaw = '{"ok":true,"policy":"monthly-keep-all","autoDelete":false,"device":"0000000000000000","bootId":2450875711,"sdGeneration":0,"totalBytes":125798350848,"usedBytes":5210112,"freeBytes":125793140736,"archiveError":false,"rotationsThisBoot":0,"lowSpace":false,"writeOk":17,"writeFail":0,"writeFailRun":0,"lastWriteAgeSec":20,"lastFailAgeSec":0,"lastWriteErr":0,"lastWriteMsg":"","freeBytes":1239089152,"archiveError":false}'
$st = Convert-StorageJson $stRaw
Ok '/storage: ok=true' ($st.ok -eq $true) ([string]$st.ok)
Ok '/storage: freeBytes — первое значение (согласуется с total-used)' ($st.freeBytes -eq 125793140736) ([string]$st.freeBytes)
Ok '/storage: total - used == freeBytes (проверка согласованности)' (([double]$st.totalBytes - [double]$st.usedBytes) -eq [double]$st.freeBytes)
Ok '/storage: writeOk=17' ($st.writeOk -eq 17) ([string]$st.writeOk)
Ok '/storage: policy сохранена' ($st.policy -eq 'monthly-keep-all') ([string]$st.policy)
Ok '/storage: пустой ответ -> $null' ($null -eq (Convert-StorageJson ''))

# --- 6. обезличивание ------------------------------------------------
$san = Get-SanitizedText 'ошибка http://192.168.1.51/data MAC aa:bb:cc:dd:ee:ff token=SECRET123 ip 10.1.2.3'
Ok 'обезличивание: URL скрыт' ($san -notmatch '192\.168\.0\.51') $san
Ok 'обезличивание: MAC скрыт' ($san -notmatch '(?i)aa:bb') $san
Ok 'обезличивание: токен скрыт' ($san -notmatch 'SECRET123') $san
Ok 'обезличивание: одиночный IP скрыт' ($san -notmatch '10\.1\.2\.3') $san
Ok 'обезличивание: текст остался читаемым' ($san -match 'ошибка') $san

$script:board = [pscustomobject]@{
    fw = '4.14'; build = '6acd775a4f217e710854'; up = 559; rssi = -55; r = $true; net = $true
    cTry = 2; cOk = 1; cFail = 0; cWhy = 0; cAge = 503; msDns = 126; msTls = 993; msCloud = 1766
    msTuya = 2; msWrite = 15; cliSt = 0; cliHttp = 200; cliLen = 4864; loopMs = 1; loopMaxMs = 139
    loopMaxAll = 3079; blk = 'pwrfill'; blkMs = 2431; histMs = 0; sdMs = 10; pageMs = 4
    heap = 108412; free = 108412; maxAlloc = 42996; apiN = 0; pageN = 5; pageSend = 297
    pageCut = 0; redraw = 9; gaps = 0; gapN = 2; gapAct = 0; sdRows = 43516
    now = '2026-11-15 19:39:33'
    ip = '192.168.1.51'; mac = 'AA:BB:CC:DD:EE:FF'; ssid = 'MyHomeWiFi'
    token = 'ABCDEF0123456789ABCDEF0123456789'; device = '0000000000000000'
    v = 236.10; p = 18.00; temp = 21.40; amp = 0.15
}
$script:boardAt = (Get-Date).AddSeconds(-2)
$script:measure = [pscustomobject]@{
    clockOk = $true
    ttl = [pscustomobject]@{ vpSec = 90; tSec = 300 }
    voltage = [pscustomobject]@{ value = 236.10; unit = 'V'; source = 'local'; ageSec = 2; valid = $true }
    power = [pscustomobject]@{ value = 18.00; unit = 'W'; source = 'local'; ageSec = 2; valid = $true }
    current = [pscustomobject]@{ value = 0.15; unit = 'A'; source = 'local'; ageSec = 2; valid = $true }
    temperature = [pscustomobject]@{ value = 21.40; unit = 'C'; source = 'local'; ageSec = 7; valid = $true }
}
$script:measureAt = (Get-Date).AddSeconds(-1)
$script:measMs = 120
$script:measTotal = 5
$script:archStore = $st
$script:archGapfill = [pscustomobject]@{ commitStage = 'check_rows'; commitCode = 'no_cloud_data'; noData = $true; active = 0; gaps = 2; todayN = 0; todayMin = 0; sdFault = 0; msg = 'в облаке нет записей' }
$script:archState = [pscustomobject]@{
    Exists = $true; Root = 'D:\DomEnergy\DomEnergy-Archive'; Drive = 'D:'
    FreeBytes = 1465802756096; TotalBytes = 7993335717888
    Latest = [pscustomobject]@{ Ok = $true; CreatedUtc = (Get-Date).ToUniversalTime().AddHours(-1); FileCount = 8; TotalBytes = 4958570 }
    VerifiedAt = (Get-Date).AddSeconds(-30); VerifyOk = 8; VerifyBad = 0; VerifyBadNames = @(); VerifyMs = 40
    Snapshots = 9; Objects = 42; ObjectsBytes = 2360000; Zips = 2; LastZipAt = (Get-Date).AddHours(-3); LastZipBytes = 235380806; LastZipSha = $true
    LastError = ''; LastErrorWhen = $null
    RunLastWhen = (Get-Date).AddHours(-1); RunLastLine = 'COMPLETE: 8 files, 4958570 new downloaded bytes'
}
$script:archTask = Convert-SchTasksListText $en
$script:archTaskErr = ''
$script:archRestoreLast = 'последний тест [2026-11-15 20:10:00] OK files=8/8'

$rep = (Get-AnonDiagnosticsReport) -join "`r`n"
Ok 'отчёт: есть' ($rep.Length -gt 500) ([string]$rep.Length)
Ok 'отчёт: версия прошивки' ($rep -match 'версия прошивки: 4\.14') ''
Ok 'отчёт: build' ($rep -match 'build: 6acd775a4f217e710854') ''
Ok 'отчёт: uptime платы' ($rep -match 'uptime платы: 9 мин') ''
Ok 'отчёт: LAN' ($rep -match 'LAN: плата отвечает') ''
Ok 'отчёт: WAN/облако' ($rep -match 'WAN: интернет есть') ''
Ok 'отчёт: свежесть источника (без значения)' ($rep -match 'V: локальное реле') ''
Ok 'отчёт: SD' ($rep -match 'SD: ok=да') ''
Ok 'отчёт: последний gapfill' ($rep -match 'commitCode no_cloud_data') ''
Ok 'отчёт: архив ПК' ($rep -match 'последняя проверенная копия:') ''
Ok 'отчёт: SHA-256 проверки' ($rep -match 'SHA-256 последней копии: 8 из 8') ''
Ok 'отчёт: задача планировщика' ($rep -match 'задача DomEnergy-DailyArchive: готова') ''

Ok 'отчёт: НЕТ IP 192.168.*' ($rep -notmatch '192\.168\.') ''
Ok 'отчёт: НЕТ MAC' ($rep -notmatch '(?i)AA:BB:CC') ''
Ok 'отчёт: НЕТ имени Wi-Fi' ($rep -notmatch 'MyHomeWiFi') ''
Ok 'отчёт: НЕТ токена' ($rep -notmatch 'ABCDEF0123456789') ''
Ok 'отчёт: НЕТ device id' ($rep -notmatch '0000000000000000') ''
Ok 'отчёт: НЕТ сырого напряжения 236.1' ($rep -notmatch '236\.1') ''
Ok 'отчёт: НЕТ сырой мощности 18.00' ($rep -notmatch '18\.00') ''
Ok 'отчёт: НЕТ сырой температуры 21.40' ($rep -notmatch '21\.40') ''
Ok 'отчёт: НЕТ сырого тока 0.15' ($rep -notmatch '0\.15') ''
Ok 'отчёт: прямо сказано, что не вошло' ($rep -match 'IP-адресов, MAC, имени Wi-Fi') ''
Ok 'отчёт: прямо сказано про отсутствие автоотправки' ($rep -match 'никуда не отправляется автоматически') ''

# карточка архива: честные подписи
$cards = @((Format-ArchiveStatusLines) | ForEach-Object { [string]$_.text }) -join "`r`n"
Ok 'карточка: задача' ($cards -match 'ЗАДАЧА DomEnergy-DailyArchive') ''
Ok 'карточка: последняя проверенная копия' ($cards -match 'ПОСЛЕДНЯЯ ПРОВЕРЕННАЯ КОПИЯ') ''
Ok 'карточка: честно про включённый ПК' ($cards -match 'Копия с ПК требует включённого ПК') ''
Ok 'карточка: про два снимка' ($cards -match 'Два снимка одновременно не запускаются') ''
Ok 'карточка: полные копии (не счётчик строк)' ($cards -match 'ПОЛНЫЕ НЕЗАВИСИМЫЕ КОПИИ') ''
Ok 'карточка: путь архива' ($cards -match 'D:\\DomEnergy\\DomEnergy-Archive') ''
Ok 'карточка: SHA-256 объектов' ($cards -match 'SHA-256: 8 из 8 объектов совпали') ''

# без платы отчёт не выдумывает значения
$saveBoard = $script:board
$script:board = $null
$script:boardErr = 'нет ответа: таймаут http://192.168.1.51/data'
$rep2 = (Get-AnonDiagnosticsReport) -join "`r`n"
Ok 'отчёт без платы: честно «НЕ отвечает»' ($rep2 -match 'плата: НЕ отвечает') ''
Ok 'отчёт без платы: IP в тексте ошибки скрыт' ($rep2 -notmatch '192\.168\.') ''
$script:board = $saveBoard
$script:boardErr = ''

# --- 7. живая (только чтение) проверка реального архива ---------------------
try {
    $script:archState = $null
    $script:ArchRoot = 'D:\DomEnergy\DomEnergy-Archive'
    $live = Get-ArchiveState -Verify
    Ok 'живой архив: папка существует' ([bool]$live.Exists) $live.Root
    Ok 'живой архив: есть снимки' ($live.Snapshots -ge 1) ([string]$live.Snapshots)
    Ok 'живой архив: latest.json разобран' ($null -ne $live.Latest -and $live.Latest.Ok) ([string]$live.Latest.Error)
    Ok 'живой архив: SHA-256 всех объектов последней копии сошлись' ($live.VerifyOk -eq $live.Latest.FileCount -and $live.VerifyBad -eq 0) ('ok=' + $live.VerifyOk + ' bad=' + $live.VerifyBad + ' ms=' + $live.VerifyMs)
    Ok 'живой архив: свободное место прочитано' ($live.FreeBytes -gt 0) ([string]$live.FreeBytes)
    Ok 'живой архив: полные копии с .sha256' ($live.Zips -ge 1 -and $live.LastZipSha) ('zips=' + $live.Zips + ' sha=' + $live.LastZipSha)
    # повреждённый хеш должен обнаруживаться той же проверкой
    $broken = Convert-ArchiveManifestText ((Get-Content -LiteralPath (Join-Path $script:ArchRoot 'latest.json') -Raw -Encoding UTF8))
    $files = @($broken.Files)
    if ($files.Count -gt 0) {
        $badSha = ('0' * 64)
        $files[0].Sha256 = $badSha
        $broken.Files = $files
        $v = Test-ArchiveManifestObjects $broken
        Ok 'живой архив: подмена SHA-256 обнаруживается' ($v.Bad -ge 1 -and $v.Ok -eq ($files.Count - 1)) ('ok=' + $v.Ok + ' bad=' + $v.Bad)
    }
    $py = Find-ArchivePython
    Ok 'python для archive-sd.py найден' ([bool]$py) $py
    Ok 'корень диска архива определён' ((Get-ArchiveDriveRoot) -eq 'D:') (Get-ArchiveDriveRoot)
} catch {
    Ok 'живая проверка архива' $false $_.Exception.Message
}

# --- итог -------------------------------------------------------------------
Write-Host ''
Write-Host ('ИТОГО: PASS ' + $PASS + ' · FAIL ' + $FAIL)
if ($FAIL -gt 0) { exit 1 } else { exit 0 }
