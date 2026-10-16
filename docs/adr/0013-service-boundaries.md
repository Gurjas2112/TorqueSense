# ADR 0013 — Services at the network boundaries; one process for the alert path

- **Status:** Accepted
- **Date:** 2026-10-16
- **Spec:** Sections 13, 14, 22, 30; invariants I1, I4, I11, I13

## Context

The owner asked whether TorqueSense should be a microservice architecture. Two different things go by
that name. One is the deployment shape: independently built, deployed and scaled services that own
their data and speak versioned contracts. The other is splitting work into many small networked
services. Here the answer differs depending on which part of the system you look at.

TorqueSense already ships as seven independently deployable services:

| Service | Language | Owns | Speaks | Scales by |
|---|---|---|---|---|
| **engine** (`torquesense.exe`, one per station) | C++20 | the verdict, the clip ring | OPC-UA to the PLC; WebSocket, MJPEG, `/health`, `/state` to the web; events to the API (`contracts/`) | one process per camera/station |
| **soft PLC** (`softplc.exe`) | C (open62541) | Andon, hold request, the heartbeat watchdog | OPC-UA | one per line |
| **sidecar** | Python, FastAPI | Florence-2 adjudication, clip cutting | HTTP `/v1/adjudicate`, `/v1/clip` | one per GPU |
| **API** | Python, FastAPI | the outbox, history, EWMA drift | HTTP `/v1/*`; Postgres and S3 to Supabase | stateless but for the outbox file |
| **fleet agent** | Go | host health, its spool | HTTP to the API | one per host |
| **Node-RED** | Node | the operator's view of the PLC tags | OPC-UA | one per line |
| **web** | React 19 | nothing (static) | the API and the engine | a CDN (Vercel) |

Each has its own process, build, tests, health endpoint and failure behaviour. The contracts between
them are JSON Schemas in `contracts/`, generated into C++ and TypeScript, so a contract change fails CI
in every consumer. There are Dockerfiles and Kustomize manifests for the engine, the API and the agent,
and the network-pull drill shows each one keeps working when its neighbour's link is gone.

The question that remains is whether the engine's inside should become services too: capture,
inference, verify, alert and io as separate networked processes.

## Decision

**No. The alert path stays one process: four pinned threads joined by lock-free SPSC rings.** The
service boundaries stay where they are, at the points where a network hop costs nothing the operator
can feel.

Splitting the alert path would break invariants the design rests on:

1. **Latency (I4).** The budget is 150 ms evidence p99. Inside one process a ring hop costs under 0.1
   ms and copies a fixed 4 KB `StateVector`. A network hop costs serialisation, a syscall pair and
   scheduling jitter on every frame, at 30 fps, on a laptop. Four hops would spend latency the spec has
   allotted to inference.
2. **No allocation, no lock on the hot path (I1).** Serialising to JSON or protobuf per frame
   allocates. The `NoAllocation` test would have to be dropped, not passed.
3. **Florence-2 stays off the alert path (I11).** Today this is enforced at link time: the verify
   library cannot link the HTTP client. Once every stage is networked, that guarantee becomes a
   convention.
4. **Fail-visible (I13).** One process that dies stops its heartbeat, and the PLC turns the station
   amber. Five processes create partial failures, such as inference up and verify down, that a single
   heartbeat cannot describe.
5. **Nothing to scale independently.** Every stage runs once per frame of one camera, so the unit of
   scaling is the station, which is already one process per camera.

## Consequences

- The deployable units are the seven services above. A new assembly is a profile folder, not a new
  service, and a new station is another engine process.
- What would justify another service is stated here: a stage that must run on different hardware than
  the engine (for example a shared inference server for many cameras, Appendix D Tier 2), or one with a
  different release cadence. It would get a contract in `contracts/`, a health endpoint, and a place in
  the drill.
- `README.md` shows the service map, the ports and the contracts.
