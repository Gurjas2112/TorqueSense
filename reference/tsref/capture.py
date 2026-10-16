"""Commissioning arithmetic for a camera: which stock lens, and will the link carry it.
Both are run by the compiler when a profile names a physical camera model."""
from __future__ import annotations
import math
from dataclasses import dataclass

STOCK_FOCAL_MM = (4, 6, 8, 12, 16, 25, 35, 50)          # common C-mount machine-vision focal lengths

@dataclass(frozen=True)
class Sensor:
    name: str
    width_px: int
    height_px: int
    pitch_um: float
    max_fps: float

# Basler ace 2 Basic a2A1920-51gcBAS: Sony IMX392, global shutter, GigE PoE, C-mount
A2A1920_51GC = Sensor("a2A1920-51gcBAS", 1920, 1200, 3.45, 51)

def lens_for(sensor: Sensor, distance_mm: float, field_width_mm: float,
             feature_mm: float, min_feature_px: float = 16):
    """A stock focal length must be SHORT enough to cover the field and LONG enough to
    resolve the smallest feature. Returns (focal_mm, feature_px, field_mm) for the longest
    stock lens in that window, or None when no stock lens satisfies both."""
    sensor_w_mm = sensor.width_px * sensor.pitch_um / 1000
    f_cover_max = sensor_w_mm * distance_mm / field_width_mm
    f_res_min = min_feature_px * sensor.pitch_um / 1000 * distance_mm / feature_mm
    fits = [f for f in STOCK_FOCAL_MM if f_res_min <= f <= f_cover_max]
    if not fits:
        return None
    f = max(fits)                                          # longest that still covers: most pixels per mm
    feature_px = f / (sensor.pitch_um / 1000) * feature_mm / distance_mm
    field_mm = sensor_w_mm * distance_mm / f
    return f, round(feature_px, 1), round(field_mm)

def hfov_deg(sensor: Sensor, focal_mm: float) -> float:
    return math.degrees(2 * math.atan(sensor.width_px * sensor.pitch_um / 1000 / 2 / focal_mm))

BYTES_PER_PIXEL = {"BayerRG8": 1.0, "Mono8": 1.0, "YCbCr422_8": 2.0, "RGB8": 3.0, "BayerRG12p": 1.5}

def link_mbps(sensor: Sensor, fps: float, pixel_format: str, overhead: float = 0.05) -> float:
    """Payload bandwidth on the wire, with a few percent for GigE Vision packet headers."""
    return sensor.width_px * sensor.height_px * BYTES_PER_PIXEL[pixel_format] * 8 * fps / 1e6 * (1 + overhead)

# --- fixed-lens cameras: a laptop webcam, a phone ------------------------------------------
# A fixed lens has no focal length to choose. What can be chosen is the resolution the source
# asks for and how far away the camera stands, so the arithmetic runs the other way round.

@dataclass(frozen=True)
class FixedCamera:
    name: str
    width_px: int          # the resolution the source REQUESTS and then verifies it got
    height_px: int
    hfov_deg: float        # placeholder until calibration; afterwards f_px comes from fx

    @property
    def f_px(self) -> float:
        return (self.width_px / 2) / math.tan(math.radians(self.hfov_deg) / 2)

    def at(self, width_px: int, height_px: int) -> "FixedCamera":
        """The same lens delivering a different resolution (same field of view)."""
        return FixedCamera(self.name, width_px, height_px, self.hfov_deg)

LAPTOP_WEBCAM_720P = FixedCamera("laptop webcam, 720p", 1280, 720, 70.0)
PHONE_1080P = FixedCamera("phone main camera, 1080p", 1920, 1080, 69.0)

def feature_px_at(cam: FixedCamera, distance_mm: float, feature_mm: float) -> float:
    return cam.f_px * feature_mm / distance_mm

def max_distance_mm(cam: FixedCamera, feature_mm: float, min_feature_px: float = 16) -> float:
    """The farthest this camera can stand and still put min_feature_px across the feature."""
    return cam.f_px * feature_mm / min_feature_px

def field_mm(cam: FixedCamera, distance_mm: float) -> tuple[float, float]:
    """Width and height of the area seen at that distance."""
    w = 2 * distance_mm * math.tan(math.radians(cam.hfov_deg) / 2)
    return round(w), round(w * cam.height_px / cam.width_px)
