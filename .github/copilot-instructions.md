# TorqueSense — instructions for the Copilot coding agent

You are building **TorqueSense**, real-time assembly verification compiled from CAD (Tata Motors
PS4), exactly as `docs/SPEC.md` (spec v5.5) says. This section applies to every change.

> **Prototype edition.** `docs/SPEC.md` is spec v5.5 with a prototype-edition preface. Phase 13
> (production track, spec Section 33) is **deferred on purpose** for the prototype — see
> `docs/adr/0010-prototype-scope.md`. Do not start it until that ADR is superseded.

## Rules for every change

**Sources of truth, in order:**

1. `docs\TorqueSense_Engineering_Specification_v5.5.md` (published as `docs/SPEC.md`).
2. This file.
3. The tested starter code already in the repository.

If they conflict, or the spec is silent, **stop and ask**. Never guess a version, a port, a file
name or a threshold.

**How you work:**

- One phase at a time, in the order of the build plan at the end of this file. Write a short plan,
  then the tests, then the code. Run the phase's gate commands and paste their output into your
  report. A phase is not done without that output.
- Small pull requests: one phase or one gate each.
- Extend the starter code; never rewrite it. It was tested, and it encodes decisions you cannot see
  from the code alone.
- A new dependency, a changed threshold or a changed decision needs an ADR in `docs/adr/` in the
  same pull request.

**Hard rules — a change that breaks one is wrong, whatever else it achieves:**

- **Hot path** (`perception/`, `procedure/`, and the capture, inference, verify and alert threads):
  - No heap allocation in steady state (I1) and no mutex (I2).
  - No I/O on the verify thread. The OPC-UA write and the heartbeat belong to the alert thread.
  - Threads talk only through `SpscRingBuffer`.
- **Florence-2 is never on the alert path.** The sidecar can only be reached through the circuit
  breaker. `sunverify_procedure` must not link `cpp-httplib` (I3, I11).
- **Inference:**
  - ONNX Runtime 1.28.0 `gpu_cuda13` is the only inference API.
  - TensorRT 10.13.3.9 is reached only through ORT's TensorRT provider. Never link it, never call it
    directly, and never add an unpinned `tensorrt` pip package.
  - Only providers listed in the release's `gates.json` may run. A fallback to CUDA is reported as
    DEGRADED. Never fall back to the CPU.
- **Models are verified before load** against `models/manifest.json` (I12). Florence-2 is pinned to
  a commit.
- **Fail visible:** the heartbeat stops when work stops, and the PLC turns the station AMBER (I13).
  Never fake liveness.
- **Generic engine:** no assembly, part or family name in engine code. Everything specific lives in
  `samples/` and `profiles/`. A change to the interpreter lands together with the matching change to
  `reference/tsref` and passes the differential test (I16).
- **Toolchain:**
  - Windows C++ is built with MSYS2 UCRT64 GCC 16.2, CMake and Ninja. No MSVC-only code, no
    vcpkg, no `#pragma comment(lib)`.
  - `#ifdef _WIN32` is allowed only in `ipc/`, `control-app/src/Affinity.cpp` and the capture
    sources.
- **Warnings:** clean under `-Wall -Wextra -Wpedantic -Werror` (I5).
- **Data:**
  - Supabase. Schema changes only as new files in `supabase/migrations/`, plain Postgres
    (`date_bin`, never `time_bucket`).
  - Every write goes through `api/outbox.py`.
  - `DATABASE_URL` and the S3 keys never appear under `web/`.
- **Never commit** weights, renders, videos, `.env`, `web/.env.local`, `outbox.db` or TensorRT
  caches (I8).
- **Soft PLC:** never set `serverUrls`. Leave open62541 1.4.21 at its defaults.
- **Go** (`fleet-agent/`, which also holds the supervisor): standard library only; `gofmt`,
  `go vet`, and `go test -race` on Linux and Windows builds.
- **PowerShell** (`scripts/station/`): ASCII only, must run on Windows PowerShell 5.1, and must
  pass PSScriptAnalyzer with `scripts/station/PSScriptAnalyzerSettings.psd1`.
- **Production rules:**
  - At run time every process is started by the supervisor from an installed release, and nothing
    reads the user's `PATH`.
  - Configuration and data never live inside a release folder.
  - Releases are published only by `Jenkinsfile.release`.
