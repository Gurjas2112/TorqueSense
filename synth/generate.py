import blenderproc as bproc  # noqa: I001 — BlenderProc requires this to be the file's first line
"""Domain-randomised renders with automatic labels, from the same CAD the compiler reads (spec Section 9).

Run through BlenderProc, which manages its own Blender (spec Section 28 step 6):

    blenderproc run synth/generate.py --gltf cad/export/wheel_assembly.glb --seed 20260721 --frames 4000
    blenderproc run synth/generate.py --gltf cad/export/wheel_assembly.glb --seed 99 --frames 400   # held out
    blenderproc run synth/generate.py --gltf cad/export/wheel_assembly.glb --seed 7 \
        --plan data/render_plans/wheel-hub-5lug.json      # scenario frames (synth/render_plan.py)

Training output goes to data/synth/<glTF sha>-g<generator>-s<seed>/ (synth/cache.py):
  yolo/images/{train,val}/*.jpg + yolo/labels/...     hands and tools, YOLO format (dataset.yaml beside them)
  coco/coco_annotations.json                          the same boxes as COCO, over the YOLO images (the I9 test)
  frames.jsonl                                        per frame: states, projected points, occlusion — the
                                                      96x96 state crops are cut from it (training/train_local.py)
  batches/<n>.json                                    one record per finished batch: the resume points
  manifest.json                                       written last: its presence is the cache hit

Scenario output goes to data/synth_scenarios/<scenario>/%05d.jpg with truth.jsonl; synth/render_plan.py
--video turns each folder into data/scenarios_rendered/<scenario>.mp4 for FileSource.

The glTF's node names carry the part numbers, exactly as FreeCAD exports them; the bolts are the same
occurrences the CAD compiler joins (cad-extract/src/StepReader.cpp), so a render label and a
procedure.json inspection point always name the same bolt. Crops are cut exactly as the engine cuts
them (perception/src/OnnxDetector.cpp): a square of 3 x feature_px around the CAD projection.

Hands and the nutrunner are primitive proxies (a gloved hand and sleeve; socket, body, handle), not
scans: a detector trained on them is validated on rendered frames only until real photos exist (I10).

Needs: the FreeCAD export in cad/export/ (cad/build_wheel.py) and `pip install blenderproc`.
"""
import argparse
import json
import math
import pathlib
import random
import sys

import bpy
import cv2
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cache  # noqa: E402

BOLT_PN = "BLT-M12-1.25-45"
STATES = ["absent", "seated", "fastened"]          # the family's order: rank = index
HAND, TOOL = 1, 2                                   # category ids; YOLO classes are id - 1
FAR = (0.0, 0.0, -50.0)                             # where an absent part waits, out of every view
W, H = 1920, 1080
NOMINAL_CAM_M = 0.9                                 # the profile's camera: 900 mm above the hub face
HFOV_DEG = 70.4


# ---- scene ---------------------------------------------------------------------------------------
def material(name, rgb, rough=0.5, metal=0.0):
    m = bproc.material.create(name)
    m.set_principled_shader_value("Base Color", [*rgb, 1.0])
    m.set_principled_shader_value("Roughness", rough)
    m.set_principled_shader_value("Metallic", metal)
    return m


def join(parts, name):
    """Joins primitives into one mesh, so one hand or one tool is one instance in the segmentation."""
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.blender_obj.select_set(True)
    bpy.context.view_layer.objects.active = parts[0].blender_obj
    bpy.ops.object.join()
    obj = bproc.types.MeshObject(bpy.context.view_layer.objects.active)
    obj.set_name(name)
    return obj


def prim(shape, scale, rot=(0, 0, 0), loc=(0, 0, 0)):
    p = bproc.object.create_primitive(shape, scale=list(scale))
    p.set_rotation_euler(list(rot))
    p.set_location(list(loc))
    p.persist_transformation_into_mesh()
    return p


