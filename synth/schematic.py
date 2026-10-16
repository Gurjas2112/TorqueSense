"""Schematic scenario videos: a stand-in for the BlenderProc renders until the CAD exists.

    python synth/schematic.py samples/wheel-hub-5lug             # -> data/scenarios/<scenario>.mp4

Each frame is drawn from the same per-frame fixture the ReplayDetector replays
(tests/fixtures/detections/<scenario>.json): the part's outline, every inspection point at its
CAD position projected through the profile's camera, drawn in its state — and hands where the
fixture has them. It gives FileSource real frames to play, the clip ring real JPEGs and the
dashboard a live feed. Every frame is stamped "SCHEMATIC REPLAY": it is a drawing of the fixture,
not a camera image and not a render, and no accuracy claim is ever made from it.

This module knows no assembly: shapes come from each point's state family.
"""
from __future__ import annotations
import argparse, json, math, pathlib, sys

import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "detections"
W, H = 960, 540

BG = (23, 17, 15)            # --bg #0F1115 in BGR
PART = (44, 35, 31)
LINE = (58, 47, 42)
METAL = (170, 166, 160)
MARK = (36, 165, 245)        # the yellow witness mark (BGR)
GLOVE = (60, 90, 150)
TEXT = (237, 234, 232)
MUTED = (166, 160, 154)


def project(procedure: dict) -> dict[str, tuple[float, float]]:
    """CAD position -> pixel, through the profile's camera, scaled to 960 px wide (the engine's projection)."""
    out = {}
    cams = {c["id"]: c for c in procedure["cameras"]}
    for ip in procedure["inspection_points"]:
        c = cams[ip["camera"]]
        P, L = np.array(c["position_mm"], float), np.array(c["look_at_mm"], float)
        f = (L - P) / np.linalg.norm(L - P)
        up = np.array([0, 0, 1.0]) if abs(f[2]) < 0.99 else np.array([0, 1.0, 0])
        r = np.cross(f, up); r /= np.linalg.norm(r)
        u = np.cross(r, f)
        d = np.array(ip["position_mm"], float) - P
        z = d @ f
        width = c["width_px"]; height = width / c["aspect"]
        px = width / 2 + c["f_px"] * (d @ r) / z
        py = height / 2 - c["f_px"] * (d @ u) / z
        s = W / width
        out[ip["id"]] = (px * s, py * s)
    return out


def draw_point(img, x, y, family, state, states, conf, size):
    rank = states.index(state) if state in states else 0
    top = rank == len(states) - 1
    if family == "threaded_fastener":
        if rank == 0:
            cv2.circle(img, (x, y), size, LINE, 2, cv2.LINE_AA)                     # empty hole
        else:
            cv2.circle(img, (x, y), size, METAL, -1, cv2.LINE_AA)                   # bolt head
            cv2.circle(img, (x, y), size, (120, 116, 112), 2, cv2.LINE_AA)
            if top:                                                                  # torqued: witness mark
                cv2.line(img, (x - size, y + size // 3), (x + size, y - size // 3), MARK, 4, cv2.LINE_AA)
    else:
        color = MARK if top else (METAL if rank > 0 else LINE)
        cv2.rectangle(img, (x - size, y - size // 2), (x + size, y + size // 2), color, -1 if rank > 0 else 2)


def render(sample: pathlib.Path, scenario: str, out_dir: pathlib.Path) -> pathlib.Path:
    fx = json.loads((FIXTURES / f"{scenario}.json").read_text(encoding="utf-8"))
    procedure = json.loads((sample / "procedure.json").read_text(encoding="utf-8"))
    pos = project(procedure)
    ips = fx["ips"]
    fam = {ip["id"]: ip["family"] for ip in procedure["inspection_points"]}
    cx = sum(pos[i][0] for i in ips) / len(ips)
    cy = sum(pos[i][1] for i in ips) / len(ips)
    radius = max(math.dist((cx, cy), pos[i]) for i in ips) + 45
    size = 14
    keys = sorted(fx["keyframes"], key=lambda k: k["frame"])
    held = {i: (0, 1.0, 0) for i in range(len(ips))}
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{scenario}.mp4"
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), fx["fps"], (W, H))
    k = 0
    for f in range(fx["n_frames"]):
        while k < len(keys) and keys[k]["frame"] <= f:
            kk = keys[k]; held[kk["ip"]] = (kk["rank"], kk["conf"], kk["flags"]); k += 1
        img = np.full((H, W, 3), BG, np.uint8)
        cv2.circle(img, (int(cx), int(cy)), int(radius), PART, -1, cv2.LINE_AA)        # the part, from above
        cv2.circle(img, (int(cx), int(cy)), int(radius), LINE, 2, cv2.LINE_AA)
        cv2.circle(img, (int(cx), int(cy)), int(radius * 0.35), LINE, 2, cv2.LINE_AA)
        for i, ip in enumerate(ips):
            x, y = int(pos[ip][0]), int(pos[ip][1])
            rank, conf, flags = held[i]
            states = fx["states"][i]
            draw_point(img, x, y, fam[ip], states[max(rank, 0)], states, conf, size)
            cv2.putText(img, ip.rsplit("-", 1)[-1], (x - 5, y + size + 16), cv2.FONT_HERSHEY_SIMPLEX, 0.45, MUTED, 1, cv2.LINE_AA)
            if flags & 2:                                                            # a glove over the point
                cv2.ellipse(img, (x + 6, y + 4), (34, 24), 25, 0, 360, GLOVE, -1, cv2.LINE_AA)
        n_hands = max((h["count"] for h in fx["hands"] if h["from_frame"] <= f < h["to_frame"]), default=0)
        for h in range(n_hands):                                                     # hands working the part
            a = f / fx["fps"] * (1.3 + 0.4 * h) + h * 2.1
            hx, hy = int(cx + (radius + 40) * math.cos(a)), int(cy + (radius * 0.55) * math.sin(a))
            cv2.ellipse(img, (hx, hy), (40, 26), math.degrees(a), 0, 360, GLOVE, -1, cv2.LINE_AA)
            if h == 0:
                cv2.line(img, (hx, hy), (hx + int(60 * math.cos(a + 1.2)), hy + int(60 * math.sin(a + 1.2))), (90, 90, 90), 8)
        t = f / fx["fps"]
        cv2.putText(img, f"SCHEMATIC REPLAY  {fx['title']}", (16, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.55, TEXT, 1, cv2.LINE_AA)
        cv2.putText(img, f"{fx['profile']}  t={t:5.2f}s  frame {f}", (16, H - 16), cv2.FONT_HERSHEY_SIMPLEX, 0.5, MUTED, 1, cv2.LINE_AA)
        cv2.putText(img, "a drawing of the replay fixture - not a camera image", (W - 420, H - 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.42, MUTED, 1, cv2.LINE_AA)
        vw.write(img)
    vw.release()
    return path


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sample", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "data" / "scenarios")
    a = ap.parse_args(argv)
    spec = __import__("yaml").safe_load((a.sample / "scenarios.yaml").read_text(encoding="utf-8"))
    for name in spec["scenarios"]:
        p = render(a.sample, name, a.out)
        print(f"wrote {p.relative_to(ROOT) if p.is_relative_to(ROOT) else p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
