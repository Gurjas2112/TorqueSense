-- Verification events: what the engine decided, one row per cycle and per violation.
-- Plain Postgres. Supabase deprecates the timescaledb extension on Postgres 17 and ships only its
-- Apache edition (no continuous aggregates, compression or retention policies), so nothing here
-- depends on it. At prototype scale — thousands of rows a day — plain tables with the right
-- indexes are faster to reason about than partitions; add pg_partman if a table passes ~10M rows.

create table public.anomalies (
  event_id        uuid        primary key,         -- minted at the source: replays are idempotent
  ts              timestamptz not null,
  station_id      text        not null,
  cycle_id        uuid        not null,
  step_id         text        not null,
  profile         text        not null,            -- e.g. wheel-fitment-rl, desk-cover-4screw
  profile_version text        not null,            -- hash of the profile folder
  violation_type  text        not null check (violation_type in
                    ('SEQUENCE','PRECEDENCE','EVIDENCE_MISSING','REGRESSION','STEP_TIMEOUT','INCOMPLETE_AT_RELEASE')),
  observed_ip     text,
  expected_ip     text,
  detail          jsonb       not null default '{}',
  engine_ms       real        not null,
  evidence_ms     real        not null,
  clip_key        text,                            -- object key in the Storage bucket `clips`
  narrative       text        not null check (narrative <> ''),
  adjudicated     boolean     not null default false,
  cad_revision    text        not null,
  model_version   text        not null,
  reviewed_at     timestamptz,
  false_positive  boolean
);
create index anomalies_station_ts on public.anomalies (station_id, ts desc);
create index anomalies_type_ts    on public.anomalies (violation_type, ts desc);

create table public.cycles (
  cycle_id    uuid        primary key,
  ts          timestamptz not null,
  station_id  text        not null,
  profile     text        not null,
  passed      boolean     not null,
  duration_s  real        not null
);
create index cycles_station_ts on public.cycles (station_id, ts desc);

-- First-time-right per station per hour. A plain view: date_bin is core Postgres (14+), and at this
-- volume computing it on read costs milliseconds. security_invoker makes it obey the caller's RLS.
create view public.ftr_hourly with (security_invoker = true) as
select date_bin('1 hour', ts, timestamptz '2000-01-01') as bucket,
       station_id,
       avg(passed::int)::real as first_time_right,
       count(*)               as cycles
from public.cycles
group by 1, 2;
