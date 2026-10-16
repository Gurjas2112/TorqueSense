"""The network-pull drill (spec Section 32, "Offline insurance"; Section 35: alerts need no network).

    python scripts\\drill_network_pull.py                 # about three minutes; writes var/drill/report.json

Proves both halves of the rehearsal step:
  1. Pull the network mid-cycle: alerts and the HMI carry on, and /health shows the outbox count rising.
  2. Restore it: the count drains to zero, and Supabase (what the Vercel page reads) fills in the gap, in order.

What is pulled. The only traffic that leaves the laptop is the API's record of what happened: Postgres to
Supabase's session pooler, and clips to Storage over S3. This drill starts its own API (port 8010, its own
outbox) whose two links run through a local link process — a TCP relay for Postgres and a CONNECT proxy for
S3 — and pulls *that*: every open connection is cut and new ones are refused, exactly what a dead link looks
like to the API. The engine, the soft PLC and the HMI run on localhost and never use the link, which is the
claim under test. It does not disable the laptop's adapter, so nothing else on the machine (or this
session) loses the network; pulling the physical cable remains a step of the final rehearsal on site.

Needs: build/windows-ci (torquesense.exe, softplc.exe), .venv with the API's requirements, and .env with
DATABASE_URL and SUPABASE_URL / SUPABASE_PUBLISHABLE_KEY. Stop scripts\\start-dev.ps1 first: the drill uses
the engine's and the PLC's standard ports.
"""
from __future__ import annotations
import json, os, pathlib, select, socket, sqlite3, subprocess, sys, threading, time, urllib.parse, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "var" / "drill"
PY = ROOT / ".venv" / "Scripts" / "python.exe"
BUILD = ROOT / "build" / "windows-ci"
API_PORT, PG_PORT, PROXY_PORT = 8010, 65432, 65480
ENGINE = "http://127.0.0.1:8095"
DOWN_S = 45


def env_file(path: pathlib.Path) -> dict[str, str]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


class Link:
    """A TCP relay to one upstream plus a CONNECT proxy to another; `up` False cuts and refuses both."""

    def __init__(self, pg_upstream: tuple[str, int], s3_host: str):
        self.pg_upstream, self.s3_host = pg_upstream, s3_host
        self.up = True
        self.socks: set[socket.socket] = set()
        self.lock = threading.Lock()
        self.cut_count = 0

    def pull(self):
        with self.lock:
            self.up = False
            for s in list(self.socks):
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                s.close()
            self.cut_count += len(self.socks)
            self.socks.clear()

    def restore(self):
        with self.lock:
            self.up = True

    def _pipe(self, a: socket.socket, b: socket.socket):
        with self.lock:
            self.socks |= {a, b}
        try:
            while True:
                r, _, _ = select.select([a, b], [], [], 1.0)
                if not self.up:
                    break
                for s in r:
                    data = s.recv(65536)
                    if not data:
                        return
                    (b if s is a else a).sendall(data)
        except OSError:
            pass
        finally:
            for s in (a, b):
                try:
                    s.close()
                except OSError:
                    pass
            with self.lock:
                self.socks -= {a, b}

    def _serve(self, port: int, handler):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(32)
        while True:
            c, _ = srv.accept()
            if not self.up:                                   # a pulled link: nothing gets through
                c.close()
                continue
            threading.Thread(target=handler, args=(c,), daemon=True).start()

    def _pg(self, c: socket.socket):
        try:
            u = socket.create_connection(self.pg_upstream, timeout=10)
        except OSError:
            c.close()
            return
        self._pipe(c, u)

    def _connect_proxy(self, c: socket.socket):
        head = b""
        while b"\r\n\r\n" not in head and len(head) < 8192:
            chunk = c.recv(4096)
            if not chunk:
                c.close()
                return
            head += chunk
        line = head.split(b"\r\n", 1)[0].decode("latin-1").split()
        if len(line) < 2 or line[0] != "CONNECT" or not line[1].startswith(self.s3_host + ":"):
            c.sendall(b"HTTP/1.1 403 Forbidden\r\n\r\n")      # this proxy carries the clip store and nothing else
            c.close()
            return
        host, port = line[1].rsplit(":", 1)
        try:
            u = socket.create_connection((host, int(port)), timeout=10)
        except OSError:
            c.sendall(b"HTTP/1.1 502 Bad Gateway\r\n\r\n")
            c.close()
            return
        c.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
        self._pipe(c, u)

    def start(self):
        threading.Thread(target=self._serve, args=(PG_PORT, self._pg), daemon=True).start()
        threading.Thread(target=self._serve, args=(PROXY_PORT, self._connect_proxy), daemon=True).start()


