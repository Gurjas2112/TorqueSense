"""A local copy of recent records, for the dashboard on the laptop (spec Section 22: offline insurance).

The outbox forwards every record to Supabase and then forgets it; this keeps what the engineer
dashboard and /fleet read when the venue network is down. It is a cache of what was recorded, never
an authority: Supabase is the system of record, and the outbox is how records reach it.
"""
from __future__ import annotations
import json, sqlite3, threading, time
from collections import Counter, defaultdict


class History:
    def __init__(self, path: str, keep_days: int = 30):
        self.db = sqlite3.connect(path, isolation_level=None, check_same_thread=False)
        self.lock = threading.Lock()
        self.keep_days = keep_days
        self.db.execute("pragma journal_mode=wal")
        for t, key in (("anomalies", "event_id"), ("cycles", "cycle_id"), ("station_health", "station_id, bucket_start"),
                       ("host_health", "station_id, node, ts"), ("health_events", "event_id")):
            self.db.execute(f"create table if not exists {t} (k text primary key, ts text not null, station_id text, "
                            f"body text not null, synthetic integer not null default 0)")
        self.db.execute("create index if not exists anomalies_ts on anomalies (ts)")
        self.db.execute("create index if not exists health_ts on station_health (station_id, ts)")

    @staticmethod
    def _key(table: str, r: dict) -> str:
        keys = {"anomalies": ("event_id",), "cycles": ("cycle_id",), "station_health": ("station_id", "bucket_start"),
                "host_health": ("station_id", "node", "ts"), "health_events": ("event_id",)}[table]
        return "|".join(str(r[k]) for k in keys)

    def add(self, table: str, r: dict, synthetic: bool = False) -> None:
        ts = r.get("ts") or r.get("bucket_start") or ""
        with self.lock:
            self.db.execute(f"insert or replace into {table} values (?, ?, ?, ?, ?)",
                            (self._key(table, r), ts, r.get("station_id"), json.dumps(r, default=str), int(synthetic)))

    def update_anomaly(self, event_id: str, **fields) -> dict | None:
        with self.lock:
            row = self.db.execute("select body from anomalies where k = ?", (event_id,)).fetchone()
            if not row:
                return None
            body = json.loads(row[0]) | fields
            self.db.execute("update anomalies set body = ? where k = ?", (json.dumps(body, default=str), event_id))
            return body

    def rows(self, table: str, station: str | None = None, since: str | None = None, limit: int = 500,
             desc: bool = True) -> list[dict]:
        q, args = f"select body, synthetic from {table} where 1=1", []
        if station:
            q += " and station_id = ?"; args.append(station)
        if since:
            q += " and ts >= ?"; args.append(since)
        q += f" order by ts {'desc' if desc else 'asc'} limit ?"; args.append(limit)
        with self.lock:
            out = []
            for body, syn in self.db.execute(q, args).fetchall():
                r = json.loads(body)
                if syn:
                    r["synthetic"] = True
                out.append(r)
            return out

    def analytics(self, since: str) -> dict:
        cycles = self.rows("cycles", since=since, limit=100000, desc=False)
        anomalies = self.rows("anomalies", since=since, limit=100000, desc=False)
        by_hour: dict[str, list[int]] = defaultdict(lambda: [0, 0])
        for c in cycles:
            h = c["ts"][:13] + ":00"
            by_hour[h][0] += 1 if c["passed"] else 0
            by_hour[h][1] += 1
        ftr = [{"bucket": h, "first_time_right": p / n, "cycles": n} for h, (p, n) in sorted(by_hour.items())]
        steps = Counter((a["step_id"], a["violation_type"]) for a in anomalies if not a.get("false_positive"))
        return {"cycles": len(cycles), "passed": sum(1 for c in cycles if c["passed"]), "anomalies": len(anomalies),
                "ftr_hourly": ftr,
                "most_violated": [{"step": s, "violation_type": v, "count": n} for (s, v), n in steps.most_common(8)],
                "synthetic": any(c.get("synthetic") for c in cycles)}

    def prune(self) -> None:
        cutoff = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(time.time() - self.keep_days * 86400))
        with self.lock:
            for t in ("anomalies", "cycles", "station_health", "host_health", "health_events"):
                self.db.execute(f"delete from {t} where ts < ?", (cutoff,))
