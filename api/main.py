"""The API (spec Sections 14, 16, 19): events and health in, history out, the outbox, the drift job, and
the built dashboard.

    uvicorn api.main:app --host 127.0.0.1 --port 8000 --env-file .env

Every write goes through api/outbox.py (store first, forward in order); the alert path never needs
this process, let alone the network. POST routes need X-API-Key (Section 15); the one exception is the
engineer's Confirm / False-positive review, which is accepted only from this machine.
"""
from __future__ import annotations
import datetime as dt, logging, os, pathlib, sys, threading, time
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))   # `outbox`, `history`, `fleet` as top-level modules

import httpx                                                        # noqa: E402
from fastapi import FastAPI, Header, HTTPException, Request         # noqa: E402
from fastapi.middleware.cors import CORSMiddleware                  # noqa: E402
from fastapi.responses import FileResponse                          # noqa: E402

from fleet.job import DriftJob                                      # noqa: E402
from fleet.playbook import diagnose                                 # noqa: E402
from history import History                                         # noqa: E402
from outbox import Outbox, SupabaseSink, prune_clips                # noqa: E402

log = logging.getLogger("torquesense.api")
ROOT = pathlib.Path(__file__).resolve().parents[1]
VAR = pathlib.Path(os.environ.get("TS_VAR", ROOT / "var"))
VAR.mkdir(parents=True, exist_ok=True)

API_KEY = os.environ.get("API_KEY", "")
OUTBOX_PATH = os.environ.get("OUTBOX_PATH", str(VAR / "outbox.db"))
DATABASE_URL = os.environ.get("DATABASE_URL", "")
SIDECAR_URL = os.environ.get("SIDECAR_URL", "http://127.0.0.1:8001")
DEMO_BASELINE = int(os.environ.get("TS_DEMO_BASELINE_BUCKETS", "0")) or None
ANOMALY_COLUMNS = {"event_id", "ts", "station_id", "cycle_id", "step_id", "profile", "profile_version", "violation_type",
                   "observed_ip", "expected_ip", "detail", "engine_ms", "evidence_ms", "clip_key", "narrative",
                   "adjudicated", "cad_revision", "model_version", "reviewed_at", "false_positive"}

app = FastAPI(title="TorqueSense API", version="5.5.0")
app.add_middleware(CORSMiddleware, allow_origin_regex=r"http://(127\.0\.0\.1|localhost)(:\d+)?",
                   allow_methods=["GET", "POST"], allow_headers=["*"])

outbox = Outbox(OUTBOX_PATH)
history = History(str(VAR / "history.db"))
_state: dict[str, Any] = {"flushed": 0, "dead": 0, "last_error": None, "last_flush": None, "sink": None,
                          "clips": 0, "last_host": {}}
_lock = threading.Lock()


def put(kind: str, record: dict, clip_path: str | None = None) -> None:
    with _lock:
        outbox.put(kind, record, clip_path)


drift = DriftJob(str(VAR / "drift.db"), put, baseline_n=DEMO_BASELINE)


def require_key(x_api_key: str | None) -> None:
    if not API_KEY or x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="X-API-Key required")


def _s3():
    if not os.environ.get("S3_ACCESS_KEY"):
        return None
    import boto3
    return boto3.client("s3", endpoint_url=os.environ["S3_ENDPOINT"], region_name=os.environ.get("S3_REGION"),
                        aws_access_key_id=os.environ["S3_ACCESS_KEY"], aws_secret_access_key=os.environ["S3_SECRET_KEY"])


# --- ingest ---------------------------------------------------------------------------------------
@app.post("/v1/events/anomaly")
def ingest_anomaly(body: dict, x_api_key: str | None = Header(default=None)):
    require_key(x_api_key)
    record = {k: v for k, v in body.items() if k in ANOMALY_COLUMNS}
    clip_path = None
    if "ring_end_seq" in body:                       # the sidecar cuts six seconds around the violation
        try:
            r = httpx.post(f"{SIDECAR_URL}/v1/clip", json={"station_id": body["station_id"], "cycle_id": body["cycle_id"],
                                                           "event_id": body["event_id"], "ring_end_seq": body["ring_end_seq"]},
                           timeout=10)
            if r.status_code == 200:
                clip = r.json()
                clip_path, record["clip_key"] = clip["path"], clip["clip_key"]
                _state["clips"] += 1
        except httpx.HTTPError as exc:
            log.warning("no clip for %s: %s", body.get("event_id"), exc)
    put("anomaly", record, clip_path)
    history.add("anomalies", dict(record, clip_local=clip_path))
    return {"queued": True, "clip_key": record.get("clip_key")}


