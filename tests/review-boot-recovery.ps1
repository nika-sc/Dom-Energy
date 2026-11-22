# Модель восстановления /energy.csv при загрузке (P0.2).
# Зеркалирует ветви initSD() из firmware/cydv4/cydv4.ino: если /energy.csv нет,
# сначала пробуем вернуть /energy.bak (>40 Б), иначе /energy.tmp; при наличии
# /energy.csv ничего не трогаем. Модель работает на словаре, а не на карте:
# проверяется порядок решений, исход и то, что чужие файлы не меняются.
$ErrorActionPreference = 'Stop'
$script:fail = 0

function Check([bool]$ok, [string]$name) {
    if ($ok) { Write-Host ("PASS: " + $name) }
    else { Write-Host ("FAIL: " + $name); $script:fail++ }
}

class FakeSd {
    [hashtable]$files = @{}
    [hashtable]$renames = @{}
    [bool] exists([string]$p) { return $this.files.ContainsKey($p) }
    [int] size([string]$p) { if ($this.files.ContainsKey($p)) { return $this.files[$p] } else { return 0 } }
    [bool] rename([string]$a, [string]$b) {
        if (-not $this.exists($a)) { return $false }
        if ($this.exists($b)) { return $false }
        $this.files[$b] = $this.files[$a]
        $this.files.Remove($a)
        $this.renames[$a + '->' + $b] = $true
        return $true
    }
}

# Точная копия решения из initSD() (без Serial/печати).
function Recover([FakeSd]$sd) {
    if ($sd.exists('/energy.csv')) { return 'kept' }
    if ($sd.size('/energy.bak') -gt 40) {
        if ($sd.rename('/energy.bak', '/energy.csv')) { return 'from_bak' }
        return 'bak_failed'
    }
    if ($sd.exists('/energy.tmp')) {
        if ($sd.rename('/energy.tmp', '/energy.csv')) { return 'from_tmp' }
    }
    return 'nothing'
}

# 1. Обрыв после rename(csv -> bak): csv нет, bak большой.
$sd = [FakeSd]::new(); $sd.files['/energy.bak'] = 1900000
Check ((Recover $sd) -eq 'from_bak') 'csv отсутствует, bak большой -> восстановлен из bak'
Check ($sd.exists('/energy.csv') -and $sd.size('/energy.csv') -eq 1900000) 'данные bak стали csv, размер сохранён'
Check ($sd.renames.ContainsKey('/energy.bak->/energy.csv')) 'использован rename, а не копирование'

# 2. bak пустой/крошечный, есть .tmp.
$sd = [FakeSd]::new(); $sd.files['/energy.bak'] = 10; $sd.files['/energy.tmp'] = 50000
Check ((Recover $sd) -eq 'from_tmp') 'bak слишком мал, есть tmp -> восстановлен из tmp'
Check ($sd.size('/energy.csv') -eq 50000) 'из tmp получены данные'

# 3. Всё на месте — ничего не трогаем.
$sd = [FakeSd]::new(); $sd.files['/energy.csv'] = 2000000; $sd.files['/energy.bak'] = 2100000
Check ((Recover $sd) -eq 'kept') 'csv на месте -> не трогаем'
Check ($sd.size('/energy.csv') -eq 2000000 -and $sd.size('/energy.bak') -eq 2100000) 'оба файла не изменены'

# 4. Ничего нет — не создаём пустышку (это сделает writeSd с заголовком).
$sd = [FakeSd]::new()
Check ((Recover $sd) -eq 'nothing') 'файлов нет -> восстановление не выдумывает данные'
Check (-not $sd.exists('/energy.csv')) 'пустой csv на загрузке не создаётся'

# 5. bak есть, но rename запрещён (файл занят) — честный отказ, без потери bak.
class StubbornSd : FakeSd { [bool] rename([string]$a, [string]$b) { return $false } }
$sd = [StubbornSd]::new(); $sd.files['/energy.bak'] = 1900000
Check ((Recover $sd) -eq 'bak_failed') 'rename не удался -> честный отказ'
Check ($sd.size('/energy.bak') -eq 1900000) 'bak при отказе сохранён'

Write-Host ''
if ($script:fail -eq 0) { Write-Host 'ИТОГ: все проверки восстановления пройдены' }
else { Write-Host ("ИТОГ: провалов " + $script:fail); exit 1 }
