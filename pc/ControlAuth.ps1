function Get-ControlKey {
    $key = [string]$env:DOM_CONTROL_KEY
    if (-not $key) {
        $path = Join-Path $env:APPDATA 'DomMonitor\control.key'
        if (Test-Path -LiteralPath $path) { $key = [IO.File]::ReadAllText($path).Trim() }
    }
    if ($key -and $key -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid device control key' }
    return $key
}
function Get-ControlHeaders {
    $headers = @{}
    $key = Get-ControlKey
    if ($key) { $headers['X-Dom-Key'] = $key }
    return $headers
}
function Set-ControlClientHeaders($client) {
    $null = $client.DefaultRequestHeaders.Remove('X-Dom-Key')
    $key = Get-ControlKey
    if ($key) { $client.DefaultRequestHeaders.Add('X-Dom-Key', $key) }
}
function Get-ControlWebUrl([string]$ip) {
    $url = 'http://' + $ip + '/'
    $key = Get-ControlKey
    if ($key) { $url += '#control=' + $key }
    return $url
}
