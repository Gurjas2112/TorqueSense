# training\pipeline.ps1 - the whole data-to-model chain on HSGPC, in order, each step resumable
# (spec Sections 9, 27; Phases 2-3). Run from C:\dev\torquesense; it takes hours, so start it detached:
#
#   Start-Process powershell -WindowStyle Hidden -ArgumentList '-File', 'training\pipeline.ps1'
#   Get-Content var\logs\pipeline.log -Wait
#
#   1. render 4,000 training frames (seed 20260721) and 400 held-out frames (seed 99)  synth/generate.py
#   2. train YOLO11n and the threaded_fastener classifier; models/ + manifest          training/train_local.py
#   3. render the ten scenario timelines frame by frame, then encode them              synth/render_plan.py
#
# The GPU does one thing at a time: a latency figure or a render is never taken while another job runs.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$py = Join-Path $root '.venv\Scripts\python.exe'
$bp = Join-Path $root '.venv\Scripts\blenderproc.exe'
$glb = 'cad\export\wheel_assembly.glb'
$log = Join-Path $root 'var\logs\pipeline.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
function Say([string]$m) { Add-Content -Path $log -Value ("{0:u}  {1}" -f (Get-Date), $m) }
function Step([string]$name, [scriptblock]$body, [int]$tries = 3) {
    for ($i = 1; $i -le $tries; $i++) {
        Say "$name (attempt $i)"
        & $body *>> $log
        if ($LASTEXITCODE -eq 0) { Say "$name done"; return }
        Say "$name failed with exit $LASTEXITCODE"
    }
    throw "$name failed $tries times"
}

if (-not (Test-Path $glb)) {
    Step 'FreeCAD export' { & 'C:\dev\tools\FreeCAD\bin\freecadcmd.exe' cad\build_wheel.py }
}
Step 'render training set' { & $bp run synth\generate.py --gltf $glb --seed 20260721 --frames 4000 --batch 50 }
Step 'render held-out set' { & $bp run synth\generate.py --gltf $glb --seed 99 --frames 400 --batch 50 }
$train = (Get-ChildItem data\synth -Directory | Where-Object { $_.Name -like '*-g*-s20260721' -and (Test-Path "$($_.FullName)\manifest.json") } | Select-Object -First 1).FullName
$held = (Get-ChildItem data\synth -Directory | Where-Object { $_.Name -like '*-g*-s99' -and (Test-Path "$($_.FullName)\manifest.json") } | Select-Object -First 1).FullName
Step 'train both models' { & $py training\train_local.py --train $train --heldout $held } 1
Step 'scenario render plan' { & $py synth\render_plan.py samples\wheel-hub-5lug }
Step 'render scenarios' { & $bp run synth\generate.py --gltf $glb --seed 7 --batch 50 --samples 16 --plan data\render_plans\wheel-hub-5lug.json }
Step 'encode scenario videos' { & $py synth\render_plan.py samples\wheel-hub-5lug --video }
Say 'pipeline complete'