def make_glove(k, mat):
    """A gloved hand and sleeve, from primitives. Local frame: the palm's centre at the origin, fingers
    along +X, the forearm along -X and rising out of the frame."""
    parts = [prim("CUBE", (0.045, 0.040, 0.012))]
    for i, y in enumerate((-0.027, -0.009, 0.009, 0.027)):
        parts.append(prim("CYLINDER", (0.0085, 0.0085, 0.036 + 0.004 * (i % 2)), (0, math.pi / 2, 0), (0.078, y, -0.004)))
    parts.append(prim("CYLINDER", (0.010, 0.010, 0.030), (math.pi / 2, 0, -0.6), (0.020, -0.055, -0.004)))
    parts.append(prim("CYLINDER", (0.038, 0.038, 0.30), (0, math.pi / 2 + 0.55, 0), (-0.29, 0, 0.17)))
    for p in parts:
        p.replace_materials(mat)
    g = join(parts, f"glove{k}")
    g.set_cp("category_id", HAND)
    return g


def make_tool():
    """A straight nutrunner: socket, motor body, orange band, side handle. Local origin: the socket's
    mouth, which sits on the bolt face when the tool is engaged; the body rises along +Z."""
    socket = prim("CYLINDER", (0.014, 0.014, 0.030), loc=(0, 0, 0.032))
    body = prim("CYLINDER", (0.030, 0.030, 0.110), loc=(0, 0, 0.17))
    band = prim("CYLINDER", (0.0315, 0.0315, 0.018), loc=(0, 0, 0.09))
    handle = prim("CYLINDER", (0.015, 0.015, 0.075), (0, math.pi / 2, 0), (0.10, 0, 0.21))
    socket.replace_materials(material("tool_steel", (0.35, 0.35, 0.37), 0.35, 1.0))
    body.replace_materials(material("tool_body", (0.04, 0.04, 0.045), 0.6))
    band.replace_materials(material("tool_band", (0.95, 0.42, 0.05), 0.5))
    handle.replace_materials(material("tool_grip", (0.06, 0.06, 0.06), 0.8))
    tool = join([socket, body, band, handle], "nutrunner")
    tool.set_cp("category_id", TOOL)
    return tool


class Scene:
    def __init__(self, gltf: pathlib.Path):
        objs = bproc.loader.load_obj(str(gltf))
        self.bolts = sorted([o for o in objs if o.get_name().startswith(BOLT_PN)], key=lambda o: o.get_name())
        self.static = [o for o in objs if not o.get_name().startswith(BOLT_PN)]
        if not self.bolts:
            raise SystemExit(f"no {BOLT_PN} occurrences in {gltf}")
        self.points = [np.array(b.get_location()) for b in self.bolts]   # head-face centres, metres
        for o in objs:
            o.set_cp("category_id", 0)
        self.mats = {
            "bolt": material("bolt", (0.55, 0.55, 0.58), 0.35, 1.0),
            "paint": material("paint", (0.95, 0.80, 0.10), 0.6),
            "hub": material("hub", (0.6, 0.6, 0.62), 0.4, 1.0),
            "rim": material("rim", (0.7, 0.7, 0.72), 0.3, 1.0),
            "tyre": material("tyre", (0.03, 0.03, 0.03), 0.9),
            "glove": material("glove", (0.1, 0.25, 0.75), 0.7),
            "floor": material("floor", (0.3, 0.3, 0.3), 0.8),
        }
        self.stripes = []
        for k in range(len(self.bolts)):           # the torque witness mark: yellow paint across the head
            s = prim("CUBE", (0.0105, 0.0016, 0.0004))
            s.set_name(f"witness{k}")
            s.set_cp("category_id", 0)
            s.replace_materials(self.mats["paint"])
            self.stripes.append(s)
        self.gloves = [make_glove(k, self.mats["glove"]) for k in range(2)]
        self.tool = make_tool()
        self.floor = bproc.object.create_primitive("PLANE", scale=[1.5, 1.5, 1])
        self.floor.set_location([0, 0, -0.16])
        self.floor.set_cp("category_id", 0)
        self.floor.replace_materials(self.mats["floor"])
        self.lights = [bproc.types.Light() for _ in range(2)]
        for lt in self.lights:
            lt.set_type("AREA")
        for b in self.bolts:
            b.replace_materials(self.mats["bolt"])
        for o in self.static:
            n = o.get_name()
            key = "hub" if n.startswith("HUB") else "rim" if n.startswith("RIM") else "tyre" if n.startswith("TYR") else None
            if key and o.get_mesh() is not None:
                o.replace_materials(self.mats[key])

    def restyle(self, rng: random.Random):
        """Per batch: materials, colours and ambient light. Geometry never changes."""
        grey = rng.uniform(0.35, 0.8)
        self.mats["hub"].set_principled_shader_value("Base Color", [grey, grey, grey * rng.uniform(0.97, 1.03), 1])
        rim = rng.choice([(0.75, 0.75, 0.77), (0.15, 0.15, 0.16), (0.45, 0.45, 0.48)])
        self.mats["rim"].set_principled_shader_value("Base Color", [*rim, 1])
        b = rng.uniform(0.4, 0.7)
        self.mats["bolt"].set_principled_shader_value("Base Color", [b, b, b * 1.02, 1])
        self.mats["bolt"].set_principled_shader_value("Roughness", rng.uniform(0.2, 0.6))
        y = rng.uniform(0.85, 1.0)
        self.mats["paint"].set_principled_shader_value("Base Color", [y, y * rng.uniform(0.72, 0.88), rng.uniform(0.0, 0.15), 1])
        glove = rng.choice([(0.1, 0.25, 0.75), (0.05, 0.05, 0.06), (0.85, 0.85, 0.88), (0.9, 0.45, 0.08), (0.1, 0.5, 0.2)])
        self.mats["glove"].set_principled_shader_value("Base Color", [*glove, 1])
        self.mats["floor"].set_principled_shader_value("Base Color", [*(rng.uniform(0.1, 0.6) for _ in range(3)), 1])
        self.mats["glove"].set_principled_shader_value("Roughness", rng.uniform(0.4, 0.95))
        self.mats["floor"].set_principled_shader_value("Roughness", rng.uniform(0.4, 0.95))
        a = rng.uniform(0.01, 0.06)
        bproc.renderer.set_world_background([a, a, a * rng.uniform(0.9, 1.1)], strength=1.0)
        bpy.context.scene.view_settings.exposure = rng.uniform(-0.5, 0.5)


