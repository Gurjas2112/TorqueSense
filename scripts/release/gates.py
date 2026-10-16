"""Decide a release from its gate results, on the laptop, and write gates.json.

    python scripts/release/gates.py --version v5.5.0 --conditions conditions.json \
        --conformance-ok --parity parity.json \
        --latency cuda=lat_cuda.json tensorrt=lat_trt.json --soak soak.json --out gates.json

The rules are the spec's (Section 33), in one place:
  cuda       validated when the samples pass and its evidence p99 is within budget
  tensorrt   validated when cuda is, parity holds, and its p99 is within budget and no worse than cuda's
  default    tensorrt if validated, else cuda
  passed     cuda validated, and the four-station soak held VRAM and frame rate
The installer refuses a release whose gates.json is missing or not passed, and the engine may run
only a provider listed in validated_providers.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

VRAM_LIMIT_GB = 5.0      # I7, four-station
MIN_FPS = 14.0


def decide(conformance_ok: bool, parity: dict | None, latency: dict[str, dict], soak: dict | None) -> dict:
    reasons: list[str] = []
    cuda = latency.get("cuda")
    cuda_ok = conformance_ok and cuda is not None and cuda["evidence_p99_ms"] <= cuda["budget_ms"]
    if not conformance_ok:
        reasons.append("samples failed in the C++ engine")
    if cuda is None:
        reasons.append("no CUDA latency run")
    elif cuda["evidence_p99_ms"] > cuda["budget_ms"]:
        reasons.append(f"CUDA evidence p99 {cuda['evidence_p99_ms']} ms over {cuda['budget_ms']} ms")

    trt = latency.get("tensorrt")
    trt_ok = False
    if trt is None:
        reasons.append("tensorrt: not measured")
    elif not cuda_ok:
        reasons.append("tensorrt: not considered — the CUDA baseline did not pass")
    elif not parity or parity.get("unmatched", 1) != 0 or not parity.get("events_identical", False) \
            or parity.get("max_conf_delta", 1.0) > 0.05:
        reasons.append(f"tensorrt: parity failed {parity}")
    elif trt["evidence_p99_ms"] > trt["budget_ms"]:
        reasons.append(f"tensorrt: p99 {trt['evidence_p99_ms']} ms over budget")
    elif trt["evidence_p99_ms"] > cuda["evidence_p99_ms"]:
        reasons.append(f"tensorrt: p99 {trt['evidence_p99_ms']} ms is worse than CUDA's {cuda['evidence_p99_ms']} ms")
    else:
        trt_ok = True

    soak_ok = soak is not None and soak["vram_peak_gb"] < VRAM_LIMIT_GB and soak["min_fps"] >= MIN_FPS
    if soak is None:
        reasons.append("no four-station soak")
    elif not soak_ok:
        reasons.append(f"soak: VRAM {soak['vram_peak_gb']} GB / min {soak['min_fps']} fps")

    validated = [p for p, ok in (("tensorrt", trt_ok), ("cuda", cuda_ok)) if ok]
    return {"passed": cuda_ok and soak_ok, "validated_providers": validated,
            "default_provider": validated[0] if validated else None, "notes": reasons}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--conditions", type=Path)
    ap.add_argument("--conformance-ok", action="store_true")
    ap.add_argument("--parity", type=Path)
    ap.add_argument("--latency", nargs="*", default=[], help="provider=file.json")
    ap.add_argument("--soak", type=Path)
    ap.add_argument("--models", type=Path, help="models/manifest.json the gates ran with")
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args(argv)
    load = lambda p: json.loads(p.read_text()) if p and p.is_file() else None
    latency = {k: load(Path(v)) for k, v in (s.split("=", 1) for s in a.latency)}
    latency = {k: v for k, v in latency.items() if v is not None}
    g = decide(a.conformance_ok, load(a.parity), latency, load(a.soak))
    g = {"version": a.version, **g, "latency": latency, "parity": load(a.parity), "soak": load(a.soak),
         "conditions": load(a.conditions), "models_manifest": load(a.models)}
    a.out.write_text(json.dumps(g, indent=2) + "\n")
    print(f"{a.version}: {'PASSED' if g['passed'] else 'FAILED'} · validated {g['validated_providers']} "
          f"· default {g['default_provider']}")
    for n in g["notes"]:
        print("  -", n)
    return 0 if g["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
