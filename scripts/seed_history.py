"""Seeded fleet history for the demo (spec Sections 21, 23): thirty days of synthetic health buckets for
four stations, a day of cycles and anomalies, and one station whose lens degrades on the last weekend.

    python scripts/seed_history.py                 # into var/history.db and var/drift.db (the local API's)
    python scripts/seed_history.py --export web/public/replay   # also the JSON the public replay page reads

Every row is flagged synthetic and the fleet view says so on screen whenever it is shown. Nothing
here goes to the outbox or to Supabase: the system of record holds only what an engine produced.
The drift charts are fed bucket by bucket through the real DriftJob, so what /fleet shows is the EWMA
chart's own verdict on this data, not a drawing of one.
"""
from __future__ import annotations
import argparse, datetime as dt, json, math, pathlib, random, sys, uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "api"))
from fleet.job import DriftJob     # noqa: E402
from history import History        # noqa: E402

STATIONS = ["W-14", "W-15", "W-16", "W-17"]
BUCKET_S = 300
STEPS = [("torque", "SEQUENCE"), ("torque", "EVIDENCE_MISSING"), ("hand_start", "STEP_TIMEOUT"),
         ("torque", "REGRESSION"), ("torque", "INCOMPLETE_AT_RELEASE"), ("torque", "PRECEDENCE")]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, default=30)
    ap.add_argument("--var", type=pathlib.Path, default=ROOT / "var")
    ap.add_argument("--export", type=pathlib.Path)
    ap.add_argument("--seed", type=int, default=20261016)
    a = ap.parse_args(argv)
    rng = random.Random(a.seed)
    a.var.mkdir(parents=True, exist_ok=True)
    hist = History(str(a.var / "history.db"))
    job = DriftJob(str(a.var / "drift.db"), emit=lambda kind, rec: None)    # never the outbox
    now = dt.datetime.now(dt.timezone.utc).replace(second=0, microsecond=0)
    start = now - dt.timedelta(days=a.days)
    n = a.days * 86400 // BUCKET_S
    smear_from = n - int(1.6 * 86400 / BUCKET_S)                              # W-16: Saturday night, lens smearing

    hist.db.execute("begin")
    job.db.execute("begin")
    for s_i, st in enumerate(STATIONS):
        base_conf = 0.93 + 0.01 * s_i
        model = "yolo11n-w14-s20260721" if st != "W-15" else "yolo11n-w15-s20261012"   # W-15 runs the canary
        for k in range(n):
            t = start + dt.timedelta(seconds=k * BUCKET_S)
            hour = t.hour + t.minute / 60
            daily = 0.004 * math.sin(2 * math.pi * hour / 24)                 # light through the day
            drift = 0.0
            if st == "W-16" and k > smear_from:
                drift = min(1.0, (k - smear_from) / (0.9 * 86400 / BUCKET_S))
            b = {"station_id": st, "bucket_start": t.isoformat().replace("+00:00", "Z"), "bucket_s": BUCKET_S,
                 "fps": round(30 - abs(rng.gauss(0, 0.05)), 3),
                 "dropped_frame_rate": max(0.0, rng.gauss(0.0008, 0.0004)),
                 "ip_conf_mean": base_conf + daily + rng.gauss(0, 0.004) - 0.06 * drift,
                 "ambiguous_rate": max(0.0, 0.012 + rng.gauss(0, 0.002) + 0.05 * drift),
                 "anchor_residual_px": max(0.0, 1.1 + rng.gauss(0, 0.15)),
                 "evidence_p99_ms": 68 + rng.gauss(0, 2.5), "camera_temp_c": None, "packets_resent": None,
                 "adjudications": rng.randint(0, 2), "model_version": model, "cad_revision": "C"}
            if st == "W-15" and k == n - 288:
                b["model_version"] = "yolo11n-w15-s20261012"
            hist.add("station_health", b, synthetic=True)
            sm = max(0.5, min(1.0, rng.gauss(0.92, 0.015)))
            for e in job.feed(b, sm):
                hist.add("health_events", e, synthetic=True)
            if k % 12 == 0:
                hist.add("host_health", {"station_id": st, "node": "hsgpc", "ts": b["bucket_start"], "engine_up": True,
                                         "gpu_temp_c": round(58 + rng.gauss(0, 2), 1), "sm_clock_ratio": sm,
                                         "gpu_mem_mib": 2900, "disk_free_bytes": 300 * 2**30}, synthetic=True)
        # the last day: cycles every takt, and their anomalies
        t = now - dt.timedelta(hours=24)
        while t < now:
            ok = rng.random() > (0.035 + (0.03 if st == "W-16" else 0))
            cid = str(uuid.uuid4())
            hist.add("cycles", {"cycle_id": cid, "ts": t.isoformat().replace("+00:00", "Z"), "station_id": st,
                                "profile": "wheel-fitment-rl", "passed": ok, "duration_s": round(rng.uniform(48, 58), 1)}, synthetic=True)
            if not ok:
                step, kind = rng.choice(STEPS)
                hist.add("anomalies", {"event_id": str(uuid.uuid4()), "ts": t.isoformat().replace("+00:00", "Z"), "station_id": st,
                                       "cycle_id": cid, "profile": "wheel-fitment-rl", "profile_version": "bdea81aa8b67",
                                       "step_id": step, "violation_type": kind, "observed_ip": f"bolt-{rng.randint(1, 5)}",
                                       "expected_ip": None, "detail": {}, "engine_ms": round(rng.uniform(1, 3), 2),
                                       "evidence_ms": round(rng.uniform(33, 40), 2), "narrative": f"Seeded {kind} on {step}.",
                                       "adjudicated": False, "cad_revision": "C", "model_version": model}, synthetic=True)
            t += dt.timedelta(seconds=58)
    hist.db.execute("commit")
    job.db.execute("commit")
    for st in STATIONS:
        print(f"{st}: drifting {sorted(job.drifting(st)) or 'nothing'}")

    if a.export:
        sys.path.insert(0, str(ROOT / "api"))
        import importlib.util, os
        os.environ.update(TS_VAR=str(a.var), TS_API_BACKGROUND="0")
        spec = importlib.util.spec_from_file_location("api_main", ROOT / "api" / "main.py")
        main = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(main)
        a.export.mkdir(parents=True, exist_ok=True)
        (a.export / "fleet.json").write_text(json.dumps(main.fleet(hours=24 * a.days)), encoding="utf-8")
        (a.export / "analytics.json").write_text(json.dumps(main.analytics(hours=24)), encoding="utf-8")
        print(f"exported fleet.json and analytics.json to {a.export}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
