param(
    [string]$Python='C:\Users\<user>\AppData\Local\Programs\Python\Python312\python.exe',
    [string]$Board='http://192.168.1.51',
    [string]$Destination='D:\DomEnergy\DomEnergy-Archive'
)
$ErrorActionPreference='Stop'
foreach($value in @($Python,$Board,$Destination,$PSScriptRoot)){if($value -match '["\r\n]'){throw 'Invalid archive argument'}}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$p=Start-Process -FilePath $Python -WindowStyle Hidden -PassThru -ArgumentList @(
    ('"'+(Join-Path $PSScriptRoot 'archive-sd.py')+'"'),'--url',('"'+$Board+'"'),
    '--dest',('"'+$Destination+'"')) -RedirectStandardOutput (Join-Path $Destination 'run-last.out.txt') -RedirectStandardError (Join-Path $Destination 'run-last.err.txt')
$null=$p.Handle
$p.WaitForExit();$code=$p.ExitCode;$p.Dispose()
if($null -eq $code){throw 'Archive process exit status unavailable'}
exit $code
