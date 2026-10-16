"""Domain-randomised renders with automatic labels, from the same CAD the compiler reads (spec Section 9).

Run through BlenderProc, which manages its own Blender (spec Section 28 step 6):

    blenderproc run synth/generate.py -- --gltf cad/export/wheel_assembly.gltf --seed 20260721 --frames 4000
    blenderproc run synth/generate.py -- --gltf cad/export/wheel_assembly.gltf --seed 7 \
        --scenarios samples/wheel-hub-5lug          # one video per scenario, for FileSource and the I9 test

Output goes to data/synth/<glTF sha>-g<generator>-s<seed>/ (synth/cache.py): COCO labels for the
hands/tools detector, 96x96 crops per inspection point and state for the family classifiers, and a
manifest.json. A folder that already has a manifest is a cache hit and is not rendered again.

The glTF's node names carry the part numbers, exactly as FreeCAD exports them; inspection points are
the same occurrences the CAD compiler joins (cad-extract/src/StepReader.cpp), so a render label and a
procedure.json inspection point always name the same bolt.

Needs: the 👤 FreeCAD export in cad/export/ (Phase 2) and `pip install blenderproc`.
"""
import blenderproc as bproc  # noqa: I001 — BlenderProc requires this to be the first import

import argparse
import json
import pathlib
import random
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "samples"))
import cache  # noqa: E402

# what the bolt's three states look like: no bolt, a bolt head without the witness mark, and the
# head with the yellow torque mark painted across it (spec Section 17)
STATE_LOOK = {"absent": None, "seated": (0.55, 0.55, 0.58), "fastened": (0.95, 0.80, 0.10)}


def randomise_scene(rng: random.Random, objs):
    """Lighting, camera jitter and materials vary per frame; geometry never does."""
    light = bproc.types.Light()
    light.set_type(rng.choice(["POINT", "AREA"]))
    light.set_location([rng.uniform(-600, 600) / 1000, rng.uniform(-600, 600) / 1000, rng.uniform(0.8, 1.6)])
    light.set_energy(rng.uniform(300, 1500))
    for o in objs:
        for mat in o.get_materials():
            mat.set_principled_shader_value("Roughness", rng.uniform(0.2, 0.9))
    return light


def camera_pose(rng: random.Random, distance_m: float = 0.9):
    loc = np.array([rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), distance_m + rng.uniform(-0.02, 0.02)])
    rot = bproc.camera.rotation_from_forward_vec(-loc / np.linalg.norm(loc), inplane_rot=rng.uniform(-0.05, 0.05))
    return bproc.math.build_transformation_mat(loc, rot)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--gltf", type=pathlib.Path, required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--frames", type=int, default=1000)
    ap.add_argument("--scenarios", type=pathlib.Path, help="a sample folder with scenarios.yaml")
    ap.add_argument("--out-root", type=pathlib.Path, default=pathlib.Path("data/synth"))
    a = ap.parse_args(argv)

    out = cache.output_dir(a.gltf, a.seed, a.out_root)
    if cache.is_cached(out):
        print(f"cache hit: {out}")
        return 0

    rng = random.Random(a.seed)
    np.random.seed(a.seed)
    bproc.init()
    objs = bproc.loader.load_obj(str(a.gltf))
    bproc.camera.set_resolution(1920, 1080)
    bproc.renderer.set_noise_threshold(0.01)
    bproc.renderer.set_max_amount_of_samples(64)
    bproc.renderer.enable_segmentation_output(map_by=["category_id", "instance", "name"])

    frames = a.frames
    if a.scenarios:
        import scenarios  # synth/scenarios.py: the same timelines ReplayDetector replays
        spec = scenarios.load(a.scenarios)
        frames = 0
        for name in spec["scenarios"]:
            fixture = json.loads((scenarios.OUT / f"{name}.json").read_text())
            frames += fixture["n_frames"]
            # per-frame states drive which bolt variant is visible; one keyframe per frame
            for f in range(fixture["n_frames"]):
                bproc.camera.add_camera_pose(camera_pose(rng), frame=f)
    else:
        for _ in range(frames):
            bproc.camera.add_camera_pose(camera_pose(rng))

    randomise_scene(rng, objs)
    data = bproc.renderer.render()
    bproc.writer.write_coco_annotations(str(out / "coco"), instance_segmaps=data["instance_segmaps"],
                                        instance_attribute_maps=data["instance_attribute_maps"],
                                        colors=data["colors"], color_file_format="JPEG")
    cache.write_manifest(out, a.gltf, a.seed, frames, {"scenarios": str(a.scenarios) if a.scenarios else None})
    print(f"rendered {frames} frames into {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
