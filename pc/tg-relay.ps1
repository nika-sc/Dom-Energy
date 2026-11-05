# ============================================================================
#  tg-relay.ps1 — отправка в Telegram через реле GitHub Actions
# ----------------------------------------------------------------------------
#  Telegram API из этой сети недоступен, поэтому сообщение уходит так:
#    скрипт -> GitHub API repository_dispatch -> workflow .github/workflows/telegram.yml
#           -> api.telegram.org (с серверов GitHub) -> твой чат
#
#  Токен GitHub НЕ хранится в репозитории: он лежит в
#  %APPDATA%\DomMonitor\settings.json (поле githubToken) или передаётся -Token.
#  Токен бота и chat_id — в секретах репозитория:
#  Settings -> Secrets and variables -> Actions:
#     TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID
#
#  Пример:
#     .\tg-relay.ps1 -Text "Плата не отвечает 5 мин"
#     .\tg-relay.ps1 -TextBase64 <base64> -Token ghp_xxx
# ============================================================================
param(
    [string]$Text = "",
    [string]$TextBase64 = "",
    [string]$Token = "",
    [string]$Repo = "nika-sc/Dom-Energy",
    [string]$EventType = "notify",
    [int]$TimeoutSec = 25
)

$ErrorActionPreference = "Continue"

function Log($m) {
    $dir = Join-Path $env:APPDATA "DomMonitor"
    if (!(Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $line = "[{0}] {1}`r`n" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m
    [System.IO.File]::AppendAllText((Join-Path $dir "tg-relay.log"), $line, (New-Object System.Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------- текст
if ($TextBase64) {
    try { $Text = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($TextBase64)) }
    catch { Log "не удалось раскодировать TextBase64"; exit 1 }
}
if (-not $Text) { Log "пустой текст — нечего отправлять"; exit 1 }
if ($Text.Length -gt 3500) { $Text = $Text.Substring(0, 3497) + "…" }

# ---------------------------------------------------------------- токен
if (-not $Token) {
    $cfgFile = Join-Path $env:APPDATA "DomMonitor\settings.json"
    if (Test-Path $cfgFile) {
        try { $Token = (Get-Content $cfgFile -Raw | ConvertFrom-Json).githubToken } catch { }
    }
}
if (-not $Token) { Log "нет githubToken (settings.json) — отправка невозможна"; exit 2 }

# ---------------------------------------------------------------- запрос
$body = @{
    event_type     = $EventType
    client_payload = @{ text = $Text }
} | ConvertTo-Json -Depth 4 -Compress

$url = "https://api.github.com/repos/$Repo/dispatches"
try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)   # иначе русский текст уходит в ????
    $r = Invoke-WebRequest -Uri $url -Method Post -Body $bytes -TimeoutSec $TimeoutSec `
        -ContentType 'application/json; charset=utf-8' `
        -Headers @{
            "Authorization"        = "Bearer $Token"
            "Accept"               = "application/vnd.github+json"
            "X-GitHub-Api-Version" = "2022-11-28"
            "User-Agent"           = "dom-monitor"
        } -UseBasicParsing
    Log ("отправлено в реле: HTTP " + $r.StatusCode + " · " + $Text)
} catch {
    $code = ""
    try { $code = $_.Exception.Response.StatusCode.value__ } catch { }
    Log ("ошибка реле: HTTP " + $code + " · " + $_.Exception.Message + " · " + $Text)
    exit 3
}

exit 0
