-- Who may read what, live updates for the public dashboard, the clip bucket, and retention.
--
-- Writes never use the Data API. The local FastAPI service connects to Postgres as the project
-- owner through the session pooler and inserts; the browser only ever holds the publishable key,
-- which acts as `anon`. So: RLS on every table, explicit grants, and read policies only on what
-- the public showcase may show.

alter table public.anomalies          enable row level security;
alter table public.cycles             enable row level security;
alter table public.station_health     enable row level security;
alter table public.health_events      enable row level security;
alter table public.host_health        enable row level security;   -- no policy: invisible to the API
alter table public.health_chart_state enable row level security;   -- no policy: invisible to the API

revoke all on public.anomalies, public.cycles, public.station_health, public.health_events,
              public.host_health, public.health_chart_state, public.ftr_hourly
  from anon, authenticated;
grant select on public.anomalies, public.cycles, public.station_health, public.health_events,
                public.ftr_hourly
  to anon, authenticated;

create policy showcase_read on public.anomalies      for select to anon, authenticated using (true);
create policy showcase_read on public.cycles         for select to anon, authenticated using (true);
create policy showcase_read on public.station_health for select to anon, authenticated using (true);
create policy showcase_read on public.health_events  for select to anon, authenticated using (true);

-- Live updates: the Vercel dashboard subscribes to new anomalies and cycles over Realtime.
alter publication supabase_realtime add table public.anomalies, public.cycles;

-- Clips: public read, because the showcase plays them and they show only your own hands at a desk
-- or rendered ones. The day real workers are filmed, make it private and serve signed URLs (spec: "Retention and privacy").
insert into storage.buckets (id, name, public)
values ('clips', 'clips', true)
on conflict (id) do nothing;

-- Retention, in the database itself. Clips are pruned by the API through the Storage API instead:
-- deleting rows from storage.objects would orphan the files.
create extension if not exists pg_cron with schema pg_catalog;
select cron.schedule('host-health-30d',    '17 3 * * *',
  $$delete from public.host_health    where ts           < now() - interval '30 days'$$);
select cron.schedule('station-health-1y',  '27 3 * * *',
  $$delete from public.station_health where bucket_start < now() - interval '365 days'$$);
