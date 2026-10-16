"""The API: keys, the outbox as the only write path, the local history, the drift job and /fleet."""
import importlib.util, pathlib, uuid

import pytest
from fastapi.testclient import TestClient


@pytest.fixture
def api(tmp_path, monkeypatch):
    monkeypatch.setenv("TS_VAR", str(tmp_path))
    monkeypatch.setenv("API_KEY", "k")
    monkeypatch.setenv("TS_API_BACKGROUND", "0")
    monkeypatch.setenv("TS_DEMO_BASELINE_BUCKETS", "12")
    monkeypatch.setenv("SIDECAR_URL", "http://127.0.0.1:9")        # nothing listens: no clip, no failure
    monkeypatch.delenv("DATABASE_URL", raising=False)
    monkeypatch.delenv("S3_ACCESS_KEY", raising=False)
    spec = importlib.util.spec_from_file_location("api_main", pathlib.Path(__file__).parents[1] / "main.py")
    main = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(main)                       # a fresh module per test: its own outbox and history
    return main, TestClient(main.app)


def anomaly(**kw):
    a = {"event_id": str(uuid.uuid4()), "ts": "2026-10-16T09:00:00Z", "station_id": "W-14", "cycle_id": str(uuid.uuid4()),
         "profile": "wheel-fitment-rl", "profile_version": "bdea81aa8b67", "step_id": "torque", "violation_type": "SEQUENCE",
         "observed_ip": "bolt-2", "expected_ip": "bolt-5", "detail": {"observed": "bolt-2", "expected": "bolt-5"},
         "engine_ms": 1.7, "evidence_ms": 34.6, "ring_end_seq": 691, "narrative": "Bolt 2 fastened before bolt 5.",
         "adjudicated": False, "cad_revision": "cad", "model_version": "replay-fixtures-v1"}
    a.update(kw)
    return a


def test_writes_need_the_key(api):
    _, c = api
    assert c.post("/v1/events/anomaly", json=anomaly()).status_code == 401
    assert c.post("/v1/events/anomaly", json=anomaly(), headers={"X-API-Key": "wrong"}).status_code == 401


def test_an_anomaly_goes_to_the_outbox_and_the_local_history(api):
    main, c = api
    a = anomaly()
    r = c.post("/v1/events/anomaly", json=a, headers={"X-API-Key": "k"})
    assert r.status_code == 200
    assert main.outbox.pending() == 1
    kind, payload = main.outbox.db.execute("select kind, payload from outbox").fetchone()
    assert kind == "anomaly" and "ring_end_seq" not in payload          # not a column: never sent to Postgres
    assert c.get("/v1/anomalies").json()[0]["event_id"] == a["event_id"]
    assert c.get("/health").json()["outbox_pending"] == 1


def test_a_review_marks_the_anomaly_and_is_forwarded_as_an_update(api):
    main, c = api
    a = anomaly()
    c.post("/v1/events/anomaly", json=a, headers={"X-API-Key": "k"})
    r = c.post(f"/v1/anomalies/{a['event_id']}/review", json={"false_positive": True})
    assert r.status_code == 200 and r.json()["false_positive"] is True
    kinds = [k for (k,) in main.outbox.db.execute("select kind from outbox order by seq")]
    assert kinds == ["anomaly", "anomaly_review"]
    assert c.get("/v1/anomalies").json()[0]["false_positive"] is True
    assert c.post("/v1/anomalies/nope/review", json={}).status_code == 404


def test_health_buckets_feed_the_drift_job_and_fleet_shows_the_episode(api):
    main, c = api
    h = {"X-API-Key": "k"}
    for i in range(40):
        conf = 0.95 if i < 25 else 0.80                               # a lens smearing after bucket 25
        c.post("/v1/health/station", headers=h, json={
            "station_id": "W-16", "bucket_start": f"2026-10-16T{9 + i // 12:02d}:{(i % 12) * 5:02d}:00Z", "bucket_s": 300,
            "fps": 30.0, "dropped_frame_rate": 0.0, "ip_conf_mean": conf + (i % 3) * 0.002,
            "ambiguous_rate": 0.01 if i < 25 else 0.06, "anchor_residual_px": 1.1, "evidence_p99_ms": 70.0,
            "adjudications": 0, "model_version": "m1", "cad_revision": "C"})
    f = c.get("/v1/fleet?hours=100000").json()
    w16 = next(x for x in f if x["station_id"] == "W-16")
    assert "ip_conf_mean" in w16["drifting"]
    assert w16["diagnosis"]["likely_cause"].startswith("lens contamination")
    kinds = [k for (k,) in main.outbox.db.execute("select kind from outbox")]
    assert "health_event" in kinds and "health_chart_state" in kinds


def test_a_new_model_resets_the_baseline(api):
    _, c = api
    h = {"X-API-Key": "k"}
    base = {"station_id": "W-15", "bucket_s": 300, "fps": 30.0, "dropped_frame_rate": 0.0, "ambiguous_rate": 0.01,
            "adjudications": 0, "cad_revision": "C"}
    c.post("/v1/health/station", headers=h, json=dict(base, bucket_start="2026-10-16T09:00:00Z", model_version="m1"))
    r = c.post("/v1/health/station", headers=h, json=dict(base, bucket_start="2026-10-16T09:05:00Z", model_version="m2"))
    assert r.json()["events"] == ["BASELINE_RESET"]


def test_the_fleet_agent_record_is_flattened_and_an_engine_death_is_an_event(api):
    main, c = api
    h = {"X-API-Key": "k"}
    rec = {"ts": "2026-10-16T09:00:00Z", "station_id": "W-14", "node": "hsgpc", "disk_free_bytes": 10,
           "engine": {"status": "up", "fps": 30}, "gpu": {"temp_c": 50, "sm_clock_mhz": 1700, "sm_clock_max_mhz": 2100,
                                                         "mem_used_mib": 900, "util_pct": 20, "sm_clock_ratio": 0.81}}
    c.post("/v1/health/host", headers=h, json=rec)
    c.post("/v1/health/host", headers=h, json=dict(rec, ts="2026-10-16T09:00:10Z", engine={"status": "down", "reason": "unreachable"}))
    payloads = [p for (k, p) in main.outbox.db.execute("select kind, payload from outbox") if k == "health_event"]
    assert len(payloads) == 1 and "ENGINE_OFFLINE" in payloads[0]
    row = [p for (k, p) in main.outbox.db.execute("select kind, payload from outbox") if k == "host_health"][0]
    assert '"sm_clock_ratio": 0.81' in row and '"engine_up": true' in row
