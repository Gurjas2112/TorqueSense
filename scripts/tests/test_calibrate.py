"""calibrate.py on synthetic views of the printed board, seen through a known pinhole camera: it must
recover the focal length and pass its own 0.5 px gate, and refuse too few poses."""
import math, pathlib, sys

import pytest

cv2 = pytest.importorskip("cv2")
np = pytest.importorskip("numpy")
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import calibrate   # noqa: E402

W, H, F = 1280, 720, 900.0


def views(n):
    b = calibrate.board()
    tex = b.generateImage((1400, 1000), marginSize=0)                 # 7 x 5 squares, 200 px per square
    m_per_px = calibrate.SQUARE_M / 200
    K = np.array([[F, 0, W / 2], [0, F, H / 2], [0, 0, 1]])
    out = []
    for i in range(n):
        rx, ry = math.radians(-25 + 50 * (i % 5) / 4), math.radians(-20 + 40 * (i // 5) / 3)
        R, _ = cv2.Rodrigues(np.array([rx, ry, 0.1 * (i % 3)]))
        t = np.array([[-0.10 + 0.01 * (i % 4)], [-0.07], [0.45 + 0.03 * (i % 3)]])
        # board plane (metres) -> image: homography K [r1 r2 t] scaled from texture pixels
        Hm = K @ np.hstack([R[:, :2], t]) @ np.diag([m_per_px, m_per_px, 1])
        img = cv2.warpPerspective(tex, Hm, (W, H), borderValue=255)
        out.append(cv2.cvtColor(img, cv2.COLOR_GRAY2BGR))
    return out


def test_recovers_the_focal_length_and_passes_the_gate():
    r = calibrate.calibrate(views(20))
    assert r["poses"] >= 15
    assert r["rms_px"] <= calibrate.MAX_RMS_PX
    assert abs(r["fx"] - F) / F < 0.02 and abs(r["fy"] - F) / F < 0.02
    assert abs(r["hfov_deg"] - math.degrees(2 * math.atan(W / 2 / F))) < 1.0


def test_refuses_too_few_poses():
    with pytest.raises(SystemExit):
        calibrate.calibrate(views(3))
