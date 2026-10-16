"""Detector accuracy, reported as two numbers, never one (spec Section 17; invariant I10).

    python training\\eval.py --model models\\yolo11n_wheel.onnx --synthetic data\\synth\\heldout \\
        --real data\\real_val --out docs\\accuracy.json
    python training\\eval.py --check        # docs/accuracy.json holds both blocks, and VALIDATION.md quotes them

The synthetic held-out set proves the model learned the rendered classes; the real set proves the
classes transfer to real pixels. A combined figure would hide the gap between them, so this script
refuses to write one. The `limits` field states what neither set covers: real hubs carry no yellow
witness marks, so `bolt_torqued` is validated on synthetic data only.
"""
from __future__ import annotations
import argparse, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACCURACY = ROOT / "docs" / "accuracy.json"
VALIDATION = ROOT / "docs" / "VALIDATION.md"
LIMITS = ["bolt_torqued has no real-set coverage: real hubs do not carry the yellow witness mark (spec Section 17)"]


def evaluate(model: pathlib.Path, dataset: pathlib.Path) -> dict:
    """mAP50 and mAP50-95 on one YOLO-format dataset folder (images/ + labels/ + data.yaml)."""
    from ultralytics import YOLO        # dev venv only; not a runtime dependency
    m = YOLO(str(model), task="detect")
    r = m.val(data=str(dataset / "data.yaml"), imgsz=640, batch=8, plots=False, verbose=False)
    images = len(list((dataset / "images").glob("*")))
    return {"set": str(dataset.relative_to(ROOT) if dataset.is_relative_to(ROOT) else dataset), "images": images,
            "map50": round(float(r.box.map50), 4), "map50_95": round(float(r.box.map), 4)}


def check() -> list[str]:
    problems = []
    if not ACCURACY.exists():
        return [f"{ACCURACY.relative_to(ROOT)} does not exist: run eval.py with a model and both sets (Phase 3)"]
    acc = json.loads(ACCURACY.read_text(encoding="utf-8"))
    for block in ("synthetic", "real"):
        b = acc.get(block)
        if not b or "map50" not in b or "map50_95" not in b:
            problems.append(f"accuracy.json has no {block} block with map50 and map50_95 (I10)")
    if "combined" in acc or "map50" in acc:
        problems.append("accuracy.json carries a combined figure (I10 forbids one)")
    if not acc.get("limits"):
        problems.append("accuracy.json must state its limits")
    text = VALIDATION.read_text(encoding="utf-8") if VALIDATION.exists() else ""
    for block in ("synthetic", "real"):
        b = acc.get(block) or {}
        if "map50" in b and f'{b["map50"]:.3f}' not in text:
            problems.append(f"VALIDATION.md does not quote the {block} mAP50 ({b['map50']:.3f})")
    return problems


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--model", type=pathlib.Path)
    ap.add_argument("--synthetic", type=pathlib.Path)
    ap.add_argument("--real", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=ACCURACY)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    if a.check:
        problems = check()
        for p in problems:
            print("FAIL", p)
        print("PASS accuracy.json: two figures, limits stated, quoted in VALIDATION.md" if not problems else "")
        return 1 if problems else 0
    if not (a.model and a.synthetic and a.real):
        ap.error("--model, --synthetic and --real are all required: one set alone is not a report (I10)")
    out = {"model": a.model.name, "synthetic": evaluate(a.model, a.synthetic), "real": evaluate(a.model, a.real),
           "limits": LIMITS}
    a.out.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
