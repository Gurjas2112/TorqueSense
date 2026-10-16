"""One interpreter, four assemblies. No test here touches profile-specific code, because
there is none: every behaviour comes from the profile's data."""
from tsref.procedure import Procedure

def run(prof, events, release_at=None):
    p = Procedure(prof); p.start(0)
    for e in events:
        kind, t = e[0], e[1]
        if kind == "s": p.state(t, e[2], e[3])
        elif kind == "g": p.tag(t, e[2], e[3])
        p.tick(t)
    ok = p.release(release_at) if release_at is not None else None
    return ok, [(v.kind, v.step, v.detail) for v in p.violations]

def kinds(v): return [k for k, _, _ in v]

NR = "Station.W14.Nutrunner.ResultOK"
def wheel_events(order, nut=True, t0=1000):
    ev = [("s", t0 + 100 * i, f"bolt-{b}", "seated") for i, b in enumerate(range(1, 6))]
    for i, b in enumerate(order):
        t = t0 + 2000 + 800 * i
        if nut: ev.append(("g", t - 100, NR, True))
        ev.append(("s", t, f"bolt-{b}", "fastened"))
    return ev

# --- wheel fitment -----------------------------------------------------------------------
def test_wheel_correct_star(profile):
    ok, v = run(profile("wheel_fitment"), wheel_events([1, 3, 5, 2, 4]), release_at=12000)
    assert ok and v == []

def test_wheel_adjacent_order(profile):
    ok, v = run(profile("wheel_fitment"), wheel_events([1, 2, 3, 4, 5]), release_at=12000)
    assert not ok and v[0] == ("SEQUENCE", "torque", {"observed": "bolt-2", "expected": "bolt-3"})

def test_wheel_missing_bolt_caught_by_deadline_and_release(profile):
    ev = [e for e in wheel_events([1, 3, 5, 2, 4]) if "bolt-4" not in e[2]]
    ok, v = run(profile("wheel_fitment"), ev + [("t", 30000)], release_at=31000)
    assert not ok and "STEP_TIMEOUT" in kinds(v) and "INCOMPLETE_AT_RELEASE" in kinds(v)

def test_wheel_looks_torqued_but_no_nutrunner_result(profile):
    ok, v = run(profile("wheel_fitment"), wheel_events([1, 3, 5, 2, 4], nut=False) + [("t", 9000)], release_at=12000)
    assert not ok and kinds(v).count("EVIDENCE_MISSING") == 5

def test_wheel_loosened_bolt_resets_the_pattern(profile):
    ev = wheel_events([1, 3, 5, 2, 4]) + [("s", 7000, "bolt-3", "seated")]
    ok, v = run(profile("wheel_fitment"), ev, release_at=12000)
    assert not ok and kinds(v) == ["REGRESSION", "INCOMPLETE_AT_RELEASE"]

# --- door harness (PS example) ---------------------------------------------------------------
LOCK = "Station.D07.ConnectorLock"
def door(clips_closed, panel_t, lock=True):
    ev = [("s", 100 * i, f"clip-{c}", "closed") for i, c in enumerate(clips_closed, 1)]
    if lock: ev.append(("g", 900, LOCK, True))
    ev += [("s", 1000, "connector-1", "mated"), ("s", panel_t, "panel-1", "installed")]
    return ev

def test_door_all_clips_then_panel(profile):
    ok, v = run(profile("door_harness"), door([1, 2, 3, 4, 5, 6], 5000), release_at=6000)
    assert ok and v == []

def test_door_panel_hides_an_open_clip(profile):
    ok, v = run(profile("door_harness"), door([1, 2, 3, 5, 6], 5000), release_at=6000)
    assert not ok and v[0] == ("PRECEDENCE", "trim_panel", {"ip": "panel-1", "before": ["clips"]})

def test_door_connector_looks_mated_but_lock_switch_never_closed(profile):
    ok, v = run(profile("door_harness"), door([1, 2, 3, 4, 5, 6], 5000, lock=False), release_at=6000)
    assert not ok and ("EVIDENCE_MISSING", "connector") in [(k, s) for k, s, _ in v]

# --- windshield (PS example) ---------------------------------------------------------------
def test_windshield_glass_before_gasket_fully_seated(profile):
    ev = [("s", 100 * i, f"gasket-{g}", "seated") for i, g in enumerate([1, 2, 3, 4, 5, 7, 8], 1)]
    ok, v = run(profile("windshield"), ev + [("s", 2000, "glass-1", "placed")], release_at=3000)
    assert not ok and v[0][0] == "PRECEDENCE" and v[0][2]["before"] == ["gasket"]

def test_windshield_correct(profile):
    ev = [("s", 100 * g, f"gasket-{g}", "seated") for g in range(1, 9)] + [("s", 2000, "glass-1", "placed")]
    ok, v = run(profile("windshield"), ev, release_at=3000)
    assert ok and v == []

# --- electronics enclosure (non-automotive) -------------------------------------------------
def test_enclosure_crisscross_then_label(profile):
    ev = [("s", 100, "connector-1", "mated")]
    ev += [("s", 1000 + 300 * i, f"screw-{s}", "fastened") for i, s in enumerate([2, 5, 3, 4, 6, 1])]
    ev += [("s", 4000, "label-1", "applied")]
    ok, v = run(profile("electronics_enclosure"), ev, release_at=5000)
    assert ok and v == []

def test_enclosure_label_forgotten_is_caught_at_release(profile):
    ev = [("s", 100, "connector-1", "mated")]
    ev += [("s", 1000 + 300 * i, f"screw-{s}", "fastened") for i, s in enumerate([2, 5, 3, 4, 6, 1])]
    ok, v = run(profile("electronics_enclosure"), ev, release_at=5000)
    assert not ok and v == [("INCOMPLETE_AT_RELEASE", "label", {"missing": ["label-1"]})]

def test_a_state_outside_its_family_is_rejected_at_load(profile):
    import copy, pytest
    bad = copy.deepcopy(profile("wheel_fitment"))
    bad["steps"][0]["state"] = "closed"                        # a clip state on a bolt
    with pytest.raises(ValueError):
        Procedure(bad)

def test_jumping_past_a_state_satisfies_the_earlier_step_first():
    # one screw, seen absent and then fastened: the "seat" step completes, which activates
    # "fasten", which the same transition then satisfies — no false precedence alarm
    prof = {"profile": "mini", "inspection_points": [{"id": "s-1", "states": ["absent", "seated", "fastened"]}],
            "steps": [{"id": "seat", "kind": "complete", "targets": ["s-1"], "state": "seated"},
                      {"id": "fasten", "kind": "complete", "targets": ["s-1"], "state": "fastened", "requires": ["seat"]}]}
    ok, v = run(prof, [("s", 100, "s-1", "fastened")], release_at=200)
    assert ok and v == []

def test_wheel_torquing_before_every_bolt_is_started_is_a_precedence_violation(profile):
    ev = [e for e in wheel_events([1, 3, 5, 2, 4]) if not (e[2] == "bolt-4" and e[3] == "seated")]
    ok, v = run(profile("wheel_fitment"), ev, release_at=12000)
    assert not ok and v[0] == ("PRECEDENCE", "torque", {"ip": "bolt-1", "before": ["hand_start"]})
