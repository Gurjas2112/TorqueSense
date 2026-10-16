"""Gate G1 and the DLL half of gate G2 in one command (Windows; run it with the venv's python).

    python scripts\\gpu_preflight.py            # exit 0: pass (warnings allowed); exit 1: fail
    python scripts\\gpu_preflight.py --bundle C:\\TorqueSense\\current\\bin\\bundle.json \\
        --path "C:\\dev\\tools\\TensorRT\\lib;C:\\Program Files\\NVIDIA\\CUDNN\\v9\\bin\\13.0;…"
                                               # a release: check exactly what its bundle requires,
                                               # on exactly the PATH the supervisor will give it

Checks the NVIDIA driver against the R580 floor that CUDA 13.x and the TensorRT 10.13 CUDA 13
build both require, the GPU's compute capability against TensorRT's SM 7.5 floor, and that every
DLL the engine loads at start-up is on PATH. Power telemetry that reports more than the board's
own limit is flagged as unreliable rather than trusted.
"""
from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

QUERY = "name,driver_version,compute_cap,memory.total,power.draw,power.limit"
MIN_DRIVER_MAJOR = 580          # CUDA 13.x and TensorRT's CUDA 13 packages
MIN_COMPUTE_CAP = (7, 5)        # TensorRT 10.x and 11.x
MIN_VRAM_MIB = 6000             # the budgets in Section 12 assume a 6 GB card

# What torquesense.exe loads, by stage. The TensorRT pair is the major-10 ABI that ONNX Runtime's
# CUDA 13 build links — read from the import table of 1.28.0's onnxruntime_providers_tensorrt.dll by
# scripts/release/bundle.py. TensorRT 11 ships nvinfer_11.dll and will not satisfy it.
REQUIRED_DLLS = {
    "MSVC runtime (ONNX Runtime's DLLs need it)": ["msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll",
                                                   "vcruntime140_1.dll"],
    "CUDA 13 runtime": ["cudart64_13.dll", "cublas64_13.dll"],
    "cuDNN 9": ["cudnn64_9.dll"],
    "ONNX Runtime gpu_cuda13": ["onnxruntime.dll", "onnxruntime_providers_cuda.dll",
                                "onnxruntime_providers_tensorrt.dll"],
    "TensorRT 10 (for the TensorRT EP)": ["nvinfer_10.dll", "nvonnxparser_10.dll"],
}


@dataclass
class Result:
    level: str   # PASS | WARN | FAIL
    check: str
    detail: str


def _num(s: str) -> float | None:
    try:
        return float(s.strip())
    except ValueError:          # "[N/A]", "[Not Supported]"
        return None


def check_gpu(csv_line: str) -> list[Result]:
    """csv_line: one row of `nvidia-smi --query-gpu=QUERY --format=csv,noheader,nounits`."""
    name, driver, cc, mem, p_draw, p_limit = [f.strip() for f in csv_line.split(",")]
    out: list[Result] = []

    major = int(driver.split(".")[0])
    out.append(Result("PASS" if major >= MIN_DRIVER_MAJOR else "FAIL", "driver",
                      f"{driver} (need R{MIN_DRIVER_MAJOR} or later)"))

    cc_t = tuple(int(x) for x in cc.split("."))
    out.append(Result("PASS" if cc_t >= MIN_COMPUTE_CAP else "FAIL", "compute capability",
                      f"{cc} on {name} (TensorRT needs SM 7.5 or later)"))

    vram = _num(mem)
    out.append(Result("PASS" if vram and vram >= MIN_VRAM_MIB else "WARN", "VRAM",
                      f"{mem} MiB (budgets assume about 6 GB)"))

    draw, limit = _num(p_draw), _num(p_limit)
    if draw is None or limit is None:
        out.append(Result("WARN", "power telemetry", "not reported; record clocks and temperature only"))
    elif draw > limit * 1.05:
        out.append(Result("WARN", "power telemetry",
                          f"draw {draw:.0f} W exceeds the {limit:.0f} W limit — the sensor reading is "
                          "not trustworthy; record clocks and temperature, not power"))
    else:
        out.append(Result("PASS", "power telemetry", f"{draw:.0f} W of {limit:.0f} W"))
    return out


def find_on_path(dll: str, path: str) -> Path | None:
    for d in path.split(os.pathsep):
        if d and (Path(d) / dll).is_file():
            return Path(d) / dll
    return None


def check_dlls(path: str) -> list[Result]:
    out = []
    for stage, dlls in REQUIRED_DLLS.items():
        missing = [d for d in dlls if find_on_path(d, path) is None]
        out.append(Result("FAIL" if missing else "PASS", stage,
                          "missing from PATH: " + ", ".join(missing) if missing else "all on PATH"))
    return out


def check_bundle(manifest: dict, path: str) -> list[Result]:
    """A release's bundle.json lists what it needs from the machine; check each is reachable."""
    out = []
    labels = {"gpu-runtime": "NVIDIA runtime for this release", "msvc-runtime": "MSVC runtime for this release"}
    for kind, dlls in manifest.get("requires", {}).items():
        missing = [d for d in dlls if find_on_path(d, path) is None and find_on_path(d.lower(), path) is None]
        out.append(Result("FAIL" if missing else "PASS", labels.get(kind, kind),
                          "missing: " + ", ".join(missing) if missing else f"{len(dlls)} DLLs found"))
    return out


def main(argv: list[str] | None = None) -> int:
    import argparse, json
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", help="a release's bin/bundle.json")
    ap.add_argument("--path", help="the PATH to check against (default: this process's PATH)")
    a = ap.parse_args(argv)
    path = a.path if a.path is not None else os.environ.get("PATH", "")
    try:
        row = subprocess.run(["nvidia-smi", f"--query-gpu={QUERY}", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, check=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError) as e:
        print(f"FAIL  nvidia-smi: {e} — install or repair the NVIDIA driver first")
        return 1
    results = check_gpu(row.splitlines()[0])
    if a.bundle:
        results += check_bundle(json.loads(Path(a.bundle).read_text()), path)
    else:
        results += check_dlls(path)
    for r in results:
        print(f"{r.level:<5} {r.check:<34} {r.detail}")
    return 1 if any(r.level == "FAIL" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
