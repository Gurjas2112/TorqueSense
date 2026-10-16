# scripts\start-jenkins-agent.ps1 - the Jenkins agent on HSGPC (spec Section 28, "Jenkins - controller in
# Docker, agent on Windows"). Runs as your user, never as Administrator (spec risk register): the pipeline
# records clocks and power instead of locking them.
#
#   .\scripts\start-jenkins-agent.ps1          # controller at http://localhost:8081/, node hsgpc-gpu
#
# The agent dials out to the controller over WebSocket: nothing on the laptop listens for Jenkins. Its
# secret is in C:\dev\jenkins-agent\secret (from the node's page, or the controller's API), never in git.
param(
    [string]$Controller = 'http://localhost:8081/',
    [string]$Node = 'hsgpc-gpu',
    [string]$Java = 'C:\dev\tools\temurin21\bin\java.exe'
)
$ErrorActionPreference = 'Stop'
$work = 'C:\dev\jenkins-agent'
New-Item -ItemType Directory -Force $work | Out-Null
if (-not (Test-Path "$work\secret")) { throw "no agent secret at $work\secret" }
Invoke-WebRequest -UseBasicParsing "$($Controller)jnlpJars/agent.jar" -OutFile "$work\agent.jar"

# Build steps inherit this environment: PATH as the registry has it now (MSYS2, CUDA, cuDNN, ONNX Runtime,
# TensorRT), ORT_ROOT for the windows-release preset, HF_HOME if set.
$machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$user = [Environment]::GetEnvironmentVariable('Path', 'User')
$env:Path = "C:\msys64\ucrt64\bin;$machine;$user"
foreach ($v in 'ORT_ROOT', 'HF_HOME') {
    $x = [Environment]::GetEnvironmentVariable($v, 'User')
    if ($x) { Set-Item "env:$v" $x }
}
$p = Start-Process -FilePath $Java -WindowStyle Hidden -PassThru -WorkingDirectory $work `
    -ArgumentList '-jar', "$work\agent.jar", '-url', $Controller, '-name', $Node, '-secret', "@$work\secret",
                  '-webSocket', '-workDir', $work `
    -RedirectStandardOutput "$work\agent.out.log" -RedirectStandardError "$work\agent.err.log"
Set-Content -Path "$work\agent.pid" -Value $p.Id
Write-Host "jenkins agent $Node started, pid $($p.Id); log $work\agent.err.log"
