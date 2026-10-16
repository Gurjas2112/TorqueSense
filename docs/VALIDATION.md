# VALIDATION — what has been measured, and under what conditions

Only numbers in this file may be quoted anywhere (coding instructions, Section 7). A gate that has
not run says **not run**, with the reason. Nothing here is an estimate.

Machine: HSGPC — i7-11800H, 16 GB, RTX 3060 Laptop 6 GB (cc 8.6), driver 596.21, Windows 11 Home
26H2. Toolchain: MSYS2 UCRT64 GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2.

## Gate G1 — GPU and driver (2026-10-16)

```text
> python scripts\gpu_preflight.py
PASS  driver                             596.21 (need R580 or later)
PASS  compute capability                 8.6 on NVIDIA GeForce RTX 3060 Laptop GPU (TensorRT needs SM 7.5 or later)
PASS  VRAM                               6144 MiB (budgets assume about 6 GB)
WARN  power telemetry                    not reported; record clocks and temperature only
PASS  MSVC runtime (ONNX Runtime's DLLs need it) all on PATH
PASS  CUDA 13 runtime                    all on PATH
FAIL  cuDNN 9                            missing from PATH: cudnn64_9.dll
FAIL  ONNX Runtime gpu_cuda13            missing from PATH: onnxruntime_providers_cuda.dll, onnxruntime_providers_tensorrt.dll
FAIL  TensorRT 10 (for the TensorRT EP)  missing from PATH: nvinfer_10.dll, nvonnxparser_10.dll
```

**G1: PASS.** Driver, compute capability and VRAM clear their floors.

## Gate G2 — the GPU stack is live (2026-10-16)

