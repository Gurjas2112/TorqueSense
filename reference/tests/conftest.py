import pathlib, sys, yaml
import pytest
sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from tsref import compiler, optics

ROOT = pathlib.Path(__file__).parents[1]
SAMPLES = ROOT.parent / "samples"
# the unit tests use the sample assemblies' own profiles: one copy of each, in samples/
NAMES = {"wheel_fitment": "wheel-hub-5lug", "door_harness": "door-trim-harness",
         "windshield": "windshield-gasket", "electronics_enclosure": "electronics-enclosure"}

def cams(m):
    return [optics.Camera(**{k: (tuple(v) if isinstance(v, list) else v) for k, v in c.items()}) for c in m["cameras"]]

@pytest.fixture
def profile():
    def load(name):
        m = yaml.safe_load((SAMPLES / NAMES.get(name, name) / "profile.yaml").read_text())
        prof, report, bad = compiler.compile_profile(m, cams(m), min_feature_px=16)
        assert not bad, f"unresolved inspection points: {bad}"
        return prof
    return load