def get(url: str, headers: dict | None = None, timeout: float = 3.0):
    req = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def wait(pred, timeout: float, every: float = 0.5):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred():
                return True
        except Exception:
            pass
        time.sleep(every)
    return False


def main() -> int:
    import yaml
    env = env_file(ROOT / ".env")
    db = urllib.parse.urlsplit(env["DATABASE_URL"])
    s3_host = urllib.parse.urlsplit(env["S3_ENDPOINT"]).hostname if env.get("S3_ENDPOINT") else "none"
    OUT.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("outbox.db*"):
        f.unlink()

    link = Link((db.hostname, db.port or 5432), s3_host)
    link.start()

    api_env = {**os.environ, **env,
               "DATABASE_URL": urllib.parse.urlunsplit(db._replace(netloc=f"{db.username}:{db.password}@127.0.0.1:{PG_PORT}")),
               "HTTPS_PROXY": f"http://127.0.0.1:{PROXY_PORT}", "NO_PROXY": "127.0.0.1,localhost",
               "OUTBOX_PATH": str(OUT / "outbox.db"), "SIDECAR_URL": "http://127.0.0.1:1"}
    cfg = yaml.safe_load((ROOT / "config" / "station_w14.yaml").read_text(encoding="utf-8"))
    cfg["source"]["playlist"] = ["fault_interrupt_resume", "fault_adjacent_order", "fault_torque_before_all_started", "fault_no_tool"]
    cfg["sidecar"]["enabled"] = False
    cfg["io"]["event_spool"] = str((OUT / "W-14.jsonl").as_posix())
    (OUT / "station_drill.yaml").write_text(yaml.safe_dump(cfg, sort_keys=False), encoding="utf-8")
    eng_env = {**os.environ, "API_KEY": env.get("API_KEY", ""), "TS_API_URL": f"http://127.0.0.1:{API_PORT}",
               "PATH": f"C:\\msys64\\ucrt64\\bin;{os.environ['PATH']}"}

    logs = {n: open(OUT / f"{n}.log", "w") for n in ("softplc", "api", "engine")}
    procs = [
        subprocess.Popen([str(BUILD / "softplc" / "softplc.exe"), "4840", "--station", "W14", "--bool", "Station.W14.Nutrunner.ResultOK"],
                         cwd=ROOT, stdout=logs["softplc"], stderr=subprocess.STDOUT, env=eng_env),
        subprocess.Popen([str(PY), "-m", "uvicorn", "api.main:app", "--host", "127.0.0.1", "--port", str(API_PORT)],
                         cwd=ROOT, stdout=logs["api"], stderr=subprocess.STDOUT, env=api_env),
    ]
    timeline, report = [], {"down_s": DOWN_S}
    api = f"http://127.0.0.1:{API_PORT}"

    def sample(phase):
        a, e = get(f"{api}/health"), get(f"{ENGINE}/health")
        row = {"t": round(time.time() - t0, 1), "phase": phase, "outbox_pending": a["outbox_pending"],
               "flushed": a.get("flushed"), "alerts": e.get("alerts"), "frames_verified": e.get("frames_verified"),
               "plc_connected": e.get("plc_connected"), "cycles": e.get("cycles"), "uplinked": e.get("uplinked")}
        timeline.append(row)
        print(json.dumps(row), flush=True)
        return row

    try:
        assert wait(lambda: get(f"{api}/health")["status"] == "up", 60), "the drill API did not come up"
        procs.append(subprocess.Popen([str(BUILD / "control-app" / "torquesense.exe"), "--config", str(OUT / "station_drill.yaml")],
                                      cwd=ROOT, stdout=logs["engine"], stderr=subprocess.STDOUT, env=eng_env))
        assert wait(lambda: get(f"{ENGINE}/health")["plc_connected"], 60), "the engine did not reach the PLC"
        t0 = time.time()
        # baseline: records reach Supabase through the link
        assert wait(lambda: (get(f"{api}/health").get("flushed") or 0) > 0 and get(f"{api}/health")["outbox_pending"] == 0, 90), \
            "nothing flushed to Supabase before the pull"
        before = sample("link up")
        # pull mid-cycle: wait until a cycle is a few seconds in
        wait(lambda: 3000 < (get(f"{ENGINE}/state").get("cycle_elapsed_ms") or 0) < 6000, 30, 0.1)
        link.pull()
        report["pulled_at_cycle_ms"] = get(f"{ENGINE}/state").get("cycle_elapsed_ms")
        pulled = sample("PULLED")
        end = time.time() + DOWN_S
        while time.time() < end:
            time.sleep(5)
            last_down = sample("down")
        con = sqlite3.connect(OUT / "outbox.db")
        queued = [(seq, kind, json.loads(p)) for seq, kind, p in con.execute("select seq, kind, payload from outbox order by seq")]
        con.close()
        link.restore()
        sample("RESTORED")
        drained = wait(lambda: get(f"{api}/health")["outbox_pending"] == 0, 180, 1.0)
        after = sample("drained" if drained else "NOT drained")

        # what the Vercel page reads: Supabase through the publishable key, under RLS
        ids = [p["event_id"] for _, k, p in queued if k == "anomaly"]
        rows = []
        if ids:
            q = urllib.parse.quote(",".join(ids))
            rows = get(f"{env['SUPABASE_URL']}/rest/v1/anomalies?select=event_id,ts,violation_type&event_id=in.({q})",
                       {"apikey": env["SUPABASE_PUBLISHABLE_KEY"], "Authorization": f"Bearer {env['SUPABASE_PUBLISHABLE_KEY']}"}, 15)
        landed = {r["event_id"] for r in rows}
        ts_by_seq = [p.get("ts") for _, k, p in queued if k == "anomaly"]
        report.update({
            "queued_during_outage": len(queued), "queued_anomalies": len(ids),
            "anomalies_in_supabase_after": len(landed & set(ids)),
            "queue_in_event_order": ts_by_seq == sorted(ts_by_seq),
            "connections_cut": link.cut_count,
            "alerts_while_down": (last_down["alerts"] or 0) - (pulled["alerts"] or 0),
            "frames_verified_while_down": (last_down["frames_verified"] or 0) - (pulled["frames_verified"] or 0),
            "plc_connected_throughout": all(r["plc_connected"] for r in timeline),
            "outbox_peak": max(r["outbox_pending"] for r in timeline),
            "drained": drained, "before": before, "after": after, "timeline": timeline,
        })
        ok = (report["alerts_while_down"] > 0 and report["frames_verified_while_down"] > 0 and report["plc_connected_throughout"]
              and report["outbox_peak"] > 0 and drained and report["anomalies_in_supabase_after"] == len(ids)
              and report["queue_in_event_order"])
        report["pass"] = ok
        (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(("PASS" if ok else "FAIL") + f": {report['alerts_while_down']} alerts and {report['frames_verified_while_down']} frames "
              f"verified with the link down; outbox peaked at {report['outbox_peak']}, drained={drained}; "
              f"{report['anomalies_in_supabase_after']}/{len(ids)} queued anomalies in Supabase, in event order: {report['queue_in_event_order']}")
        return 0 if ok else 1
    finally:
        for p in reversed(procs):
            p.terminate()
        for p in procs:
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()


if __name__ == "__main__":
    sys.exit(main())
