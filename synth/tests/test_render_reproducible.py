"""I9 — renders are reproducible: same seed, identical labels, pixels within PSNR >= 45 dB.

Runs only where its inputs exist: BlenderProc installed and the 👤 FreeCAD export at
cad/export/wheel_assembly.glb (cad/build_wheel.py). Elsewhere it skips and says why — it never passes vacuously.
The cache-key and scenario tests below run everywhere.
"""
from __future__ import annotations
import json, math, pathlib, shutil, subprocess, sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "synth"))
import cache       # noqa: E402
import scenarios   # noqa: E402

GLTF = ROOT / "cad" / "export" / "wheel_assembly.glb"


def psnr(a, b) -> float:
    import numpy as np
    mse = float(np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2))
    return math.inf if mse == 0 else 10 * math.log10(255.0 ** 2 / mse)


def render(seed: int, frames: int, out: pathlib.Path):
    subprocess.run(["blenderproc", "run", str(ROOT / "synth" / "generate.py"), "--gltf", str(GLTF),
                    "--seed", str(seed), "--frames", str(frames), "--out-root", str(out)], check=True)
    d = cache.output_dir(GLTF, seed, out)
    labels = json.loads((d / "coco" / "coco_annotations.json").read_text())
    import cv2
    images = [cv2.imread(str(d / "coco" / im["file_name"])) for im in sorted(labels["images"], key=lambda im: im["id"])]
    return labels, images


@pytest.mark.skipif(shutil.which("blenderproc") is None or not GLTF.exists(),
                    reason="needs BlenderProc and the FreeCAD export cad/export/wheel_assembly.glb (cad/build_wheel.py)")
def test_same_seed_same_render(tmp_path):
    la, ia = render(7, 10, tmp_path / "a")
    lb, ib = render(7, 10, tmp_path / "b")
    assert la["annotations"] == lb["annotations"]               # labels must be identical
    for fa, fb in zip(ia, ib):
        assert psnr(fa, fb) >= 45.0                              # pixels: near-identical, not bit-exact


def test_the_cache_key_changes_with_the_cad_and_the_generator(tmp_path):
    g = tmp_path / "w.gltf"
    g.write_bytes(b"rev C")
    a = cache.output_dir(g, 7, tmp_path)
    g.write_bytes(b"rev D")
    b = cache.output_dir(g, 7, tmp_path)
    assert a != b and a.name.endswith("-g" + cache.GENERATOR_VERSION + "-s7")
    assert not cache.is_cached(b)
    cache.write_manifest(b, g, 7, 10)
    assert cache.is_cached(b)


def test_scenario_fixtures_are_up_to_date():
    assert scenarios.main([str(ROOT / "samples" / "wheel-hub-5lug"), "--check"]) == 0


def test_every_fault_scenario_alerts_and_both_edge_cases_stay_silent():
    files = scenarios.generate(ROOT / "samples" / "wheel-hub-5lug")
    exp = {n[:-len(".expected.json")]: json.loads(t) for n, t in files.items() if n.endswith(".expected.json")}
    assert len(exp) == 10
    for name, e in exp.items():
        if name.startswith("fault_"):
            assert e["must_alert"] and e["violations"], name
        else:
            assert not e["must_alert"] and not e["violations"], name
