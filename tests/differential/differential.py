"""I16 — the C++ engine and the reference interpreter agree on every trace, violation for violation.

    python tests/differential/differential.py --engine build/windows-ci/control-app/torquesense.exe

Runs `torquesense --conformance samples --json` and replays the same traces through
reference/tsref, then compares the two full violation lists — kind, step, time and every detail
field — not just the subset a trace's expect: block names. A disagreement names the sample, the
trace and the first violation at which the two implementations diverged. The reference is the
specification: when they disagree, the reference wins until a test proves it wrong.
"""
from __future__ import annotations
import argparse, json, pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "reference"))
sys.path.insert(0, str(ROOT / "samples"))
import yaml                                    # noqa: E402
import run_samples                             # noqa: E402
from tsref.procedure import Procedure          # noqa: E402


def reference_run(prof: dict, trace: dict):
    p = Procedure(prof)
    p.start(0)
    for e in trace.get("events", []):
        if "ip" in e:
            p.state(e["t"], e["ip"], e["state"])
        elif "tag" in e:
            p.tag(e["t"], e["tag"], e["value"])
        p.tick(e["t"])
    ok = p.release(trace["release_at"])
    return ok, [{"kind": v.kind, "step": v.step, "t_ms": v.t_ms, **v.detail} for v in p.violations]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    a = ap.parse_args(argv)
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "cpp.json"
        engine = str(pathlib.Path(a.engine).resolve())
        r = subprocess.run([engine, "--conformance", str(ROOT / "samples"), "--template", "--json", str(out)],
                           capture_output=True, text=True, encoding="utf-8")
        if not out.exists():
            print(r.stdout, r.stderr)
            return 1
        cpp = {s["sample"]: {t["trace"]: t for t in s["traces"]} for s in json.loads(out.read_text(encoding="utf-8"))}

    traces = disagreements = 0
    for d in run_samples.sample_dirs(template=True):
        prof, _, _ = run_samples.compile_folder(d)
        for path in sorted((d / "traces").glob("*.yaml")):
            trace = yaml.safe_load(path.read_text(encoding="utf-8"))
            ok, ref = reference_run(prof, trace)
            got = cpp.get(d.name, {}).get(path.stem)
            traces += 1
            if got is None:
                print(f"{d.name}/{path.stem}: missing from the C++ run")
                disagreements += 1
                continue
            if got["release_ok"] != ok:
                print(f"{d.name}/{path.stem}: release gate — reference {ok}, C++ {got['release_ok']}")
                disagreements += 1
            for i in range(max(len(ref), len(got["violations"]))):
                rv = ref[i] if i < len(ref) else None
                cv = got["violations"][i] if i < len(got["violations"]) else None
                if rv != cv:
                    print(f"{d.name}/{path.stem}: violation {i + 1} — reference {rv}, C++ {cv}")
                    disagreements += 1
                    break
    print(f"differential: {traces} traces, {disagreements} disagreement(s)")
    return 0 if disagreements == 0 and traces > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
