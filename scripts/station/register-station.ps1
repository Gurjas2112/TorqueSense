<#
.SYNOPSIS
  One-time setup of HSGPC as an unattended station, as Administrator. Creates the folders and their
  permissions, writes config\supervisor.json with the real NVIDIA DLL folders, sets the power plan, and
  registers three scheduled tasks for the station account: the supervisor and the kiosk at sign-in,
  and the nightly backup.
.EXAMPLE
  .\register-station.ps1 -User tsdemo
#>
#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$User,          # a standard local account, signed in automatically
    [string]$Root = 'C:\TorqueSense',
    [string]$TrtLib = 'C:\dev\tools\TensorRT\lib',
    [string]$Template                                      # defaults to the current release's template
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$account = "$env:COMPUTERNAME\$User"
if (-not (Get-LocalUser -Name $User -ErrorAction SilentlyContinue)) {
    throw "Create the standard local account '$User' first (Settings > Accounts > Other users)."
}

# folders: config readable only by the station account and administrators; data writable by it
foreach ($d in 'config', 'data', 'releases', 'venvs', 'data\logs', 'data\spool', 'data\backups') {
    New-Item -ItemType Directory -Force (Join-Path $Root $d) | Out-Null
}
icacls (Join-Path $Root 'config') /inheritance:r /grant:r "${account}:(OI)(CI)R" 'Administrators:(OI)(CI)F' 'SYSTEM:(OI)(CI)F' | Out-Null
icacls (Join-Path $Root 'data') /grant "${account}:(OI)(CI)M" | Out-Null
# the developer account (the one running this script, and the Jenkins agent) stages releases
$dev = "$env:USERDOMAIN\$env:USERNAME"
foreach ($d in 'releases', 'venvs', 'data') { icacls (Join-Path $Root $d) /grant "${dev}:(OI)(CI)M" | Out-Null }

# find the NVIDIA DLL folders once, and write them down; nothing at run time depends on PATH
function Find-DllDir([string]$dll, [string[]]$under) {
    foreach ($u in $under) {
        if (-not $u -or -not (Test-Path $u)) { continue }
        $hit = Get-ChildItem $u -Recurse -Filter $dll -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.DirectoryName }
    }
    throw "$dll not found under $($under -join ', ')"
}
$cudaBin  = Find-DllDir 'cudart64_13.dll' @($env:CUDA_PATH)
$cudnnBin = Find-DllDir 'cudnn64_9.dll' @('C:\Program Files\NVIDIA\CUDNN')
if (-not (Test-Path (Join-Path $TrtLib 'nvinfer_10.dll'))) { throw "nvinfer_10.dll not in $TrtLib (spec Section 28)" }
$node = (Get-Command node.exe -ErrorAction Stop).Source

if (-not $Template) { $Template = Join-Path $Root 'current\config\supervisor.template.json' }
$cfg = Get-Content $Template -Raw | ConvertFrom-Json
$cfg.vars.cuda_bin  = $cudaBin
$cfg.vars.cudnn_bin = $cudnnBin
$cfg.vars.trt_lib   = $TrtLib
$cfg.vars.node      = $node
$cfg.vars.release   = Join-Path $Root 'current'
$cfg.vars.config    = Join-Path $Root 'config'
$cfg.vars.data      = Join-Path $Root 'data'
$cfg | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Root 'config\supervisor.json') -Encoding UTF8
Write-Host "supervisor.json: CUDA $cudaBin - cuDNN $cudnnBin - TensorRT $TrtLib - node $node"

# power: never sleep on AC, and closing the lid on AC does nothing
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0
powercfg /change monitor-timeout-ac 0
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setactive SCHEME_CURRENT

# tasks
$principal = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Limited
$forever = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -MultipleInstances IgnoreNew -StartWhenAvailable
$logon = New-ScheduledTaskTrigger -AtLogOn -User $account

$sup = New-ScheduledTaskAction -Execute (Join-Path $Root 'current\bin\supervisor.exe') `
    -Argument "-config `"$Root\config\supervisor.json`"" -WorkingDirectory (Join-Path $Root 'current')
Register-ScheduledTask -TaskName 'TorqueSense Supervisor' -Action $sup -Trigger $logon -Principal $principal -Settings $forever -Force | Out-Null

$kioskTrigger = New-ScheduledTaskTrigger -AtLogOn -User $account
$kioskTrigger.Delay = 'PT20S'
$kiosk = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$Root\current\scripts\station\kiosk.ps1`""
Register-ScheduledTask -TaskName 'TorqueSense Kiosk' -Action $kiosk -Trigger $kioskTrigger -Principal $principal -Settings $forever -Force | Out-Null

$nightly = New-ScheduledTaskTrigger -Daily -At '02:30'
$backupSettings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 1)
$backup = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$Root\current\scripts\station\backup.ps1`""
Register-ScheduledTask -TaskName 'TorqueSense Backup' -Action $backup -Trigger $nightly -Principal $principal -Settings $backupSettings -Force | Out-Null

Write-Host "Registered. Next: Sysinternals Autologon for $User, then install a release (install-release.ps1)."
