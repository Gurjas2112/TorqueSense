"""Run every sample assembly through the reference engine and check it against its contract.

    python samples/run_samples.py                 # all samples
    python samples/run_samples.py desk-cover-4screw
    python samples/run_samples.py --template      # also check samples/_template

A sample is a folder: profile.yaml, traces/*.yaml, and optionally media/. This script has no
knowledge of any assembly. Everything it checks comes from the folder, which is the point: if a
new assembly passes here, the engine accepts it without a code change.

The contract, per sample:
  C1  the profile compiles, and every inspection point resolves to >= 16 px on some camera (I17)
  C2  every step has a deadline, max_duration_s (I15)
  C3  traces/correct.yaml exists, passes the release gate, and raises no violation
  C4  at least one fault trace, and every fault trace expects the release gate to refuse
  C5  every trace produces exactly the violations it expects, in order
"""
from __future__ import annotations
import argparse, pathlib, sys
from dataclasses import dataclass, field
import yaml

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "reference"))
from tsref import compiler, optics                    # noqa: E402
from tsref.procedure import Procedure                  # noqa: E402

MIN_FEATURE_PX = 16
VIOLATION_KINDS = ("SEQUENCE", "PRECEDENCE", "EVIDENCE_MISSING", "REGRESSION", "STEP_TIMEOUT",
                   "INCOMPLETE_AT_RELEASE")


@dataclass
class TraceResult:
    name: str
    ok: bool
    release_ok: bool
    violations: list
    problem: str = ""


@dataclass
class Report:
    sample: str
    profile: str = ""
    problems: list = field(default_factory=list)
    traces: list = field(default_factory=list)
    n_ips: int = 0
    patterns: list = field(default_factory=list)
    min_px: float = 0.0

    @property
    def ok(self) -> bool:
        return not self.problems and all(t.ok for t in self.traces)

    @property
    def kinds_covered(self) -> set:
        return {v["kind"] for t in self.traces for v in t.violations}


def load_profile(folder: pathlib.Path) -> dict:
    return yaml.safe_load((folder / "profile.yaml").read_text(encoding="utf-8"))


def cameras(manifest: dict) -> list[optics.Camera]:
    return [optics.Camera(**{k: (tuple(v) if isinstance(v, list) else v) for k, v in c.items()})
            for c in manifest["cameras"]]


def compile_folder(folder: pathlib.Path):
    m = load_profile(folder)
    return compiler.compile_profile(m, cameras(m), MIN_FEATURE_PX)


def replay(prof: dict, trace: dict):
    p = Procedure(prof)
    p.start(0)
    for e in trace.get("events", []):
        t = e["t"]
        if "ip" in e:
            p.state(t, e["ip"], e["state"])
        elif "tag" in e:
            p.tag(t, e["tag"], e["value"])
        p.tick(t)
    ok = p.release(trace["release_at"])
    return ok, [{"kind": v.kind, "step": v.step, **v.detail} for v in p.violations]


def matches(expected: list, got: list) -> str:
    if len(expected) != len(got):
        return f"expected {len(expected)} violation(s), got {len(got)}: {got}"
    for i, (e, g) in enumerate(zip(expected, got)):
        for k, v in e.items():
            if g.get(k) != v:
                return f"violation {i + 1}: expected {k}={v!r}, got {g}"
    return ""


def check(folder: pathlib.Path) -> Report:
    r = Report(folder.name)
    try:
        m = load_profile(folder)
        prof, report, unresolved = compile_folder(folder)
    except Exception as exc:                                         # a bad profile is a result
        r.problems.append(f"C1 profile does not compile: {exc}")
        return r
    r.profile, r.n_ips = prof["profile"], len(prof["inspection_points"])
    r.min_px = min(px for _, _, px, _ in report)
    r.patterns = [f'{s["id"]}: {s["pattern"]} ' + ",".join(x.rsplit("-", 1)[1] for x in s["targets"])
                  for s in prof["steps"] if "pattern" in s]
    if unresolved:
        r.problems.append("C1 unresolved inspection points: "
                          + ", ".join(f"{ip} {px} px on {cam}" for ip, cam, px, _ in unresolved))
    no_deadline = [s["id"] for s in m["steps"] if not s.get("max_duration_s")]
    if no_deadline:
        r.problems.append(f"C2 steps without max_duration_s: {no_deadline}")
    try:
        Procedure(prof)                                              # rejects a state outside its family
    except ValueError as exc:
        r.problems.append(f"C1 {exc}")
        return r

    traces = sorted((folder / "traces").glob("*.yaml"))
    names = [t.stem for t in traces]
    if "correct" not in names:
        r.problems.append("C3 traces/correct.yaml is missing")
    if not [n for n in names if n != "correct"]:
        r.problems.append("C4 no fault trace")
    for path in traces:
        tr = yaml.safe_load(path.read_text(encoding="utf-8"))
        exp = tr["expect"]
        release_ok, got = replay(prof, tr)
        problem = ""
        if path.stem == "correct" and (exp.get("violations") or not exp["release_ok"]):
            problem = "C3 the correct trace must expect a clean release"
        elif path.stem != "correct" and exp["release_ok"]:
            problem = "C4 a fault trace must expect the release gate to refuse"
        elif release_ok != exp["release_ok"]:
            problem = f"C5 release gate returned {release_ok}, expected {exp['release_ok']}"
        else:
            problem = matches(exp.get("violations", []), got)
            problem = f"C5 {problem}" if problem else ""
        r.traces.append(TraceResult(path.stem, not problem, release_ok, got, problem))
    return r


def sample_dirs(names=(), template=False):
    dirs = sorted(d for d in HERE.iterdir() if (d / "profile.yaml").exists())
    if not template:
        dirs = [d for d in dirs if not d.name.startswith("_")]
    if names:
        dirs = [d for d in dirs if d.name in names]
    return dirs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("samples", nargs="*")
    ap.add_argument("--template", action="store_true", help="include samples/_template")
    a = ap.parse_args(argv)
    reports = [check(d) for d in sample_dirs(a.samples, a.template)]
    print(f"{'sample':24} {'IPs':>4} {'min px':>7}  {'traces':>7}  derived order")
    for r in reports:
        passed = sum(t.ok for t in r.traces)
        print(f"{r.sample:24} {r.n_ips:>4} {r.min_px:>7.1f}  {passed:>3}/{len(r.traces):<3}  "
              f"{', '.join(r.patterns) or '—'}  {'PASS' if r.ok else 'FAIL'}")
        for p in r.problems:
            print(f"    {p}")
        for t in r.traces:
            if not t.ok:
                print(f"    {t.name}: {t.problem}")
    covered = set().union(*(r.kinds_covered for r in reports)) if reports else set()
    missing = [k for k in VIOLATION_KINDS if k not in covered]
    print(f"\nviolation kinds exercised: {len(covered)}/{len(VIOLATION_KINDS)}"
          + (f" — not exercised: {', '.join(missing)}" if missing else ""))
    return 0 if reports and all(r.ok for r in reports) else 1


if __name__ == "__main__":
    sys.exit(main())
