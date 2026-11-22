$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'pc/DomMonitor.ps1'),[ref]$tokens,[ref]$errors)
if($errors){throw ($errors|Out-String)}
foreach($name in @('Pump-Board','Pump-Days','Pump-History','Request-Day','Start-ChartRange','Pump-ChartRange','Update-EnergyChart','Pump-Ota','Field','Num')) {
    $n=$ast.Find({param($x) $x -is [Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $name},$true)
    Invoke-Expression $n.Extent.Text
}
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output ('PASS: '+$message)}
function Add-Log {param($text,$color)}
function Write-AppLog {param($text,$color)}
function Refresh-ChartNow {}
function C($hex){[Drawing.ColorTranslator]::FromHtml($hex)}
function Fnt($name,$size,$bold){New-Object Drawing.Font($name,$size)}
$script:http=New-Object Object
$script:board=[pscustomobject]@{v=239;up=1000}
$script:boardAt=Get-Date;$script:pollStartAt=Get-Date
$script:pollTask=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"v":0,"vSeen":0,"pSeen":0,"tSeen":1,"temp":19.5,"up":5}'}
Pump-Board
Assert ($script:board.up -eq 5) 'zero-voltage startup response replaces old board state'
$script:days=@('2026-11-12');$script:daysAt=Get-Date
$script:httpSlow=New-Object Object
$script:daysTask=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"pending":true}'}
Pump-Days
Assert ($script:days[0] -eq '2026-11-12' -and ((Get-Date)-$script:daysAt).TotalSeconds -ge 119) 'pending date index preserves old list and retries promptly'
$script:httpSlow=New-Object Object
$script:histCache=@{};$script:histAt=@{};$script:histTaskDate='2026-11-12'
$script:histTask=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"date":"2026-11-12","trunc":true,"hours":[]}'}
Pump-History
Assert (-not $script:histAt.ContainsKey('2026-11-12')) 'truncated history is never cached as complete'
Assert ($script:histRequests.ContainsKey('2026-11-12')) 'failed history is queued for retry'
$script:histTaskDate='2026-11-11'
$script:histTask=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"pending":true}'}
Pump-History
Assert ($script:histRequests.ContainsKey('2026-11-11') -and -not $script:histAt.ContainsKey('2026-11-11')) 'pending history waits without poisoning cache'
$script:histTaskDate='2026-11-11'
$script:histTask=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"date":"2026-11-11","hours":[{"h":0,"n":2,"t":0}]}'}
Pump-History
Assert ($script:histAt.ContainsKey('2026-11-11') -and $script:histCache['2026-11-11'][0].t -eq 0) 'complete zero-temperature history is accepted'
$script:cfg=@{boardIp='192.168.1.51'}
$script:http=[pscustomobject]@{Calls=0}
$script:http|Add-Member ScriptMethod GetStringAsync {param($url);$this.Calls++;return [pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::Faulted}}
$script:httpSlow=[pscustomobject]@{Calls=0}
$script:httpSlow|Add-Member ScriptMethod GetStringAsync {param($url);$this.Calls++;return [pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::Faulted}}
$script:chartTasks=@{}
$from=(Get-Date).AddDays(-6).ToString('yyyy-MM-dd');$key='week'+$from
Start-ChartRange $key $from (Get-Date).ToString('yyyy-MM-dd')
Assert ($script:http.Calls -eq 0 -and $script:httpSlow.Calls -eq 1) 'range uses the slow HTTP client'
Pump-ChartRange
Assert (-not $script:chartRangeCache.ContainsKey($key)) 'failed range is not cached as empty'
Start-ChartRange $key $from (Get-Date).ToString('yyyy-MM-dd')
Assert ($script:httpSlow.Calls -eq 1) 'range retries respect backoff'
$script:chartRangeNext[$key]=(Get-Date).AddSeconds(-1)
Start-ChartRange $key $from (Get-Date).ToString('yyyy-MM-dd')
Assert ($script:httpSlow.Calls -eq 2) 'range retries after backoff'
$script:chartTasks[$key]=[pscustomobject]@{task=[pscustomobject]@{IsCompleted=$true;Status=[Threading.Tasks.TaskStatus]::RanToCompletion;Result='{"pending":true,"retryMs":700}'};start=(Get-Date)}
Pump-ChartRange
Assert (-not $script:chartRangeCache.ContainsKey($key) -and -not $script:chartRangeError.ContainsKey($key) -and $script:chartRangeNext.ContainsKey($key)) 'pending period waits without caching empty data or reporting failure'
$script:chartMode='day';$script:CLR=@{dim='#888888';warn='#FFAA00';text='#FFFFFF';line='#333333'}
$ch=New-Object Windows.Forms.DataVisualization.Charting.Chart
foreach($name in @('main','temp')){$ch.ChartAreas.Add((New-Object Windows.Forms.DataVisualization.Charting.ChartArea $name))}
foreach($name in @('Энергия, Вт·ч','День, Вт·ч','Ночь, Вт·ч','Напряжение, В','Температура, °C','Вчера, В')){
    $series=New-Object Windows.Forms.DataVisualization.Charting.Series ([string]$name);$series.ChartArea='main';$ch.Series.Add($series)
}
$script:histCache['2026-11-11']=@([pscustomobject]@{h=0;n=2;t=0;v=230;kwh=.1},[pscustomobject]@{h=1;n=2;t=-3;v=231;kwh=.1},[pscustomobject]@{h=2;n=0;t=0;v=0;kwh=0})
Update-EnergyChart $ch '2026-11-11'
Assert ($ch.Series['Температура, °C'].Points.Count -eq 2) 'main chart retains zero and negative temperature, skips empty hours'
$ch.Dispose()
$ota=$ast.Find({param($x) $x -is [Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Pump-Ota'},$true)
Assert ($null -ne $ota -and (Get-Content pc/DomMonitor.ps1 -Raw) -notmatch 'WindowStyle Hidden -Wait -PassThru') 'OTA is pumped asynchronously'
$path=[IO.Path]::GetTempFileName()
$lock=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $script:otaProc=[pscustomobject]@{HasExited=$false}
    $script:otaFiles=@{out=$path+'.out';err=$path+'.err';progress=$path}
    Pump-Ota
    Assert $true 'locked progress file cannot break the UI timer'
} finally {$lock.Dispose();Remove-Item -LiteralPath $path}
$script:otaRead=@{out=0;err=0}
$btnOta=[pscustomobject]@{Enabled=$false};$btnFlash=[pscustomobject]@{Enabled=$false};$btnCompile=[pscustomobject]@{Enabled=$false}
$lblFlashState=[pscustomobject]@{Text=''}
$script:otaProc=Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-Command','exit 0') -RedirectStandardOutput $script:otaFiles.out -RedirectStandardError $script:otaFiles.err
$null=$script:otaProc.Handle
$script:otaProc.WaitForExit()
Pump-Ota
Assert ($btnOta.Enabled -and $lblFlashState.Text -like 'Прошивка и сборка*') 'native successful child process is reported as successful OTA'
foreach($name in @('out','err')){Remove-Item -LiteralPath $script:otaFiles[$name] -ErrorAction SilentlyContinue}
