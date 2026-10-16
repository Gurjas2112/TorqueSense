"""Outbox: order, persistence, dead letters — and, against a real Postgres with Supabase's roles
emulated, the migrations, idempotent inserts and what the public key can and cannot see.
Set TS_TEST_PG to a libpq conninfo for the integration half; it is skipped otherwise."""
import datetime as dt, os, pathlib, uuid
import pytest
from outbox import Outbox, Permanent, SupabaseSink, prune_clips

MIGRATIONS = sorted((pathlib.Path(__file__).parents[2] / "supabase" / "migrations").glob("*.sql"))


class FakeSink:
    def __init__(self, fail_at=None, bad=()):
        self.got, self.fail_at, self.bad = [], fail_at, set(bad)
    def deliver(self, kind, record, clip_path):
        if record.get("n") in self.bad:
            raise Permanent("check constraint")
        if self.fail_at is not None and record.get("n") == self.fail_at:
            raise ConnectionError("Wi-Fi dropped")
        self.got.append(record["n"])


def test_delivers_in_order_and_empties(tmp_path):
    ob = Outbox(str(tmp_path / "o.db"))
    for n in range(5): ob.put("cycle", {"n": n})
    r = ob.flush(FakeSink())
    assert r.delivered == 5 and ob.pending() == 0

def test_a_dropped_link_stops_the_flush_and_order_survives(tmp_path):
    ob = Outbox(str(tmp_path / "o.db"))
    for n in range(5): ob.put("cycle", {"n": n})
    s = FakeSink(fail_at=2)
    r = ob.flush(s)
    assert s.got == [0, 1] and r.stopped_on and ob.pending() == 3
    s.fail_at = None                                   # link back
    ob.flush(s)
    assert s.got == [0, 1, 2, 3, 4] and ob.pending() == 0

def test_records_survive_a_restart(tmp_path):
    p = str(tmp_path / "o.db")
    ob = Outbox(p); ob.put("cycle", {"n": 1}); ob.put("cycle", {"n": 2}); del ob
    s = FakeSink(); Outbox(p).flush(s)
    assert s.got == [1, 2]

def test_a_bad_record_goes_to_dead_letter_and_does_not_block(tmp_path):
    ob = Outbox(str(tmp_path / "o.db"))
    for n in range(4): ob.put("cycle", {"n": n})
    s = FakeSink(bad={1})
    r = ob.flush(s)
    assert s.got == [0, 2, 3] and r.dead == 1
    assert ob.db.execute("select count(*) from dead_letter").fetchone()[0] == 1

def test_clip_pruning_deletes_only_old_objects():
    now = dt.datetime(2026, 10, 16, tzinfo=dt.timezone.utc)
    class S3:
        deleted = []
        def list_objects_v2(self, **kw):
            return {"Contents": [{"Key": "clips/a.mp4", "LastModified": now - dt.timedelta(days=31)},
                                 {"Key": "clips/b.mp4", "LastModified": now - dt.timedelta(days=2)}],
                    "IsTruncated": False}
        def delete_objects(self, Bucket, Delete): self.deleted += [o["Key"] for o in Delete["Objects"]]
    s3 = S3()
    assert prune_clips(s3, "clips", 30, now=now.timestamp()) == 1 and s3.deleted == ["clips/a.mp4"]


# --- integration: real Postgres, Supabase emulated ------------------------------------------------
PG = os.environ.get("TS_TEST_PG")
pg = pytest.mark.skipif(not PG, reason="set TS_TEST_PG to run against Postgres")

SUPABASE_SHIM = """
drop schema if exists public cascade; create schema public;
drop schema if exists storage cascade; create schema storage;
create table storage.buckets (id text primary key, name text not null, public boolean default false);
do $$ begin
  if not exists (select from pg_roles where rolname = 'anon') then create role anon nologin; end if;
  if not exists (select from pg_roles where rolname = 'authenticated') then create role authenticated nologin; end if;
end $$;
drop publication if exists supabase_realtime; create publication supabase_realtime;
grant usage on schema public to anon, authenticated;
-- Supabase's own default: every new table in public is fully granted to the API roles.
alter default privileges in schema public grant all on tables to anon, authenticated;
"""

@pytest.fixture
def db():
    psycopg = pytest.importorskip("psycopg")
    conn = psycopg.connect(PG, autocommit=True)
    conn.execute("select cron.unschedule(jobname) from cron.job") if conn.execute(
        "select 1 from pg_extension where extname = 'pg_cron'").fetchone() else None
    conn.execute(SUPABASE_SHIM)
    for m in MIGRATIONS:
        conn.execute(m.read_text())
    yield conn
    conn.close()

