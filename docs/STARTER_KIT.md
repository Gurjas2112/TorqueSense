# TorqueSense: starter code (spec v5.5, production-ready prototype)

The tested pieces the v5.5 specification refers to. Copy them into `C:\dev\torquesense` at the
paths shown. It is not the whole engine: the C++ engine is built from the spec, and these are the
parts that had to be proven first.

| Path | What it is | How it was checked |
|---|---|---|
| `samples/` | **One folder per assembly**: wheel (PS4 reference), door trim, windshield, electronics enclosure, and two desk samples for your phone and laptop webcam; `_template/` to copy; `_print/` printable marker sheets and ChArUco board; `run_samples.py`, the generic runner | `python samples\run_samples.py --template`: 7 folders, 20 traces, all six violation kinds, PASS. Printed sheets detected back at scale |
| `reference/` | Executable specification: procedure interpreter, profile compiler, pattern solvers, optics, camera arithmetic (C-mount and webcam/phone) | `pytest reference/tests`: 42 tests, including the samples contract proven to refuse broken samples |
| `tools/check-mingw/` | **Run this first.** `ort_check.cpp` builds the engine's ONNX Runtime calls with your `g++`; `trt_check.cpp` the TensorRT provider's calls, TensorRT first and CUDA behind it; `platform_check.cpp` its Win32 calls | Built with MinGW-w64 GCC against ORT 1.28.0 `gpu_cuda13`. `ort_check` ran a model correctly; `trt_check`, run with no CUDA present, failed loudly and named the missing DLL |
| `fleet-agent/cmd/supervisor`, `internal/supervise` | **The station supervisor**: dependency order, readiness, restart with backoff, crash-loop flag, Windows job object (no orphans), explicit `PATH`, `env_file`, rotating logs, `/status` `/health` `/maintenance` `/shutdown` | 10 Go tests with real child processes, `-race`; the Windows build passes under Wine; `kill -9` of the supervisor left no child on Linux or Wine |
| `config/supervisor.template.json` | The station's processes: soft PLC, API, sidecar, Node-RED, engine, fleet agent | `supervisor -check` resolves it once registered and refuses it before |
| `scripts/station/` | `register-station.ps1` (folders, ACLs, DLL discovery, power plan, three scheduled tasks), `install-release.ps1` (verify, prove, switch, auto-rollback), `rollback-release.ps1`, `kiosk.ps1`, `backup.ps1` | Parsed; PSScriptAnalyzer with Windows PowerShell 5.1 syntax compatibility: 0 findings; ASCII only. Not run on Windows here |
| `scripts/release/bundle.py` | Stages a release from PE import tables; fails on any DLL it cannot place | 4 tests on synthetic PE files; run on ONNX Runtime 1.28.0's real DLLs |
| `scripts/release/gates.py`, `Jenkinsfile.release` | The release gate's decision rules, and the laptop job that runs them and publishes | 5 tests; `npm-groovy-lint`: no errors |
| `scripts/watchdog.py`, `.github/workflows/watchdog.yml` | Hourly keep-alive for Supabase; fails (GitHub emails you) on stale health data | 2 tests, one against a local HTTP server; `actionlint` clean |
| `.github/workflows/release.yml` | Tag → MSYS2 build → bundle → attestation → draft release | `actionlint` clean; first real run is your first tag |
| `api/spa.py` | Serves the built dashboard from the API, with deep-link fallback and no path escape | 5 tests with FastAPI's TestClient |
| `sidecar/prefetch.py` | Fetches Florence-2 at its pinned commit at install, so the sidecar runs offline | 5 tests |
| `tools/node-red/` | Node-RED 5.0.8 and its two nodes, pinned with a lockfile, run natively | `package-lock.json` resolved (864 packages) |
| `requirements-runtime.in` | What a release's Python needs; the header has the `uv pip compile --generate-hashes` command | the command checked for the Windows target without torch (the PyTorch index was not reachable here) |
| `scripts/gpu_preflight.py` | Driver ≥ R580, compute capability ≥ 7.5, VRAM, power-sensor sanity, and every DLL the engine loads — MSVC runtime, CUDA 13, cuDNN 9, ONNX Runtime with its TensorRT provider, TensorRT 10 | 5 pytest tests, one on HSGPC's own `nvidia-smi` row (driver 596.21, and its 752 W reading) |
| `cmake/deps.cmake`, `CMakePresets.json` | The five pinned FetchContent libraries, vendored open62541, ONNX Runtime as an imported target; Windows presets for MSYS2 GCC, Linux presets for the sanitizers | Configured and built with MinGW-w64 (cross) and GCC on Linux |
| `third_party/open62541/` | open62541 1.4.21 amalgamation for `win32` and `posix` | Soft PLC built with MinGW passed the heartbeat test: AMBER 1.86 s after the last write |
| `softplc/softplc_reference.c`, `tests/plc/test_heartbeat.py` | Soft PLC with the fail-visible watchdog, and its test | as above |
| `fleet-agent/` | Go fleet agent, standard library only | `gofmt`, `go vet`, `go test -race`; Windows and Linux cross-builds |
| `supabase/migrations/` | The schema for Supabase: plain Postgres (no `timescaledb`), RLS on every table, read-only showcase policies, Realtime publication, the `clips` bucket, `pg_cron` retention | Applied to a real Postgres with Supabase's roles, publication and Storage table emulated |
| `api/outbox.py` | Store-and-forward to Supabase: in order, idempotent, dead letters; clip pruning through Storage's S3 API | 9 tests — 3 of them against Postgres when `TS_TEST_PG` is set |
| `api/fleet/ewma.py`, `api/tests/` | One-sided EWMA drift charts | 7 pytest tests |
| `.github/workflows/ci.yml` | CI: Linux sanitizers (apt), **Windows with MSYS2 UCRT64 GCC**, Go, Python, web, contracts, **samples** | `actionlint` clean |
| `.github/workflows/deferred-production.yml`, `deploy/k8s/`, `docker/` | Production — deferred on purpose (spec Appendix D). The kind smoke test runs only when started by hand | `actionlint`; `kustomize build` + `kubeconform -strict`, Kubernetes 1.36. Images not built here |
| `Jenkinsfile` | GPU gates on the laptop, with the MSYS2 toolchain on `PATH` and a samples stage | `npm-groovy-lint`: no errors |

