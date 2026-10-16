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

## Gate G2 — the GPU stack is live

**Not run: blocked on 👤 installs** (spec Section 28 step 3): cuDNN 9 for CUDA 13, ONNX Runtime
1.28.0 `onnxruntime-win-x64-gpu_cuda13`, TensorRT 10.13.3.9 (CUDA 13.0 zip). Until then:

```text
> g++ -std=c++20 -O2 -I"C:\dev\tools\onnxruntime\include" ort_check.cpp …
ort_check.cpp:3:10: fatal error: onnxruntime_cxx_api.h: No such file or directory

> cmake --preset windows-release
CMake Error at CMakeLists.txt:19 (message):
  SUNVERIFY_WITH_CUDA=ON but no ONNX Runtime at ONNXRUNTIME_ROOT=''.
```

The engine's Win32 calls do build and run under GCC:

```text
> g++ -std=c++20 -O2 platform_check.cpp -o platform_check.exe -lwinmm; .\platform_check.exe
affinity_prev=65535 eco=1 map=0000025b93840000 seq=1 align=64
```

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
