# TorqueSense

Real-time assembly verification for the production line (Tata Motors PS4). A camera watches the station.
A procedure compiled from the CAD model says what must happen and in which order. A wrong or missing step
stops the station with a physical alert through the PLC, well inside the one-second requirement.

Any assembly is a folder of data: a profile, its CAD and its traces. The engine binary never names a
product. The same executable verifies a five-lug wheel, a door-trim harness, a windshield gasket and an
electronics enclosure.

![Operator HMI: the hero fault caught](app_screenshots_screen_recording/06_hmi_red_stop_bolt_2_corrective_order.png)

| | |
|---|---|
| **Demo walk-through** | [app_screenshots_screen_recording/](app_screenshots_screen_recording/README.md): 16 screenshots in flow order and `torquesense_demo_full.mp4` (116 s) |
| **Public replay** | https://dist-replay-one.vercel.app: a recorded engine session, with live anomalies read from Supabase through the read-only key |
| **Specification** | [docs/SPEC.md](docs/SPEC.md): prototype preface plus the full v5.5 engineering specification |
| **Measured results** | [docs/VALIDATION.md](docs/VALIDATION.md): every gate's real output. A number not in that file is not claimed |
| **Decisions** | [docs/adr/](docs/adr/): 13 architecture decision records |

> **Prototype edition** (spec v5.5-proto). The production track (installed releases, supervisor,
> Autologon, power-cut test) is deferred on purpose; see [ADR 0010](docs/adr/0010-prototype-scope.md).

---

## Contents

