"""Schematic frames: a drawn stand-in for camera footage and BlenderProc renders, until those exist.

    python synth/schematic.py samples/wheel-hub-5lug          # data/scenarios/<scenario>.mp4 (the demo feed)
    python synth/sample_media.py                              # samples/*/media/<trace>.mp4 + keyframe PNGs

Every frame is drawn from a per-frame fixture (the same one ReplayDetector replays): each camera of
the profile gets a tile, and every inspection point is drawn at its CAD position projected through its
camera (the engine's own projection), in a colour that encodes its rank in its ordered state family:

    rank 0           a ring, empty inside          (absent, open, unmated, unseated …)
    middle ranks     a grey disc                   (seated …)
    the last rank    a yellow disc                 (fastened — the witness mark; closed, mated, applied …)
    occluded         a glove over the point        (no answer this frame)

The engine's SchematicDetector (perception/src/SchematicDetector.cpp) reads those colours back from the
pixels with the same layout, so a schematic video can be verified frame by frame through the real
debounce, committer and interpreter. It is a drawing of the fixture: no accuracy claim is ever made
from it, and every frame says so. This module knows no assembly.
"""
from __future__ import annotations
import argparse, json, math, pathlib, sys

import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "detections"
W, H = 960, 540

# BGR. Keep in step with perception/src/SchematicDetector.cpp.
BG = (23, 17, 15)
TILE = (44, 35, 31)
RING = (110, 100, 95)
METAL = (170, 166, 160)
MARK = (36, 190, 245)
GLOVE = (60, 90, 150)
TEXT = (237, 234, 232)
MUTED = (166, 160, 154)


def project(procedure: dict) -> dict[str, tuple[float, float]]:
    """CAD position -> full-resolution pixel of the point's own camera (the engine's projection)."""
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
        out[ip["id"]] = (c["width_px"] / 2 + c["f_px"] * (d @ r) / z, c["width_px"] / c["aspect"] / 2 - c["f_px"] * (d @ u) / z)
    return out


def layout(procedure: dict) -> tuple[dict[str, tuple[int, int]], int, list[tuple[int, int, int, int, str]]]:
    """Each camera a tile, side by side across the 960 x 540 frame; every point inside its camera's tile.
    Returns ({ip: (x, y)}, marker radius, [(x0, y0, w, h, camera id)]). Mirrored in the engine."""
    cams = procedure["cameras"]
    k = len(cams)
    tile_w = W / k
    tiles, origin = [], {}
    for i, c in enumerate(cams):
        s = tile_w / c["width_px"]
        th = c["width_px"] / c["aspect"] * s
        x0, y0 = i * tile_w, (H - th) / 2
        tiles.append((int(round(x0)), int(round(y0)), int(round(tile_w)), int(round(th)), c["id"]))
        origin[c["id"]] = (x0, y0, s)
    uv = project(procedure)
    radius = max(5, int(9 / math.sqrt(k)))
    pts = {}
    for ip in procedure["inspection_points"]:
        x0, y0, s = origin[ip["camera"]]
        u, v = uv[ip["id"]]
        x, y = int(math.floor(x0 + u * s + 0.5)), int(math.floor(y0 + v * s + 0.5))
        # two points at one position (a part and the screw at its centre) would share a marker: nudge the later down
        while any((x - px) ** 2 + (y - py) ** 2 < (2 * radius + 4) ** 2 for px, py in pts.values()):
            y += 2 * radius + 6
        pts[ip["id"]] = (x, y)
    return pts, radius, tiles


