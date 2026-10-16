"""The clip writer (spec Section 16): six seconds of the station's JPEG ring around a violation, as an
H.264 MP4 a browser can play. The object key follows spec Section 16's Storage layout; the API uploads
the file before inserting the row, so a row never points at nothing."""
from __future__ import annotations
import datetime as dt, io, pathlib


def clip_key(station: str, cycle_id: str, event_id: str, when: dt.datetime | None = None) -> str:
    d = when or dt.datetime.now(dt.timezone.utc)
    return f"clips/{station}/{d:%Y}/{d:%m}/{d:%d}/{cycle_id}/{event_id}.mp4"


def frames_before(ring, end_seq: int, count: int) -> list[bytes]:
    """The last `count` frames before end_seq that are still in the ring, oldest first."""
    out = []
    for n in range(max(0, end_seq - count), end_seq):
        f = ring.read(n)
        if f is not None:
            out.append(f[0])
    return out


def write_mp4(jpegs: list[bytes], path: pathlib.Path, fps: int = 30) -> int:
    """Encode JPEG frames to H.264 (yuv420p, the profile every browser plays). Returns frames written."""
    import av
    from PIL import Image
    path.parent.mkdir(parents=True, exist_ok=True)
    first = Image.open(io.BytesIO(jpegs[0]))
    w, h = first.size
    with av.open(str(path), mode="w") as out:
        s = out.add_stream("libx264", rate=fps)
        s.width, s.height, s.pix_fmt = w - w % 2, h - h % 2, "yuv420p"
        s.options = {"preset": "veryfast", "crf": "26", "movflags": "+faststart"}
        n = 0
        for j in jpegs:
            img = Image.open(io.BytesIO(j)).convert("RGB")
            if img.size != (s.width, s.height):
                img = img.resize((s.width, s.height))
            for pkt in s.encode(av.VideoFrame.from_image(img)):
                out.mux(pkt)
            n += 1
        for pkt in s.encode():
            out.mux(pkt)
    return n
