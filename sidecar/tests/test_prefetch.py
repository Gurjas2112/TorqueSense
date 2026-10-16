import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
import pytest
from sidecar.prefetch import prefetch, read_env

SHA = "0123456789abcdef0123456789abcdef01234567"


def test_pinned_commit_is_downloaded():
    calls = {}
    path = prefetch("florence-community/Florence-2-base", SHA, download=lambda **kw: calls.update(kw) or "/cache/x")
    assert path == "/cache/x" and calls == {"repo_id": "florence-community/Florence-2-base", "revision": SHA}


@pytest.mark.parametrize("rev", ["", "main", SHA[:12]])
def test_unpinned_revision_is_refused(rev):
    with pytest.raises(SystemExit):
        prefetch("m", rev, download=lambda **kw: "/never")


def test_read_env(tmp_path):
    p = tmp_path / ".env"
    p.write_text(f'# c\nFLORENCE_MODEL=florence-community/Florence-2-base\nFLORENCE_REVISION="{SHA}"\n')
    assert read_env(p)["FLORENCE_REVISION"] == SHA