# ---- per-frame placement --------------------------------------------------------------------------
def place_bolts(scene, ranks, f, rng):
    for k, (bolt, stripe) in enumerate(zip(scene.bolts, scene.stripes)):
        p = scene.points[k]
        bolt.set_location(FAR if ranks[k] == 0 else p.tolist(), frame=f)
        bolt.set_rotation_euler([0, 0, rng.uniform(0, math.pi / 3)], frame=f)
        stripe.set_location([p[0], p[1], p[2] + 0.0005] if ranks[k] == 2 else FAR, frame=f)
        stripe.set_rotation_euler([0, 0, rng.uniform(0, math.pi)], frame=f)


def place_glove(glove, f, target, yaw=0.0, height=0.0, roll=0.0):
    if target is None:
        glove.set_location(FAR, frame=f)
        glove.set_rotation_euler([0, 0, 0], frame=f)
        return
    glove.set_location([float(target[0]), float(target[1]), float(target[2]) + height], frame=f)
    glove.set_rotation_euler([roll, 0, yaw], frame=f)


def place_tool(tool, f, target, tilt=(0.0, 0.0), yaw=0.0, lift=0.0):
    if target is None:
        tool.set_location(FAR, frame=f)
        tool.set_rotation_euler([0, 0, 0], frame=f)
        return
    tool.set_location([float(target[0]), float(target[1]), float(target[2]) + 0.001 + lift], frame=f)
    tool.set_rotation_euler([tilt[0], tilt[1], yaw], frame=f)


def light_frame(scene, f, rng):
    for lt in scene.lights:
        lt.set_location([rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(0.9, 1.8)], frame=f)
        lt.set_energy(rng.uniform(1.0, 6.0), frame=f)


def camera(f, jitter, rng):
    """Above the hub, looking down -Z with world +Y up in the image: the profile's camera and the
    convention the engine projects with (control-app/src/SchematicLayout.cpp). Jitter tilts it back at
    the hub from a displaced position, as a knocked mount would."""
    from mathutils import Euler
    loc = np.array([0.0, 0.0, NOMINAL_CAM_M])
    inplane = 0.0
    if jitter:
        loc += [rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), rng.uniform(-0.02, 0.02)]
        inplane = rng.uniform(-0.05, 0.05)
    L = float(np.linalg.norm(loc))
    rot = np.array(Euler((math.asin(-loc[1] / L), math.asin(loc[0] / L), inplane), "XYZ").to_matrix())
    bproc.camera.add_camera_pose(bproc.math.build_transformation_mat(loc, rot), frame=f)


