$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$venvPath = Join-Path $repoRoot ".venv"
$pythonExe = Join-Path $venvPath "Scripts\\python.exe"

if (-not (Test-Path $venvPath)) {
    python -m venv $venvPath
}

& $pythonExe -m pip install --upgrade pip
& $pythonExe -m pip install -r (Join-Path $repoRoot "tools\\requirements.txt")

Write-Host "Environment ready."
Write-Host "Python:" $pythonExe
