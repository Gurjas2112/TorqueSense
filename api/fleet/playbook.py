"""From signal to cause (spec Section 19, "the playbook"): which signals moved together, and what that
pattern usually means. The fleet view shows this next to every open drift episode."""
from __future__ import annotations

# (signals that must be drifting, signals that must NOT be drifting, likely cause, action)
RULES: list[tuple[set[str], set[str], str, str]] = [
    ({"ip_conf_mean", "ambiguous_rate"}, {"evidence_p99_ms"},
     "lens contamination or a lighting change", "clean the lens or check lighting at the next break"),
    ({"anchor_residual_px"}, set(),
     "camera knocked or mount loose", "check the ghost overlay alignment; re-tighten and re-calibrate"),
    ({"evidence_p99_ms", "sm_clock_ratio"}, set(),
     "thermal throttling", "check fans, filters and cabinet temperature"),
    ({"evidence_p99_ms"}, {"sm_clock_ratio"},
     "contention: another process on the GPU or the pinned cores", "check what else is running"),
    ({"dropped_frame_rate"}, set(),
     "capture link, decode or disk stalls", "check the camera cable and the clip writer's free disk"),
    ({"ambiguous_rate"}, {"ip_conf_mean"},
     "partial occlusion: a cable or fixture in view", "inspect the station"),
    ({"ip_conf_mean"}, set(),
     "lens contamination, lighting or focus", "clean the lens or check lighting at the next break"),
]


def diagnose(drifting: set[str]) -> dict | None:
    """The first rule whose signals all drift and whose exclusions do not."""
    for need, exclude, cause, action in RULES:
        if need <= drifting and not (exclude & drifting):
            return {"signals": sorted(need), "likely_cause": cause, "action": action}
    if drifting:
        return {"signals": sorted(drifting), "likely_cause": "unrecognised pattern", "action": "inspect the station"}
    return None
