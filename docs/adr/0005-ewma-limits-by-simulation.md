# ADR 0005 — EWMA limits set by simulation

- **Status:** Accepted
- **Spec:** Section 19 ("Drift detection — limits set by simulation, not by habit")

## Context

Station health is watched with one-sided EWMA control charts. The textbook λ 0.2, L 3 produces
about 48 false drift tickets per station-month. The budget is one.

## Decision

λ = 0.05, L = 4.2, a 288-bucket (24 h) phase-I baseline from the median and scaled MAD, a σ floor
per signal and six buckets of hysteresis — chosen by Monte Carlo simulation with an *estimated*
baseline (about 0.92 false tickets per station-month).

## Consequences

- `api/fleet/ewma.py` parameters change only with this simulation re-run and a new ADR.
- The simulation assumes independent buckets. Real data is autocorrelated, so every ticket's
  outcome is logged and L is recalibrated after two weeks of real data.
- Baselines reset when `model_version` or `cad_revision` changes.