# ---- outputs --------------------------------------------------------------------------------------
def boxes(seg_inst, attr_map):
    """Every hand and tool instance as (category, x, y, w, h) in pixels."""
    out = []
    for a in attr_map:
        cat = int(a.get("category_id", 0))
        if cat not in (HAND, TOOL):
            continue
        ys, xs = np.nonzero(seg_inst == a["idx"])
        if xs.size < 60:                       # a sliver at the frame edge is not a labelable object
            continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        out.append((cat, int(x0), int(y0), int(x1 - x0), int(y1 - y0)))
    return sorted(out)


def occluded(cat_seg, u, v, r):
    """A hand or tool over more than a fifth of the point's face: no state information this frame."""
    x0, x1 = max(0, int(u - r)), min(cat_seg.shape[1], int(u + r) + 1)
    y0, y1 = max(0, int(v - r)), min(cat_seg.shape[0], int(v + r) + 1)
    patch = cat_seg[y0:y1, x0:x1]
    return patch.size == 0 or float(np.mean(patch > 0)) > 0.2


def render_batch(n_frames, setup):
    """Keyframes n_frames with setup(f) and renders them. Returns BlenderProc's output dict."""
    bproc.utility.reset_keyframes()
    for f in range(n_frames):
        setup(f)
    return bproc.renderer.render()


def configure_renderer(samples):
    bproc.renderer.set_render_devices(desired_gpu_device_type=["OPTIX", "CUDA"])
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for dev in prefs.devices:                       # the GPU alone: a hybrid CPU+GPU render is slower here
        dev.use = dev.type != "CPU"
    bproc.camera.set_intrinsics_from_blender_params(lens=math.radians(HFOV_DEG), lens_unit="FOV",
                                                    image_width=W, image_height=H)
    bpy.context.scene.view_settings.view_transform = "Standard"   # a camera's tone curve, not a film look
    bproc.renderer.set_noise_threshold(0.02)
    bproc.renderer.set_max_amount_of_samples(samples)
    bproc.renderer.set_denoiser("OPTIX")
    bproc.renderer.set_light_bounces(diffuse_bounces=2, glossy_bounces=2, max_bounces=4,
                                     transmission_bounces=0, transparent_max_bounces=2)


