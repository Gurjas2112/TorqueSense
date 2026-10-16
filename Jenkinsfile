// Jenkinsfile: the GPU gates, on HSGPC.
// GitHub Actions runs everything that needs no GPU. These stages need the RTX 3060, so
// they run on a Jenkins agent on the laptop. The controller polls GitHub; nothing on the
// laptop is exposed to the internet, and builds run only from this repository's main
// branch, never from fork pull requests.
pipeline {
  agent { label 'hsgpc-gpu' }
  options {
    timeout(time: 2, unit: 'HOURS')
    disableConcurrentBuilds()                 // one GPU: concurrent builds would corrupt latency figures
    buildDiscarder(logRotator(numToKeepStr: '30'))
    timestamps()
  }
  triggers {
    pollSCM('H/15 * * * *')                   // laptop pulls; no inbound webhook
    cron('H 2 * * *')                         // nightly run adds the four-station soak
  }
  environment {
    MSYS2  = 'C:\\msys64\\ucrt64\\bin'           // GCC, CMake, Ninja, OpenCV and their DLLs
    PY     = 'C:\\dev\\torquesense\\.venv\\Scripts\\python.exe'
    API_KEY = credentials('torquesense-api-key')
  }
  stages {
    stage('Conditions') {
      steps {
        // On battery the GPU clocks down and p99 roughly doubles. Record the conditions,
        // and mark the build UNSTABLE rather than trusting numbers taken on battery.
        catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
          powershell '''
            $b  = Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue | Select-Object -First 1
            $ac = if ($b) { [bool]$b.PowerOnline } else { $true }
            $g  = nvidia-smi --query-gpu=clocks.sm,clocks.max.sm,temperature.gpu,driver_version --format=csv,noheader
            @{ ac_power = $ac; gpu = "$g"; at = (Get-Date).ToString("o") } | ConvertTo-Json | Set-Content conditions.json
            if (-not $ac) { Write-Error "On battery: latency figures from this run are not comparable"; exit 3 }
          '''
        }
      }
    }
    stage('Data') {
      steps { bat '"%PY%" -m dvc pull' }      // scenarios, real validation set, model: from the local DVC store
    }
    stage('Build') {
      steps { bat 'set "PATH=%MSYS2%;%PATH%" && cmake --preset windows-release && cmake --build --preset windows-release' }
    }
    stage('Unit and scenarios') {
      steps { bat 'set "PATH=%MSYS2%;%PATH%" && ctest --preset windows-release --output-junit "%WORKSPACE%\\ctest.xml"' }
      post { always { junit testResults: 'ctest.xml' } }
    }
    stage('Samples, both engines') {
      // every sample through the reference interpreter, then every trace through the C++ engine (I16)
      steps {
        bat '"%PY%" samples\\run_samples.py --template'
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\control-app\\torquesense.exe --conformance samples'
      }
    }
    stage('Latency gate') {
      steps {
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\tests\\latency_test.exe --scenario data\\scenarios\\fault_interrupt_resume.mp4 --runs 100 --budget-evidence-p99-ms 150 --histogram docs\\latency.hgrm'
      }
    }
    stage('Accuracy report') {
      steps {
        bat '"%PY%" training\\eval.py --model models\\yolo11n_wheel.onnx --synthetic data\\synth\\heldout --real data\\real_val --out docs\\accuracy.json'
      }
    }
    stage('Soak, four stations') {
      when { triggeredBy 'TimerTrigger' }
      steps {
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\tests\\soak_test.exe --minutes 10 --stations 4 --vram-limit-gb 5 --min-fps 14'
      }
    }
  }
  post {
    always {
      archiveArtifacts artifacts: 'conditions.json, docs/latency.hgrm, docs/accuracy.json', allowEmptyArchive: true
    }
  }
}
