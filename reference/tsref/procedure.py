"""Generic procedure interpreter: the executable specification of the C++ ProcedureEngine.

A profile is data: inspection points (IPs) with a state family, and steps over them.
A state family is an ORDERED list (absent < seated < fastened): reaching a later state also
satisfies steps that ask for an earlier one, and only a move to a lower state is a regression.
  complete  — every target reaches `state`, any order
  sequence  — targets reach `state` in exactly the listed order
  requires  — a step may not make progress before its prerequisite steps are complete
  max_duration_s — a step that is active too long is a timeout
  evidence  — a vision transition must be corroborated by a plant signal in a time window
The release gate is where omissions are caught: nothing proves a step was skipped until the
deadline passes, so every required step must have one (I15)."""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any

@dataclass
class Violation:
    t_ms: int
    kind: str           # SEQUENCE | PRECEDENCE | STEP_TIMEOUT | EVIDENCE_MISSING | REGRESSION | INCOMPLETE_AT_RELEASE
    step: str
    detail: dict

@dataclass
class _Step:
    id: str
    kind: str
    targets: list
    state: str
    requires: list = field(default_factory=list)
    max_duration_s: float | None = None
    evidence: dict | None = None
    done: list = field(default_factory=list)
    activated_ms: int | None = None
    completed_ms: int | None = None
    timed_out: bool = False

class Procedure:
    def __init__(self, profile: dict):
        self.profile = profile["profile"]
        self.rank = {ip["id"]: {s: i for i, s in enumerate(ip["states"])} for ip in profile["inspection_points"]}
        self.steps = {s["id"]: _Step(s["id"], s["kind"], list(s["targets"]), s["state"],
                                     list(s.get("requires", [])), s.get("max_duration_s"),
                                     s.get("evidence")) for s in profile["steps"]}
        self.by_target: dict[str, list[_Step]] = {}
        for s in self.steps.values():
            for tgt in s.targets:
                if s.state not in self.rank[tgt]:
                    raise ValueError(f"{s.id}: state {s.state!r} not in {tgt}'s family")  # fail at load
                self.by_target.setdefault(tgt, []).append(s)
        for lst in self.by_target.values():
            lst.sort(key=lambda s: self.rank[s.targets[0]][s.state])
        self.violations: list[Violation] = []
        self.tags: list[tuple[int, str, Any]] = []
        self.pending: list[tuple[int, _Step, str]] = []    # (deadline_ms, step, target) awaiting evidence

    # -- lifecycle -----------------------------------------------------------------------
    def start(self, t_ms: int):
        for s in self.steps.values():
            if not s.requires:
                s.activated_ms = t_ms

    def _complete(self, s: _Step) -> bool:
        return s.completed_ms is not None

    def _maybe_activate(self, t_ms: int):
        for s in self.steps.values():
            if s.activated_ms is None and all(self._complete(self.steps[r]) for r in s.requires):
                s.activated_ms = t_ms

    def _flag(self, t, kind, step, **detail):
        self.violations.append(Violation(t, kind, step, detail))

    # -- events --------------------------------------------------------------------------
    def tag(self, t_ms: int, node: str, value):
        self.tags.append((t_ms, node, value))
        self.pending = [(d, s, tgt) for d, s, tgt in self.pending
                        if not (node == s.evidence["tag"] and value == s.evidence["equals"])]

    def state(self, t_ms: int, ip: str, new: str):
        r = self.rank[ip][new]
        for s in self.by_target.get(ip, []):
            need = self.rank[ip][s.state]
            if ip in s.done and r < need:                       # moved back below the step's state
                self._flag(t_ms, "REGRESSION", s.id, ip=ip, now=new)
                s.done = [] if s.kind == "sequence" else [x for x in s.done if x != ip]  # a loosened bolt: redo the pattern
                s.completed_ms = None
            elif ip not in s.done and r >= need:                # reached, or skipped past, the step's state
                self._advance(t_ms, s, ip)

    def _advance(self, t_ms: int, s: "_Step", ip: str):
        if s.activated_ms is None:
            missing = [q for q in s.requires if not self._complete(self.steps[q])]
            self._flag(t_ms, "PRECEDENCE", s.id, ip=ip, before=missing)
        elif s.kind == "sequence":
            expected = s.targets[len(s.done)]
            if ip != expected:
                self._flag(t_ms, "SEQUENCE", s.id, observed=ip, expected=expected)
        s.done.append(ip)
        if s.evidence:
            w = s.evidence["within_ms"]
            seen = any(n == s.evidence["tag"] and v == s.evidence["equals"] and t_ms - w <= tt <= t_ms
                       for tt, n, v in self.tags)
            if not seen:
                self.pending.append((t_ms + w, s, ip))
        if set(s.done) >= set(s.targets) and s.completed_ms is None:
            s.completed_ms = t_ms
            self._maybe_activate(t_ms)

    def tick(self, t_ms: int):
        for d, s, ip in [p for p in self.pending if p[0] < t_ms]:
            self._flag(t_ms, "EVIDENCE_MISSING", s.id, ip=ip, signal=s.evidence["tag"])
        self.pending = [p for p in self.pending if p[0] >= t_ms]
        for s in self.steps.values():
            if (s.max_duration_s and s.activated_ms is not None and not self._complete(s)
                    and not s.timed_out and t_ms - s.activated_ms > s.max_duration_s * 1000):
                s.timed_out = True
                self._flag(t_ms, "STEP_TIMEOUT", s.id, missing=[x for x in s.targets if x not in s.done])

    def release(self, t_ms: int) -> bool:
        """The release gate: the last chance to catch an omission before the part moves on."""
        self.tick(t_ms)
        ok = True
        for s in self.steps.values():
            if not self._complete(s):
                ok = False
                self._flag(t_ms, "INCOMPLETE_AT_RELEASE", s.id,
                           missing=[x for x in s.targets if x not in s.done])
        return ok and not any(v.kind in ("SEQUENCE", "PRECEDENCE", "EVIDENCE_MISSING") for v in self.violations)
