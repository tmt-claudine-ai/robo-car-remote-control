$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$tmpDir = Join-Path $repoRoot "tmp"
$pidFile = Join-Path $tmpDir "local_broker.pid"
$stdoutLog = Join-Path $tmpDir "local_broker.log"
$stderrLog = Join-Path $tmpDir "local_broker.err.log"
$pythonExe = Join-Path $repoRoot ".venv\\Scripts\\python.exe"

New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

if (Test-Path $pidFile) {
    $existingPid = Get-Content $pidFile -ErrorAction SilentlyContinue
    if ($existingPid) {
        $existingProcess = Get-Process -Id $existingPid -ErrorAction SilentlyContinue
        if ($existingProcess) {
            Write-Host "Local broker already running on mqtt://127.0.0.1:1883"
            exit 0
        }
    }
    Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
}

if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

$process = Start-Process -FilePath $pythonExe `
    -ArgumentList ".\\tools\\local_broker.py","--host","0.0.0.0","--port","1883" `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -PassThru

Set-Content -Path $pidFile -Value $process.Id
Write-Host "Local broker is starting on mqtt://0.0.0.0:1883"
