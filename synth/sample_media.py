"""Sample media: one schematic video per trace, and keyframe images, in every samples/<x>/media/.

    python synth/sample_media.py                      # every sample
    python synth/sample_media.py desk-cover-4screw    # one

For each trace of each sample this writes

    media/<trace>.mp4             every frame of the cycle, drawn from the trace (synth/schematic.py)
    media/<trace>/frame_*.png     the first frame of each state change, plus frame 0
    media/<trace>/frames.json     for each image: frame, time, and the state every point must read as

The engine then verifies them through perception: `torquesense --sample <dir> --verify-media all`
reads every frame of every video with its SchematicDetector, commits through the 2-frame debounce and
runs the interpreter — each recording must raise exactly its trace's violations (spec Section 12, the
third use of the expect: block); `--classify-image` checks every keyframe against frames.json.

These are drawings, small and synthetic — the one kind of video the repository keeps (docs/adr/0012).
Real recordings of the desk kits replace them, and must pass the same check with a taught classifier.
"""
from __future__ import annotations
import argparse, json, pathlib, shutil, sys

import cv2

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import scenarios           # noqa: E402  (trace -> per-frame fixture)
import schematic           # noqa: E402

FPS = 30.0


def media_for(sample: pathlib.Path) -> list[str]:
    procedure = json.loads((sample / "procedure.json").read_text(encoding="utf-8"))
    prof, _, _ = scenarios.run_samples.compile_folder(sample)
    out_dir = sample / "media"
    out_dir.mkdir(exist_ok=True)
    written = []
    for trace in sorted((sample / "traces").glob("*.yaml")):
        name = trace.stem
        fixture, expected = scenarios.build(sample, name, {"trace": name, "title": f"{sample.name} / {name}"}, FPS, prof)
        video = out_dir / f"{name}.mp4"
        schematic.write_video(procedure, fixture, video)
        kdir = out_dir / name
        if kdir.exists():
            shutil.rmtree(kdir)
        kdir.mkdir()
        rows = []
        for f, img, held, changed in schematic.frames(procedure, fixture):
            if f == 0 or changed:
                file = f"frame_{f:04d}.png"
                cv2.imwrite(str(kdir / file), img)
                rows.append({"file": file, "frame": f, "t_ms": round(f * 1000 / FPS, 1),
                             "expect": {fixture["ips"][i]: fixture["states"][i][max(0, held[i][0])] for i in held},
                             "changed": [fixture["ips"][i] for i in changed]})
        (kdir / "frames.json").write_text(json.dumps({"sample": sample.name, "trace": name, "fps": FPS,
                                                      "release_ok": expected["release_ok"], "frames": rows}, indent=1) + "\n",
                                          encoding="utf-8", newline="\n")
        written.append(f"{sample.name}/media/{name}.mp4 ({fixture['n_frames']} frames, {len(rows)} keyframes)")
    return written


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("samples", nargs="*")
    a = ap.parse_args(argv)
    dirs = sorted(d for d in (ROOT / "samples").iterdir() if (d / "profile.yaml").exists() and (d / "traces").exists())
    if a.samples:
        dirs = [d for d in dirs if d.name in a.samples]
    for d in dirs:
        for line in media_for(d):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
