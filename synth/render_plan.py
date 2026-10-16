"""Scenario timelines -> per-frame render plans, and rendered frames -> scenario videos (spec Sections 9, 21).

    python synth/render_plan.py samples/wheel-hub-5lug                 # data/render_plans/wheel-hub-5lug.json
    blenderproc run synth/generate.py --gltf cad/export/wheel_assembly.glb --seed 7 \
        --plan data/render_plans/wheel-hub-5lug.json                    # frames into data/synth_scenarios/
    python synth/render_plan.py samples/wheel-hub-5lug --video          # data/scenarios_rendered/<name>.mp4

The plan is the same timeline synth/scenarios.py turns into replay fixtures: a state is first visible
one frame before it commits (debounce), the cycle runs to one second past the release request, and
the hands windows and occlusions are the scenario's own. What a camera adds, the plan places in the
scene: the operator's hand moves between bolts outside the bolt circle (it covers a bolt only where
the scenario says `occlude`), and the nutrunner sits on a bolt for the 600 ms before each nutrunner
result, then lifts. A detector's single-frame flicker and ambiguous confidence are properties of a
detector, not of a scene, so they are not rendered: the rendered video tests the real classifier's
own noise instead.

This module knows no assembly: positions come from the compiled profile, events from the sample.
"""
from __future__ import annotations
import argparse, json, math, pathlib, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "synth"))
sys.path.insert(0, str(ROOT / "samples"))
import scenarios                      # noqa: E402
import run_samples                    # noqa: E402

PLANS = ROOT / "data" / "render_plans"
FRAMES = ROOT / "data" / "synth_scenarios"
VIDEOS = ROOT / "data" / "scenarios_rendered"
TOOL_LEAD_MS = 600                    # the socket is on the bolt for this long before the result
HAND_OUT_M = 0.075                    # the working hand's palm sits this far outside the bolt it works
HAND_H_M = 0.06
MOVE_FRAMES = 8                       # frames to move the hand from one bolt to the next


def _lerp(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(len(a))]


