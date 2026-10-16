# TorqueSense

Real-time assembly verification for the line (Tata Motors PS4): a camera watches the station, a
procedure compiled from CAD says what must happen in which order, and a wrong step stops the station
with a physical alert in well under one second. Any assembly is a folder of data — the engine binary
never names a product.

**Prototype edition** (`docs/SPEC.md`, v5.5-proto). The production track (installed releases,
supervisor, Autologon, power-cut test) is deferred on purpose: [docs/adr/0010](docs/adr/0010-prototype-scope.md).

![Operator HMI: the hero fault caught](app_screenshots_screen_recording/06_hmi_red_stop_bolt_2_corrective_order.png)

- **Demo walk-through:** [app_screenshots_screen_recording/](app_screenshots_screen_recording/README.md) —
  16 screenshots in flow order and `torquesense_demo_full.mp4` (116 s).
- **Public replay:** https://dist-replay-one.vercel.app — a recorded engine session, anomalies read
  from Supabase with the read-only key.

## How it works

```
camera ─► capture ─► detector ─► committer ─► ProcedureEngine ─► alerter ─► soft PLC (OPC-UA) ─► Andon / hold
  │          (lock-free SPSC rings, no allocation, no mutex)        │
  └─► shared clip ring ─► sidecar (Florence-2 adjudication, 6 s clips)   └─► outbox ─► API ─► Supabase ─► web
```

- **Engine (C++20):** `perception/`, `procedure/`, `ipc/`, `control-app/`. Zero allocations and no
  locks on the alert path; Florence-2 is never on it (enforced by a CMake link check).
- **CAD compiler:** `cad-extract/` turns geometry into a procedure profile (e.g. the star order
  1 → 3 → 5 → 2 → 4 is derived from bolt positions, not typed).
- **Fail-visible:** the soft PLC (`softplc/`, open62541) owns a heartbeat watchdog — if frames stop,
  the PLC, not the engine, turns the station AMBER.
- **Services (Python):** `api/` (FastAPI, outbox to Supabase, EWMA fleet drift), `sidecar/`
  (Florence-2, clip assembly).
- **Web (React 19 / Vite):** `web/` — engineer dashboard, operator HMI, fault harness, line view, fleet.
- **Executable specification:** `reference/` (Python) — the engine is differentially tested against it.

## Status

| Area | State |
|---|---|
| Procedure engine, 6 violation kinds, release gate | Done — 22 conformance traces PASS, 0 differential disagreements vs `reference/` |
| Hot path invariants | Done — 0 allocations per tick, no mutex, Florence off the alert path |
| Samples (7 assemblies) incl. schematic media read from pixels | Done — 22 videos, 198 keyframes verified frame by frame |
| Soft PLC + heartbeat watchdog, Node-RED | Done — AMBER 2.37 s after the last beat |
| API / outbox / Supabase (RLS, Storage clips), sidecar with Florence-2 on GPU | Done |
| Web app, Vercel replay | Done — vitest 6, Playwright 5 |
| Latency (replay provider) | Evidence p50 5.6 ms / p99 36.7 ms — **not** the camera + GPU gate (I4) |
| GPU detector (ONNX Runtime / cuDNN / TensorRT), CAD from FreeCAD, Colab training, live camera calibration | Pending — needs installs or human steps |
| Production track (Phase 13) | Deferred on purpose |

Every number above is measured and recorded in [docs/VALIDATION.md](docs/VALIDATION.md); nothing
unmeasured is claimed. No detector is trained yet, so no accuracy is claimed.

## Build and run (Windows, MSYS2 UCRT64)

```powershell
# toolchain: MSYS2 UCRT64 GCC, CMake, Ninja (see docs/SPEC.md Section 28)
cmake --preset windows-ci; cmake --build --preset windows-ci
ctest --preset windows-ci

py -3.12 -m venv .venv; .\.venv\Scripts\Activate.ps1
pip install -r api\requirements.txt -r sidecar\requirements-ci.txt pyyaml pytest
cd web; npm ci; npm run build; cd ..

copy .env.example .env            # fill in your own Supabase values; .env is gitignored
.\scripts\start-dev.ps1 -Door     # engine W-14 + D-07, soft PLC, sidecar, API, Node-RED
# dashboard http://127.0.0.1:8000/   HMI /hmi/W-14   harness /harness   line /line   fleet /fleet
.\scripts\stop-dev.ps1
```

Without a camera or trained model, the engine runs on the `replay` provider (recorded detections)
or the `schematic` provider (states read from the pixels of schematic sample videos). Verify a sample
frame by frame: `torquesense --sample samples\door-trim-harness --verify-media all`.

## Repository map

| Path | What |
|---|---|
| `docs/SPEC.md` | The specification (prototype preface + full v5.5) |
| `docs/adr/` | Architecture decisions 0001–0012 |
| `docs/VALIDATION.md` | Every gate's measured output |
| `docs/STARTER_KIT.md` | The starter kit this repo grew from, and how each piece was checked |
| `contracts/` | JSON Schemas shared by C++, Python and TypeScript (`scripts/gen-contracts.sh`) |
| `samples/` | One folder per assembly: procedure, coverage, scenarios, media |
| `config/` | Station configs (`station_w14.yaml`, `station_d07.yaml`) |
| `synth/`, `training/`, `models/` | Synthetic scenarios and media, training notebook, model manifest |
| `supabase/migrations/` | Postgres schema, RLS, Storage bucket |
| `fleet-agent/` | Go fleet agent and supervisor |
| `tests/` | Contracts, allocation, differential, latency, PLC heartbeat |

## Licence

See [LICENSE](LICENSE).
