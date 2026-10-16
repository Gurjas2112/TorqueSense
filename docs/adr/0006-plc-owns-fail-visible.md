# ADR 0006 — The PLC owns fail-visible

- **Status:** Accepted
- **Spec:** Sections 14, 19; invariant I13

## Context

A quality gate that silently stops checking is worse than none. A dead engine cannot report that
it is dead, and the fleet agent runs on the same PC.

## Decision

The alert thread increments `Station.<id>.Heartbeat` every 500 ms, and only when the verify
thread's frame counter has moved. The PLC (here the soft PLC) checks every 250 ms. If the counter
has not changed for 2 s, it sets `InspectionOnline = false` and `Andon.Amber = true`.

## Consequences

- A frozen camera, a hung thread and a crashed process all look the same: AMBER within about
  2.25 s.
- A station is AMBER from PLC start until the first beat: not ready is not OK.
- Liveness is never faked. Nothing increments the heartbeat except verified frames.
