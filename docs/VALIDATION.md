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
