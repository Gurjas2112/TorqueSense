"""Can this camera see this inspection point well enough? Pinhole model.
The compiler runs this for every inspection point and refuses to deploy a profile with a
point that no camera resolves."""
from __future__ import annotations
import math
from dataclasses import dataclass

@dataclass
class Camera:
    id: str
    width_px: int          # processing width, after any downscale
    hfov_deg: float        # horizontal field of view of the lens at that width
    position_mm: tuple     # in the assembly frame
    look_at_mm: tuple
    aspect: float = 16 / 9

    @property
    def f_px(self) -> float:
        return (self.width_px / 2) / math.tan(math.radians(self.hfov_deg) / 2)

@dataclass
class Footprint:
    camera: str
    distance_mm: float
    obliquity_deg: float
    feature_px: float
    visible: bool

def _sub(a, b): return [x - y for x, y in zip(a, b)]
def _dot(a, b): return sum(x * y for x, y in zip(a, b))
def _cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def _unit(a):
    n = _dot(a, a) ** 0.5
    return [x / n for x in a]

def in_frame(cam: Camera, p) -> bool:
    """Inside the image rectangle, from the camera's pose, horizontal FOV and aspect ratio."""
    f = _unit(_sub(cam.look_at_mm, cam.position_mm))
    up_hint = [0, 0, 1] if abs(f[2]) < 0.99 else [0, 1, 0]
    r = _unit(_cross(f, up_hint)); u = _cross(r, f)
    d = _sub(p, cam.position_mm); z = _dot(d, f)
    if z <= 0:
        return False
    th = math.tan(math.radians(cam.hfov_deg) / 2)
    return abs(_dot(d, r) / z) <= th and abs(_dot(d, u) / z) <= th / cam.aspect

def footprint(cam: Camera, ip_pos, ip_normal, feature_mm: float, max_obliquity_deg=70.0) -> Footprint:
    """Projected size of the distinguishing feature, in pixels, with foreshortening.
    Occlusion by other parts is checked separately by ray-casting the CAD (not modelled here)."""
    v = [c - p for c, p in zip(cam.position_mm, ip_pos)]
    d = math.sqrt(sum(x * x for x in v))
    n = math.sqrt(sum(x * x for x in ip_normal))
    cos_t = sum(a * b for a, b in zip(v, ip_normal)) / (d * n)
    obliq = math.degrees(math.acos(max(-1.0, min(1.0, cos_t))))
    facing = obliq < max_obliquity_deg and in_frame(cam, ip_pos)
    px = cam.f_px * feature_mm / d * max(0.0, cos_t) if facing else 0.0
    return Footprint(cam.id, d, obliq, px, facing)

def best(cams, ip_pos, ip_normal, feature_mm):
    return max((footprint(c, ip_pos, ip_normal, feature_mm) for c in cams), key=lambda f: f.feature_px)
