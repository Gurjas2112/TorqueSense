# ADR 0001 — Remove TensorRT

- **Status:** Superseded by [ADR 0009](0009-tensorrt-through-ort-gated.md)
- **Spec:** Appendix A, item 2

## Context

Earlier drafts pinned CUDA 12.1 and TensorRT 8.6 as a standalone dependency with its own version
lock. HSGPC already has CUDA 13.2, and the ONNX Runtime CUDA execution provider is expected to run
YOLO11n in about 14 ms on the RTX 3060 — inside the 33 ms frame budget.

## Decision

Drop standalone TensorRT. Inference runs on ONNX Runtime's CUDA execution provider only.

## Consequences

- One fewer exact version pin; CUDA 13.2 stays as installed.
- The few milliseconds TensorRT might save are not needed to meet I4.
- Kept as a record: ADR 0009 brings TensorRT back *through ONNX Runtime*, behind a gate, and the
  reason it left is part of why it returned that way.