- **Do not start** spec Appendix D (industrial camera, RKE2, Tier 2) or anything marked ❌ cut.

---

## 1. How to use this file

- **Where it goes:** this file is **`.github/copilot-instructions.md`**. Every Copilot
  surface reads that path: VS Code, Visual Studio, JetBrains, Eclipse, Xcode, the cloud agent on
  GitHub, and the CLI.
- **The spec:** `docs/SPEC.md`.
- **The starter kit:** unzipped into the repository root.
- **To start a phase**, assign or ask: *"Follow `.github/copilot-instructions.md`. Do Phase N. Report
  with the gate output."*
- **Commands are plain shell commands**, PowerShell on Windows and bash on Linux and CI. Nothing
  depends on IDE tasks, launch configurations or extensions.
- **No terminal?** If your Copilot surface cannot run commands, give the human the exact commands
  and wait for their output before you claim the gate.
- **Steps marked 👤** need a human: CAD authoring in FreeCAD, Colab training, photographing parts,
  NVIDIA downloads behind a login, and Windows account setup. Prepare everything around them, list
  what the human must do, and stop.

## 2. The machine and fixed versions

| Item | Value |
|---|---|
| Developer machine | HSGPC: Windows 11 Home 26H2 (build 26300.9711), i7-11800H 8C/16T, 16 GB RAM, RTX 3060 Laptop 6 GB (Ampere, cc 8.6), driver 596.21 |
| Project root | `C:\dev\torquesense` — never under `C:\Users\Gurjas Gandhi` (the space breaks tools) |
| Station root (installed releases) | `C:\TorqueSense` (`current\` junction, `releases\`, `config\`, `data\`, `venvs\`) |
| C++ | C++20 · MSYS2 UCRT64 GCC 16.2 · GDB 18.1 · CMake + Ninja · libraries from `pacman` · five pinned by `cmake/deps.cmake` (IXWebSocket v12.1.0, cpp-httplib v0.61.0, HdrHistogram_c 0.12.0, PicoSHA2 v1.0.1, OpenXLSX v0.5.1) · open62541 1.4.21 vendored in `third_party/` |
| GPU stack | CUDA 13.2 · cuDNN 9 for CUDA 13 · ONNX Runtime **1.28.0** `onnxruntime-win-x64-gpu_cuda13` (never the `-gpu-` CUDA 12 asset) · TensorRT **10.13.3.9** CUDA 13.0 zip at `C:\dev\tools\TensorRT` |
| Python | 3.12 from python.org; create every venv with `py -3.12` (MSYS2's Python and uv's CPython 3.14 also exist on this machine). Dev venv: torch from the cu130 index **first**. Release venv: `requirements.lock` with hashes |
| Go | ≥ 1.24 (module `github.com/Gurjas2112/torquesense/fleet-agent`) |
| Node | 22 · Node-RED 5.0.8 pinned in `tools/node-red/` |
| Linux CI and sanitizers | Ubuntu 24.04 · Clang · presets `linux-asan`, `linux-tsan`, `linux-headless` |
| Services | Supabase (one free project: Postgres, Storage, Realtime) · Vercel (Tier 1 showcase) |

## 3. Processes and ports (all loopback unless stated)

| Process | Port | Notes |
|---|---|---|
| `torquesense.exe` (one per station) | 8080 WebSocket · 8090 MJPEG · 8095 control | each further station adds 100 |
| `softplc.exe` | 4840 OPC-UA | binds all interfaces by open62541 default; no firewall rule |
| `sidecar` (FastAPI, Florence-2) | 8001 | offline at run time (`HF_HUB_OFFLINE=1`) |
| `api` (FastAPI, outbox) | 8000 | also serves `web/dist` through `api/spa.py` |
| Node-RED | 1880 | tag monitor at `/ui` |
| `supervisor.exe` | 8099 | `/status` `/health` `/maintenance?on=1\|0` `/shutdown` |
| `fleet-agent.exe` | none | push only |
| Jenkins (Docker, CI mode only) | 8081 | never 8080 |
| Vite dev server | 5173 | development only, never in a release |

Engine threads: capture, inference, verify and alert pinned to logical CPUs 8, 10, 12 and 14 at
`HIGH_PRIORITY_CLASS` (never realtime). io and control threads are unpinned. Ring sizes: capture →
inference 32, inference → verify 32, verify → alert 16, verify → io 64, alert → io 16.

## 4. Repository layout

Follow spec Section 22, "Repository layout", exactly. These directories exist from Phase 0
(starter kit) or are created by the phase named:

```text
contracts/      JSON Schemas → generated C++ and TS types (P1)
reference/      Python executable specification: tsref interpreter, compiler, solvers, optics (starter)
samples/        one folder per assembly + run_samples.py (starter)
cad-extract/    C++ OCCT reader, pattern solvers, ProcedureEmitter (P4)
perception/     C++ SpscRingBuffer, FramePool, YoloOnnxDetector, Tracker, crop classifiers (P4, P6)
procedure/      C++ ProcedureEngine, Verdict, IAlerter, OpcUaAlerter, narrator, XlsxReporter (P5)
ipc/            C++ SharedClipRing, Win32 + POSIX (P6)
control-app/    C++ main, Pipeline, AlertThread, Affinity, IFrameSource, ReasoningClient, HealthBuckets (P6+)
softplc/        C++ soft PLC with heartbeat watchdog; node-red/flows.json (starter + P5)
fleet-agent/    Go: agent + cmd/supervisor + internal/supervise (starter)
sidecar/        Python Florence-2 adjudicator, clip writer (P7; prefetch.py from starter)
api/            Python FastAPI, outbox.py, spa.py, fleet/ewma.py (starter + P7)
synth/          BlenderProc generation, scenarios (P2)
training/       colab_train.ipynb, eval.py (P3)
web/            React dashboard, /hmi, /harness, /line, /fleet (P8)
supabase/       migrations (starter)
scripts/        gpu_preflight.py, watchdog.py, release/, station/ (starter)
tools/          check-mingw/, node-red/ (starter)
tests/          latency/, soak/, scenarios/, fixtures/, plc/, e2e/ (P5+)
docs/           SPEC.md, VALIDATION.md, adr/
```

---

## 5. The build plan

Each phase lists what to build, which spec sections to read first, and the gate that proves it.
Run the gate commands yourself; report their real output. "Should pass" is not evidence.

### Phase 0 — Bootstrap, and prove the starter kit

**Read:** spec Sections 25–29 and Appendix B.

- [ ] Repository at `C:\dev\torquesense` with the starter kit, `docs/SPEC.md`, this file,
  `.gitignore` covering I8, `.env.example` (the spec's Section 29 layout, placeholder values only).
- [ ] `.github/pull_request_template.md` with the fields: phase, invariants touched, gate output.
- [ ] `docs/adr/0001…0009` as one-page stubs with the titles the spec lists (Section 22).

**Gate:**

```powershell
py -3.12 -m venv .venv; .\.venv\Scripts\Activate.ps1; pip install pyyaml pytest fastapi httpx
python samples\run_samples.py --template                       # PASS, 6/6 violation kinds
pytest reference\tests scripts\tests scripts\release\tests api\tests\test_spa.py sidecar\tests
cd fleet-agent; gofmt -l .; go vet ./...; go test -race ./...; cd ..
python scripts\gpu_preflight.py                                # driver 596.21 PASS; DLL lines may FAIL until Phase 1
```

The CI workflow (`.github/workflows/ci.yml`) is green on the first push.

### Phase 1 — Toolchain, contracts, and the GPU stack — gates G1 and G2

**Read:** spec Sections 26 and 28, and Section 30 ("The contract, generated").

- [ ] 👤 The human installs cuDNN 9 (CUDA 13), the ONNX Runtime 1.28.0 `gpu_cuda13` zip, and the
  TensorRT 10.13.3.9 zip, at the paths in Section 28. Give them the exact list.
- [ ] `CMakeLists.txt` with targets `sunverify_perception`, `sunverify_procedure` (no
  `cpp-httplib`), `sunverify_ipc`, `cad_extract`, `torquesense`, `softplc`, and tests.
  `CMakePresets.json` and `cmake/deps.cmake` come from the starter kit.
- [ ] `contracts/*.schema.json` (state, anomaly, adjudication, health, host_health) and
  `scripts/gen-contracts.sh`, which generates C++ structs and TS types. CI checks
  `git diff --exit-code` after generation.
- [ ] `torquesense --version` prints the version, the ORT version, and the providers it can load.

**Gate:**

```powershell
cd tools\check-mingw
g++ -std=c++20 -O2 -I"C:\dev\tools\onnxruntime\include" ort_check.cpp "C:\dev\tools\onnxruntime\lib\onnxruntime.lib" -o ort_check.exe
g++ -std=c++20 -O2 -I"C:\dev\tools\onnxruntime\include" trt_check.cpp "C:\dev\tools\onnxruntime\lib\onnxruntime.lib" -o trt_check.exe
.\ort_check.exe mul2.onnx; .\trt_check.exe mul2.onnx; cd ..\..
python scripts\gpu_preflight.py                                # every line PASS (power may WARN)
cmake --preset windows-release; cmake --build --preset windows-release
```

### Phase 2 — CAD and synthetic data · 👤 CAD authoring

**Read:** spec Sections 6, 7 and 9, and Section 34 ("The render manifest is the cache key").

- [ ] 👤 The human models the wheel assembly in FreeCAD (5 lugs, 114.3 mm PCD; plus a 4-lug
  variant) and exports STEP and glTF to `cad/export/`.
- [ ] `synth/generate.py` (BlenderProc): domain-randomised renders with automatic labels; output
  folder named by the render manifest hash; 10 scenario sequences.
- [ ] `synth/tests/test_render_reproducible.py` (I9: identical labels, PSNR ≥ 45 dB).

**Gate:** I9 test passes; `dvc status` clean; nothing in `data/` is committed to git.

### Phase 3 — Training · 👤 Colab

**Read:** spec Section 27 and Section 17 ("Is the detector accurate?").

- [ ] `training/colab_train.ipynb`: YOLO11n (hands and tools) and the `threaded_fastener`
  MobileNetV3-Small classifier on 96×96 crops; ONNX export; `models/manifest.json` with SHA-256.
- [ ] `training/eval.py`: synthetic and real mAP, reported separately (I10).

**Gate:** `docs/accuracy.json` holds both figures, and `python training\eval.py --check` passes.

### Phase 4 — Lock-free core and the CAD compiler

**Read:** spec Sections 7, 9 and 14.

- [ ] `SpscRingBuffer<T,N>` (acquire/release, cache-line padded); `FramePool` (960×540) and the
  full-resolution crop pool; `StateVector`.
- [ ] `cad-extract`: OCCT XCAF traversal, manifest join, pattern solvers (circle → star from member
  1 in numbering direction; line → centre-out alternating; field → centre-out, most-opposite),
  resolution check (16 px), occlusion with `IntCurvesFace_ShapeIntersector`; it emits
  `procedure.json` and `coverage.json`. The compiler refuses unresolved points (I17).
- [ ] Property tests for the solvers: wheel 1,3,5,2,4; 7-point line 4,5,3,6,2,7,1; 2×3 field
  2,5,3,4,6,1. Expected values come from `reference/tsref`.

**Gate:** `ctest --preset windows-release` is green; the `linux-tsan` torture test on the ring is
green; the wheel's `coverage.json` is clean.

### Phase 5 — Procedure engine, alerting, soft PLC

**Read:** spec Sections 6, 10, 18 and 19 ("Fail-visible").

- [ ] Tests first (I6): table-driven `ProcedureEngine` tests for every violation kind —
  SEQUENCE, PRECEDENCE, EVIDENCE_MISSING, REGRESSION, STEP_TIMEOUT, INCOMPLETE_AT_RELEASE.
- [ ] Generic `ProcedureEngine` loading `procedure.json`. Ordered state families, `requires`,
  deadlines (I15), evidence windows, the release gate. No assembly names.
- [ ] Differential test: every fixture under `samples/` through C++ and `reference/tsref`, with
  identical violation lists (I16). `torquesense --conformance samples` runs it.
- [ ] `IAlerter` and `OpcUaAlerter` on the alert thread. The heartbeat counter advances only when
  frames are verified.
- [ ] `softplc` from `softplc/softplc_reference.c`, with the 2 s watchdog.

**Gate:**

```powershell
ctest --preset windows-release -R "Procedure|Differential|NoAllocation"
.\build\windows-release\control-app\torquesense.exe --conformance samples    # agrees on all traces
python tests\plc\test_heartbeat.py                                            # AMBER ≤ 2.5 s after last beat
```

### Phase 6 — Pipeline and the latency gate

**Read:** spec Sections 8, 13, 14 and 30 ("ONNX Runtime — fail loudly, then warm up").

- [ ] `IFrameSource`: `FileSource` (scenarios), plus `ReplayDetector` for sanitizer builds.
- [ ] `YoloOnnxDetector`: CUDA provider options exactly as the spec; catch the append exception;
  20 warm-up runs; refuse to start if the median is over 40 ms after warm-up.
- [ ] Crop classifiers batched on full-resolution crops; debounce of 2 frames; confidence floor 0.65;
  ambiguous band 0.35–0.65.
- [ ] `Pipeline` with thread pinning and priorities; HdrHistogram; `SharedClipRing`
  (Win32 + POSIX, per-slot seqlocks).
- [ ] `tests/latency/latency_test` with `--provider`, `--histogram` and `--summary-json`.

**Gate:** with clocks locked and on AC power,
`latency_test --scenario data\scenarios\fault_interrupt_resume.mp4 --runs 100 --budget-evidence-p99-ms 150`
passes (I4). `NoAllocationTest` passes over 10,000 frames (I1). Paste the p50/p99/p99.9.

### Phase 7 — Sidecar, API, outbox, Supabase

**Read:** spec Sections 16 and 30 ("The sidecar", "The engine's sidecar client").

- [ ] `sidecar/` Florence-2 adjudicator: native `Florence2ForConditionalGeneration`, pinned
  `FLORENCE_REVISION`, torch memory fraction capped, clip writer reading the clip ring.
- [ ] Engine `ReasoningClient`: circuit breaker (3 failures → open 60 s), queue depth 16, timeout
  1500 ms. It is never on the alert path (I3, I11).
- [ ] `api/main.py`: event and health ingest (`X-API-Key`), `/health`, EWMA job; `api/outbox.py`
  for every write; `mount_spa(app, TS_WEB_DIST)` registered last.
- [ ] Apply `supabase/migrations/` to the project. RLS is on, and the publishable key only reads
  the showcase.

**Gate:** an anomaly row and its clip arrive in Supabase end to end. With the network pulled
mid-cycle, alerts continue and the outbox drains in order afterwards. `pytest api\tests` passes.

### Phase 8 — Dashboard, HMI, harness, Node-RED

**Read:** spec Section 21 in full.

- [ ] React app: the four-zone dashboard, `/hmi/:station` (accepts no input), `/harness`, `/line`
  and `/fleet`. Types come from `contracts/`. A replay mode for Vercel.
- [ ] Node-RED flow in `softplc/node-red/flows.json` with the three lamps and the hold indicator.
- [ ] Playwright e2e for the HMI states (GREEN, RED, AMBER, offline).

**Gate:** `npx vitest run` and `npx playwright test` are green. The demo runbook passes once by
hand with `scripts/start-dev.ps1`.

### Phase 9 — Operations track

**Read:** spec Section 19 in full, and Section 31 ("The GPU pipeline — Jenkins on the laptop").

- [ ] Engine health buckets: the six signals plus camera temperature and resends. EWMA drift with
  λ = 0.05, L = 4.2, a 288-bucket median/MAD baseline; `api/fleet/ewma.py` is already tested — do
  not change its parameters without an ADR and the simulation re-run.
- [ ] Fleet agent from the starter kit, wired to the real `/health`.
- [ ] Jenkins controller (Docker, profile `ci`, port 8081) and Windows agent; `Jenkinsfile` from
  the starter kit.

**Gate:** 7 EWMA tests pass; a drifting seeded station is visible on `/fleet`; one green Jenkins
build archives the latency histogram.

### Phase 10 — Profiles track

**Read:** spec Sections 11 and 12.

- [ ] The door-trim profile end to end; windshield and enclosure at procedure level only.
  Assemblies are folders, never code.

**Gate:** `python samples\run_samples.py --template` passes, and the C++ engine agrees on every
trace.

### Phase 11 — Desk track: laptop webcam and phone

**Read:** spec Section 8 in full.

- [ ] `UvcSource` (DirectShow, 1280×720 MJPG) and `StreamSource` (phone MJPEG over its hotspot).
  Ask for a resolution and read it back. Settings come from `cameras.yaml`.
- [ ] `scripts/calibrate.py` (ChArUco, ≤ 0.5 px RMS) and `scripts/teach.py`.

**Gate:** both cameras calibrated; each recorded trace raises exactly its violations.

### Phase 12 — TensorRT track

**Read:** spec Section 30 ("TensorRT — the fast path, adopted only through a gate").

- [ ] Provider selection: `execution_providers: [tensorrt, cuda]` from the station config,
  filtered by the release's `gates.json`. TensorRT options exactly as the spec (FP16, 256 MB
  workspace, engine and timing cache under `cache/trt/<TensorRT version>/<model hash>/`).
- [ ] `torquesense --build-trt-cache`. With no cache, the engine refuses to start: an engine is
  never built during a demo.
- [ ] `tests/ProviderParity` (`provider_parity.exe`): IoU ≥ 0.9, confidence within 0.05, and
  identical scenario events. It writes `--json`.
- [ ] `/health` reports the provider in use and `degraded` when it fell back.

**Gate:** `latency_test` once per provider, and parity passes. `gates.py` decides which providers
are validated. Record the outcome in `VALIDATION.md` either way.

### Phase 13 — Production track — ⏸ deferred on purpose for the prototype (ADR 0010)

**Read:** spec Section 33 in full. This is the last phase. It changes how the software runs, never
what it decides.

- [ ] **Engine features the release tooling calls:**
  - `--release <dir>`: read `gates.json` and `release.json`, and report the version on `/health`.
  - `POST /shutdown` on the control port: an orderly stop.
  - `--conformance <dir>`, `--build-trt-cache`, and `-DTORQUESENSE_VERSION` baked in at build time.
- [ ] `config/supervisor.template.json` matches the processes; `api/main.py` serves `web/dist`.
- [ ] The supervisor, `scripts/station/*`, `scripts/release/*`, `release.yml`, `watchdog.yml` and
  `Jenkinsfile.release` exist in the starter kit. Wire them up; do not rewrite them.
- [ ] `requirements.lock` from `requirements-runtime.in`, using the `uv pip compile` command in its
  header, run on HSGPC.
- [ ] 👤 The human creates the `tsdemo` account, runs `register-station.ps1` and Autologon, and
  sets the repository variables `SUPABASE_URL`, `SUPABASE_PUBLISHABLE_KEY` and `TS_EXPECT_ONLINE`.

**Gate — the release checklist in spec Section 33:**

1. A tag produces a draft release with its attestation.
2. `Jenkinsfile.release` publishes it with a passing `gates.json`.
3. `install-release.ps1` switches to it, and `rollback-release.ps1` works.
4. Ending `torquesense.exe` in Task Manager turns the HMI AMBER, and it returns to green on its own.
5. Power-cut test: off at the button, on again, green with no key pressed.
6. The watchdog workflow is green.
7. A backup has been restored once.

---

## 6. Reporting — every pull request and every phase

```text
Phase: N — <name>
Spec sections read: …
Changed: <files>
Invariants touched: I…  — how each still holds
Tests added: …
Gate output (pasted, not summarised):
  <command>
  <output>
Not done / needs the human (👤): …
Questions (if any part of the spec was unclear): …
```

## 7. Never

- Never weaken a test, a threshold or a gate to make it pass. Report the failure instead.
- Never add a mutex, an allocation or blocking I/O to the hot path "temporarily".
- Never call Florence-2, HTTP or the database from the verify or alert thread.
- Never switch the compiler, the ONNX Runtime asset, the TensorRT major version, or the Python
  version.
- Never start production-deferred work (Appendix D), even if asked mid-phase. Point to the spec
  and ask.
- Never commit secrets, models, renders, videos or TensorRT caches.
- Never claim a measured number (latency, mAP, VRAM) that `docs/VALIDATION.md` does not contain.
