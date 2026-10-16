# scripts/start-dev.ps1 - development only (spec Section 32). Starts the whole station from the build
# tree, natively: soft PLC, sidecar, API (serving web/dist), Node-RED if installed, the engine and the
# fleet agent. Run from C:\dev\torquesense. Production starts the same processes from an installed
# release under supervisor.exe instead (spec Section 33, deferred on purpose: docs/adr/0010).
#
#   .\scripts\start-dev.ps1                 # one station, W-14
#   .\scripts\start-dev.ps1 -NoFlorence     # sidecar without Florence-2 (clips only)
#   .\scripts\start-dev.ps1 -Door           # also station D-07, the door-trim profile, read from pixels
#   .\scripts\stop-dev.ps1
param(
    [switch]$NoFlorence,
    [switch]$NoNodeRed,
    [switch]$Kiosk,
    [switch]$Door,
    [int]$ApiPort = 8000
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$msys = 'C:\msys64\ucrt64\bin'
$env:Path = "$msys;$env:Path"
$build = Join-Path $root 'build\windows-ci'
$py = Join-Path $root '.venv\Scripts\python.exe'
$logs = Join-Path $root 'var\logs'
New-Item -ItemType Directory -Force $logs | Out-Null

# .env: the API key and the rest, for the engine and the fleet agent (the Python services read it themselves)
foreach ($line in Get-Content (Join-Path $root '.env')) {
    if ($line -match '^\s*([A-Z_][A-Z0-9_]*)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if ($NoFlorence) { $env:TS_FLORENCE = '0' }
$env:TS_DEMO_BASELINE_BUCKETS = '12'
$api = "http://127.0.0.1:$ApiPort"
$env:TS_API_URL = $api

function Start-Bg([string]$name, [string]$exe, [string[]]$arguments) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logs "$name.out.log") -RedirectStandardError (Join-Path $logs "$name.err.log")
    Add-Content -Path (Join-Path $logs 'pids.txt') -Value "$name $($p.Id)"
    Write-Host ("started {0,-12} pid {1}" -f $name, $p.Id)
}

function Wait-Url([string]$url, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        try { Invoke-WebRequest -UseBasicParsing -Uri $url -TimeoutSec 2 | Out-Null; return $true } catch { Start-Sleep -Milliseconds 500 }
    }
    return $false
}

Remove-Item -ErrorAction SilentlyContinue (Join-Path $logs 'pids.txt')
Start-Bg 'softplc' (Join-Path $build 'softplc\softplc.exe') @('4840', '--station', 'W14', '--station', 'D07', '--bool', 'Station.W14.Nutrunner.ResultOK', '--bool', 'Station.D07.ConnectorLock')
Start-Bg 'sidecar' $py @('-m', 'uvicorn', 'sidecar.main:app', '--host', '127.0.0.1', '--port', '8001', '--env-file', '.env')
Start-Bg 'api' $py @('-m', 'uvicorn', 'api.main:app', '--host', '127.0.0.1', '--port', "$ApiPort", '--env-file', '.env')
if (-not (Wait-Url "$api/health" 30)) { throw 'the API did not come up: see var\logs\api.err.log' }

$nodeRed = Join-Path $root 'tools\node-red\node_modules\node-red\red.js'
if (-not $NoNodeRed -and (Test-Path $nodeRed)) {
    # npm 12 leaves the lockfile's peer copy of node-opcua-client out of the top level; node-opcua ships the
    # matching 2.160.0 itself, so point Node-RED's OPC-UA nodes at it instead of changing the pinned lockfile
    $env:NODE_PATH = Join-Path $root 'tools\node-red\node_modules\node-opcua\node_modules'
    Start-Bg 'node-red' 'node' @($nodeRed, '-p', '1880', '-u', (Join-Path $root 'var\node-red'), (Join-Path $root 'softplc\node-red\flows.json'))
}

Start-Bg 'engine-w14' (Join-Path $build 'control-app\torquesense.exe') @('--config', 'config\station_w14.yaml')
if (-not (Wait-Url 'http://127.0.0.1:8095/health' 30)) { throw 'the engine did not come up: see var\logs\engine-w14.err.log' }

if ($Door) {
    Start-Bg 'engine-d07' (Join-Path $build 'control-app\torquesense.exe') @('--config', 'config\station_d07.yaml')
    if (-not (Wait-Url 'http://127.0.0.1:8195/health' 30)) { throw 'the D-07 engine did not come up: see var\logs\engine-d07.err.log' }
}

$agent = Join-Path $root 'build\fleet-agent\fleet-agent.exe'
if (Test-Path $agent) {
    Start-Bg 'agent-w14' $agent @('-station', 'W-14', '-engine', 'http://127.0.0.1:8095/health', '-api', "$api/v1/health/host",
        '-disk', 'C:\', '-spool', (Join-Path $root 'var\spool\agent-w14.jsonl'))
    if ($Door) {
        Start-Bg 'agent-d07' $agent @('-station', 'D-07', '-engine', 'http://127.0.0.1:8195/health', '-api', "$api/v1/health/host",
            '-disk', 'C:\', '-spool', (Join-Path $root 'var\spool\agent-d07.jsonl'))
    }
}

Write-Host ''
Write-Host "dashboard  $api/          HMI $api/hmi/W-14"
Write-Host "harness    $api/harness   fleet $api/fleet"
Write-Host 'node-red   http://127.0.0.1:1880/dashboard/tags'
if ($Door) { Write-Host "line       $api/line?stations=W-14,D-07" }
if ($Kiosk) {
    Start-Process msedge -ArgumentList '--kiosk', "$api/hmi/W-14", '--edge-kiosk-type=fullscreen', '--user-data-dir=C:\dev\tools\edge-kiosk'
}
