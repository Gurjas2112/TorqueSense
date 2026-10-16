# ADR 0010 — Prototype scope: production deferred on purpose

- **Status:** Accepted
- **Date:** 2026-10-16
- **Spec:** `docs/SPEC.md` preface P.1; Section 33; Appendix D

## Context

The PS4 deliverable is a working prototype on a pilot station that captures video, identifies a
missing step and triggers a physical alert in under one second. Spec v5.5 adds a production track
(Section 33, Phase 13): a Windows `tsdemo` account with Autologon, releases installed under
`C:\TorqueSense`, `Jenkinsfile.release`, a power-cut test and a nightly backup. The owner decided
to build the prototype first and keep the production track for later, on purpose.

Three facts of the actual setup also differ from what the spec assumes:

1. The Supabase project was created in **ap-southeast-2 (Sydney)**, not Mumbai.
2. The PS asks for a flow-wise set of screenshots and a full-length screen recording, kept with
   the code.
3. The GPU runtime libraries (cuDNN 9, ONNX Runtime 1.28.0 `gpu_cuda13`, TensorRT 10.13.3.9) and
   the MSYS2 C++ packages are not yet installed on HSGPC.

## Decision

1. **Phase 13 is deferred.** Its starter-kit files (supervisor, station scripts, release tooling,
   workflows) stay in the repository and keep running in CI. Nothing is installed under
   `C:\TorqueSense`, and no release is tagged or published. Development runs from the build tree
   with `scripts/start-dev.ps1`.
2. **The Sydney region is accepted.** Only the outbox's flush to Supabase crosses the network, and
   it is never on the alert path.
3. **One exception to I8.** `app_screenshots_screen_recording/` holds the PS deliverable recording
   and screenshots, compressed, and nothing else. Scenario videos, renders and weights stay out of
   git.
4. **Missing GPU libraries are 👤 steps.** Gates that need them (G2, I4, I7, I10, the TensorRT
   gate) are reported as *not run*, never estimated, until they are installed.

## Consequences

- Phase 13's gate (the Section 33 release checklist) is not claimed.
- Every invariant I1–I17 still applies to what is built.
- When production resumes, this ADR is superseded and Phase 13 starts from the starter kit as is.
