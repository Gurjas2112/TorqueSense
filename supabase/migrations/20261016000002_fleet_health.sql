-- Fleet health: the engine's health buckets and the fleet agent's host samples.

create table public.station_health (
  station_id          text        not null,
  bucket_start        timestamptz not null,
  bucket_s            smallint    not null,           -- 300 normally, 5 in demo mode
  fps                 real        not null,
  dropped_frame_rate  real        not null,
  ip_conf_mean        real,
  ambiguous_rate      real        not null,
  anchor_residual_px  real,
  evidence_p99_ms     real,
  camera_temp_c       real,                           -- NULL for file, webcam and phone sources
  packets_resent      integer,                        -- industrial GigE only (Appendix D.1)
  adjudications       integer     not null default 0,
  model_version       text        not null,
  cad_revision        text        not null,
  primary key (station_id, bucket_start)
);

create table public.host_health (                     -- every 10 s from the fleet agent
  station_id       text        not null,
  node             text        not null,
  ts               timestamptz not null,
  engine_up        boolean     not null,
  gpu_temp_c       real,
  sm_clock_ratio   real,
  gpu_mem_mib      real,
  disk_free_bytes  bigint,
  primary key (station_id, node, ts)
);
create index host_health_ts on public.host_health using brin (ts);   -- the retention delete's index

create table public.health_events (                   -- drift episodes, offline periods, baseline resets
  event_id    uuid        primary key,
  ts          timestamptz not null,
  station_id  text        not null,
  kind        text        not null check (kind in
                ('DRIFT','DRIFT_CLEARED','ENGINE_OFFLINE','ENGINE_ONLINE','BASELINE_RESET')),
  metric      text,
  detail      jsonb       not null default '{}'
);
create index health_events_station_ts on public.health_events (station_id, ts desc);

create table public.health_chart_state (              -- EWMA state: a 24 h baseline survives a restart
  station_id  text        not null,
  metric      text        not null,
  state       jsonb       not null,
  updated_at  timestamptz not null,
  primary key (station_id, metric)
);
