"""Cluster smoke test, run by CI against kind.

1. The engine replays the adjacent-order fixture: an anomaly must reach the API with the
   right bolts, and the soft PLC's red lamp must be on.
2. Scale the engine to zero: within 5 s the PLC must show AMBER (fail-visible, I13),
   because the heartbeat stopped, not because anything told it to.
"""
import asyncio, subprocess, sys, time
import httpx
from asyncua import Client

NS = "torquesense"

def port_forward(target: str, ports: str) -> subprocess.Popen:
    p = subprocess.Popen(["kubectl", "-n", NS, "port-forward", target, ports],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    return p

async def tag(name: str):
    async with Client(url="opc.tcp://127.0.0.1:4840") as c:
        return await c.get_node(f"ns=2;s=Station.W14.{name}").read_value()

def wait_for(pred, timeout_s: float, what: str):
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        if pred():
            return
        time.sleep(0.5)
    sys.exit(f"FAIL: {what} not seen within {timeout_s} s")

def main():
    api, plc = port_forward("svc/api", "8000:8000"), port_forward("svc/softplc", "4840:4840")
    try:
        h = {"X-API-Key": "ci"}
        def violation():
            r = httpx.get("http://127.0.0.1:8000/v1/anomalies?station_id=W-14", headers=h, timeout=3)
            return any(a["violation_type"] == "SEQUENCE" and a["profile"] == "wheel-fitment-rl"
                       and a["observed_ip"] == "bolt-2" and a["expected_ip"] == "bolt-3" for a in r.json())
        wait_for(violation, 120, "sequence violation in the API")
        assert asyncio.run(tag("Andon.Red")) is True, "red lamp not set"
        print("ok: violation logged and red lamp set")

        subprocess.run(["kubectl", "-n", NS, "scale", "deployment/engine-w14", "--replicas=0"], check=True)
        t0 = time.monotonic()
        wait_for(lambda: asyncio.run(tag("Andon.Amber")) is True, 5, "AMBER after engine stopped")
        print(f"ok: AMBER {time.monotonic() - t0:.1f} s after the engine stopped")
    finally:
        api.terminate(); plc.terminate()

if __name__ == "__main__":
    main()
