<#
.SYNOPSIS
  Install a TorqueSense release beside the current one, prove it on this machine, then switch to it.
  If the new release is not healthy within four minutes, the previous one is put back automatically.
.EXAMPLE
  .\install-release.ps1 -Version v5.5.0
.EXAMPLE
  .\install-release.ps1 -Version v5.5.0 -ZipDir .\rel -StageOnly -SkipGates    # the release gate's use
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^v\d+\.\d+\.\d+$')][string]$Version,
    [string]$Repo = 'Gurjas2112/torquesense',
    [string]$Root = 'C:\TorqueSense',
    [string]$ZipDir,           # assets already downloaded; otherwise gh downloads them
    [switch]$StageOnly,        # install and verify, do not switch
    [switch]$SkipGates         # only with -StageOnly: the gates have not run yet
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TorqueSense.Station.psm1') -Force

function Step([string]$m) { Write-Host "==> $m" }
function Fail([string]$m) { throw "install-release: $m" }
function Invoke-Checked([string]$what, [scriptblock]$block) {
    & $block
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit $LASTEXITCODE)" }
}

if ($SkipGates -and -not $StageOnly) { Fail '-SkipGates is allowed only with -StageOnly' }
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $StageOnly -and -not $admin) { Fail 'switching releases restarts the station task: run as Administrator (or use -StageOnly)' }
$releases = Join-Path $Root 'releases'
$dest     = Join-Path $releases $Version
$cfgDir   = Join-Path $Root 'config'
$dataDir  = Join-Path $Root 'data'
foreach ($d in @($releases, $cfgDir, $dataDir, (Join-Path $Root 'venvs'))) { New-Item -ItemType Directory -Force $d | Out-Null }

# 1 - assets ------------------------------------------------------------------------------------
$zipName    = "torquesense-$Version-win64.zip"
$modelsName = "models-$Version.zip"
if (-not $ZipDir) {
    $ZipDir = Join-Path $env:TEMP "torquesense-$Version"
    New-Item -ItemType Directory -Force $ZipDir | Out-Null
    Step "downloading $Version from $Repo"
    Invoke-Checked 'gh release download' { gh release download $Version -R $Repo -D $ZipDir --clobber -p $zipName -p 'SHA256SUMS' -p 'gates.json' -p $modelsName }
}
$zip = Join-Path $ZipDir $zipName

# 2 - integrity and provenance -----------------------------------------------------------------
Step 'checking SHA-256 and build provenance'
$line = Get-Content (Join-Path $ZipDir 'SHA256SUMS') | Where-Object { $_ -like "*$zipName" } | Select-Object -First 1
if (-not $line) { Fail "SHA256SUMS has no line for $zipName" }
$want = ($line -split '\s+')[0].ToLower()
$got  = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($got -ne $want) { Fail "checksum mismatch for $zipName" }
Invoke-Checked 'gh attestation verify' { gh attestation verify $zip -R $Repo | Out-Null }

# 3 - the release gate's verdict ---------------------------------------------------------------
$gates = $null
if (-not $SkipGates) {
    $gp = Join-Path $ZipDir 'gates.json'
    if (-not (Test-Path $gp)) { Fail 'gates.json missing: this release has not been through the release gate' }
    $gates = Get-Content $gp -Raw | ConvertFrom-Json
    if (-not $gates.passed -or $gates.version -ne $Version) { Fail "gates.json does not pass $Version" }
}

# 4 - unpack beside the current release ---------------------------------------------------------
if (Test-Path $dest) { Fail "$dest already exists; remove it or install another version" }
Step "unpacking to $dest"
Expand-Archive $zip $dest
if ($gates) { Copy-Item (Join-Path $ZipDir 'gates.json') $dest }
$models = Join-Path $ZipDir $modelsName
if (Test-Path $models) { Expand-Archive $models (Join-Path $dest 'models') -Force }

# the model pack is built on the laptop, so it is checked against the manifest inside the attested zip (I12)
$manifest = Get-Content (Join-Path $dest 'models\manifest.json') -Raw | ConvertFrom-Json
foreach ($m in $manifest.PSObject.Properties) {
    $f = Join-Path $dest "models\$($m.Name)"
    if (-not (Test-Path $f)) { if ($SkipGates) { continue } else { Fail "model $($m.Name) missing" } }
    if ((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $m.Value.sha256.ToLower()) { Fail "model $($m.Name) does not match models\manifest.json" }
}

# 5 - Python, keyed by the lock file's hash: unchanged dependencies reuse the same environment -----
$lock = Join-Path $dest 'requirements.lock'
$lockHash = (Get-FileHash $lock -Algorithm SHA256).Hash.Substring(0, 12).ToLower()
$venv = Join-Path $Root "venvs\$lockHash"
if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
    Step "creating Python environment $lockHash"
    Invoke-Checked 'venv' { py -3.12 -m venv $venv }
    Invoke-Checked 'pip install' { & (Join-Path $venv 'Scripts\python.exe') -m pip install --require-hashes --no-deps -r $lock }
}
New-Item -ItemType Junction -Path (Join-Path $dest 'venv') -Target $venv | Out-Null
$py = Join-Path $dest 'venv\Scripts\python.exe'

# Florence-2 weights, fetched once at the pinned revision; the sidecar then runs with HF_HUB_OFFLINE=1
$env:HF_HOME = Join-Path $dataDir 'hf-cache'
Invoke-Checked 'Florence-2 prefetch' { & $py -m sidecar.prefetch --env-file (Join-Path $cfgDir '.env') }

# 6 - prove it on this machine, on the PATH the supervisor will give it -------------------------
$runtimePath = Get-RuntimePath $Root $dest
Step 'GPU, driver and every DLL this release needs'
Invoke-Checked 'gpu_preflight' { & $py (Join-Path $dest 'scripts\gpu_preflight.py') --bundle (Join-Path $dest 'bin\bundle.json') --path $runtimePath }

$saved = $env:Path
try {
    $env:Path = "$runtimePath;$env:SystemRoot\System32;$env:SystemRoot"
    $engine = Join-Path $dest 'bin\torquesense.exe'
    $station = Join-Path $cfgDir 'station_w14.yaml'
    Step 'every sample through the C++ engine'
    Invoke-Checked 'conformance' { & $engine --conformance (Join-Path $dest 'samples') }
    if ($gates -and ($gates.validated_providers -contains 'tensorrt')) {
        Step 'building the TensorRT engine cache (once per release and model)'
        Invoke-Checked 'TensorRT cache' { & $engine --build-trt-cache --config $station --release $dest }
    }
} finally { $env:Path = $saved }

if ($StageOnly) { Step "staged at $dest (not switched)"; return }

# 7 - switch, or roll back by itself --------------------------------------------------------------
Step "switching to $Version"
if (-not (Switch-Release $Root $dest)) { Fail "$Version did not become healthy; the previous release is running again" }
Step "$Version is live - status at http://127.0.0.1:8099/status"