def train_mode(a, scene, out):
    """Renders in batches of a.batch frames. Each batch has its own seed (seed:batch), so a batch's frames
    do not depend on any other batch: an interrupted run resumes at the first batch without a record in
    batches/, and the result is the same as an uninterrupted one (I9)."""
    yolo, coco_dir, parts = out / "yolo", out / "coco", out / "batches"
    for d in [yolo / k / s for k in ("images", "labels") for s in ("train", "val")] + [coco_dir, parts]:
        d.mkdir(parents=True, exist_ok=True)
    bproc.renderer.enable_segmentation_output(map_by=["category_id", "instance", "name"], default_values={"category_id": 0})
    n = len(scene.points)
    n_batches = (a.frames + a.batch - 1) // a.batch
    for bi in range(n_batches):
        part = parts / f"{bi:04d}.json"
        if part.exists():
            continue
        rng = random.Random(f"{a.seed}:{bi}")
        first = bi * a.batch
        b = min(a.batch, a.frames - first)
        scene.restyle(rng)
        plan = []

        def setup(f):
            ranks = [rng.choices([0, 1, 2], weights=[0.25, 0.35, 0.40])[0] for _ in range(n)]
            place_bolts(scene, ranks, f, rng)
            hands = rng.choices([0, 1, 2], weights=[0.3, 0.55, 0.15])[0]
            for gi, g in enumerate(scene.gloves):
                if gi < hands:
                    t = scene.points[rng.randrange(n)] + np.array([rng.uniform(-0.06, 0.06), rng.uniform(-0.06, 0.06), 0])
                    place_glove(g, f, t, rng.uniform(-math.pi, math.pi), rng.uniform(0.025, 0.14), rng.uniform(-0.3, 0.3))
                else:
                    place_glove(g, f, None)
            if rng.random() < 0.5:
                engaged = rng.random() < 0.6
                t = scene.points[rng.randrange(n)] if engaged else np.array([rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), 0.0])
                place_tool(scene.tool, f, t, (rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25)), rng.uniform(-math.pi, math.pi),
                           0.0 if engaged else rng.uniform(0.02, 0.12))
            else:
                place_tool(scene.tool, f, None)
            light_frame(scene, f, rng)
            camera(f, True, rng)
            plan.append(ranks)

        data = render_batch(b, setup)
        frames, images, anns = [], [], []
        for f in range(b):
            idx = first + f
            split = "val" if idx % 10 == 9 else "train"
            bgr = cv2.cvtColor(np.asarray(data["colors"][f]), cv2.COLOR_RGB2BGR)
            name = f"{idx:05d}.jpg"
            cv2.imwrite(str(yolo / "images" / split / name), bgr, [cv2.IMWRITE_JPEG_QUALITY, 92])
            bx = boxes(data["instance_segmaps"][f], data["instance_attribute_maps"][f])
            with open(yolo / "labels" / split / f"{idx:05d}.txt", "w", encoding="utf-8") as lf:
                for cat, x, y, w, h in bx:
                    lf.write(f"{cat - 1} {(x + w / 2) / W:.6f} {(y + h / 2) / H:.6f} {w / W:.6f} {h / H:.6f}\n")
            images.append({"id": idx, "file_name": f"../yolo/images/{split}/{name}", "width": W, "height": H})
            anns += [{"image_id": idx, "category_id": cat, "bbox": [x, y, w, h], "area": w * h, "iscrowd": 0}
                     for cat, x, y, w, h in bx]
            bpy.context.scene.frame_set(f)
            pts3 = []
            for p in scene.points:                 # centre, and the face's two edges along X (17 mm apart)
                pts3 += [p, p + [0.0085, 0, 0], p - [0.0085, 0, 0]]
            uv = bproc.camera.project_points(np.array(pts3), frame=f)
            cat_seg = data["category_id_segmaps"][f]
            pts = []
            for k in range(n):
                u, v = float(uv[3 * k][0]), float(uv[3 * k][1])
                fpx = float(np.linalg.norm(uv[3 * k + 1] - uv[3 * k + 2]))
                pts.append({"u": round(u, 2), "v": round(v, 2), "feature_px": round(fpx, 2), "rank": plan[f][k],
                            "occluded": occluded(cat_seg, u, v, fpx / 2)})
            frames.append({"frame": idx, "split": split, "points": pts,
                           "boxes": [{"cls": c - 1, "xywh": [x, y, w, h]} for c, x, y, w, h in bx]})
        part.write_text(json.dumps({"frames": frames, "images": images, "annotations": anns}), encoding="utf-8")
        print(f"rendered {first + b}/{a.frames}", flush=True)

    # assemble: one frames.jsonl (crops are cut from it by training/train_local.py) and one COCO file
    coco = {"images": [], "annotations": [], "categories": [{"id": HAND, "name": "hand"}, {"id": TOOL, "name": "tool"}]}
    with open(out / "frames.jsonl", "w", encoding="utf-8") as fl:
        for bi in range(n_batches):
            d = json.loads((parts / f"{bi:04d}.json").read_text(encoding="utf-8"))
            for fr in d["frames"]:
                fl.write(json.dumps(fr) + "\n")
            coco["images"] += d["images"]
            for an in d["annotations"]:
                coco["annotations"].append({"id": len(coco["annotations"]), **an})
    (coco_dir / "coco_annotations.json").write_text(json.dumps(coco), encoding="utf-8")
    (yolo / "dataset.yaml").write_text(
        f"path: {yolo.resolve().as_posix()}\ntrain: images/train\nval: images/val\nnames:\n  0: hand\n  1: tool\n",
        encoding="utf-8")


