<#
.SYNOPSIS  Put the previous release back. One command, no download, about a minute.
#>
#Requires -RunAsAdministrator
[CmdletBinding()]
param([string]$Root = 'C:\TorqueSense')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TorqueSense.Station.psm1') -Force

$prevFile = Join-Path $Root 'previous.txt'
if (-not (Test-Path $prevFile)) { throw 'rollback-release: no previous release recorded' }
$previous = (Get-Content $prevFile -Raw).Trim()
if (-not (Test-Path (Join-Path $previous 'bin\torquesense.exe'))) { throw "rollback-release: $previous is not a release" }
Write-Host "==> rolling back to $previous"
if (-not (Switch-Release $Root $previous)) { throw 'rollback-release: the previous release did not become healthy either' }
Write-Host '==> done'
