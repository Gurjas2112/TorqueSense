"""Hourly, from GitHub Actions: keep the free Supabase project awake, and fail — so GitHub emails
you — when the station should be online but its newest health bucket is stale.

    SUPABASE_URL=… SUPABASE_PUBLISHABLE_KEY=… TS_EXPECT_ONLINE=true python scripts/watchdog.py

It reads one row through the same public, RLS-limited key the Vercel dashboard uses, so the
workflow holds no secret that is not already public. Standard library only.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.request
from datetime import datetime, timezone

MAX_AGE_MIN = 30   # six missed 5-minute buckets


def newest_bucket(url: str, key: str, timeout: float = 20) -> tuple[str, datetime] | None:
    q = f"{url.rstrip('/')}/rest/v1/station_health?select=station_id,bucket_start&order=bucket_start.desc&limit=1"
    req = urllib.request.Request(q, headers={"apikey": key, "Authorization": f"Bearer {key}"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        rows = json.load(r)
    if not rows:
        return None
    ts = datetime.fromisoformat(rows[0]["bucket_start"].replace("Z", "+00:00"))
    return rows[0]["station_id"], ts


def verdict(row, expect_online: bool, now: datetime) -> tuple[int, str]:
    if row is None:
        return (1 if expect_online else 0), "no health rows yet"
    station, ts = row
    age = (now - ts).total_seconds() / 60
    msg = f"newest bucket: {station} at {ts.isoformat()} ({age:.0f} min ago)"
    if expect_online and age > MAX_AGE_MIN:
        return 1, f"STALE — {msg}; the station is expected online (TS_EXPECT_ONLINE=true)"
    return 0, msg


def main() -> int:
    url, key = os.environ["SUPABASE_URL"], os.environ["SUPABASE_PUBLISHABLE_KEY"]
    expect = os.environ.get("TS_EXPECT_ONLINE", "false").lower() == "true"
    try:
        row = newest_bucket(url, key)          # this request is also the keep-alive
    except Exception as e:                     # paused, unreachable, or the key was rotated
        print(f"Supabase unreachable: {e}")
        return 1
    code, msg = verdict(row, expect, datetime.now(timezone.utc))
    print(msg)
    return code


if __name__ == "__main__":
    sys.exit(main())
