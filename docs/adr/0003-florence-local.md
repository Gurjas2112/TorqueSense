# ADR 0003 — Florence-2, local

- **Status:** Accepted
- **Spec:** Sections 4, 13, 30; Appendix A, items 3, 4, 6

## Context

The "LLM" half of the rubric's "CV + LLM" needs a vision-language model. An earlier design served
Qwen2.5-VL from a Colab T4 through a tunnel, and a free session idling out mid-demo was the largest
demo-day risk.

## Decision

Run Florence-2-base (230M parameters) locally in a Python sidecar on the same GPU, as an
adjudicator for ambiguous inspection points only. It is reached through a circuit breaker on the
io thread and is never on the alert path (I3, I11). It is pinned to a commit (I12).

## Consequences

- About 0.46 GB of fp16 weights, 1.2–1.5 GB as a process: fits beside the detector in 6 GB
  (Section 13).
- 200–400 ms per call: it can delay a state commit, never cause or block an alert.
- Its agreement rate on ambiguous crops must be measured before it is trusted (Section 30).
