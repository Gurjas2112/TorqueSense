# ADR 0008 — Supabase, without TimescaleDB

- **Status:** Accepted
- **Spec:** Section 16; Appendix A, item 31

## Context

The local TimescaleDB and Garage containers cost about 1.2 GB of a 4 GB WSL2 cap. Supabase marks
the `timescaledb` extension deprecated on Postgres 17 and ships only its Apache edition, which
lacks continuous aggregates, compression and retention policies.

## Decision

One free Supabase project: Postgres for events and health, Storage (S3 API) for clips, and
Realtime for the public dashboard. The schema is plain Postgres: `date_bin`, a plain view and
`pg_cron` retention. Every write goes through the local SQLite outbox (`api/outbox.py`), in order
and idempotent.

## Consequences

- The network is never on the alert path; a dropped link loses nothing.
- Schema changes only as new files in `supabase/migrations/`.
- `DATABASE_URL` and the S3 keys stay server-side. The browser holds only the publishable key, and
  RLS limits it to the showcase tables.
