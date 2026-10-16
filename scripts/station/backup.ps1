# Nightly logical backup of the Supabase database - the free plan keeps none. Keeps the newest 14.
param([string]$Root = 'C:\TorqueSense', [int]$Keep = 14,
      [string]$PgDump = 'C:\msys64\ucrt64\bin\pg_dump.exe')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$dir = Join-Path $Root 'data\backups'
New-Item -ItemType Directory -Force $dir | Out-Null
$line = Get-Content (Join-Path $Root 'config\.env') | Where-Object { $_ -match '^DATABASE_URL=' } | Select-Object -First 1
if (-not $line) { throw 'DATABASE_URL not in config\.env' }
$url = $line.Substring('DATABASE_URL='.Length).Trim('"')
$out = Join-Path $dir ("torquesense-{0}.dump" -f (Get-Date -Format 'yyyyMMdd-HHmm'))
& $PgDump --format=custom --no-owner --no-privileges --schema=public --file $out $url
$ok = ($LASTEXITCODE -eq 0) -and (Test-Path $out) -and ((Get-Item $out).Length -gt 0)
@{ at = (Get-Date).ToString('o'); file = $out; ok = $ok } | ConvertTo-Json | Set-Content (Join-Path $dir 'last.json')
if (-not $ok) { throw "pg_dump failed (exit $LASTEXITCODE)" }
Get-ChildItem $dir -Filter 'torquesense-*.dump' | Sort-Object LastWriteTime -Descending |
    Select-Object -Skip $Keep | Remove-Item
