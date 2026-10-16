"""Camera intrinsics from the printed ChArUco board (spec Section 8, "printed, not bought").

    python scripts\\calibrate.py --camera phone --url http://192.168.43.1:8080/video --size 1920x1080 \\
        --sample samples\\desk-cover-4screw            # live: shows the view, SPACE keeps a pose, 20 poses
    python scripts\\calibrate.py --camera webcam --device 0 --size 1280x720 --sample samples\\desk-harness-clips
    python scripts\\calibrate.py --camera phone --images path\\to\\poses --sample …   # offline, from saved frames

The board is samples/_print/charuco_7x5_30mm_a4.pdf printed at 100 %: 7 x 5 squares of 30 mm, 22 mm
markers, DICT_5X5_100 (so it can never be mistaken for a DICT_4X4_50 registration marker). A
reprojection error above 0.5 px RMS is refused: take more poses. On success it writes fx, fy, cx, cy,
the distortion and the read-back resolution into the sample's cameras.yaml, and the measured horizontal
field of view the compiler uses instead of the placeholder in profile.yaml.
"""
from __future__ import annotations
import argparse, math, pathlib, sys

import cv2
import numpy as np
import yaml

SQUARES, SQUARE_M, MARKER_M = (7, 5), 0.030, 0.022
MAX_RMS_PX = 0.5


def board():
    d = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_5X5_100)
    return cv2.aruco.CharucoBoard(SQUARES, SQUARE_M, MARKER_M, d)


def detect(gray, b):
    det = cv2.aruco.CharucoDetector(b)
    corners, ids, _, _ = det.detectBoard(gray)
    return corners, ids


def calibrate(frames: list[np.ndarray], min_corners: int = 8) -> dict:
    b = board()
    obj_pts, img_pts, used = [], [], 0
    size = None
    for f in frames:
        gray = cv2.cvtColor(f, cv2.COLOR_BGR2GRAY) if f.ndim == 3 else f
        size = gray.shape[::-1]
        corners, ids = detect(gray, b)
        if ids is None or len(ids) < min_corners:
            continue
        o, i = b.matchImagePoints(corners, ids)
        obj_pts.append(o)
        img_pts.append(i)
        used += 1
    if used < 5:
        raise SystemExit(f"only {used} usable poses: show the whole board, tilted, at several distances")
    rms, K, dist, _, _ = cv2.calibrateCamera(obj_pts, img_pts, size, None, None)
    w, h = size
    return {"rms_px": float(rms), "poses": used, "width": w, "height": h, "fx": float(K[0, 0]), "fy": float(K[1, 1]),
            "cx": float(K[0, 2]), "cy": float(K[1, 2]), "dist": [float(x) for x in dist.ravel()],
            "hfov_deg": math.degrees(2 * math.atan(w / 2 / K[0, 0]))}


def capture_live(args) -> list[np.ndarray]:
    cap = cv2.VideoCapture(args.url, cv2.CAP_FFMPEG) if args.url else cv2.VideoCapture(args.device, cv2.CAP_DSHOW)
    w, h = (int(x) for x in args.size.split("x"))
    if not args.url:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    ok, f = cap.read()
    if not ok:
        raise SystemExit("the camera did not deliver a frame (privacy setting? hotspot? URL?)")
    if (f.shape[1], f.shape[0]) != (w, h):
        raise SystemExit(f"asked for {w}x{h}, the camera delivers {f.shape[1]}x{f.shape[0]}: fix the source first (spec Section 8)")
    keep, b = [], board()
    while len(keep) < args.poses:
        ok, f = cap.read()
        if not ok:
            break
        view = f.copy()
        corners, ids = detect(cv2.cvtColor(f, cv2.COLOR_BGR2GRAY), b)
        if ids is not None:
            cv2.aruco.drawDetectedCornersCharuco(view, corners, ids)
        cv2.putText(view, f"poses {len(keep)}/{args.poses}  SPACE keep  Q quit", (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        cv2.imshow("calibrate", view)
        k = cv2.waitKey(1) & 0xFF
        if k == ord(" ") and ids is not None and len(ids) >= 8:
            keep.append(f)
        elif k == ord("q"):
            break
    cv2.destroyAllWindows()
    return keep


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--camera", required=True, help="the camera id in the sample's profile (cameras.yaml key)")
    ap.add_argument("--sample", type=pathlib.Path, required=True)
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument("--url")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--images", type=pathlib.Path, help="calibrate from saved frames instead of a live camera")
    ap.add_argument("--poses", type=int, default=20)
    a = ap.parse_args(argv)
    frames = ([cv2.imread(str(p)) for p in sorted(a.images.glob("*.png")) + sorted(a.images.glob("*.jpg"))]
              if a.images else capture_live(a))
    r = calibrate(frames)
    print(f"{a.camera}: {r['width']}x{r['height']}, {r['poses']} poses, RMS {r['rms_px']:.3f} px, "
          f"fx {r['fx']:.1f} fy {r['fy']:.1f}, measured hfov {r['hfov_deg']:.2f} deg")
    if r["rms_px"] > MAX_RMS_PX:
        print(f"REFUSED: RMS {r['rms_px']:.3f} px > {MAX_RMS_PX} px — take more poses, tilt the board, check focus")
        return 1
    out = a.sample / "cameras.yaml"
    doc = yaml.safe_load(out.read_text(encoding="utf-8")) if out.exists() else {}
    doc = doc or {}
    doc.setdefault("cameras", {})[a.camera] = {k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()}
    out.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
    print(f"wrote {out} — re-run the compiler: the measured field of view replaces the placeholder")
    return 0


if __name__ == "__main__":
    sys.exit(main())