@app.post("/v1/events/cycle")
def ingest_cycle(body: dict, x_api_key: str | None = Header(default=None)):
    require_key(x_api_key)
    put("cycle", body)
    history.add("cycles", body)
    return {"queued": True}


@app.post("/v1/health/station")
def ingest_station_health(body: dict, x_api_key: str | None = Header(default=None)):
    require_key(x_api_key)
    put("station_health", body)
    history.add("station_health", body)
    host = _state["last_host"].get(body["station_id"]) or {}
    events = drift.feed(body, host.get("sm_clock_ratio"))
    for e in events:
        history.add("health_events", e)
    return {"queued": True, "events": [e["kind"] for e in events]}


@app.post("/v1/health/host")
def ingest_host_health(body: dict, x_api_key: str | None = Header(default=None)):
    """The fleet agent's record (contracts/host_health.schema.json), flattened into public.host_health."""
    require_key(x_api_key)
    gpu = body.get("gpu") or {}
    row = {"station_id": body["station_id"], "node": body["node"], "ts": body["ts"],
           "engine_up": (body.get("engine") or {}).get("status") == "up",
           "gpu_temp_c": gpu.get("temp_c"), "sm_clock_ratio": gpu.get("sm_clock_ratio"),
           "gpu_mem_mib": gpu.get("mem_used_mib"), "disk_free_bytes": body.get("disk_free_bytes")}
    put("host_health", row)
    history.add("host_health", row)
    prev = _state["last_host"].get(row["station_id"])
    if prev is not None and prev["engine_up"] != row["engine_up"]:
        ev = {"event_id": str(__import__("uuid").uuid4()), "ts": row["ts"], "station_id": row["station_id"],
              "kind": "ENGINE_ONLINE" if row["engine_up"] else "ENGINE_OFFLINE", "metric": None,
              "detail": {"reason": (body.get("engine") or {}).get("reason")}}
        put("health_event", ev)
        history.add("health_events", ev)
    _state["last_host"][row["station_id"]] = row
    return {"queued": True}


@app.post("/v1/anomalies/{event_id}/review")
def review(event_id: str, body: dict, request: Request, x_api_key: str | None = Header(default=None)):
    """Confirm or False positive (spec Section 21, zone C). From this machine only, or with the key."""
    if request.client and request.client.host not in ("127.0.0.1", "::1", "testclient"):
        require_key(x_api_key)
    fields = {"false_positive": bool(body.get("false_positive")), "reviewed_at": dt.datetime.now(dt.timezone.utc).isoformat()}
    if history.update_anomaly(event_id, **fields) is None:
        raise HTTPException(status_code=404, detail="no such anomaly here")
    put("anomaly_review", {"event_id": event_id, **fields})
    return {"queued": True, **fields}


# --- read ---------------------------------------------------------------------------------------
@app.get("/health")
def health():
    return {"status": "up", "outbox_pending": outbox.pending(), "outbox_oldest_s": outbox.oldest_age_s(),
            "flushed": _state["flushed"], "dead_letters": _state["dead"], "last_error": _state["last_error"],
            "last_flush": _state["last_flush"], "supabase": bool(DATABASE_URL), "clips": _state["clips"]}


@app.get("/v1/anomalies")
def anomalies(station: str | None = None, limit: int = 50):
    return history.rows("anomalies", station=station, limit=limit)


@app.get("/v1/cycles")
def cycles(station: str | None = None, limit: int = 200):
    return history.rows("cycles", station=station, limit=limit)


@app.get("/v1/analytics")
def analytics(hours: int = 24):
    since = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%S")
    return history.analytics(since)