def anomaly(**kw):
    r = {"event_id": str(uuid.uuid4()), "ts": "2026-10-16T09:00:00+05:30", "station_id": "DESK-1",
         "cycle_id": str(uuid.uuid4()), "step_id": "screws", "profile": "desk-cover-4screw",
         "profile_version": "abc123", "violation_type": "SEQUENCE", "observed_ip": "screw-2",
         "expected_ip": "screw-3", "detail": {"order": [1, 3, 2, 4]}, "engine_ms": 31.5,
         "evidence_ms": 68.2, "narrative": "Screw 2 tightened before screw 3.",
         "cad_revision": "measured", "model_version": "mnv3-desk-1"}
    r.update(kw); return r

@pg
def test_migrations_apply_and_jobs_are_scheduled(db):
    jobs = {r[0] for r in db.execute("select jobname from cron.job")}
    assert {"host-health-30d", "station-health-1y"} <= jobs
    assert db.execute("select public from storage.buckets where id = 'clips'").fetchone() == (True,)
    pub = {r[0] for r in db.execute("select tablename from pg_publication_tables where pubname='supabase_realtime'")}
    assert pub == {"anomalies", "cycles"}

@pg
def test_delivery_is_idempotent_and_bad_rows_are_permanent(db, tmp_path):
    sink = SupabaseSink(PG)
    a = anomaly()
    sink.deliver("anomaly", a, None); sink.deliver("anomaly", a, None)     # delivered twice
    assert db.execute("select count(*) from anomalies").fetchone() == (1,)
    with pytest.raises(Permanent):
        sink.deliver("anomaly", anomaly(violation_type="WRONG_KIND"), None)
    with pytest.raises(Permanent):
        sink.deliver("anomaly", anomaly(not_a_column=1), None)
    ob = Outbox(str(tmp_path / "o.db"))
    ob.put("cycle", {"cycle_id": str(uuid.uuid4()), "ts": "2026-10-16T09:00:00Z", "station_id": "DESK-1",
                     "profile": "desk-cover-4screw", "passed": False, "duration_s": 41.0})
    ob.put("anomaly", anomaly(narrative=""))                                 # violates the check: dead letter
    ob.put("host_health", {"station_id": "DESK-1", "node": "hsgpc", "ts": "2026-10-16T09:00:10Z",
                           "engine_up": True, "gpu_temp_c": 61.0})
    r = ob.flush(sink)
    assert (r.delivered, r.dead, r.stopped_on) == (2, 1, None)

@pg
def test_the_public_key_reads_the_showcase_and_nothing_else(db):
    SupabaseSink(PG).deliver("anomaly", anomaly(), None)
    db.execute("insert into cycles values (gen_random_uuid(), now(), 'DESK-1', 'desk-cover-4screw', true, 30)")
    db.execute("insert into host_health (station_id, node, ts, engine_up) values ('DESK-1','hsgpc',now(),true)")
    db.execute("set role anon")
    try:
        assert db.execute("select count(*) from anomalies").fetchone() == (1,)
        assert db.execute("select first_time_right from ftr_hourly").fetchone() == (1.0,)
        psycopg = pytest.importorskip("psycopg")
        for stmt in ["select * from host_health", "select * from health_chart_state",
                     "insert into anomalies (event_id) values (gen_random_uuid())",
                     "delete from cycles"]:
            with pytest.raises(psycopg.errors.InsufficientPrivilege):
                db.execute(stmt)
    finally:
        db.execute("reset role")

def test_an_unreachable_database_keeps_everything_queued(tmp_path):
    pytest.importorskip("psycopg")
    ob = Outbox(str(tmp_path / "o.db"))
    ob.put("cycle", {"cycle_id": str(uuid.uuid4())}); ob.put("cycle", {"cycle_id": str(uuid.uuid4())})
    r = ob.flush(SupabaseSink("host=127.0.0.1 port=1 user=postgres dbname=postgres connect_timeout=2"))
    assert r.delivered == 0 and r.stopped_on and ob.pending() == 2


@pg
def test_chart_state_is_replaced_and_a_review_marks_the_anomaly(db):
    sink = SupabaseSink(PG)
    st = {"station_id": "W-14", "metric": "ip_conf_mean", "state": {"z": 0.1}, "updated_at": "2026-10-16T09:00:00Z"}
    sink.deliver("health_chart_state", st, None)
    sink.deliver("health_chart_state", dict(st, state={"z": 0.7}, updated_at="2026-10-16T09:05:00Z"), None)
    assert db.execute("select state->>'z' from health_chart_state").fetchall() == [("0.7",)]
    a = anomaly()
    sink.deliver("anomaly", a, None)
    sink.deliver("anomaly_review", {"event_id": a["event_id"], "false_positive": True,
                                    "reviewed_at": "2026-10-16T18:00:00Z"}, None)
    assert db.execute("select false_positive from anomalies").fetchone() == (True,)
    with pytest.raises(Permanent):
        sink.deliver("anomaly_review", {"false_positive": True}, None)       # no key: can never succeed
