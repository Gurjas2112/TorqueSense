import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from gpu_preflight import check_gpu, check_dlls, check_bundle, REQUIRED_DLLS

# The row HSGPC produced on 16 Oct 2026 (driver 596.21), including its impossible power reading.
HSGPC = "NVIDIA GeForce RTX 3060 Laptop GPU, 596.21, 8.6, 6144, 752.00, 130.00"

def levels(rs): return {r.check: r.level for r in rs}

def test_hsgpc_passes_with_power_warning():
    lv = levels(check_gpu(HSGPC))
    assert lv["driver"] == "PASS" and lv["compute capability"] == "PASS" and lv["VRAM"] == "PASS"
    assert lv["power telemetry"] == "WARN"

def test_old_driver_fails():
    assert levels(check_gpu(HSGPC.replace("596.21", "566.36")))["driver"] == "FAIL"

def test_turing_floor_and_below():
    assert levels(check_gpu(HSGPC.replace(", 8.6,", ", 7.5,")))["compute capability"] == "PASS"
    assert levels(check_gpu(HSGPC.replace(", 8.6,", ", 6.1,")))["compute capability"] == "FAIL"

def test_power_not_supported_is_a_warning_not_a_crash():
    lv = levels(check_gpu(HSGPC.replace("752.00, 130.00", "[N/A], [N/A]")))
    assert lv["power telemetry"] == "WARN"

def test_dlls(tmp_path):
    a, b = tmp_path / "a", tmp_path / "b"; a.mkdir(); b.mkdir()
    every = [d for ds in REQUIRED_DLLS.values() for d in ds]
    for d in every[:6]: (a / d).write_bytes(b"")
    for d in every[6:]: (b / d).write_bytes(b"")
    import os
    assert all(r.level == "PASS" for r in check_dlls(f"{a}{os.pathsep}{b}"))
    (b / "nvinfer_10.dll").unlink(); (b / "nvinfer_11.dll").write_bytes(b"")   # TensorRT 11 instead
    lv = levels(check_dlls(f"{a}{os.pathsep}{b}"))
    assert lv["TensorRT 10 (for the TensorRT EP)"] == "FAIL"


def test_bundle_requirements(tmp_path):
    (tmp_path / "nvinfer_10.dll").write_bytes(b"")
    (tmp_path / "msvcp140.dll").write_bytes(b"")
    m = {"requires": {"gpu-runtime": ["cudnn64_9.dll", "nvinfer_10.dll"], "msvc-runtime": ["MSVCP140.dll"]}}
    lv = levels(check_bundle(m, str(tmp_path)))
    assert lv["NVIDIA runtime for this release"] == "FAIL"     # cudnn64_9.dll missing
    assert lv["MSVC runtime for this release"] == "PASS"       # found despite the case difference
