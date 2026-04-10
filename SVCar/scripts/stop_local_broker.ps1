$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$pidFile = Join-Path $repoRoot "tmp\\local_broker.pid"

if (-not (Test-Path $pidFile)) {
    Write-Host "Local broker pid file not found."
    exit 0
}

$brokerPid = Get-Content $pidFile -ErrorAction SilentlyContinue
if ($brokerPid) {
    Stop-Process -Id $brokerPid -Force -ErrorAction SilentlyContinue
}

Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
Write-Host "Local broker stopped."