1. [The problem and the idea](#1-the-problem-and-the-idea)
2. [Architecture](#2-architecture)
3. [Is it a microservice architecture?](#3-is-it-a-microservice-architecture)
4. [How a verdict is made](#4-how-a-verdict-is-made)
5. [Status](#5-status)
6. [Quick start](#6-quick-start)
7. [Installation in full](#7-installation-in-full)
8. [Running](#8-running)
9. [From CAD to a trained model](#9-from-cad-to-a-trained-model)
10. [Testing and CI](#10-testing-and-ci)
11. [Drills](#11-drills)
12. [Cloud: Supabase and Vercel](#12-cloud-supabase-and-vercel)
13. [Configuration](#13-configuration)
14. [Repository map](#14-repository-map)
15. [Invariants](#15-invariants)
16. [Limits, honestly stated](#16-limits-honestly-stated)
17. [Security](#17-security)

---

## 1. The problem and the idea

On an assembly line, the costly mistakes are the ones that look right: a bolt torqued out of the star
order, a clip left open under a panel, a step skipped and then hidden by the next part. Inspecting the
finished product is too late, and a human watching every cycle is unreliable.

TorqueSense checks the **procedure**, not the product:

- **Compiled, not written.** Engineering already has the assembly in CAD. `cad-extract` reads the STEP
  file and finds every fastener and clip by its part number. It then derives the order the procedure
  requires (for example the star order **1 → 3 → 5 → 2 → 4** from five bolt positions) and checks each
  point is visible to the camera at a resolvable size.
- **A closed question per point.** The vision model never asks "what is this?". At each inspection
  point it asks "which of these two or three known states is it in?" (absent / seated / fastened, open /
  closed). A classifier for one part family serves every assembly that uses it.
- **A deterministic engine decides.** A state machine, differentially tested against a Python
  reference interpreter, turns committed states into verdicts. Six violation kinds are covered:
  `SEQUENCE`, `PRECEDENCE`, `EVIDENCE_MISSING`, `REGRESSION`, `STEP_TIMEOUT`, `INCOMPLETE_AT_RELEASE`.
- **The PLC acts.** A violation is an OPC-UA write: Andon red and a conveyor-hold *request*. The PLC
  also runs a heartbeat watchdog, so a dead engine turns the station amber on its own.

## 2. Architecture

```
                         ┌──────────────────────── one process per station (C++20) ───────────────────────┐
 camera / video ──►  capture ──SPSC──► inference ──SPSC──► verify ──SPSC──► alert ──OPC-UA──► soft PLC ──► Andon / hold
                     │  (pinned threads, lock-free rings, zero allocation)    │                  ▲ heartbeat watchdog
                     └─► shared clip ring ─────────────────────► sidecar      └─► io ──HTTP──► API ──outbox──► Supabase
                         (seqlocked shared memory)               (Florence-2,                   (FastAPI)      (Postgres, Storage)
                                                                  clip cutting)                     │                │
                                                                                     web (React) ◄──┘                └──► Vercel replay
```

### The services

| Service | Code | Language | Port(s) | Owns | Talks to |
|---|---|---|---|---|---|
| **engine** | `control-app/`, `perception/`, `procedure/`, `ipc/` | C++20 | WebSocket 8080 · MJPEG 8090 · control/health 8095 (each further station adds 100) | the verdict, the clip ring | soft PLC (OPC-UA), API (HTTP), sidecar (HTTP, off the alert path) |
| **soft PLC** | `softplc/` | C, open62541 | OPC-UA 4840 | Andon, hold request, heartbeat watchdog | engine, Node-RED |
| **sidecar** | `sidecar/` | Python, FastAPI | 8001 | Florence-2 adjudication, 6 s clips | engine (clip ring), API |
| **API** | `api/` | Python, FastAPI | 8000 | outbox (SQLite), history, EWMA fleet drift; serves the web build | Supabase (Postgres + S3), engine, agents |
| **fleet agent** | `fleet-agent/` | Go | — | host health, its own spool | API |
| **Node-RED** | `softplc/node-red/` | Node | 1880 | the operator's tag view | soft PLC (OPC-UA) |
| **web** | `web/` | React 19, Vite | served by the API; public replay on Vercel | nothing (static) | API, engine |
| **Jenkins** (CI only) | `docker/jenkins/`, `Jenkinsfile` | — | 8081 (localhost) | the GPU gates | GitHub (polls `main`) |

The services agree on JSON Schemas in [`contracts/`](contracts/) (anomaly, state, health, host health,
adjudication). [`scripts/gen-contracts.sh`](scripts/gen-contracts.sh) generates the C++ and
TypeScript types from them, and CI fails if a generated file drifts.

### Inside the engine

| Thread | Does | Never does |
|---|---|---|
| **capture** | paces frames, reads the source into a fixed frame pool, writes JPEGs to the shared clip ring | block on anything downstream |
| **inference** | runs the detector: `replay` (recorded output), `schematic` (pixels of schematic media), or `cuda` / `tensorrt` (YOLO11n + family classifier on ONNX Runtime) | allocate in steady state |
| **verify** | debounces (2 frames, 0.65 confidence), runs the procedure engine, emits violations | I/O, locks, allocation, network |
| **alert** | OPC-UA writes, the heartbeat, plant-signal polling | anything slow |
| **io** (unpinned) | WebSocket, MJPEG, `/health`, events to the API, asks to the sidecar | sit on the alert path |

## 3. Is it a microservice architecture?

**Short answer: it already is, at the right boundaries. The one part that must not be split, the alert
path inside the engine, deliberately stays a single process.** The full reasoning is in
[ADR 0013](docs/adr/0013-service-boundaries.md).

**What is already a service.** Seven services ship and deploy independently (table above). Each has its
own:

- process, build and tests
- health endpoint and failure behaviour
- data: the API owns the outbox, the PLC owns the Andon state, the sidecar owns the clips

They share data only through versioned contracts. The engine, API and fleet agent have Dockerfiles and
Kustomize manifests ([`deploy/k8s/`](deploy/k8s/)) with `edge` and `kind` overlays. A station is scaled
by starting another engine process.

**What is not split, and why.** Capture → inference → verify → alert run as four pinned threads in one
process, joined by lock-free rings. Turning those into networked microservices would break the system's
core guarantees:

| If the alert path were split into services | Consequence |
|---|---|
| A network hop per stage, per frame, at 30 fps | spends the latency the 150 ms evidence-p99 budget gives to inference (I4) |
| Serialising each frame's state to JSON or protobuf | allocation on the hot path (I1) |
| Every stage reachable over HTTP | "Florence-2 is never on the alert path" becomes a convention, not a link-time guarantee (I11) |
| Five processes instead of one | partial failures (inference up, verify down) that one heartbeat cannot describe (I13) |
| Independent scaling | nothing to gain: every stage runs once per frame of one camera |

**When a new service would be right.** A stage that needs different hardware from the engine, such as
a shared inference server for many cameras, or one with a different release cadence. It would get a
schema in `contracts/`, a health endpoint and a place in the network-pull drill.

## 4. How a verdict is made

1. **Profile.** `samples/<assembly>/profile.yaml` names the groups (part number, state family, feature
   size), the steps (`complete` or `sequence`), deadlines and plant evidence, such as the nutrunner's
   `ResultOK` within 1.5 s.
2. **Compile.** `cad-extract` joins the profile to `assembly.step` by part number. It derives orders
   from geometry, projects every point through the camera model, checks occlusion by ray-casting the
   CAD, and refuses to compile if any point is unresolvable. It writes `procedure.json` and
   `coverage.json`.
3. **Perceive.** For each frame the detector fills a fixed-size `StateVector`: per inspection point a
   rank, a confidence and a visible/occluded flag, plus the number of hands and tools in view.
4. **Commit.** A state is committed after two consecutive agreeing frames above the 0.65 floor.
   Ambiguous frames hold the last state and ask Florence-2, which only informs and never decides.
5. **Decide.** The procedure engine applies the steps and deadlines and emits typed violations, with
   the frame the evidence first appeared.
6. **Act.** The alert thread writes Andon red and the hold request over OPC-UA and records engine and
   evidence latency. The io side narrates the violation, sends the event through the outbox, and the
   sidecar cuts a six-second clip.

## 5. Status

| Area | State | Evidence |
|---|---|---|
| Procedure engine, 6 violation kinds, release gate | ✅ Done | 22 conformance traces PASS; 0 differential disagreements vs `reference/` |
| Hot-path invariants | ✅ Done | 0 allocations over 10,000 frames; no mutex; Florence-2 off the alert path (link-time check) |
| CAD from FreeCAD (Phase 2) | ✅ Done | `cad/build_wheel.py` → STEP → `cad-extract`: 5 bolts at 114.3 mm PCD to 0.01 mm; order 1,3,5,2,4 derived; 4-lug revision 1,3,2,4 |
| GPU stack, gate G2 | ✅ Done | cuDNN 9.27 (CUDA 13), ONNX Runtime 1.28.0 gpu_cuda13, TensorRT 10.13.3.9; CUDA and TensorRT providers load from GCC; `trtexec` stock YOLO11n FP16 median 1.95 ms |
| GPU detector in the engine | ✅ Built | `OnnxDetector`: YOLO11n + per-point classifier, TensorRT → CUDA, model SHA-256 checked (I12); 29.8 fps end to end with stand-in models |
| Rendered training data, I9 | ✅ Done / ⏳ rendering | BlenderProc renders from the FreeCAD glTF; I9 reproducibility passes on real renders; the 4,000 + 400 frame sets are rendering on the RTX 3060 |
| Trained models (Phase 3) | ⏳ In progress | `training/train_local.py` on the RTX 3060 (Colab not required); results go to `docs/VALIDATION.md` |
| **I4 latency on the GPU detector** | ⏳ Not yet measured | runs once the models and rendered scenario videos exist. Replay-provider figures (evidence p50 5.6 ms / p99 36.7 ms) are **not** I4 |
| **I10 accuracy (synthetic + real)** | ⏳ Synthetic pending; real 👤 | the real half needs labelled photos of a real hub |
| Soft PLC + heartbeat watchdog, Node-RED | ✅ Done | station amber 2.37 s after the last beat |
| API / outbox / Supabase (RLS, Storage clips), sidecar with Florence-2 on GPU | ✅ Done | anomaly row and H.264 clip land end to end |
| Network-pull drill | ✅ PASS | 45 s outage: 14 alerts and 1,356 frames verified, outbox peak 18, drained in 16 s, 14/14 anomalies in Supabase in order |
| Web app and Vercel replay (live mode) | ✅ Done | vitest 6, Playwright 5; Supabase env vars set on Vercel |
| Jenkins for the GPU gates (Phase 9) | ✅ Running, first build pending | controller as code in Docker, agent `hsgpc-gpu` connected over WebSocket |
| Samples: 8 assemblies, schematic media read from pixels | ✅ Done | 22 videos, 198 keyframes verified frame by frame |
| Real camera recordings: earbuds into a charging case | ✅ Procedure level | [samples/earbud-case-charge](samples/earbud-case-charge/README.md): golden + 3 faulty runs; both engines raise the omission the moment the lid closes over an empty slot |
| Camera calibration and desk recordings (Phase 11) | 👤 Human step | needs printed ChArUco sheets, a webcam/phone and the physical desk kits |
| Production track (Phase 13) | ⏸ Deferred on purpose | [ADR 0010](docs/adr/0010-prototype-scope.md) |

**Tests today:** ctest 84/84 (windows-ci) · pytest across `api`, `sidecar`, `reference`, `scripts`,
`synth` · vitest 6 · Playwright 5 · GitHub Actions 10 jobs green.

## 6. Quick start

No GPU is needed to see the system work. The `replay` provider replays recorded detector output through
the real engine, PLC and API.

```powershell
git clone https://github.com/Gurjas2112/TorqueSense.git C:\dev\torquesense
cd C:\dev\torquesense
cmake --preset windows-ci; cmake --build --preset windows-ci        # MSYS2 UCRT64 GCC (section 7)
py -3.12 -m venv .venv; .\.venv\Scripts\Activate.ps1
pip install -r api\requirements.txt -r sidecar\requirements-ci.txt pyyaml pytest
cd web; npm ci; npm run build; cd ..
copy .env.example .env                                            # fill in your Supabase project
.\scripts\start-dev.ps1 -Door -NoFlorence
start http://127.0.0.1:8000/                                       # dashboard; /hmi/W-14, /harness, /line, /fleet
.\scripts\stop-dev.ps1
```

Verify a sample frame by frame, with no services at all:

```powershell
.\build\windows-ci\control-app\torquesense.exe --sample samples\door-trim-harness --verify-media all
.\build\windows-ci\control-app\torquesense.exe --conformance samples --template
```

## 7. Installation in full

Target machine: Windows 11, RTX 3060 Laptop (6 GB, SM 8.6), driver R580 or later. Everything below
installs without admin rights except where noted.

| Component | Version | Where | Notes |
|---|---|---|---|
| MSYS2 UCRT64 toolchain | GCC 16.2, CMake 4.x, Ninja | `C:\msys64\ucrt64\bin` first on PATH | `pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,opencv,opencascade,spdlog,nlohmann-json,gtest,benchmark,yaml-cpp}` |
| Python | 3.12 | `.venv` | `api`, `sidecar`, `reference`, training, BlenderProc |
| Node | 22.12+ | | `web/`, Node-RED (`tools/node-red`) |
| Go | 1.24+ | | `fleet-agent/` |
| CUDA toolkit | 13.x | system | |
| cuDNN | 9.x for **CUDA 13** | `C:\dev\tools\cudnn` | NVIDIA's redistributable archive |
| ONNX Runtime | 1.28.0 `onnxruntime-win-x64-gpu_cuda13` | `C:\dev\tools\onnxruntime` | **not** the `-gpu-` (CUDA 12) asset; set user variable `ORT_ROOT` |
| TensorRT | 10.13.3.9, CUDA 13.0 zip | `C:\dev\tools\TensorRT` | not `pip install tensorrt-cu13` (that is TensorRT 11) |
| FreeCAD | 1.1.x portable | `C:\dev\tools\FreeCAD` | CAD from script |
| BlenderProc | 2.8 (`pip`) | downloads its own Blender 4.2 | renders |
| Docker Desktop | | | Jenkins only |
| Temurin JRE | 21 | `C:\dev\tools\temurin21` | Jenkins agent |

PATH (user scope) gains `C:\dev\tools\onnxruntime\lib`, `C:\dev\tools\cudnn\bin\x64`,
`C:\dev\tools\TensorRT\lib` and `C:\dev\tools\TensorRT\bin`. Check the whole GPU stack with:

```powershell
python scripts\gpu_preflight.py          # one PASS/WARN/FAIL line per check; non-zero on any FAIL
```

> **Windows trap.** System32 ships its own `onnxruntime.dll` (1.17), and Windows loads it ahead of PATH.
> The build copies ONNX Runtime's DLLs beside every executable that loads them. If you see *"requested
> API version [28] is not available"*, an executable is picking up the System32 copy.

### Build presets

| Preset | GPU | Use |
|---|---|---|
| `windows-ci` | off | everything except the GPU detector; what GitHub Actions builds |
| `windows-release` | on (`SUNVERIFY_WITH_CUDA`, needs `ORT_ROOT`) | the station with `provider: cuda` or `tensorrt`; Jenkins |
| `windows-debug` | off | debugging |
| `linux-asan`, `linux-tsan`, `linux-headless` | off | sanitizers, containers |

```powershell
cmake --preset windows-release; cmake --build --preset windows-release
.\build\windows-release\control-app\torquesense.exe --version     # lists the providers that actually load
```

## 8. Running

| Command | What it does |
|---|---|
| `.\scripts\start-dev.ps1 [-Door] [-NoFlorence] [-Kiosk]` | soft PLC, sidecar, API (serving `web/dist`), Node-RED, engine W-14 (and D-07), fleet agents |
| `.\scripts\stop-dev.ps1` | stops what `start-dev` started |
| `torquesense --config config\station_w14.yaml` | one station, replay provider |
| `torquesense --config config\station_w14_gpu.yaml` | one station on the GPU detector, full resolution, rendered scenario videos (`windows-release`) |
| `torquesense --config <tensorrt config> --build-trt-cache` | builds the TensorRT engine once, offline; afterwards a missing cache refuses to start |
| `torquesense --conformance samples --template` | every trace of every sample through the C++ engine |
| `torquesense --sample <dir> --verify-media all` | a sample's media through perception, frame by frame |
| `torquesense --probe-camera uvc --device 0 --size 1280x720` | asks a camera for a mode and refuses a mismatch |

Web routes (served by the API on :8000):

- `/`: engineer dashboard (live feed, procedure rail, anomaly feed, shift analytics)
- `/hmi/<station>`: operator HMI
- `/harness`: fault injection
- `/line`: several stations at once
- `/fleet`: EWMA drift per station and signal

## 9. From CAD to a trained model

The whole chain runs on the laptop. [`training/pipeline.ps1`](training/pipeline.ps1) runs it unattended,
and each step can resume after an interruption:

| Step | Command | Output |
|---|---|---|
| CAD | `freecadcmd cad\build_wheel.py` | `cad/*.FCStd`, `cad/export/*.step`, `*.glb` |
| Compile | `cad-extract cad\profiles\wheel-5lug` | `procedure.json`, `coverage.json` |
| Render training set | `blenderproc run synth\generate.py --gltf cad\export\wheel_assembly.glb --seed 20260721 --frames 4000` | YOLO + COCO labels, per-frame point projections and occlusion |
| Render held-out set | same, `--seed 99 --frames 400` | a different seed: new lighting, poses and states |
| Train | `python training\train_local.py --train <render> --heldout <render>` | `models/yolo11n_wheel.onnx`, `models/threaded_fastener.onnx`, SHA-256 in `models/manifest.json`, `docs/accuracy.synthetic.json` |
| Scenario videos | `python synth\render_plan.py samples\wheel-hub-5lug`, render with `--plan`, then `--video` | `data/scenarios_rendered/*.mp4`, the ten scenarios as camera footage |

- **Classifier output.** The classifier has four outputs: the three bolt states in rank order, plus
  `occluded`. A glove or tool over a bolt is the classifier's call, per point. The engine maps it to
  "no answer this frame".
- **Where the data lives.** Data and models are versioned with DVC in a local store
  (`C:\dev\dvc-store`). Git holds only the `.dvc` pointers (I8).

## 10. Testing and CI

| Suite | Run | Covers |
|---|---|---|
| ctest | `ctest --preset windows-ci` | ring buffers, frame pool, detector math, CAD compiler and STEP reader, FreeCAD profiles, procedure engine, scenarios, zero allocation, conformance, differential vs reference, media frame by frame |
| pytest | `pytest api/tests sidecar/tests reference/tests scripts/tests synth/tests` | outbox ordering and outage, EWMA drift, Florence (mocked), reference interpreter, render reproducibility (I9) |
| web | `cd web; npx vitest run; npx playwright test` | components, HMI and harness flows |
| Go | `cd fleet-agent; go test -race ./...` | agent spool, supervisor |

**GitHub Actions** ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs 10 jobs on every push:

- Linux ASan and TSan
- Windows MinGW
- samples
- fleet agent
- station-script lint
- release tools
- Python
- web
- contract drift

**Jenkins** runs the gates that need the GPU. It never runs fork code: it polls `main`, and nothing on
the laptop listens to the internet.

```powershell
# var\jenkins.env (gitignored): JENKINS_ADMIN_PASSWORD=…  TORQUESENSE_API_KEY=…
docker compose -f docker-compose.jenkins.yml --profile ci up -d --build      # http://localhost:8081
.\scripts\start-jenkins-agent.ps1                                           # agent hsgpc-gpu, WebSocket
```

Pipeline stages:

1. Conditions (AC power, clocks)
2. DVC pull
3. `windows-release` build
4. ctest
5. Both engines over the samples
6. **I4 latency gate**: 100 cycles of the hero fault on the GPU detector, writing to the soft PLC
7. Accuracy report: UNSTABLE until a real validation set exists
8. Nightly four-station soak

## 11. Drills

| Drill | Command | Result |
|---|---|---|
| Network pull: alerts need no network | `python scripts\drill_network_pull.py` | PASS: see [VALIDATION](docs/VALIDATION.md#the-network-pull-drill-spec-section-32--2026-10-16) |
| Fail-visible: a frozen camera | dashboard **Freeze**, or `POST :8095/freeze?on=1` | the PLC turns the station amber 2.37 s after the last beat |
| Fault injection | `/harness` | each fault scenario raises its violation; the edge cases stay silent |

## 12. Cloud: Supabase and Vercel

- **Supabase** is the system of record: Postgres with row-level security, Storage for clips.
  - Migrations live in [`supabase/migrations/`](supabase/migrations/); apply them with
    `python scripts\apply_migrations.py`.
  - The publishable key can read anomalies, cycles and health. It cannot insert anything, or read
    host health.
- **Vercel** builds the replay dashboard from `web/` on every push to `main` ([`vercel.json`](vercel.json)).
  For live mode the project needs `VITE_SUPABASE_URL` and `VITE_SUPABASE_PUBLISHABLE_KEY`, which are
  set. Only the publishable key ever goes to the browser.

## 13. Configuration

- **Station configs** ([`config/`](config/)) set the source, playlist, frame size, CPU pinning,
  provider, models, thresholds, profile, PLC endpoint, clip ring, sidecar, ports and health buckets.
  Settings are read back and logged, never assumed.
- **Secrets** live in `.env` at the repository root (gitignored); [`.env.example`](.env.example) lists
  every key with placeholders:
  - Supabase URL and keys
  - `DATABASE_URL` (session pooler)
  - S3 keys for Storage
  - the API key
  - sidecar and Florence settings

  `DATABASE_URL` and the S3 keys are server-side only and never go under `web/`.

## 14. Repository map

| Path | What |
|---|---|
| `control-app/` | the engine executable: pipeline, io side, sources, conformance, media verification |
| `perception/` | SPSC ring, frame pools, `StateVector`, committer, replay / schematic / ONNX detectors |
| `procedure/` | procedure engine, verdicts, `IAlerter`, OPC-UA alerter, heartbeat |
| `ipc/` | `SharedClipRing`: seqlocked shared-memory JPEG ring |
| `cad-extract/` | the CAD compiler: OCCT STEP reader, pattern solvers, optics, coverage |
| `cad/` | FreeCAD model script, `.FCStd` files, compiled wheel profiles |
| `softplc/` | the soft PLC (open62541) and the Node-RED flow |
| `api/`, `sidecar/` | FastAPI services: outbox, history, drift; Florence-2 and clips |
| `fleet-agent/` | Go host agent and supervisor |
| `web/` | React 19 dashboard, HMI, harness, line, fleet; replay build for Vercel |
| `reference/` | the executable specification (Python); the engine is tested against it |
| `samples/` | eight assemblies: profile, traces, scenarios, media; `earbud-case-charge` holds real recordings |
| `synth/` | renders, scenario plans and videos, schematic media |
| `training/` | local training, evaluation, the Colab notebook, the unattended pipeline |
| `contracts/` | JSON Schemas shared by C++, Python and TypeScript |
| `config/` | station configurations |
| `supabase/` | migrations: schema, RLS, Storage bucket |
| `docker/`, `deploy/k8s/` | Dockerfiles (engine, agent, Jenkins) and Kustomize manifests |
| `scripts/` | dev stack, GPU preflight, Jenkins agent, drills, migrations, calibration |
| `tests/` | contracts, allocation, differential, latency tool, PLC heartbeat |
| `docs/` | spec, validation, ADRs, starter-kit notes |
| `app_screenshots_screen_recording/` | the PS deliverable: flow screenshots and the full recording |

## 15. Invariants

The rules every change is held to (spec Section 7). The table shows the main ones.

| # | Rule |
|---|---|
| I1 | No allocation and no mutex on the verify path |
| I4 | Evidence latency p99 ≤ 150 ms at 30 fps, single station, GPU detector, AC power |
| I5 | `-Wall -Wextra -Wpedantic -Werror` on all first-party code |
| I8 | Never commit weights, renders, videos, secrets, the outbox or TensorRT caches |
| I9 | Renders are reproducible from their seed |
| I10 | Detector accuracy is two numbers, synthetic and real, never one |
| I11 | Florence-2 never on the alert path |
| I12 | A model loads only if its SHA-256 matches the manifest |
| I13 | The PLC, not the engine, decides a station is offline |
| I16 | The C++ engine and the reference interpreter agree on every trace |
| I17 | A profile with an unresolvable point does not compile |

## 16. Limits, honestly stated

- **No real-camera accuracy figure yet.** The detector is trained on renders of the CAD, and the
  hands and nutrunner in those renders are primitive proxies. Real-image accuracy needs labelled photos
  of a real hub, which is a human step. Until then I10 is not met and nothing claims it.
- **Latency figures shown so far come from the replay and schematic providers.** The I4 figure on the
  GPU detector will be recorded in `VALIDATION.md` when it has been measured.
- **Sample media are schematic.** The schematic sample videos are drawings of recorded scenarios,
  stamped as such. The rendered scenario videos are photoreal renders, but they are still not camera
  footage.
- **The network drill is a software drill.** It cuts the API's link to Supabase. Pulling the physical
  cable remains a step of the on-site rehearsal.
- **Out of scope:** route tracing of cables, cosmetic defects and metrology, as the spec states.

## 17. Security

- **Secrets stay out of git.** `.env`, `var/` (including `var/jenkins.env`), the outbox, model weights
  and TensorRT caches are gitignored, and every push is preceded by a secret scan.
- **The browser gets only the publishable key.** Row-level security limits that key to reading.
- **Nothing on the laptop is reachable from the internet.** Jenkins listens on localhost and its agent
  dials out over WebSocket.

## Licence

See [LICENSE](LICENSE).
