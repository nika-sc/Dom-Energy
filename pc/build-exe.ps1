# build-exe.ps1 — собирает DomMonitor.exe из launcher.cs без .NET SDK и без интернета.
#
# Используется компилятор C#, который уже лежит в Windows вместе с .NET Framework:
#   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe
#
# Запуск:
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-exe.ps1
#   (или просто правой кнопкой -> "Выполнить с помощью PowerShell")
#
# Результат: DomMonitor.exe рядом с этим скриптом — GUI-приложение (winexe),
# без консольного окна, размером около 10-20 КБ.

[CmdletBinding()]
param(
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $here 'DomMonitor.exe'
}

function Fail([string]$Message) {
    Write-Host ''
    Write-Host 'ОШИБКА: ' -ForegroundColor Red -NoNewline
    Write-Host $Message
    exit 1
}

# --- 1. Компилятор -----------------------------------------------------------
$cscCandidates = @(
    (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'),
    (Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe')
)
$csc = $null
foreach ($candidate in $cscCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        $csc = $candidate
        break
    }
}
if (-not $csc) {
    Fail @"
csc.exe для .NET Framework 4.x не найден.
Проверены пути:
  $($cscCandidates -join "`r`n  ")
Установите .NET Framework 4.8 (он уже есть на Windows 10/11) или укажите csc.exe вручную.
"@
}

# --- 2. Исходники ------------------------------------------------------------
$source = Join-Path $here 'launcher.cs'
$icon = Join-Path $here 'app.ico'

if (-not (Test-Path -LiteralPath $source)) { Fail "Не найден исходник: $source" }
if (-not (Test-Path -LiteralPath $icon))   { Fail "Не найдена иконка: $icon" }

# --- 3. Компиляция -----------------------------------------------------------
if (Test-Path -LiteralPath $OutputPath) {
    Remove-Item -LiteralPath $OutputPath -Force
}

$cscArgs = @(
    '/nologo',
    '/target:winexe',
    '/platform:anycpu',
    '/optimize+',
    "/win32icon:$icon",
    "/out:$OutputPath",
    $source
)

Write-Host 'Компилятор: ' -NoNewline
Write-Host $csc
Write-Host 'Исходник:   ' -NoNewline
Write-Host $source
Write-Host ''

& $csc $cscArgs
$compileCode = $LASTEXITCODE

if ($compileCode -ne 0) {
    Fail "csc.exe завершился с кодом $compileCode. Текст ошибки — выше."
}
if (-not (Test-Path -LiteralPath $OutputPath)) {
    Fail "csc.exe не создал файл $OutputPath"
}

# --- 4. Проверка результата --------------------------------------------------
$exe = Get-Item -LiteralPath $OutputPath
$sizeKb = $exe.Length / 1KB

# Читаем заголовок PE и проверяем подсистему: 2 = Windows GUI (без консоли).
$subsystem = $null
try {
    $bytes = [System.IO.File]::ReadAllBytes($OutputPath)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $optionalHeader = $peOffset + 24
    $subsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 68)
} catch {
    $subsystem = $null
}

Write-Host ''
Write-Host 'ГОТОВО' -ForegroundColor Green
Write-Host ("  Файл:       {0}" -f $exe.FullName)
Write-Host ("  Размер:     {0:N0} байт ({1:N1} КБ)" -f $exe.Length, $sizeKb)
if ($subsystem -eq 2) {
    Write-Host '  Подсистема: Windows GUI (2) — консольного окна нет' -ForegroundColor Green
} elseif ($null -ne $subsystem) {
    Write-Host ("  Подсистема: {0} (ожидалась 2 = Windows GUI!)" -f $subsystem) -ForegroundColor Yellow
} else {
    Write-Host '  Подсистема: не удалось прочитать заголовок PE' -ForegroundColor Yellow
}

if ($sizeKb -ge 100) {
    Write-Host ("  ВНИМАНИЕ: размер {0:N1} КБ >= 100 КБ" -f $sizeKb) -ForegroundColor Yellow
}

exit 0
