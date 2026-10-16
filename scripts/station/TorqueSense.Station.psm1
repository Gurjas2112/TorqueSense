# Shared by install-release.ps1, rollback-release.ps1 and register-station.ps1.
# Windows PowerShell 5.1 and PowerShell 7 both run it.
Set-StrictMode -Version Latest

$script:TaskName  = 'TorqueSense Supervisor'
$script:StatusUrl = 'http://127.0.0.1:8099'

function Get-ReleaseTarget([string]$Root) {
    $current = Join-Path $Root 'current'
    if (-not (Test-Path $current)) { return $null }
    $t = (Get-Item $current).Target
    if ($t -is [array]) { $t = $t[0] }
    return $t
}

function Set-ReleaseTarget([string]$Root, [string]$Release) {
    $current = Join-Path $Root 'current'
    if (Test-Path $current) {
        # rmdir removes the junction itself. Remove-Item on a junction can recurse into its target in
        # Windows PowerShell 5.1 and delete the release it points at.
        cmd /c rmdir "$current" | Out-Null
        if (Test-Path $current) { throw "could not remove the junction $current" }
    }
    New-Item -ItemType Junction -Path $current -Target $Release | Out-Null
}

function Stop-Supervisor([int]$TimeoutSec = 60) {
    try { Invoke-RestMethod -Method Post "$script:StatusUrl/shutdown" -TimeoutSec 5 | Out-Null } catch { $null = $_ }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $task = Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
        if (-not $task -or $task.State -ne 'Running') { return }
        Start-Sleep -Seconds 1
    }
    Stop-ScheduledTask -TaskName $script:TaskName    # last resort: the job object takes the children with it
}

function Start-Supervisor { Start-ScheduledTask -TaskName $script:TaskName }

function Wait-Healthy([int]$TimeoutSec = 240) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $r = Invoke-WebRequest "$script:StatusUrl/health" -UseBasicParsing -TimeoutSec 3
            if ($r.StatusCode -eq 200) { return $true }
        } catch { $null = $_ }
        Start-Sleep -Seconds 2
    }
    try { Write-Warning ((Invoke-RestMethod "$script:StatusUrl/status" -TimeoutSec 3) | ConvertTo-Json -Depth 3) } catch { $null = $_ }
    return $false
}

function Switch-Release([string]$Root, [string]$Release) {
    $previous = Get-ReleaseTarget $Root
    Stop-Supervisor
    Set-ReleaseTarget $Root $Release
    if ($previous) { Set-Content (Join-Path $Root 'previous.txt') $previous }
    Start-Supervisor
    if (Wait-Healthy) { return $true }
    if ($previous) {
        Write-Warning "$Release did not become healthy; rolling back to $previous"
        Stop-Supervisor
        Set-ReleaseTarget $Root $previous
        Start-Supervisor
        [void](Wait-Healthy)
    }
    return $false
}

function Get-RuntimePath([string]$Root, [string]$Release) {
    # the PATH the supervisor gives its children, from the machine's supervisor.json
    $cfg = Get-Content (Join-Path $Root 'config\supervisor.json') -Raw | ConvertFrom-Json
    $dirs = @()
    foreach ($p in $cfg.path) {
        $d = $p
        foreach ($v in $cfg.vars.PSObject.Properties) { $d = $d.Replace('{' + $v.Name + '}', [string]$v.Value) }
        $dirs += $d.Replace((Join-Path $Root 'current'), $Release)
    }
    return ($dirs -join ';')
}

Export-ModuleMember -Function Get-ReleaseTarget, Set-ReleaseTarget, Stop-Supervisor, Start-Supervisor,
    Wait-Healthy, Switch-Release, Get-RuntimePath
