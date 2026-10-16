# The operator HMI, full screen, kept open. Started by the 'TorqueSense Kiosk' task at sign-in.
param([string]$Station = 'W-14', [string]$Root = 'C:\TorqueSense')
$ErrorActionPreference = 'Continue'
$profileDir = Join-Path $Root 'data\edge-kiosk'
$url = "http://127.0.0.1:8000/hmi/$Station"

# wait for the API to serve the dashboard; the HMI itself shows AMBER until the engine is ready
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline) {
    try { if ((Invoke-WebRequest $url -UseBasicParsing -TimeoutSec 3).StatusCode -eq 200) { break } } catch { $null = $_ }
    Start-Sleep -Seconds 3
}
while ($true) {
    $running = Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($profileDir) }
    if (-not $running) {
        Start-Process msedge -ArgumentList '--kiosk', $url, '--edge-kiosk-type=fullscreen',
            "--user-data-dir=$profileDir", '--no-first-run'
    }
    Start-Sleep -Seconds 10
}