def scenario_mode(a, scene, rng):
    """Renders each scenario of a plan (synth/render_plan.py) frame by frame from the fixed camera."""
    plan = json.loads(a.plan.read_text(encoding="utf-8"))
    for sc in plan["scenarios"]:
        if a.only and sc["name"] not in a.only:
            continue
        d = a.scenario_root / sc["name"]
        if (d / "done.json").exists():
            print(f"cache hit: {d}")
            continue
        d.mkdir(parents=True, exist_ok=True)
        srng = random.Random(f'{a.seed}:{sc["name"]}')
        scene.restyle(srng)
        lights = [([srng.uniform(-0.6, 0.6), srng.uniform(-0.6, 0.6), srng.uniform(1.0, 1.6)], srng.uniform(40, 80))
                  for _ in scene.lights]
        yaws = [srng.uniform(0, math.pi / 3) for _ in scene.points]   # a bolt keeps its orientation
        truth = open(d / "truth.jsonl", "w", encoding="utf-8")
        frames = sc["frames"]
        for start in range(0, len(frames), a.batch):
            chunk = frames[start:start + a.batch]

            def setup(f):
                fr = chunk[f]
                place_bolts(scene, fr["ranks"], f, random.Random(0))
                for k, bolt in enumerate(scene.bolts):
                    bolt.set_rotation_euler([0, 0, yaws[k]], frame=f)
                    scene.stripes[k].set_rotation_euler([0, 0, yaws[k] + 0.5], frame=f)
                for gi, g in enumerate(scene.gloves):
                    if gi < len(fr["hands"]):
                        h = fr["hands"][gi]
                        place_glove(g, f, np.array(h["at"]), h["yaw"], h["height"], h.get("roll", 0.0))
                    else:
                        place_glove(g, f, None)
                t = fr.get("tool")
                if t:
                    place_tool(scene.tool, f, np.array(t["at"]), tuple(t.get("tilt", (0.0, 0.0))), t.get("yaw", 0.0), t.get("lift", 0.0))
                else:
                    place_tool(scene.tool, f, None)
                for lt, (loc, e) in zip(scene.lights, lights):
                    lt.set_location(loc, frame=f)
                    lt.set_energy(e, frame=f)
                camera(f, False, rng)

            data = render_batch(len(chunk), setup)
            for f, fr in enumerate(chunk):
                bgr = cv2.cvtColor(np.asarray(data["colors"][f]), cv2.COLOR_RGB2BGR)
                cv2.imwrite(str(d / f"{start + f:05d}.jpg"), bgr, [cv2.IMWRITE_JPEG_QUALITY, 95])
                truth.write(json.dumps({"frame": start + f, "ranks": fr["ranks"], "hands": len(fr["hands"]),
                                        "tool": bool(fr.get("tool"))}) + "\n")
            print(f'{sc["name"]}: {min(start + a.batch, len(frames))}/{len(frames)}', flush=True)
        truth.close()
        (d / "done.json").write_text(json.dumps({"frames": len(frames), "fps": plan["fps"]}) + "\n", encoding="utf-8")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--gltf", type=pathlib.Path, required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--frames", type=int, default=1000)
    ap.add_argument("--batch", type=int, default=50)
    ap.add_argument("--samples", type=int, default=24)
    ap.add_argument("--plan", type=pathlib.Path, help="a render plan from synth/render_plan.py: scenario mode")
    ap.add_argument("--only", nargs="*", help="scenario mode: render only these scenarios")
    ap.add_argument("--out-root", type=pathlib.Path, default=pathlib.Path("data/synth"))
    ap.add_argument("--scenario-root", type=pathlib.Path, default=pathlib.Path("data/synth_scenarios"))
    a = ap.parse_args(argv)

    rng = random.Random(a.seed)
    np.random.seed(a.seed)
    out = None
    if not a.plan:
        out = cache.output_dir(a.gltf, a.seed, a.out_root)
        if cache.is_cached(out):
            print(f"cache hit: {out}")
            return 0
    bproc.init()
    configure_renderer(a.samples)
    scene = Scene(a.gltf)
    if a.plan:
        scenario_mode(a, scene, rng)
        return 0
    train_mode(a, scene, out)
    cache.write_manifest(out, a.gltf, a.seed, a.frames, {"resolution": [W, H], "states": STATES, "classes": ["hand", "tool"]})
    print(f"rendered {a.frames} frames into {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]))