Installed without admin rights, each archive checked against its published SHA-256: cuDNN 9.27.0.42
for CUDA 13 (NVIDIA's redistributable archive) at `C:\dev\tools\cudnn`, ONNX Runtime 1.28.0
`onnxruntime-win-x64-gpu_cuda13` at `C:\dev\tools\onnxruntime`, TensorRT 10.13.3.9 (CUDA 13.0 zip) at
`C:\dev\tools\TensorRT`. User variables: `ORT_ROOT`, and `PATH` gains the four `lib`/`bin` folders.

```text
> python scripts\gpu_preflight.py
PASS  driver                             596.21 (need R580 or later)
PASS  compute capability                 8.6 on NVIDIA GeForce RTX 3060 Laptop GPU (TensorRT needs SM 7.5 or later)
PASS  VRAM                               6144 MiB (budgets assume about 6 GB)
WARN  power telemetry                    not reported; record clocks and temperature only
PASS  MSVC runtime (ONNX Runtime's DLLs need it) all on PATH
PASS  CUDA 13 runtime                    all on PATH
PASS  cuDNN 9                            all on PATH
PASS  ONNX Runtime gpu_cuda13            all on PATH
PASS  TensorRT 10 (for the TensorRT EP)  all on PATH

> ort_check.exe mul2.onnx           (MinGW-w64 GCC 16.2 against the MSVC-built package)
ORT 1.28.0, CUDA EP appended
out[0]=2.000000

> trt_check.exe mul2.onnx
TensorRT EP appended
CUDA EP appended behind it
out[0]=2.000000

> trtexec --onnx=yolo11n.onnx --fp16 --saveEngine=probe.engine        (stock YOLO11n, 640²)
TensorRT version: 10.13.3
GPU Compute Time: min = 1.64343 ms, max = 9.1904 ms, mean = 2.00833 ms, median = 1.9502 ms,
                  percentile(99%) = 2.61841 ms
&&&& PASSED TensorRT.trtexec [TensorRT v101303]

> torquesense --version              (windows-release preset)
torquesense 5.5.0-dev (GCC 16.2.0, __cplusplus 202002)
onnxruntime 1.28.0
provider tensorrt: loads
provider cuda: loads
```

**G2: PASS.** One trap found and fixed on the way: Windows ships `onnxruntime.dll` 1.17.1 in
System32, and the loader searches System32 before PATH, so the first run failed with *"requested API
version [28] is not available"*. The build now copies ORT's DLLs beside every executable that loads
them (`perception/CMakeLists.txt`).

## Phase 2 — CAD from FreeCAD, compiled (2026-10-16)

`cad/build_wheel.py` in FreeCAD 1.1.4 (portable, checksum verified) → STEP + glTF → `cad-extract` (OCCT):

```text
> cad-extract cad\profiles\wheel-5lug
wheel-fitment-rl: 5 inspection points, OCCT built in
  torque: circle order bolt-1,bolt-3,bolt-5,bolt-2,bolt-4
  coverage clean: every point >= 16 px on some camera
> cad-extract cad\profiles\wheel-4lug
wheel-fitment-rl: 4 inspection points, OCCT built in
  torque: circle order bolt-1,bolt-3,bolt-2,bolt-4
  coverage clean: every point >= 16 px on some camera
```

The five positions read from the STEP occurrences equal the profile's hand-entered ones to 0.01 mm
(bolt-1 [0.0, 57.15, 0.0] … bolt-5 [-54.35, 17.66, 0.0]); all points `clear` of occlusion at 25.6 px.
**Gate: PASS** (five fasteners at 114.3 mm PCD; `cmake --preset windows-release` configures).
`CadFromFreeCad.wheel-5lug` and `.wheel-4lug` keep the compiled profiles current in ctest.

## I9 — renders are reproducible (2026-10-16)

`pytest synth/tests` with BlenderProc 2.8 (Blender 4.2.1, OptiX on the RTX 3060) and the FreeCAD glTF:
`test_same_seed_same_render` renders 10 frames twice with seed 7 — identical labels, every frame's
PSNR ≥ 45 dB. **4 passed** (the render test runs; it does not skip).

## Latency (I4), accuracy (I10), VRAM (I7), TensorRT gate

**Not run.** They need the G2 stack and a trained detector (Phases 3, 6, 12).

## The engine agrees with the reference (I16) — 2026-10-16

```text
> torquesense --conformance samples --template
sample                    IPs  min px   traces  derived order (C++ engine)
_template                   4    27.7    2/2    fasten: line 2,3,1  PASS
desk-cover-4screw           6    27.2    4/4    screws: circle 1,3,2,4  PASS
desk-harness-clips          7    16.6    3/3    —  PASS
door-trim-harness           8    17.4    3/3    —  PASS
electronics-enclosure       8    17.8    2/2    cover_screws: field 2,5,3,4,6,1  PASS
wheel-hub-5lug              5    25.6    6/6    torque: circle 1,3,5,2,4  PASS
windshield-gasket           9    16.6    2/2    —  PASS

violation kinds exercised: 6/6

> python tests\differential\differential.py --engine build\windows-ci\control-app\torquesense.exe
differential: 22 traces, 0 disagreement(s)
```

The differential test compares complete violation lists — kind, step, time and every detail field.

## Zero allocation on the verify path (I1)

`NoAllocation.ProcedureEngineOverTenThousandEvents`: 0 allocations over 10,000 events (good and
faulty cycles). The full `Pipeline::tick()` test follows in Phase 6.

## Fail-visible (I13) — soft PLC built with GCC 16.2 against open62541 1.4.21

```text
> python tests\plc\test_heartbeat.py
advertised endpoint URL: ['opc.tcp://127.0.0.1:14840']
before first beat  -> Amber: True | Online: False
while beating      -> Amber: False | Online: True
engine stopped     -> Amber after 1.97 s | Online: False
```

The last beat was written 0.4 s before the clock started: AMBER came 2.37 s after the last
heartbeat, inside the 2.5 s bound.

## Pipeline, scenarios and allocation (Phase 6) — 2026-10-16

- `Scenario.EveryRecordedScenarioRaisesExactlyItsExpectedVerdict`: all ten scenarios through replay
  perception, the 2-frame debounce / 0.65 commit floor / ambiguous band, and the interpreter — every
  fault raises exactly its expected violations inside its frame window; `edge_hand_occlusion` (with a
  flicker frame, an occlusion and an ambiguous frame) and `edge_second_person` raise nothing.
- `NoAllocation.PipelineTickOverTenThousandFrames`: **0 allocations** in `Pipeline::tick()` (detector
  replay + committer + interpreter + deadlines) over 10,000 frames cycling through every scenario (I1).

### Latency on the replay provider — NOT the I4 measurement

```text
> latency_test --scenario data/scenarios/fault_interrupt_resume.mp4 --runs 100 --budget-evidence-p99-ms 150
latency_test: 100 runs of fault_interrupt_resume (15.0 s each), provider replay, PLC opc.tcp://127.0.0.1:14841 (opcua)
conditions before: NVIDIA GeForce RTX 3060 Laptop GPU, 1702 MHz, 2100 MHz, 50, 596.21
engine   latency (ms): n=199 p50=1.66 p99=3.26 p99.9=6.33 max=6.33
evidence latency (ms): n=199 p50=5.60 p99=36.73 p99.9=40.06 max=40.06
frames verified 45025, dropped 901, fps 18.80
conditions after: NVIDIA GeForce RTX 3060 Laptop GPU, 1702 MHz, 2100 MHz, 38, 596.21
PASS: evidence p99 36.73 ms against a 150 ms budget
```

What this does and does not show:

- **Measured:** the engine's own path — capture pacing, the SPSC rings, the debounce frame, the
  interpreter, the alert thread and a real OPC-UA write acknowledged by the soft PLC on the same
  machine. Histogram: `docs/latency-replay.hgrm`; summary: `docs/latency-replay.json`.
- **Not measured:** detector inference. The replay provider has none, so the ~14 ms YOLO11n budget of
  Section 13 is absent. I4 stays **not run** until the GPU stack and a trained model exist.
- **Conditions not controlled:** clocks unlocked, other work running (builds, history seeding), and the
  process was descheduled for part of the run — the average of 18.8 fps and 901 dropped frames show
  it. 199 of the expected 200 alerts were acknowledged; the last cycle ended with the run.

## Sample media through perception, frame by frame — 2026-10-16

Schematic videos and keyframes in `samples/*/media/` (docs/adr/0012), read from the pixels by
SchematicDetector, committed through the debounce, run through the interpreter:

```text
 (my-assembly): schematic media through SchematicDetector -> debounce -> interpreter
  correct                      330 frames    4 commits     0 unread  release ok     no violation                             PASS
  wrong_order                  330 frames    4 commits     0 unread  release held   SEQUENCE,SEQUENCE,SEQUENCE               PASS
  videos 2/2 PASS · keyframe images 10 all read as expected
 (desk-cover-4screw): schematic media through SchematicDetector -> debounce -> interpreter
  correct                      930 frames   10 commits     0 unread  release ok     no violation                             PASS
  round_the_box                930 frames   10 commits     0 unread  release held   SEQUENCE,SEQUENCE                        PASS
  screw_backed_out             930 frames   11 commits     0 unread  release held   REGRESSION,INCOMPLETE_AT_RELEASE         PASS
  screw_left_out              2160 frames    8 commits     0 unread  release held   PRECEDENCE,STEP_TIMEOUT,INCOMPLETE_AT_RELEASE PASS
  videos 4/4 PASS · keyframe images 43 all read as expected
 (desk-harness-clips): schematic media through SchematicDetector -> debounce -> interpreter
  correct                      780 frames    7 commits     0 unread  release ok     no violation                             PASS
  cover_over_open_clip         780 frames    6 commits     0 unread  release held   PRECEDENCE,INCOMPLETE_AT_RELEASE         PASS
  plug_not_mated              1440 frames    6 commits     0 unread  release held   PRECEDENCE,STEP_TIMEOUT,INCOMPLETE_AT_RELEASE PASS
  videos 3/3 PASS · keyframe images 22 all read as expected
 (door-trim-harness-fl): schematic media through SchematicDetector -> debounce -> interpreter
  connector_not_locked         210 frames    8 commits     0 unread  release held   EVIDENCE_MISSING                         PASS
  correct                      210 frames    8 commits     0 unread  release ok     no violation                             PASS
  panel_hides_open_clip        210 frames    7 commits     0 unread  release held   PRECEDENCE,INCOMPLETE_AT_RELEASE         PASS
  videos 3/3 PASS · keyframe images 26 all read as expected
 (enclosure-cover): schematic media through SchematicDetector -> debounce -> interpreter
  correct                      180 frames    8 commits     0 unread  release ok     no violation                             PASS
  label_forgotten              180 frames    7 commits     0 unread  release held   INCOMPLETE_AT_RELEASE                    PASS
  videos 2/2 PASS · keyframe images 17 all read as expected
 (wheel-fitment-rl): schematic media through SchematicDetector -> debounce -> interpreter
  adjacent_order               390 frames   10 commits     0 unread  release held   SEQUENCE,SEQUENCE,SEQUENCE,SEQUENCE      PASS
  correct                      390 frames   10 commits     0 unread  release ok     no violation                             PASS
  loosened_bolt                390 frames   11 commits     0 unread  release held   REGRESSION,INCOMPLETE_AT_RELEASE         PASS
  missing_bolt                 960 frames    8 commits     0 unread  release held   PRECEDENCE,PRECEDENCE,PRECEDENCE,PRECEDENCE,STEP_TIMEOUT,INCOMPLETE_AT_RELEASE,INCOMPLETE_AT_RELEASE PASS
  no_nutrunner_result          390 frames   10 commits     0 unread  release held   EVIDENCE_MISSING,EVIDENCE_MISSING,EVIDENCE_MISSING,EVIDENCE_MISSING,EVIDENCE_MISSING PASS
  torque_before_all_started    390 frames    9 commits     0 unread  release held   PRECEDENCE,PRECEDENCE,PRECEDENCE,PRECEDENCE PASS
  videos 6/6 PASS · keyframe images 64 all read as expected
 (windshield-bond): schematic media through SchematicDetector -> debounce -> interpreter
  correct                      120 frames    9 commits     0 unread  release ok     no violation                             PASS
  glass_before_gasket_seated   120 frames    8 commits     0 unread  release held   PRECEDENCE,INCOMPLETE_AT_RELEASE         PASS
  videos 2/2 PASS · keyframe images 19 all read as expected
```

This proves the media path and the procedure on frames. It is not an accuracy figure: the frames are
drawings in a known colour code, and SchematicDetector is valid on nothing else.

## Records reach Supabase, end to end (Phase 7) — 2026-10-16

Stack from `scripts/start-dev.ps1 -NoFlorence`: soft PLC, engine (replay provider), sidecar (clip
writer), API with the outbox, fleet agent. The harness queued `fault_interrupt_resume`; the live
project (`rfebayoatwpbuengzotw`, ap-southeast-2) then held:

```text
anomalies: SEQUENCE bolt-2 / expected bolt-5, evidence 36.2 ms, clip_key clips/W-14/2026/10/16/<cycle>/<event>.mp4
           INCOMPLETE_AT_RELEASE, evidence 1.6 ms, with its clip
API /health: outbox flushed 30, dead letters 0, last_error null
clip (public URL): 200 video/mp4 · h264 960x540 yuv420p · 172 frames
publishable key: reads anomalies, cycles, station_health, health_events, ftr_hourly; 401 on host_health,
                 health_chart_state and on every insert (RLS)
```

## The network-pull drill (spec Section 32) — 2026-10-16

`python scripts\drill_network_pull.py`: engine (replay provider, four fault scenarios on loop), soft
PLC, and a drill API whose Postgres and S3 links to Supabase pass through a local relay; the relay is
pulled 4 s into a cycle for 45 s, then restored. The laptop's adapter is not touched, so this proves the
software claim; pulling the physical cable remains a step of the on-site rehearsal.

```text
{"t": 24.1, "phase": "link up",  "outbox_pending": 0,  "flushed": 4,  "alerts": 6,  "frames_verified": 727,  "plc_connected": true}
{"t": 31.0, "phase": "PULLED",   "outbox_pending": 0,  "flushed": 6,  "alerts": 7,  "frames_verified": 933,  "plc_connected": true}
{"t": 51.1, "phase": "down",     "outbox_pending": 9,  "flushed": 6,  "alerts": 15, "frames_verified": 1537, "plc_connected": true}
{"t": 76.2, "phase": "RESTORED", "outbox_pending": 18, "flushed": 6,  "alerts": 21, "frames_verified": 2290, "plc_connected": true}
{"t": 92.4, "phase": "drained",  "outbox_pending": 0,  "flushed": 27, "alerts": 25, "frames_verified": 2777, "plc_connected": true}
PASS: 14 alerts and 1356 frames verified with the link down; outbox peaked at 18, drained=True;
      14/14 queued anomalies in Supabase, in event order: True
```

**PASS.** Alerts and the HMI's frame stream carried on with no link; the outbox rose and drained in 16 s
after the link returned; every anomaly queued during the outage is in Supabase (read back with the
publishable key, as the Vercel page reads it), queued in event order.

## Florence-2 in the loop — 2026-10-16

`florence-community/Florence-2-base` at commit `00921df66db728a9ceb750f5eca43e5c203a2051` (I12), torch
2.14.1+cu130, transformers 5.20.1, on the RTX 3060: loaded in 67.9 s (first load includes the
download); whole-GPU memory in use with the sidecar loaded: 797 MiB. `edge_hand_occlusion` drove
bolt 3 into the 0.35–0.65 band: the engine held the state and asked the sidecar through its circuit
breaker (1 adjudication served, circuit closed); the scenario raised no violation, as it must.

```text
direct probe, six calls on frames from the live clip ring:
Florence-2 latency ms per call: [750.0, 15.0, 0.0, 0.0, 0.0, 16.0]
```

One uncached call took **750 ms** — slower than the spec's 200–400 ms estimate; repeats of the same crop
are answered from the 256-entry cache. Not yet measured: the agreement rate on ambiguous crops
(spec Section 30 asks for it before the adjudicator is trusted), which needs labelled real crops.

## Dashboard, HMI, Node-RED (Phase 8) — 2026-10-16

```text
> npx vitest run          6 passed
> npx playwright test     5 passed: GREEN · RED (strobe + corrective order) · AMBER (PLC down) ·
                          offline after one silent second · the HMI accepts no input
```

Node-RED 5.0.8 (tools/node-red) reads the soft PLC's tags over OPC-UA and lights GREEN and
INSPECTION ONLINE live; the demo stack runs natively from `scripts/start-dev.ps1`.

## Profiles track (Phase 10): a second assembly, live, from the pixels — 2026-10-16

Station D-07 (`config/station_d07.yaml`) runs `samples/door-trim-harness` in the same engine binary as
the wheel, with perception provider `schematic`: frames from the sample's media through FileSource,
states read from the pixels by SchematicDetector, plant signals (the connector lock) through the soft
PLC. After four cycles (correct · panel over an open clip · correct · connector not locked):

```text
D-07 /health: provider schematic · profile door-trim-harness-fl · fps 30.00 · frames dropped 0 · PLC connected
PRECEDENCE trim_panel panel-1  evidence 36.1 ms | Panel 1 installed before step 'clips' was complete.
INCOMPLETE_AT_RELEASE clips    evidence 1.4 ms  | Release requested with step 'clips' unfinished. Missing: clip 4.
```

Windshield and electronics enclosure stay at procedure level, as Section 22 plans: both pass the
conformance suite in the C++ engine and the reference (22 traces, 0 disagreements) and their media
pass `MediaConformance`. `python samples\run_samples.py --template` PASS, 6/6 kinds.

## The recorded demo run — 2026-10-16

`app_screenshots_screen_recording/` (16 screenshots, 116 s video) was captured from the live stack:
two engines, soft PLC, Node-RED, Florence-2, API, Supabase, Vercel. During the first recording attempt
W-14 recorded one alert at **193.5 ms engine / 226.7 ms evidence** (n = 20; p50 2.5 / 8.6 ms) while
two engines, Florence-2, Node-RED, a headless browser and video encoding shared the laptop — inside the
one-second requirement, far outside the ~36 ms seen unloaded, and unexplained. The second (published)
run measured evidence p99 36.7 ms (W-14) and 38.1 ms (D-07). The outlier is recorded here rather than
re-run away; I4 remains not run.
