# ADR 0004 — Jenkins for the GPU gates

- **Status:** Accepted
- **Spec:** Sections 20, 31

## Context

The latency (I4), accuracy (I10) and soak (I7) gates need the RTX 3060. Hosted CI has no GPU.
A GitHub self-hosted runner on a public repository lets a fork's pull request run code on the
laptop.

## Decision

Run a Jenkins LTS controller in Docker (profile `ci`, `127.0.0.1:8081`) and an inbound agent on
HSGPC under the owner's own account. It polls `main` only. GitHub Actions keeps everything that
needs no GPU.

## Consequences

- No inbound connection to the laptop; fork code never runs on it.
- Port 8081, never 8080 (the engine's WebSocket).
- The pipeline records clocks rather than locking them (`conditions.json`), because locking needs
  Administrator rights.
