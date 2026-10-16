# ADR 0002 — Garage, not MinIO

- **Status:** Superseded for the prototype by [ADR 0008](0008-supabase-without-timescaledb.md)
  (clips live in Supabase Storage); still the plant choice in Appendix D.
- **Spec:** Appendix A, item 1; Appendix D.3

## Context

Clips need an S3-compatible object store. MinIO stopped publishing community Docker images in
October 2025 and archived its repository in 2026.

## Decision

Use Garage for self-hosted object storage.

## Consequences

- The sidecar's `boto3` code speaks S3 and does not care which server answers.
- In the prototype, Supabase Storage's S3 API replaces the local Garage container (ADR 0008).
- In the plant (Appendix D), Garage runs with three nodes and replication factor 3.
