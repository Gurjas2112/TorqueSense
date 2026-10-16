import json, sys, pathlib, threading
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import watchdog

NOW = datetime(2026, 10, 16, 12, 0, tzinfo=timezone.utc)


def test_fresh_and_stale():
    fresh = ("W-14", NOW - timedelta(minutes=7))
    stale = ("W-14", NOW - timedelta(hours=3))
    assert watchdog.verdict(fresh, True, NOW)[0] == 0
    assert watchdog.verdict(stale, True, NOW)[0] == 1
    assert watchdog.verdict(stale, False, NOW)[0] == 0          # laptop off on purpose: keep-alive only
    assert watchdog.verdict(None, False, NOW)[0] == 0


def test_reads_the_newest_row_with_the_public_key():
    seen = {}

    class H(BaseHTTPRequestHandler):
        def do_GET(self):
            seen["path"], seen["key"] = self.path, self.headers["apikey"]
            body = json.dumps([{"station_id": "W-14", "bucket_start": "2026-10-16T11:55:00+00:00"}]).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json"); self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a): pass

    srv = HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        row = watchdog.newest_bucket(f"http://127.0.0.1:{srv.server_port}", "sb_publishable_x")
    finally:
        srv.shutdown()
    assert row == ("W-14", datetime(2026, 10, 16, 11, 55, tzinfo=timezone.utc))
    assert seen["key"] == "sb_publishable_x"
    assert "order=bucket_start.desc" in seen["path"] and "limit=1" in seen["path"]