@app.get("/v1/fleet")
def fleet(hours: int = 24):
    """Per station: the latest host record, every signal's series against its chart, open episodes and the playbook."""
    since = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%S")
    stations = sorted({r["station_id"] for r in history.rows("station_health", since=since, limit=100000)} |
                      set(_state["last_host"]))
    out = []
    for s in stations:
        buckets = history.rows("station_health", station=s, since=since, limit=5000, desc=False)
        hosts = history.rows("host_health", station=s, since=since, limit=5000, desc=False)
        charts = {c.metric: c for c in drift.charts(s)}
        signals = {}
        for m, c in charts.items():
            series = [{"t": b["bucket_start"], "v": b.get(m)} for b in buckets if b.get(m) is not None]
            if m == "sm_clock_ratio":
                series = [{"t": h["ts"], "v": h.get("sm_clock_ratio")} for h in hosts if h.get("sm_clock_ratio") is not None]
            limit = (c.mu0 + (1 if c.direction == "up" else -1) * c.L * c.sigma0 * (c.lam / (2 - c.lam)) ** 0.5
                     if c.mu0 is not None else None)
            signals[m] = {"state": c.state.value, "direction": c.direction, "center": c.mu0, "limit": limit,
                          "series": series[-288:]}
        drifting = {m for m, v in signals.items() if v["state"] == "drift"}
        latest = buckets[-1] if buckets else {}
        host = hosts[-1] if hosts else _state["last_host"].get(s, {})
        events = history.rows("health_events", station=s, since=since, limit=50)
        out.append({"station_id": s, "engine_up": host.get("engine_up"), "last_host": host,
                    "model_version": latest.get("model_version"), "cad_revision": latest.get("cad_revision"),
                    "signals": signals, "drifting": sorted(drifting), "diagnosis": diagnose(drifting),
                    "events": events, "synthetic": any(b.get("synthetic") for b in buckets)})
    versions = [x["model_version"] for x in out if x["model_version"]]
    for x in out:   # the canary tag: a model the rest of the line is not running (spec Section 21, /fleet)
        x["canary"] = bool(x["model_version"]) and versions.count(x["model_version"]) == 1 and len(versions) > 1
    return out


@app.get("/v1/clips/{event_id}.mp4")
def clip(event_id: str):
    rows = [r for r in history.rows("anomalies", limit=2000) if r.get("event_id") == event_id]
    if not rows or not rows[0].get("clip_local") or not pathlib.Path(rows[0]["clip_local"]).exists():
        raise HTTPException(status_code=404)
    return FileResponse(rows[0]["clip_local"], media_type="video/mp4")


# --- background: the flusher and the daily prune ------------------------------------------------
def _flusher():
    sink = None
    while True:
        try:
            if DATABASE_URL and sink is None:
                sink = SupabaseSink(DATABASE_URL, s3=_s3(), bucket=os.environ.get("S3_BUCKET", "clips"))
                _state["sink"] = "supabase"
            if sink is not None and outbox.pending():
                with _lock:
                    r = outbox.flush(sink)
                _state["flushed"] += r.delivered
                _state["dead"] += r.dead
                _state["last_error"] = r.stopped_on
                _state["last_flush"] = dt.datetime.now(dt.timezone.utc).isoformat()
        except Exception as exc:                                    # transient: the outbox keeps everything
            _state["last_error"] = repr(exc)[:300]
            sink = None
        time.sleep(2)


def _daily():
    while True:
        try:
            history.prune()
            s3 = _s3()
            if s3 is not None:
                prune_clips(s3, os.environ.get("S3_BUCKET", "clips"), 30)
        except Exception as exc:
            log.warning("daily prune: %s", exc)
        time.sleep(86400)


if os.environ.get("TS_API_BACKGROUND", "1") == "1":
    threading.Thread(target=_flusher, name="flusher", daemon=True).start()
    threading.Thread(target=_daily, name="daily", daemon=True).start()

# the built dashboard, registered last so it never shadows an API route (api/spa.py)
_dist = pathlib.Path(os.environ.get("TS_WEB_DIST", ROOT / "web" / "dist"))
if (_dist / "index.html").exists():
    from spa import mount_spa
    mount_spa(app, _dist)
