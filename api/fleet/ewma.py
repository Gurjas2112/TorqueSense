"""One-sided EWMA control charts for station-fleet health.

Parameters come from a fleet false-alarm budget, set by simulation, not from a table.
Budget: at most one false drift ticket per station per month (6 charts, 5-minute buckets).

With known parameters, lambda = 0.05 and L = 3.9 would meet it. With parameters
*estimated* from a phase I baseline they do not: estimation error in mu0 acts like a
permanent small shift. Simulated with a 288-bucket (24 h) median/MAD baseline:
  L = 3.9 -> ~28% of charts per month, ~1.7 tickets per station-month (over budget)
  L = 4.2 -> ~15% of charts per month, ~0.92 per station-month; median delay
            0.5 sigma: 73, 1 sigma: 17, 2 sigma: 5 buckets
Assumes independent, roughly normal bucket means; recalibrate L on two weeks of real data.
"""
from __future__ import annotations

import math
import statistics
from dataclasses import dataclass, field
from enum import Enum
from typing import Literal

Direction = Literal["up", "down"]


class State(str, Enum):
    BASELINE = "baseline"      # phase I: collecting buckets to estimate mu0, sigma0
    IN_CONTROL = "in_control"
    DRIFT = "drift"


@dataclass
class Chart:
    metric: str
    direction: Direction             # which way is "bad"
    sigma_floor: float               # stops a near-constant baseline producing sigma ~ 0
    lam: float = 0.05
    L: float = 4.2
    baseline_n: int = 288            # phase I: 24 h at 5-min buckets; shorter inflates false alarms
    clear_n: int = 6                 # buckets back inside limits before DRIFT clears

    state: State = State.BASELINE
    mu0: float | None = None
    sigma0: float | None = None
    z: float = 0.0                   # EWMA of the standardised, sign-adjusted value
    t: int = 0                       # buckets since phase I ended
    _phase1: list[float] = field(default_factory=list)
    _inside: int = 0

    def to_state(self) -> dict:
        """JSON-safe snapshot. Persisted every bucket: a 24 h baseline must survive a restart."""
        return {"metric": self.metric, "direction": self.direction, "sigma_floor": self.sigma_floor,
                "lam": self.lam, "L": self.L, "baseline_n": self.baseline_n, "clear_n": self.clear_n,
                "state": self.state.value, "mu0": self.mu0, "sigma0": self.sigma0, "z": self.z,
                "t": self.t, "phase1": list(self._phase1), "inside": self._inside}

    @classmethod
    def from_state(cls, s: dict) -> "Chart":
        c = cls(s["metric"], s["direction"], s["sigma_floor"], s["lam"], s["L"],
                s["baseline_n"], s["clear_n"])
        c.state, c.mu0, c.sigma0 = State(s["state"]), s["mu0"], s["sigma0"]
        c.z, c.t, c._phase1, c._inside = s["z"], s["t"], list(s["phase1"]), s["inside"]
        return c

    def reset(self) -> None:
        """Call when model_version or cad_revision changes: old limits are invalid."""
        self.state, self.mu0, self.sigma0 = State.BASELINE, None, None
        self.z, self.t, self._inside = 0.0, 0, 0
        self._phase1.clear()

    def limit(self) -> float:
        # exact time-varying limit; converges to L * sqrt(lam / (2 - lam))
        return self.L * math.sqrt(self.lam / (2 - self.lam) * (1 - (1 - self.lam) ** (2 * self.t)))

    def update(self, x: float) -> tuple[State, bool]:
        """Feed one bucket aggregate. Returns (state, entered_drift_now)."""
        if self.state is State.BASELINE:
            self._phase1.append(x)
            if len(self._phase1) >= self.baseline_n:
                self.mu0 = statistics.median(self._phase1)
                mad = statistics.median(abs(v - self.mu0) for v in self._phase1)
                self.sigma0 = max(1.4826 * mad, self.sigma_floor)   # robust to phase I outliers
                self.state = State.IN_CONTROL
            return self.state, False

        sign = 1.0 if self.direction == "up" else -1.0
        u = sign * (x - self.mu0) / self.sigma0
        self.t += 1
        self.z = self.lam * u + (1 - self.lam) * self.z
        out = self.z > self.limit()

        if self.state is State.IN_CONTROL and out:
            self.state, self._inside = State.DRIFT, 0
            return self.state, True
        if self.state is State.DRIFT:
            self._inside = 0 if out else self._inside + 1
            if self._inside >= self.clear_n:
                self.state = State.IN_CONTROL
        return self.state, False


def default_charts() -> list[Chart]:
    """The six per-station signals. Floors are in each metric's own units."""
    return [
        Chart("ip_conf_mean",   "down", sigma_floor=0.005),   # lens, lighting, focus
        Chart("ambiguous_rate",   "up",   sigma_floor=0.002),   # same causes, earlier
        Chart("evidence_p99_ms",  "up",   sigma_floor=2.0),     # thermal, contention
        Chart("sm_clock_ratio",   "down", sigma_floor=0.01),    # throttling
        Chart("dropped_frame_rate", "up", sigma_floor=0.001),   # capture or decode stalls
        Chart("anchor_residual_px", "up", sigma_floor=0.5),     # camera bumped: detections off CAD
    ]
