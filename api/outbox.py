"""Store-and-forward from the laptop to Supabase.

The alert path never touches the network: engine -> soft PLC -> HMI is local. Everything that is
*recorded* — anomalies, cycles, health — goes into this outbox first, a SQLite file on the
laptop, and a flusher forwards it to Supabase in order. If the venue Wi-Fi drops mid-demo, alerts
keep firing, nothing is lost, and the cloud dashboard catches up when the link returns.

Rules:
  * strictly in order — a transient failure (network, pooler, Storage) stops the flush and the same
    record is retried first next time;
  * idempotent — every record carries its primary key, inserts are ON CONFLICT DO NOTHING, so a
    record delivered twice (crash between insert and delete) is harmless;
  * a record the database rejects as invalid can never succeed, so it moves to a dead-letter table
    instead of blocking everything behind it.
"""
from __future__ import annotations
import json, sqlite3, time
from dataclasses import dataclass
from typing import Any, Protocol


class Permanent(Exception):
    """The record itself is bad: retrying cannot help."""


class Sink(Protocol):
    def deliver(self, kind: str, record: dict[str, Any], clip_path: str | None) -> None: ...


@dataclass
class FlushResult:
    delivered: int
    dead: int
    stopped_on: str | None          # the transient error that stopped the flush, if any


class Outbox:
    def __init__(self, path: str):
        self.db = sqlite3.connect(path, isolation_level=None)          # autocommit; one writer
        self.db.execute("pragma journal_mode=wal")
        self.db.execute("pragma synchronous=full")                     # a power cut loses nothing acknowledged
        self.db.execute("""create table if not exists outbox (
            seq integer primary key autoincrement, kind text not null, payload text not null,
            clip_path text, attempts integer not null default 0, last_error text, queued_at real not null)""")
        self.db.execute("""create table if not exists dead_letter (
            seq integer primary key, kind text not null, payload text not null, clip_path text,
            error text not null, dead_at real not null)""")

    def put(self, kind: str, record: dict[str, Any], clip_path: str | None = None) -> None:
        self.db.execute("insert into outbox (kind, payload, clip_path, queued_at) values (?, ?, ?, ?)",
                        (kind, json.dumps(record, default=str), clip_path, time.time()))

    def pending(self) -> int:
        return self.db.execute("select count(*) from outbox").fetchone()[0]

    def oldest_age_s(self) -> float | None:
        row = self.db.execute("select min(queued_at) from outbox").fetchone()
        return None if row[0] is None else time.time() - row[0]

    def flush(self, sink: Sink, limit: int = 500) -> FlushResult:
        delivered = dead = 0
        rows = self.db.execute("select seq, kind, payload, clip_path from outbox order by seq limit ?",
                               (limit,)).fetchall()
        for seq, kind, payload, clip_path in rows:
            try:
                sink.deliver(kind, json.loads(payload), clip_path)
            except Permanent as exc:
                self.db.execute("begin")
                self.db.execute("insert into dead_letter values (?, ?, ?, ?, ?, ?)",
                                (seq, kind, payload, clip_path, str(exc), time.time()))
                self.db.execute("delete from outbox where seq = ?", (seq,))
                self.db.execute("commit")
                dead += 1
                continue
            except Exception as exc:                                      # transient: keep order, stop
                self.db.execute("update outbox set attempts = attempts + 1, last_error = ? where seq = ?",
                                (repr(exc)[:500], seq))
                return FlushResult(delivered, dead, repr(exc))
            self.db.execute("delete from outbox where seq = ?", (seq,))
            delivered += 1
        return FlushResult(delivered, dead, None)


# --- the Supabase sink ----------------------------------------------------------------------

# kind -> (table, conflict target). Columns come from the record itself, checked against the table.
TABLES = {
    "anomaly":        ("anomalies",          "event_id"),
    "cycle":          ("cycles",             "cycle_id"),
    "station_health": ("station_health",     "station_id, bucket_start"),
    "host_health":    ("host_health",        "station_id, node, ts"),
    "health_event":   ("health_events",      "event_id"),
}


class SupabaseSink:
    """Postgres through Supabase's session pooler (IPv4 everywhere), clips through Storage's S3 API."""

    def __init__(self, conninfo: str, s3=None, bucket: str = "clips"):
        import psycopg
        self.psycopg = psycopg
        self.conninfo, self.s3, self.bucket = conninfo, s3, bucket
        self.conn = None
        self.columns: dict[str, set[str]] = {}

    def _connect(self):
        if self.conn is None or self.conn.closed:
            self.conn = self.psycopg.connect(self.conninfo, autocommit=True, connect_timeout=5,
                                             prepare_threshold=None)
            for table, _ in TABLES.values():
                cur = self.conn.execute(
                    "select column_name from information_schema.columns "
                    "where table_schema = 'public' and table_name = %s", (table,))
                self.columns[table] = {r[0] for r in cur.fetchall()}
        return self.conn

    def deliver(self, kind, record, clip_path):
        from psycopg import sql, errors
        if kind not in TABLES:
            raise Permanent(f"unknown kind {kind!r}")
        table, conflict = TABLES[kind]
        try:
            conn = self._connect()
        except self.psycopg.OperationalError:
            self.conn = None
            raise
        unknown = set(record) - self.columns[table]
        if unknown:
            raise Permanent(f"{table} has no column(s) {sorted(unknown)}")
        if clip_path and self.s3 is not None:                  # object first: a row never points at nothing
            self.s3.upload_file(clip_path, self.bucket, record["clip_key"],
                                ExtraArgs={"ContentType": "video/mp4"})
        cols = list(record)
        stmt = sql.SQL("insert into public.{} ({}) values ({}) on conflict ({}) do nothing").format(
            sql.Identifier(table), sql.SQL(", ").join(map(sql.Identifier, cols)),
            sql.SQL(", ").join(sql.Placeholder() * len(cols)), sql.SQL(conflict))
        values = [self.psycopg.types.json.Jsonb(v) if isinstance(v, (dict, list)) else v
                  for v in (record[c] for c in cols)]
        try:
            conn.execute(stmt, values)
        except (errors.DataError, errors.IntegrityError) as exc:      # the record is bad, not the link
            raise Permanent(str(exc).splitlines()[0]) from exc
        except self.psycopg.OperationalError:
            self.conn = None
            raise


def prune_clips(s3, bucket: str, older_than_days: int = 30, now: float | None = None) -> int:
    """Clip retention through the Storage API (S3 protocol). Run daily by the API."""
    import datetime as dt
    cutoff = dt.datetime.fromtimestamp(now or time.time(), dt.timezone.utc) - dt.timedelta(days=older_than_days)
    removed, token = 0, None
    while True:
        kw = {"Bucket": bucket, "Prefix": "clips/"}
        if token:
            kw["ContinuationToken"] = token
        page = s3.list_objects_v2(**kw)
        old = [{"Key": o["Key"]} for o in page.get("Contents", []) if o["LastModified"] < cutoff]
        if old:
            s3.delete_objects(Bucket=bucket, Delete={"Objects": old})
            removed += len(old)
        if not page.get("IsTruncated"):
            return removed
        token = page["NextContinuationToken"]
