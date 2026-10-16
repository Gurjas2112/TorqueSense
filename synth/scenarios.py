"""Scenario sequences -> per-frame perception fixtures and expected verdicts (spec Sections 21, 31).

    python synth/scenarios.py samples/wheel-hub-5lug            # writes tests/fixtures/detections/
    python synth/scenarios.py samples/wheel-hub-5lug --check    # exit 1 if the committed fixtures are stale

A scenario (samples/<x>/scenarios.yaml) is a trace's committed states plus what a camera adds:
hands, occlusion, a frame of flicker, confidence in the ambiguous band. This module knows no
assembly: everything comes from the sample folder.

Each scenario produces two files:
  <name>.json           the detector's output, frame by frame (keyframes only: an observation holds
                        until the next keyframe for that point) — what ReplayDetector replays;
  <name>.expected.json  the verdict the reference interpreter gives the scenario's committed states,
                        with the frame window each violation must fire in.

Timing. A trace's `t` is when a state is *committed*. The engine commits after two consecutive
frames (debounce), so the state first appears one frame earlier: evidence latency includes that
frame (spec Section 13).
"""
from __future__ import annotations
import argparse, json, math, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference"))
sys.path.insert(0, str(ROOT / "samples"))
import yaml                                   # noqa: E402
import run_samples                            # noqa: E402  (compile_folder, replay — the contract's own)

OUT = ROOT / "tests" / "fixtures" / "detections"
CONF_HIGH = 0.95                              # a clear answer: above the 0.65 commit floor
DEBOUNCE_FRAMES = 2


def frame_of(t_ms: float, fps: float) -> int:
    """The frame at which a state committed at t_ms is committed: the frame that ends at or after t."""
    return max(1, math.ceil(t_ms * fps / 1000.0 - 1e-9))


def load(sample: pathlib.Path) -> dict:
    return yaml.safe_load((sample / "scenarios.yaml").read_text(encoding="utf-8"))


def scenario_events(sample: pathlib.Path, scn: dict) -> tuple[list, int]:
    if "trace" in scn:
        tr = yaml.safe_load((sample / "traces" / f'{scn["trace"]}.yaml').read_text(encoding="utf-8"))
        return list(tr["events"]), int(scn.get("release_at", tr["release_at"]))
    return list(scn["events"]), int(scn["release_at"])


def build(sample: pathlib.Path, name: str, scn: dict, fps: float, prof: dict) -> tuple[dict, dict]:
    events, release_at = scenario_events(sample, scn)
    ips = [ip["id"] for ip in prof["inspection_points"]]
    states = {ip["id"]: ip["states"] for ip in prof["inspection_points"]}
    n_frames = frame_of(release_at, fps) + int(fps)            # one second past the release request
    frame_ms = 1000.0 / fps

    keyframes = []                                             # (frame, ip, rank, conf, flags)
    for ip in ips:                                             # every point starts in its first state
        keyframes.append((0, ip, 0, CONF_HIGH, 1))
    for e in events:
        if "ip" in e:
            f_commit = frame_of(e["t"], fps)
            keyframes.append((f_commit - (DEBOUNCE_FRAMES - 1), e["ip"], states[e["ip"]].index(e["state"]), CONF_HIGH, 1))

    # what the camera adds — none of it may change a committed state
    def state_at(ip: str, frame: int) -> int:
        r = 0
        for f, i, rank, _, _ in sorted(k for k in keyframes if k[1] == ip):
            if f <= frame:
                r = rank
        return r

    extra = []
    for o in scn.get("occlude", []):                           # no answer at all while covered
        a, b = frame_of(o["from"], fps), frame_of(o["to"], fps)
        extra.append((a, o["ip"], -1, 0.0, 2))
        extra.append((b, o["ip"], state_at(o["ip"], b), CONF_HIGH, 1))
    for fl in scn.get("flicker", []):                          # one wrong frame
        f = frame_of(fl["at"], fps)
        extra.append((f, fl["ip"], states[fl["ip"]].index(fl["state"]), CONF_HIGH, 1))
        extra.append((f + 1, fl["ip"], state_at(fl["ip"], f + 1), CONF_HIGH, 1))
    for am in scn.get("ambiguous", []):                        # the detector unsure: hold, ask Florence-2
        a, b = frame_of(am["from"], fps), frame_of(am["to"], fps)
        nxt = min(state_at(am["ip"], a) + 1, len(states[am["ip"]]) - 1)
        extra.append((a, am["ip"], nxt, float(am["conf"]), 1))
        extra.append((b, am["ip"], state_at(am["ip"], b), CONF_HIGH, 1))
    # occlusion/flicker/ambiguity end on the true state, so re-assert later keyframes after them
    keyframes = sorted(keyframes + extra, key=lambda k: (k[0], ips.index(k[1])))

    hands = []
    for h in scn.get("hands", []):
        hands.append({"from_frame": frame_of(h["from"], fps), "to_frame": frame_of(h["to"], fps), "count": h["count"]})
    tags = [{"t_ms": e["t"], "tag": e["tag"], "value": e["value"]} for e in events if "tag" in e]

    fixture = {
        "scenario": name, "title": scn.get("title", name), "profile": prof["profile"], "fps": fps,
        "frame_ms": frame_ms, "n_frames": n_frames, "release_at_ms": release_at, "ips": ips,
        "states": [states[i] for i in ips],
        "keyframes": [{"frame": f, "ip": ips.index(i), "rank": r, "conf": c, "flags": fl} for f, i, r, c, fl in keyframes],
        "hands": hands, "tags": tags, "anchor_residual_px": scn.get("anchor_residual_px", 1.2),
        "synthetic": True,
    }

    ok, got = run_samples.replay(prof, {"events": events, "release_at": release_at})
    tol = 2 * frame_ms                                          # commits land on frame boundaries
    expected = {
        "scenario": name, "release_ok": ok, "must_alert": not ok,
        "violations": [dict(v, window_ms=[max(0.0, v_t - tol), v_t + tol]) for v, v_t in
                       zip(got, _violation_times(prof, events, release_at))],
    }
    return fixture, expected


def _violation_times(prof: dict, events: list, release_at: int) -> list[int]:
    from tsref.procedure import Procedure
    p = Procedure(prof)
    p.start(0)
    for e in events:
        if "ip" in e:
            p.state(e["t"], e["ip"], e["state"])
        elif "tag" in e:
            p.tag(e["t"], e["tag"], e["value"])
        p.tick(e["t"])
    p.release(release_at)
    return [v.t_ms for v in p.violations]


def generate(sample: pathlib.Path) -> dict[str, str]:
    spec = load(sample)
    fps = float(spec.get("fps", 30))
    prof, _, _ = run_samples.compile_folder(sample)
    out = {}
    for name, scn in spec["scenarios"].items():
        fixture, expected = build(sample, name, scn, fps, prof)
        out[f"{name}.json"] = json.dumps(fixture, indent=1) + "\n"
        out[f"{name}.expected.json"] = json.dumps(expected, indent=1) + "\n"
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sample", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=OUT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    files = generate(a.sample.resolve())
    if a.check:
        stale = [n for n, text in files.items()
                 if not (a.out / n).exists() or (a.out / n).read_text(encoding="utf-8") != text]
        print("fixtures up to date" if not stale else f"stale: {stale}")
        return 1 if stale else 0
    a.out.mkdir(parents=True, exist_ok=True)
    for n, text in files.items():
        (a.out / n).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {len(files)} files to {a.out.relative_to(ROOT) if a.out.is_relative_to(ROOT) else a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
