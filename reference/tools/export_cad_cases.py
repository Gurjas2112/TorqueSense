"""Export the reference compiler's answers as a fixture for the C++ CAD compiler's tests.

    python reference/tools/export_cad_cases.py            # writes cad-extract/tests/fixtures/tsref_cases.json
    python reference/tools/export_cad_cases.py --check    # exit 1 if the committed fixture is stale

The reference is the specification (spec Section 6): the C++ solvers, optics and compiler must
reproduce exactly what this prints, so the expected values in the C++ tests come from here and
never from a hand-typed table.
"""
from __future__ import annotations
import argparse, json, math, pathlib, random, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "reference"))
import yaml                                      # noqa: E402
from tsref import compiler, optics, patterns     # noqa: E402

OUT = ROOT / "cad-extract" / "tests" / "fixtures" / "tsref_cases.json"


def circle(n, r=57.15):
    return [(r * math.cos(math.radians(90 - 360 / n * i)), r * math.sin(math.radians(90 - 360 / n * i)))
            for i in range(n)]


def cases() -> dict:
    layouts = [{"name": f"circle-{n}", "points": circle(n)} for n in range(3, 13)]
    layouts.append({"name": "line-7", "points": [(float(x), 0.0) for x in range(0, 700, 100)]})
    layouts.append({"name": "field-2x3", "points": [(float(x), float(y)) for y in (50, -50) for x in (-100, 0, 100)]})
    layouts.append({"name": "square-4", "points": [(-40.0, 40.0), (40.0, 40.0), (40.0, -40.0), (-40.0, -40.0)]})
    rng = random.Random(5)
    for k in range(200):
        pts = [(rng.uniform(-200, 200), rng.uniform(-200, 200)) for _ in range(rng.randint(3, 16))]
        layouts.append({"name": f"random-{k}", "points": pts})
    for lay in layouts:
        kind, order = patterns.solve(lay["points"])
        lay["kind"], lay["order"] = kind, order

    samples = []
    for d in sorted(p for p in (ROOT / "samples").iterdir() if (p / "profile.yaml").exists()):
        m = yaml.safe_load((d / "profile.yaml").read_text(encoding="utf-8"))
        cams = [optics.Camera(**{k: (tuple(v) if isinstance(v, list) else v) for k, v in c.items()})
                for c in m["cameras"]]
        prof, report, unresolved = compiler.compile_profile(m, cams, 16)
        samples.append({
            "sample": d.name,
            "profile": prof["profile"],
            "ips": [{"id": ip["id"], "camera": ip["camera"], "feature_px": ip["feature_px"]}
                    for ip in prof["inspection_points"]],
            "steps": [{"id": s["id"], "targets": s["targets"], "pattern": s.get("pattern")} for s in prof["steps"]],
            "unresolved": [r[0] for r in unresolved],
        })
    return {"generated_by": "reference/tools/export_cad_cases.py", "layouts": layouts, "samples": samples}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    text = json.dumps(cases(), indent=1) + "\n"
    if a.check:
        ok = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        print("fixture up to date" if ok else f"{OUT} is stale: re-run this script")
        return 0 if ok else 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
