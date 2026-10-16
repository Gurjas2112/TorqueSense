"""The samples/ folder is the conformance suite: every assembly in it must pass the same
contract through the same engine. These tests also prove the contract has teeth — a sample that
breaks a rule is refused with the rule's name."""
import importlib.util, pathlib, shutil, sys, yaml
import pytest

SAMPLES = pathlib.Path(__file__).parents[2] / "samples"
spec = importlib.util.spec_from_file_location("run_samples", SAMPLES / "run_samples.py")
rs = importlib.util.module_from_spec(spec); sys.modules["run_samples"] = rs; spec.loader.exec_module(rs)

def test_every_sample_and_the_template_pass():
    reports = [rs.check(d) for d in rs.sample_dirs(template=True)]
    assert len(reports) >= 7
    assert [r.sample for r in reports if not r.ok] == []

def test_the_samples_together_exercise_every_violation_kind():
    covered = set().union(*(rs.check(d).kinds_covered for d in rs.sample_dirs()))
    assert covered == set(rs.VIOLATION_KINDS)

def test_desk_cover_order_is_derived_not_typed():
    r = rs.check(SAMPLES / "desk-cover-4screw")
    assert r.patterns == ["screws: circle 1,3,2,4"]          # a square of screws is tightened across

@pytest.fixture
def copy(tmp_path):
    def make(name="desk-cover-4screw"):
        d = tmp_path / name
        shutil.copytree(SAMPLES / name, d)
        return d
    return make

def edit_profile(d, fn):
    m = yaml.safe_load((d / "profile.yaml").read_text()); fn(m)
    (d / "profile.yaml").write_text(yaml.safe_dump(m, sort_keys=False))

def test_c1_a_point_too_small_to_see_is_refused(copy):
    d = copy(); edit_profile(d, lambda m: m["groups"][1].__setitem__("feature_mm", 2))
    assert any(p.startswith("C1 unresolved") for p in rs.check(d).problems)

def test_c1_a_camera_too_far_away_is_refused(copy):
    d = copy("desk-harness-clips")
    edit_profile(d, lambda m: m["cameras"][0].__setitem__("position_mm", [0, 0, 600]))
    assert any(p.startswith("C1 unresolved") for p in rs.check(d).problems)

def test_c2_a_step_without_a_deadline_is_refused(copy):
    d = copy(); edit_profile(d, lambda m: m["steps"][2].pop("max_duration_s"))
    assert any(p.startswith("C2") for p in rs.check(d).problems)

def test_c3_a_sample_without_a_correct_trace_is_refused(copy):
    d = copy(); (d / "traces" / "correct.yaml").unlink()
    assert any(p.startswith("C3") for p in rs.check(d).problems)

def test_c5_a_wrong_expectation_is_caught(copy):
    d = copy(); p = d / "traces" / "round_the_box.yaml"
    p.write_text(p.read_text().replace("expected: screw-3", "expected: screw-4"))
    r = rs.check(d)
    assert not r.ok and [t.name for t in r.traces if not t.ok] == ["round_the_box"]

def test_runner_cli_returns_zero(capsys):
    assert rs.main([]) == 0
    assert "violation kinds exercised: 6/6" in capsys.readouterr().out
