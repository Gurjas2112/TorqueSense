import json, sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from gates import decide, main

CUDA = {"evidence_p99_ms": 71.0, "budget_ms": 150}
TRT_FAST = {"evidence_p99_ms": 64.0, "budget_ms": 150}
TRT_SLOW = {"evidence_p99_ms": 75.0, "budget_ms": 150}
PARITY = {"unmatched": 0, "events_identical": True, "max_conf_delta": 0.02}
SOAK = {"vram_peak_gb": 4.6, "min_fps": 14.8}


def test_tensorrt_becomes_default_when_it_earns_it():
    g = decide(True, PARITY, {"cuda": CUDA, "tensorrt": TRT_FAST}, SOAK)
    assert g["passed"] and g["validated_providers"] == ["tensorrt", "cuda"] and g["default_provider"] == "tensorrt"


def test_slower_tensorrt_is_not_validated_but_release_passes_on_cuda():
    g = decide(True, PARITY, {"cuda": CUDA, "tensorrt": TRT_SLOW}, SOAK)
    assert g["passed"] and g["validated_providers"] == ["cuda"] and g["default_provider"] == "cuda"
    assert any("worse than CUDA" in n for n in g["notes"])


def test_parity_failure_blocks_tensorrt():
    g = decide(True, {**PARITY, "events_identical": False}, {"cuda": CUDA, "tensorrt": TRT_FAST}, SOAK)
    assert g["validated_providers"] == ["cuda"]


def test_failed_samples_or_soak_fail_the_release():
    assert not decide(False, PARITY, {"cuda": CUDA, "tensorrt": TRT_FAST}, SOAK)["passed"]
    assert not decide(True, PARITY, {"cuda": CUDA}, {"vram_peak_gb": 5.3, "min_fps": 15})["passed"]
    assert not decide(True, PARITY, {"cuda": {"evidence_p99_ms": 160, "budget_ms": 150}}, SOAK)["passed"]


def test_cli_writes_gates_json(tmp_path):
    (tmp_path / "c.json").write_text(json.dumps(CUDA)); (tmp_path / "p.json").write_text(json.dumps(PARITY))
    (tmp_path / "s.json").write_text(json.dumps(SOAK))
    rc = main(["--version", "v5.5.0", "--conformance-ok", "--parity", str(tmp_path / "p.json"),
               "--latency", f"cuda={tmp_path / 'c.json'}", f"tensorrt={tmp_path / 'missing.json'}",
               "--soak", str(tmp_path / "s.json"), "--out", str(tmp_path / "gates.json")])
    g = json.loads((tmp_path / "gates.json").read_text())
    assert rc == 0 and g["validated_providers"] == ["cuda"] and g["version"] == "v5.5.0"
