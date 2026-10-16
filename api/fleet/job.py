"""The drift job (spec Section 19): once per health bucket, feed each station's six charts, persist
their state, and open or close drift episodes. Plain Python; at four stations and six charts it does
24 updates a bucket.

Chart state lives where it must survive a restart: a local SQLite table, mirrored to Supabase's
health_chart_state through the outbox (an upsert). A change of model_version or cad_revision returns
the station's charts to phase I and logs a BASELINE_RESET — otherwise every deploy would look like drift.
"""
from __future__ import annotations
import datetime as dt, json, sqlite3, uuid
from typing import Callable

from .ewma import Chart, State, default_charts

Emit = Callable[[str, dict], None]          # (outbox kind, record)


class DriftJob:
    def __init__(self, db_path: str, emit: Emit, baseline_n: int | None = None):
        self.db = sqlite3.connect(db_path, isolation_level=None, check_same_thread=False)
        self.db.execute("""create table if not exists chart_state (
            station_id text not null, metric text not null, state text not null, updated_at text not null,
            primary key (station_id, metric))""")
        self.db.execute("""create table if not exists station_revision (
            station_id text primary key, model_version text, cad_revision text)""")
        self.emit = emit
        self.baseline_n = baseline_n        # demo mode shortens phase I (spec Section 32: a 24-minute baseline)

    def charts(self, station: str) -> list[Chart]:
        rows = dict(self.db.execute("select metric, state from chart_state where station_id = ?", (station,)).fetchall())
        out = []
        for c in default_charts():
            if self.baseline_n:
                c.baseline_n = self.baseline_n
            out.append(Chart.from_state(json.loads(rows[c.metric])) if c.metric in rows else c)
        return out

    def _save(self, station: str, charts: list[Chart], ts: str) -> None:
        for c in charts:
            s = c.to_state()
            self.db.execute("insert or replace into chart_state values (?, ?, ?, ?)", (station, c.metric, json.dumps(s), ts))
            self.emit("health_chart_state", {"station_id": station, "metric": c.metric, "state": s, "updated_at": ts})

    def _event(self, station: str, kind: str, metric: str | None, detail: dict, ts: str) -> dict:
        ev = {"event_id": str(uuid.uuid4()), "ts": ts, "station_id": station, "kind": kind, "metric": metric, "detail": detail}
        self.emit("health_event", ev)
        return ev

    def feed(self, bucket: dict, sm_clock_ratio: float | None = None) -> list[dict]:
        """One station_health bucket (plus the fleet agent's latest SM clock ratio). Returns the events raised."""
        station = bucket["station_id"]
        ts = bucket.get("bucket_start") or dt.datetime.now(dt.timezone.utc).isoformat()
        events: list[dict] = []
        charts = self.charts(station)
        rev = self.db.execute("select model_version, cad_revision from station_revision where station_id = ?", (station,)).fetchone()
        now_rev = (bucket.get("model_version"), bucket.get("cad_revision"))
        if rev is not None and tuple(rev) != now_rev:
            for c in charts:
                c.reset()
            events.append(self._event(station, "BASELINE_RESET", None,
                                      {"from": {"model_version": rev[0], "cad_revision": rev[1]},
                                       "to": {"model_version": now_rev[0], "cad_revision": now_rev[1]}}, ts))
        self.db.execute("insert or replace into station_revision values (?, ?, ?)", (station, *now_rev))
        values = dict(bucket)
        if sm_clock_ratio is not None:
            values["sm_clock_ratio"] = sm_clock_ratio
        for c in charts:
            x = values.get(c.metric)
            if x is None:
                continue
            before = c.state
            state, entered = c.update(float(x))
            if entered:
                events.append(self._event(station, "DRIFT", c.metric, {"z": c.z, "limit": c.limit(), "mu0": c.mu0,
                                                                        "sigma0": c.sigma0, "value": x}, ts))
            elif before is State.DRIFT and state is State.IN_CONTROL:
                events.append(self._event(station, "DRIFT_CLEARED", c.metric, {"value": x}, ts))
        self._save(station, charts, ts)
        return events

    def drifting(self, station: str) -> set[str]:
        return {c.metric for c in self.charts(station) if c.state is State.DRIFT}
