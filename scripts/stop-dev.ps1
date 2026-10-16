# scripts/stop-dev.ps1 - stops what start-dev.ps1 started: the engine first (an orderly POST /shutdown),
# then everything else by the process ids it recorded.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$pids = Join-Path $root 'var\logs\pids.txt'
try { Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'http://127.0.0.1:8095/shutdown' -TimeoutSec 2 | Out-Null; Start-Sleep -Seconds 2 } catch { Write-Host 'engine: no answer on :8095' }
if (Test-Path $pids) {
    foreach ($line in Get-Content $pids) {
        $name, $id = $line -split ' '
        try { Stop-Process -Id ([int]$id) -Force -ErrorAction Stop; Write-Host "stopped $name" } catch { Write-Host "$name already gone" }
    }
    Remove-Item $pids
}
