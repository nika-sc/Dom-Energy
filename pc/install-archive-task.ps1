param(
    [string]$Python='C:\Users\<user>\AppData\Local\Programs\Python\Python312\python.exe',
    [string]$Board='http://192.168.1.51',
    [string]$Destination='D:\DomEnergy\DomEnergy-Archive'
)
$ErrorActionPreference='Stop'
foreach($value in @($Python,$Board,$Destination,$PSScriptRoot)){if($value -match '["\r\n]'){throw 'Invalid task argument'}}
foreach($file in @($Python,(Join-Path $PSScriptRoot 'archive-sd.py'),(Join-Path $PSScriptRoot 'board_http.py'))){if(-not(Test-Path -LiteralPath $file)){throw ('Missing dependency: '+$file)}}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$scheduler=New-Object -ComObject Schedule.Service;$scheduler.Connect()
$definition=$scheduler.NewTask(0)
$definition.RegistrationInfo.Description='Dom Energy: daily lossless SD backup, SHA256/gzip verification, keep all snapshots.'
$definition.Principal.UserId=[Security.Principal.WindowsIdentity]::GetCurrent().Name
$definition.Principal.LogonType=3 # InteractiveToken, no stored password or elevated privileges
$definition.Principal.RunLevel=0
$definition.Settings.Enabled=$true
$definition.Settings.StartWhenAvailable=$true
$definition.Settings.DisallowStartIfOnBatteries=$false
$definition.Settings.StopIfGoingOnBatteries=$false
$definition.Settings.WakeToRun=$false
$definition.Settings.MultipleInstances=2 # IgnoreNew
$definition.Settings.ExecutionTimeLimit='PT3H' # first full two-year copy can be slow over Wi-Fi
$definition.Settings.RestartCount=3
$definition.Settings.RestartInterval='PT10M'
$daily=$definition.Triggers.Create(2)
$daily.StartBoundary=(Get-Date).Date.AddHours(5).AddMinutes(30).ToString('yyyy-MM-ddTHH:mm:ss')
$daily.DaysInterval=1;$daily.Enabled=$true
$logon=$definition.Triggers.Create(9);$logon.UserId=$definition.Principal.UserId;$logon.Delay='PT2M';$logon.Enabled=$true
$action=$definition.Actions.Create(0)
$action.Path=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$action.Arguments='-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+(Join-Path $PSScriptRoot 'archive-run.ps1')+'" -Python "'+$Python+'" -Board "'+$Board+'" -Destination "'+$Destination+'"'
$action.WorkingDirectory=$PSScriptRoot
$task=$scheduler.GetFolder('\').RegisterTaskDefinition('DomEnergy-DailyArchive',$definition,6,$definition.Principal.UserId,$null,3)
Write-Output ('Installed task: '+$task.Name+'; daily 05:30 and 2 minutes after logon; missed run on next availability.')
Write-Output ('Destination: '+$Destination)
