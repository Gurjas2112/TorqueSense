"""Fetch Florence-2 once, at install, at the pinned commit — so the sidecar can run with
HF_HUB_OFFLINE=1 and a demo never downloads anything.

    python -m sidecar.prefetch --env-file C:\\TorqueSense\\config\\.env      (HF_HOME decides where)

Refuses a branch name or a missing revision: I12 pins Florence-2 to a commit.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

COMMIT = re.compile(r"^[0-9a-f]{40}$")


def read_env(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"').strip("'")
    return out


def prefetch(model: str, revision: str, download=None) -> str:
    if not COMMIT.match(revision or ""):
        raise SystemExit(f"FLORENCE_REVISION must be a 40-character commit hash, got {revision!r} (I12)")
    if download is None:
        from huggingface_hub import snapshot_download as download
    return download(repo_id=model, revision=revision)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env-file", type=Path, required=True)
    a = ap.parse_args(argv)
    env = read_env(a.env_file)
    path = prefetch(env.get("FLORENCE_MODEL", ""), env.get("FLORENCE_REVISION", ""))
    print(f"Florence-2 at {env['FLORENCE_REVISION'][:12]} cached in {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