def plan_scenario(name: str, scn: dict, sample: pathlib.Path, prof: dict, fps: float) -> dict:
    events, release_at = scenarios.scenario_events(sample, scn)
    ips = [ip["id"] for ip in prof["inspection_points"]]
    states = {ip["id"]: ip["states"] for ip in prof["inspection_points"]}
    pos = {ip["id"]: [c / 1000.0 for c in ip["position_mm"]] for ip in prof["inspection_points"]}
    n_frames = scenarios.frame_of(release_at, fps) + int(fps)
    first_visible = lambda t: scenarios.frame_of(t, fps) - (scenarios.DEBOUNCE_FRAMES - 1)   # noqa: E731

    changes = sorted((first_visible(e["t"]), ips.index(e["ip"]), states[e["ip"]].index(e["state"]))
                     for e in events if "ip" in e)
    ranks = [0] * len(ips)
    ranks_at = []
    k = 0
    for f in range(n_frames):
        while k < len(changes) and changes[k][0] <= f:
            ranks[changes[k][1]] = changes[k][2]
            k += 1
        ranks_at.append(list(ranks))

    # where the work is: the point of the next state change, else the last one
    work = sorted((first_visible(e["t"]), e["ip"]) for e in events if "ip" in e)

    def target(f):
        nxt = [ip for g, ip in work if g >= f]
        prev = [ip for g, ip in work if g < f]
        return nxt[0] if nxt else (prev[-1] if prev else ips[0])

    def outside(ip, side=1.0):
        x, y, z = pos[ip]
        r = math.hypot(x, y) or 1.0
        ux, uy = x / r, y / r
        return [x + side * ux * HAND_OUT_M, y + side * uy * HAND_OUT_M, z], math.atan2(uy, ux) + math.pi / 2

    hands_win = [(scenarios.frame_of(h["from"], fps), scenarios.frame_of(h["to"], fps), h["count"]) for h in scn.get("hands", [])]
    occl = [(scenarios.frame_of(o["from"], fps), scenarios.frame_of(o["to"], fps), o["ip"]) for o in scn.get("occlude", [])]
    tools = []
    for e in events:
        if "tag" in e and e.get("value") is True:
            nxt = [x for x in events if "ip" in x and x["t"] > e["t"]]
            ip = min(nxt, key=lambda x: x["t"])["ip"] if nxt else target(scenarios.frame_of(e["t"], fps))
            tools.append((scenarios.frame_of(e["t"] - TOOL_LEAD_MS, fps), scenarios.frame_of(e["t"], fps), ip))

    frames = []
    prev_at, prev_tgt, moved = None, None, 0
    for f in range(n_frames):
        count = max([c for a, b, c in hands_win if a <= f < b], default=0)
        hands = []
        if count:
            tgt = target(f)
            at, yaw = outside(tgt)
            if prev_tgt is not None and tgt != prev_tgt:
                moved = 0
            if prev_at is not None and moved < MOVE_FRAMES:
                at = _lerp(prev_at, at, (moved + 1) / MOVE_FRAMES)
                moved += 1
            prev_at, prev_tgt = at, tgt
            hands.append({"at": [round(v, 5) for v in at], "yaw": round(yaw, 4), "height": HAND_H_M})
            if count > 1:                               # a second person: idle, on the far side
                far, fyaw = outside(ips[(ips.index(tgt) + len(ips) // 2) % len(ips)], 2.2)
                hands.append({"at": [round(v, 5) for v in far], "yaw": round(fyaw, 4), "height": 0.09, "roll": 0.2})
        for a, b, ip in occl:                           # a glove flat over the point: no state this frame
            if a <= f < b:
                x, y, z = pos[ip]
                g = {"at": [round(x - 0.01, 5), round(y, 5), z], "yaw": 0.0, "height": 0.035}
                hands = [g] + hands[1:] if hands else [g]
        tool = None
        for a, b, ip in tools:
            if a <= f < b:
                tool = {"at": pos[ip], "yaw": 0.4}
            elif b <= f < b + int(fps // 3):            # lifted, still in view, for a third of a second
                at, _ = outside(ip, 1.8)
                tool = {"at": at, "yaw": 0.4, "lift": 0.12}
        frames.append({"ranks": ranks_at[f], "hands": hands, "tool": tool})
    return {"name": name, "title": scn.get("title", name), "n_frames": n_frames, "frames": frames}


def build_plan(sample: pathlib.Path) -> dict:
    spec = scenarios.load(sample)
    fps = float(spec.get("fps", 30))
    prof, _, _ = run_samples.compile_folder(sample)
    return {"sample": sample.name, "fps": fps, "ips": [ip["id"] for ip in prof["inspection_points"]],
            "scenarios": [plan_scenario(n, s, sample, prof, fps) for n, s in spec["scenarios"].items()]}


def make_videos(plan: dict, frames_root: pathlib.Path, out: pathlib.Path) -> list[str]:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg is not on PATH")
    out.mkdir(parents=True, exist_ok=True)
    made = []
    for sc in plan["scenarios"]:
        d = frames_root / sc["name"]
        if not (d / "done.json").exists():
            print(f"skip {sc['name']}: not rendered")
            continue
        dst = out / f"{sc['name']}.mp4"
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(int(plan["fps"])), "-i", str(d / "%05d.jpg"),
                        "-c:v", "libx264", "-crf", "16", "-preset", "medium", "-pix_fmt", "yuv420p", str(dst)], check=True)
        shutil.copy(d / "truth.jsonl", out / f"{sc['name']}.truth.jsonl")
        made.append(dst.name)
    return made


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sample", type=pathlib.Path)
    ap.add_argument("--video", action="store_true", help="assemble rendered frames into scenario videos")
    a = ap.parse_args(argv)
    sample = a.sample.resolve()
    plan = build_plan(sample)
    if a.video:
        made = make_videos(plan, FRAMES, VIDEOS)
        print(f"{len(made)} videos in {VIDEOS.relative_to(ROOT)}: {', '.join(made)}")
        return 0
    PLANS.mkdir(parents=True, exist_ok=True)
    dst = PLANS / f"{sample.name}.json"
    dst.write_text(json.dumps(plan) + "\n", encoding="utf-8")
    total = sum(s["n_frames"] for s in plan["scenarios"])
    print(f"{dst.relative_to(ROOT)}: {len(plan['scenarios'])} scenarios, {total} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
