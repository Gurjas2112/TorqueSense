import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient
from api.spa import mount_spa


@pytest.fixture
def client(tmp_path):
    dist = tmp_path / "dist"
    (dist / "assets").mkdir(parents=True)
    (dist / "index.html").write_text("<div id=app></div>")
    (dist / "assets" / "index-3f9a.js").write_text("console.log(1)")
    (dist / "favicon.svg").write_text("<svg/>")
    (tmp_path / "secret.env").write_text("SUPABASE_SECRET=x")
    app = FastAPI()

    @app.get("/v1/health/host")
    def host():
        return {"ok": True}

    mount_spa(app, dist)
    return TestClient(app)


def test_deep_link_returns_index(client):
    r = client.get("/hmi/W-14")
    assert r.status_code == 200 and "id=app" in r.text and r.headers["cache-control"] == "no-cache"


def test_hashed_asset_is_immutable(client):
    r = client.get("/assets/index-3f9a.js")
    assert r.text == "console.log(1)" and "immutable" in r.headers["cache-control"]


def test_api_routes_win_and_unknown_api_is_404(client):
    assert client.get("/v1/health/host").json() == {"ok": True}
    assert client.get("/v1/nope").status_code == 404


def test_no_escape_from_dist(client):
    r = client.get("/..%2fsecret.env")
    assert "SUPABASE_SECRET" not in r.text


def test_missing_build_is_a_startup_error(tmp_path):
    with pytest.raises(RuntimeError):
        mount_spa(FastAPI(), tmp_path / "nothing")