def draw_frame(procedure: dict, fixture: dict, held: dict, f: int, glove_at: tuple[int, int] | None = None) -> np.ndarray:
    pts, r, tiles = layout(procedure)
    img = np.full((H, W, 3), BG, np.uint8)
    for x0, y0, w, h, cid in tiles:
        cv2.rectangle(img, (x0 + 2, y0), (x0 + w - 3, y0 + h), TILE, -1)
        cv2.putText(img, cid, (x0 + 8, y0 + h + 18), cv2.FONT_HERSHEY_SIMPLEX, 0.45, MUTED, 1, cv2.LINE_AA)   # below: never over a point
    for i, ip in enumerate(fixture["ips"]):
        x, y = pts[ip]
        n = len(fixture["states"][i])
        rank, _conf, flags = held[i]
        if rank <= 0:
            cv2.circle(img, (x, y), r, RING, 2, cv2.LINE_AA)
        elif rank >= n - 1:
            cv2.circle(img, (x, y), r, MARK, -1, cv2.LINE_AA)
        else:
            cv2.circle(img, (x, y), r, METAL, -1, cv2.LINE_AA)
        if r >= 7:
            cv2.putText(img, ip.rsplit("-", 1)[-1], (x - 4, y + r + 13), cv2.FONT_HERSHEY_SIMPLEX, 0.38, MUTED, 1, cv2.LINE_AA)
        if flags & 2:                                                      # a glove over the point: no answer
            cv2.ellipse(img, (x + 2, y + 1), (r + 10, r + 6), 25, 0, 360, GLOVE, -1, cv2.LINE_AA)
    if glove_at is not None:                                               # a hand working beside a point
        cv2.ellipse(img, glove_at, (16, 11), 30, 0, 360, GLOVE, -1, cv2.LINE_AA)
    t = f / fixture["fps"]
    cv2.putText(img, f"SCHEMATIC  {fixture['title']}"[:80], (12, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.5, TEXT, 1, cv2.LINE_AA)
    cv2.putText(img, f"{fixture['profile']}  t={t:6.2f}s  frame {f}", (12, H - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.45, MUTED, 1, cv2.LINE_AA)
    cv2.putText(img, "a drawing of the trace - not a camera image", (W - 360, H - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.4, MUTED, 1, cv2.LINE_AA)
    return img


def clear_spot(x: int, y: int, r: int, points) -> tuple[int, int] | None:
    """A place for a hand near (x, y) that covers no inspection point (the glove is 16 x 11 px)."""
    for d in (3 * r + 18, 4 * r + 26, 6 * r + 34):
        for k in range(16):
            a = 2 * math.pi * k / 16 + 0.4
            gx, gy = int(x + d * math.cos(a)), int(y + d * math.sin(a))
            if 20 < gx < W - 20 and 30 < gy < H - 30 and all(math.dist((gx, gy), p) > r + 22 for p in points):
                return gx, gy
    return None


def frames(procedure: dict, fixture: dict, hands: bool = True):
    """Yields (frame index, image, held states, changed point indices) for the whole fixture."""
    keys = sorted(fixture["keyframes"], key=lambda k: k["frame"])
    pts, r, _ = layout(procedure)
    held = {i: (0, 1.0, 0) for i in range(len(fixture["ips"]))}
    changes = [k for k in keys if k["frame"] > 0]
    k = 0
    for f in range(fixture["n_frames"]):
        changed = []
        while k < len(keys) and keys[k]["frame"] <= f:
            kk = keys[k]
            if held[kk["ip"]][0] != kk["rank"]:
                changed.append(kk["ip"])
            held[kk["ip"]] = (kk["rank"], kk["conf"], kk["flags"]); k += 1
        glove = None
        if hands:   # beside the next point to change, for the half second before it changes — never on it
            soon = next((c for c in changes if 0 <= c["frame"] - f < 15), None)
            if soon is not None:
                x, y = pts[fixture["ips"][soon["ip"]]]
                glove = clear_spot(x, y, r, pts.values())
        yield f, draw_frame(procedure, fixture, held, f, glove), dict(held), changed


def write_video(procedure: dict, fixture: dict, path: pathlib.Path, hands: bool = True) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), fixture["fps"], (W, H))
    n = 0
    for _, img, _, _ in frames(procedure, fixture, hands):
        vw.write(img)
        n += 1
    vw.release()
    return n


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sample", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "data" / "scenarios")
    a = ap.parse_args(argv)
    import yaml
    procedure = json.loads((a.sample / "procedure.json").read_text(encoding="utf-8"))
    spec = yaml.safe_load((a.sample / "scenarios.yaml").read_text(encoding="utf-8"))
    for name in spec["scenarios"]:
        fx = json.loads((FIXTURES / f"{name}.json").read_text(encoding="utf-8"))
        p = a.out / f"{name}.mp4"
        write_video(procedure, fx, p, hands=False)
        print(f"wrote {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
