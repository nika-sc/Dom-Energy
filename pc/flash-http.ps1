﻿param(
    [string]$Ip='192.168.1.51',
    [string]$Bin='',
    [string]$Sketch='C:\esp32\cydv4',
    [string]$Cli='C:\Program Files\Arduino CLI\arduino-cli.exe',
    [string]$Fqbn='esp32:esp32:esp32:PartitionScheme=min_spiffs',
    [switch]$Build,
    [int]$TimeoutSec=300,
    [ValidateRange(2,300)][int]$ReadyTimeoutSec=150,
    [string]$ProgressPath='',
    [string]$UploadRate=$env:DOM_OTA_RATE
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding $false
$OutputEncoding = [Console]::OutputEncoding
$env:PYTHONIOENCODING='utf-8'
. (Join-Path $PSScriptRoot 'ControlAuth.ps1')
function Step($message){Write-Host ('['+(Get-Date -Format 'HH:mm:ss')+'] '+$message)}
function Read-BoardText([string]$Path,[int]$ReadTimeout=10,[int]$Attempts=3){
    for($attempt=0;$attempt -lt $Attempts;$attempt++){
        $response=$null;$reader=$null
        try {
            $request=[Net.HttpWebRequest]::Create('http://'+$Ip+$Path)
            $request.Proxy=$null;$request.KeepAlive=$false
            $request.Timeout=$ReadTimeout*1000;$request.ReadWriteTimeout=$ReadTimeout*1000
            $response=$request.GetResponse()
            $reader=New-Object IO.StreamReader -ArgumentList @($response.GetResponseStream(),[Text.Encoding]::UTF8)
            return $reader.ReadToEnd()
        } catch {
            if($attempt+1 -ge $Attempts){throw}
            Start-Sleep -Milliseconds (350*($attempt+1))
        } finally {
            if($reader){$reader.Dispose()}
            if($response){$response.Dispose()}
        }
    }
}
if($UploadRate -and $UploadRate -notmatch '^[1-9][0-9]*[kKmM]?$'){throw 'Invalid OTA test rate'}
function ImageHash([string]$path){
    $stream=[IO.File]::OpenRead($path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
}
if($Ip -notmatch '^[a-zA-Z0-9.-]+(:[0-9]{1,5})?$'){throw 'Invalid device address'}
if($Build){
    if(-not (Test-Path -LiteralPath $Sketch) -or -not (Test-Path -LiteralPath $Cli)){throw 'Sketch or compiler not found'}
    if($Fqbn -notmatch '^esp32:esp32:esp32(:|$)'){throw 'Unexpected board type'}
    $Sketch=[IO.Path]::GetFullPath($Sketch)
    $generator=Join-Path $Sketch 'web\build_page.py'
    if(-not (Test-Path -LiteralPath $generator)){throw 'Web asset generator missing'}
    & python $generator
    if($LASTEXITCODE -ne 0){throw 'Web asset generation failed'}
    $out=Join-Path $env:TEMP ('dommon-build-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $out | Out-Null
    if(-not $env:ARDUINO_DIRECTORIES_DATA -and (Test-Path 'D:\DomEnergy\_moved\Arduino15')){$env:ARDUINO_DIRECTORIES_DATA='D:\DomEnergy\_moved\Arduino15'}
    if(-not $env:ARDUINO_DIRECTORIES_USER -and (Test-Path 'C:\Users\<user>\Documents\Arduino')){$env:ARDUINO_DIRECTORIES_USER='C:\Users\<user>\Documents\Arduino'}
    $argsCompile=@('compile','--fqbn',$Fqbn,'--build-path',(Join-Path $out 'build'),'--output-dir',$out)
    $extra=Join-Path $env:USERPROFILE 'OneDrive\Документы\Arduino\libraries'
    if(Test-Path -LiteralPath $extra){$argsCompile+=@('--libraries',$extra)}
    $argsCompile+=$Sketch
    Step ('Сборка выбранного скетча: '+$Sketch)
    & $Cli @argsCompile | Out-Host
    if($LASTEXITCODE -ne 0){throw ('Compilation failed: '+$LASTEXITCODE)}
    $Bin=Join-Path $out 'cydv4.ino.bin'
    $idText=[IO.File]::ReadAllText((Join-Path $Sketch 'build_identity.h'))
    $match=[regex]::Match($idText,'DOM_BUILD_ID\s+"([0-9a-f]{20})"')
    if(-not $match.Success){throw 'Build identity missing'}
    $manifest=[ordered]@{build=$match.Groups[1].Value;sha256=(ImageHash $Bin);fqbn=$Fqbn;sketch=$Sketch}
    $manifest|ConvertTo-Json|Set-Content -LiteralPath ($Bin+'.manifest.json') -Encoding UTF8
}
if(-not $Bin -or -not (Test-Path -LiteralPath $Bin)){throw 'Select a concrete binary or build the selected sketch'}
$manifestPath=$Bin+'.manifest.json'
if(-not (Test-Path -LiteralPath $manifestPath)){throw 'Image manifest missing; build the selected sketch'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$hash=ImageHash $Bin
if($hash -ne $manifest.sha256 -or [string]$manifest.build -notmatch '^[0-9a-f]{20}$'){throw 'Image identity or SHA256 mismatch'}
$size=(Get-Item -LiteralPath $Bin).Length
if($size -lt 256 -or $size -gt 1966080){throw 'Image size outside application partition'}
$reader=[IO.File]::OpenRead($Bin)
try {if($reader.ReadByte() -ne 0xE9){throw 'Not an ESP32 application image'}}finally{$reader.Dispose()}
Step ('Образ: '+$Bin+' · '+$size+' байт · сборка '+$manifest.build)
Step ('SHA256: '+$hash)
$before=Read-BoardText '/data' | ConvertFrom-Json
$key=Get-ControlKey
if($before.controlAuth -and -not $key){throw 'Control key is required for this device'}
if(-not $ProgressPath){$ProgressPath=Join-Path $env:TEMP ('dom-ota-'+[Guid]::NewGuid().ToString('N')+'.progress')}
Step 'Передача прошивки…'
$watch=[Diagnostics.Stopwatch]::StartNew()
$config=if($key){'header = "X-Dom-Key: '+$key+'"'}else{''}
$prior=$ErrorActionPreference
$ErrorActionPreference='Continue'
try {
    $curlArgs=@('--config','-','--progress-bar','--fail','--noproxy','*','--connect-timeout','5','--max-time',"$TimeoutSec")
    if($UploadRate){$curlArgs+=@('--limit-rate',$UploadRate);Step ('Тестовый лимит скорости: '+$UploadRate)}
    $curlArgs+=@('-F',('firmware=@'+$Bin+';type=application/octet-stream'),('http://'+$Ip+'/update'))
    $response=$config | & curl.exe @curlArgs 2> $ProgressPath
    $code=$LASTEXITCODE
} finally {$ErrorActionPreference=$prior}
if($code -ne 0 -or (($response|Out-String).Trim() -ne 'OK')){throw ('Device rejected image; curl code '+$code)}
Step ('Образ принят за '+[math]::Round($watch.Elapsed.TotalSeconds,1)+' с. Ожидание запуска…')
$ready=$false
for($i=0;$i -lt [math]::Ceiling($ReadyTimeoutSec/2);$i++){
    Start-Sleep -Seconds 2
    try {
        $data=Read-BoardText '/data' 5 1 | ConvertFrom-Json
        if($data.build -eq $manifest.build -and [double]$data.up+2 -lt [double]$before.up+$watch.Elapsed.TotalSeconds){
            $ready=$true;break
        }
    }catch{}
}
if(-not $ready){throw 'Expected build did not start; check power and Wi-Fi'}
Step ('Установлена сборка '+$data.build+'; uptime '+$data.up+' с')
$null=Read-BoardText '/' 15
Step 'Готово: сборка и веб проверены'
