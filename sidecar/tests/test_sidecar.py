"""The sidecar without a GPU: the clip ring read from Python (layout and seqlock), clip keys, phrases,
the grounding test, and the API's refusals while Florence-2 is unavailable. Florence is mocked in CI."""
import struct, sys, pathlib, uuid

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import clips          # noqa: E402
from florence import FlorenceAdjudicator, phrase_for   # noqa: E402


class FakeRing:
    """The same seqlock contract as the engine's ring, held in a bytearray."""
    def __init__(self, slots=4):
        self.slots, self.frames = slots, {}

    def publish(self, n, data):
        self.frames[n % self.slots] = (n, data)

    def read(self, n):
        got = self.frames.get(n % self.slots)
        return (got[1], 0, n) if got and got[0] == n else None


def test_frames_before_keeps_only_what_the_ring_still_holds():
    r = FakeRing(4)
    for n in range(10):
        r.publish(n, bytes([n]))
    assert clips.frames_before(r, 10, 180) == [bytes([n]) for n in range(6, 10)]


def test_clip_keys_follow_the_storage_layout():
    import datetime as dt
    k = clips.clip_key("W-14", "c1", "e1", dt.datetime(2026, 10, 16, tzinfo=dt.timezone.utc))
    assert k == "clips/W-14/2026/10/16/c1/e1.mp4"


def test_every_family_has_a_phrase():
    assert phrase_for("threaded_fastener", "fastened") == "yellow marked bolt head"
    assert phrase_for("gear", "meshed") == "meshed gear"            # an unknown family still gets words


def test_a_grounded_box_must_sit_on_the_point():
    assert FlorenceAdjudicator.on_point([[100, 100, 156, 156]], 128, 128, 20)
    assert not FlorenceAdjudicator.on_point([[0, 0, 30, 30]], 128, 128, 20)


@pytest.mark.skipif(sys.platform != "win32", reason="named Windows mappings")
def test_the_python_reader_parses_the_engine_layout():
    import mmap
    from clipring import ClipRing, PAYLOAD
    name = "pytest_" + uuid.uuid4().hex[:8]
    slots, slot_bytes = 4, 64
    m = mmap.mmap(-1, PAYLOAD + slots * slot_bytes, tagname=f"Local\\torquesense_{name}")
    m[4:16] = struct.pack("<III", 1, slots, slot_bytes)
    m[0:4] = b"TSCR"
    payload = b"\xff\xd8jpeg"
    n = 6                                                        # frame 6 lives in slot 2
    struct.pack_into("<QQQI", m, 128 + (n % slots) * 32, 2 * n + 2, 123, 99, len(payload))
    m[PAYLOAD + (n % slots) * slot_bytes: PAYLOAD + (n % slots) * slot_bytes + len(payload)] = payload
    struct.pack_into("<Q", m, 64, n + 1)
    r = ClipRing(name)
    assert r.write_seq() == 7
    assert r.read(6) == (payload, 123, 99)
    assert r.read(2) is None                                     # overwritten by frame 6
    struct.pack_into("<Q", m, 128 + (n % slots) * 32, 2 * n + 1) # writer mid-copy: never a torn read
    assert r.read(6) is None
    r.close(); m.close()


def test_adjudication_refuses_while_florence_is_unavailable(monkeypatch):
    monkeypatch.setenv("TS_FLORENCE", "0")
    import importlib.util
    spec = importlib.util.spec_from_file_location("sidecar_main", pathlib.Path(__file__).parents[1] / "main.py")
    main = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(main)
    from fastapi.testclient import TestClient
    import time
    time.sleep(0.1)
    c = TestClient(main.app)
    assert c.get("/health").json()["florence"] == "unavailable"
    r = c.post("/v1/adjudicate", json={"request_id": "r", "station_id": "W-14", "ip": "bolt-1", "family": "threaded_fastener",
                                       "candidate_state": "fastened", "ring_seq": 1, "crop": {"x": 0, "y": 0, "w": 96, "h": 96}})
    assert r.status_code == 503          # the engine's circuit breaker counts this; nothing waits on it
