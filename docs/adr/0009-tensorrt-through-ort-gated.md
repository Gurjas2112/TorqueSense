# ADR 0009 — TensorRT through ONNX Runtime, gated

- **Status:** Accepted. Supersedes [ADR 0001](0001-remove-tensorrt.md).
- **Spec:** Sections 28, 30, 33; Appendix A, items 32, 33, 37

## Context

ONNX Runtime 1.28.0's `gpu_cuda13` package already contains `onnxruntime_providers_tensorrt.dll`,
which imports `nvinfer_10.dll`. The engine is built with MSYS2 GCC, and TensorRT's native C++ API
is an MSVC ABI. An unpinned `pip install tensorrt-cu13` now brings TensorRT 11, which that
provider cannot load.

## Decision

Reach TensorRT 10.13.3.9 (the CUDA 13.0 zip, at `C:\dev\tools\TensorRT`) only through ORT's
TensorRT execution provider: FP16, a 256 MB workspace, and engine and timing caches under
`cache/trt/<TensorRT version>/<model hash>/`. Adopt it only if the parity gate (IoU ≥ 0.9,
confidence within 0.05, identical scenario events) and the latency gate both pass. Only providers
in the release's `gates.json` may run. A fall to CUDA is reported as DEGRADED; there is never a
fall to the CPU.

## Consequences

- GCC never links TensorRT. The C API boundary already proven for CUDA is the same one.
- An engine is never built during a demo: `--build-trt-cache` runs during setup, and a missing
  cache refuses to start.
- If the gate fails, the demo stays on CUDA and `VALIDATION.md` records why.