## First ten minutes on HSGPC

```powershell
# 1. the samples — needs only Python
py -3.12 -m venv .venv; .\.venv\Scripts\Activate.ps1; pip install pyyaml pytest
python samples\run_samples.py --template
pytest reference\tests

# 2. the outbox tests (the database half runs when TS_TEST_PG points at a Postgres)
cd api; pip install -r requirements.txt pytest; python -m pytest; cd ..

# 3. your compiler against ONNX Runtime (after spec Section 28, step 3)
cd tools\check-mingw
g++ -std=c++20 -O2 -I"C:\dev\tools\onnxruntime\include" ort_check.cpp "C:\dev\tools\onnxruntime\lib\onnxruntime.lib" -o ort_check.exe
$env:Path = "C:\dev\tools\onnxruntime\lib;$env:Path"; .\ort_check.exe mul2.onnx
g++ -std=c++20 -O2 platform_check.cpp -o platform_check.exe -lwinmm; .\platform_check.exe

# 4. TensorRT, after spec Section 28 step 3 (TensorRT 10.13.3.9 zip on PATH)
g++ -std=c++20 -O2 -I"C:\dev\tools\onnxruntime\include" trt_check.cpp "C:\dev\tools\onnxruntime\lib\onnxruntime.lib" -o trt_check.exe
.\trt_check.exe mul2.onnx
cd ..\..; python scripts\gpu_preflight.py
```

`deploy/k8s/base/secrets.env.example` holds CI placeholder values; copy it to `secrets.env`
(gitignored) before `kustomize build`. To print the desk sheets again: `python samples\_print\make_prints.py`
(needs `opencv-python` and `pillow`), and print at 100 %.

## Making this laptop a station (spec Section 33)

```powershell
# once, as Administrator: a standard account 'tsdemo' must exist first
scripts\station\register-station.ps1 -User tsdemo -Template config\supervisor.template.json
# then Sysinternals Autologon for tsdemo, and for every release:
scripts\station\install-release.ps1 -Version v5.5.0
scripts\station\rollback-release.ps1          # if you need the previous one back
```

