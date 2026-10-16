"""The render manifest is the cache key (spec Section 34).

A render folder is named by what produced it: the glTF's SHA-256, the generator's version and the
seed. Change the CAD and the hash changes; change the generator and bump GENERATOR_VERSION. Either
way the old folder stops matching, and nothing is ever deleted to "invalidate" it.
"""
from __future__ import annotations
import hashlib, json, pathlib

GENERATOR_VERSION = "5"


def gltf_sha(gltf: pathlib.Path) -> str:
    return hashlib.sha256(gltf.read_bytes()).hexdigest()[:12]


def output_dir(gltf: pathlib.Path, seed: int, root: pathlib.Path = pathlib.Path("data/synth")) -> pathlib.Path:
    return root / f"{gltf_sha(gltf)}-g{GENERATOR_VERSION}-s{seed}"


def is_cached(d: pathlib.Path) -> bool:
    return (d / "manifest.json").exists()


def write_manifest(d: pathlib.Path, gltf: pathlib.Path, seed: int, frames: int, extra: dict | None = None) -> None:
    d.mkdir(parents=True, exist_ok=True)
    m = {"gltf": gltf.name, "gltf_sha256_12": gltf_sha(gltf), "generator_version": GENERATOR_VERSION,
         "seed": seed, "frames": frames, **(extra or {})}
    (d / "manifest.json").write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")
