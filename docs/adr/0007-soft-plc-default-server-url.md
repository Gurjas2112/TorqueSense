# ADR 0007 — Soft PLC: leave the server URL at its default

- **Status:** Accepted
- **Spec:** Section 29; Appendix A, item 18

## Context

v4.0 told the soft PLC to override `serverUrls` with `host.docker.internal` so containers could
resolve it. Tested against open62541 1.4.21, the server already echoes each client's own URL in
GetEndpoints. In 1.4, `serverUrls` also sets the bind address: a name that is not a local
interface leaves the server with no socket, and it exits.

## Decision

The soft PLC calls `UA_ServerConfig_setMinimal(cfg, port, NULL)` and nothing more. It never sets
`serverUrls`.

## Consequences

- Clients connect with whatever name reaches the server (`localhost`, `host.docker.internal`, a
  Service name).
- The server binds all interfaces by open62541's default. With Node-RED native, no firewall rule
  is needed.
