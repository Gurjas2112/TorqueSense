"""The sidecar (spec Sections 4, 14, 30): Florence-2 and the clip writer, beside the engine on the same
GPU, in a separate process so a stall here can never stall the engine.

    uvicorn sidecar.main:app --host 127.0.0.1 --port 8001 --env-file .env

  POST /v1/adjudicate  contracts/adjudication.schema.json — a crop from the clip ring, a phrase, an answer
  POST /v1/clip        six seconds of the ring around a violation, as H.264, for the API to upload
  GET  /health         whether Florence-2 is loaded, and why not if it is not
"""
from __future__ import annotations
import io, os, pathlib, sys, threading, time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from fastapi import FastAPI, HTTPException      # noqa: E402

import clips                                    # noqa: E402
from florence import FlorenceAdjudicator, phrase_for   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIP_DIR = pathlib.Path(os.environ.get("TS_CLIP_DIR", ROOT / "var" / "clips"))
MODEL = os.environ.get("FLORENCE_MODEL", "florence-community/Florence-2-base")
REVISION = os.environ.get("FLORENCE_REVISION", "")
FRACTION = float(os.environ.get("TORCH_MEMORY_FRACTION", "0.30"))

app = FastAPI(title="TorqueSense sidecar", version="5.5.0")
_state = {"florence": None, "florence_error": None, "loading": False, "served": 0, "rings": {}}
_lock = threading.Lock()


def _load_florence():
    if os.environ.get("TS_FLORENCE", "1") != "1":
        _state["florence_error"] = "disabled (TS_FLORENCE=0)"
        return
    if len(REVISION) != 40:
        _state["florence_error"] = "FLORENCE_REVISION must be a 40-character commit (I12)"
        return
    _state["loading"] = True
    try:
        t0 = time.monotonic()
        _state["florence"] = FlorenceAdjudicator(MODEL, REVISION, FRACTION)
        _state["load_s"] = round(time.monotonic() - t0, 1)
    except Exception as exc:                     # torch missing, weights not prefetched, CUDA out of memory …
        _state["florence_error"] = f"{type(exc).__name__}: {exc}"[:400]
    finally:
        _state["loading"] = False


threading.Thread(target=_load_florence, name="florence-load", daemon=True).start()


def ring(station: str):
    from clipring import ClipRing
    with _lock:
        r = _state["rings"].get(station)
        if r is None:
            r = ClipRing(station)
            _state["rings"][station] = r
        return r


@app.get("/health")
def health():
    f = _state["florence"]
    return {"status": "up", "florence": "ready" if f else ("loading" if _state["loading"] else "unavailable"),
            "florence_error": _state["florence_error"], "model": MODEL, "revision": REVISION,
            "device": getattr(f, "device", None), "load_s": _state.get("load_s"), "served": _state["served"]}


@app.post("/v1/adjudicate")
def adjudicate(req: dict):
    f = _state["florence"]
    if f is None:
        raise HTTPException(status_code=503, detail=_state["florence_error"] or "Florence-2 loading")
    t0 = time.monotonic()
    try:
        got = ring(req["station_id"]).read(int(req["ring_seq"]))
    except FileNotFoundError as exc:
        raise HTTPException(status_code=503, detail=str(exc))
    if got is None:
        raise HTTPException(status_code=410, detail="frame no longer in the ring")
    from PIL import Image
    img = Image.open(io.BytesIO(got[0])).convert("RGB")
    c = req["crop"]
    x0, y0 = max(0, c["x"]), max(0, c["y"])
    crop = img.crop((x0, y0, min(img.width, x0 + c["w"]), min(img.height, y0 + c["h"]))).resize((256, 256))
    phrase = req.get("phrase") or phrase_for(req["family"], req["candidate_state"])
    boxes = f.ground(crop, phrase)
    confirmed = f.on_point(boxes, 128, 128, 64)
    _state["served"] += 1
    return {"request_id": req["request_id"], "confirmed": confirmed, "boxes": boxes, "model": MODEL,
            "revision": REVISION, "latency_ms": round((time.monotonic() - t0) * 1000, 1), "cached": False, "error": None}


@app.post("/v1/clip")
def clip(req: dict):
    station, cycle_id, event_id = req["station_id"], req["cycle_id"], req["event_id"]
    try:
        r = ring(station)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=503, detail=str(exc))
    jpegs = clips.frames_before(r, int(req["ring_end_seq"]) + 1, int(req.get("frames", 180)))   # 6 s at 30 fps
    if not jpegs:
        raise HTTPException(status_code=410, detail="no frames left in the ring")
    key = clips.clip_key(station, cycle_id, event_id)
    path = CLIP_DIR / key
    n = clips.write_mp4(jpegs, path)
    return {"clip_key": key, "path": str(path), "frames": n}
