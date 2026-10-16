"""Training on HSGPC's RTX 3060 instead of a Colab T4 (spec Section 27; the notebook's steps, locally).

    python training\\train_local.py --train data\\synth\\<sha>-g4-s20260721 --heldout data\\synth\\<sha>-g4-s99

The spec uses Colab for parallelism and heat, not capability: the laptop renders while Colab trains. Here
the render finished first, so the same two models train on the laptop's GPU with the notebook's settings
(batch sized for 6 GB). Steps, each skipped if its output exists:

  1. crops    — 96x96 crops at every inspection point of every rendered frame, cut exactly as the engine
                cuts them (perception/src/OnnxDetector.cpp: 3 x feature_px around the CAD projection), in
                four classes: the family's three states in rank order, and `occluded` (a glove or the tool
                over the face, from the render's segmentation). The engine reads `occluded` as "no answer".
  2. yolo     — YOLO11n, hands and tools, 640 px, seed-fixed; exported to ONNX (opset 17, static 640²).
  3. classify — MobileNetV3-Small (ImageNet weights) fine-tuned on the crops; exported to ONNX with the
                softmax inside, dynamic batch.
  4. models   — both files into models/ and their SHA-256 into models/manifest.json (I12).
  5. report   — the synthetic held-out figures (a different seed: different lighting, poses, states).
                The real-photo half of I10 needs labelled photos of a real hub (👤) and is not produced here.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, random, shutil, sys, time

import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
STATES = ["absent", "seated", "fastened"]
CLASSES = STATES + ["occluded"]                    # rank order, then `occluded`: the ONNX output columns
SEED = 20260721
CROP_SCALE = 3.0


def crop(img, u, v, side, out=96):
    s = max(8, int(round(side)))
    x0 = min(max(0, int(round(u - s / 2))), img.shape[1] - s)
    y0 = min(max(0, int(round(v - s / 2))), img.shape[0] - s)
    return cv2.resize(img[y0:y0 + s, x0:x0 + s], (out, out), interpolation=cv2.INTER_LINEAR)


def make_crops(render: pathlib.Path, dst: pathlib.Path, jitter: bool, seed: int) -> dict:
    """Crops for every point of every frame in a render folder, by class; returns counts per class."""
    rng = random.Random(seed)
    counts = {c: 0 for c in CLASSES}
    for c in CLASSES:
        for split in ("train", "val"):
            (dst / split / c).mkdir(parents=True, exist_ok=True)
    for line in (render / "frames.jsonl").read_text(encoding="utf-8").splitlines():
        fr = json.loads(line)
        img = cv2.imread(str(render / "yolo" / "images" / fr["split"] / f'{fr["frame"]:05d}.jpg'))
        for k, p in enumerate(fr["points"]):
            if not (0 <= p["u"] < img.shape[1] and 0 <= p["v"] < img.shape[0]):
                continue
            cls = "occluded" if p["occluded"] else STATES[p["rank"]]
            du = dv = 0.0
            scale = 1.0
            if jitter:                         # a knocked camera: some registration error and scale change
                du, dv = rng.uniform(-0.12, 0.12) * p["feature_px"], rng.uniform(-0.12, 0.12) * p["feature_px"]
                scale = rng.uniform(0.9, 1.1)
            c = crop(img, p["u"] + du, p["v"] + dv, CROP_SCALE * p["feature_px"] * scale)
            cv2.imwrite(str(dst / fr["split"] / cls / f'{fr["frame"]:05d}_{k}.jpg'), c, [cv2.IMWRITE_JPEG_QUALITY, 95])
            counts[cls] += 1
    return counts


def sha(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def train_yolo(render: pathlib.Path, runs: pathlib.Path, epochs: int) -> pathlib.Path:
    from ultralytics import YOLO
    best = runs / "w14" / "weights" / "best.pt"
    if not best.exists():
        model = YOLO("yolo11n.pt")
        model.train(data=str(render / "yolo" / "dataset.yaml"), epochs=epochs, imgsz=640, batch=16, workers=4,
                    close_mosaic=10, patience=25, cache="ram", project=str(runs), name="w14", exist_ok=True,
                    seed=SEED, deterministic=True, plots=False, verbose=False)
    onnx = best.with_suffix(".onnx")
    if not onnx.exists():
        YOLO(str(best)).export(format="onnx", opset=17, imgsz=640, simplify=True, dynamic=False)
    return onnx


class CropSet:
    """ImageFolder with the class index fixed to CLASSES, not to alphabetical order."""

    def __init__(self, root: pathlib.Path, tf):
        self.items = [(p, CLASSES.index(p.parent.name)) for c in CLASSES for p in sorted((root / c).glob("*.jpg"))]
        self.tf = tf

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        from PIL import Image
        p, y = self.items[i]
        return self.tf(Image.open(p).convert("RGB")), y


def train_classifier(crops: pathlib.Path, out: pathlib.Path, epochs: int) -> dict:
    import torch, torch.nn as nn, torchvision as tv
    from torch.utils.data import DataLoader
    torch.manual_seed(SEED)
    norm = tv.transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    tf_train = tv.transforms.Compose([tv.transforms.ColorJitter(0.3, 0.3, 0.3, 0.02), tv.transforms.RandomRotation(15),
                                      tv.transforms.RandomResizedCrop(96, scale=(0.85, 1.0), ratio=(0.95, 1.05)),
                                      tv.transforms.ToTensor(), norm])
    tf_val = tv.transforms.Compose([tv.transforms.Resize((96, 96)), tv.transforms.ToTensor(), norm])
    train, val = CropSet(crops / "train", tf_train), CropSet(crops / "val", tf_val)
    counts = np.bincount([y for _, y in train.items], minlength=len(CLASSES))
    weights = torch.tensor(counts.sum() / np.maximum(counts, 1) / len(CLASSES), dtype=torch.float32)
    dl = DataLoader(train, batch_size=128, shuffle=True, num_workers=4, persistent_workers=True)
    dv = DataLoader(val, batch_size=256, num_workers=2)
    net = tv.models.mobilenet_v3_small(weights=tv.models.MobileNet_V3_Small_Weights.IMAGENET1K_V1)
    net.classifier[3] = nn.Linear(net.classifier[3].in_features, len(CLASSES))
    net = net.cuda()
    opt = torch.optim.AdamW(net.parameters(), lr=2e-3, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=2e-3, total_steps=epochs * len(dl))
    lossf = nn.CrossEntropyLoss(weight=weights.cuda(), label_smoothing=0.05)
    best, best_state = -1.0, None
    for ep in range(epochs):
        net.train()
        for x, y in dl:
            opt.zero_grad()
            loss = lossf(net(x.cuda(non_blocking=True)), y.cuda(non_blocking=True))
            loss.backward()
            opt.step()
            sched.step()
        acc = evaluate_classifier(net, dv)["top1"]
        print(f"classifier epoch {ep + 1}/{epochs}: val top-1 {acc:.4f}", flush=True)
        if acc > best:
            best, best_state = acc, {k: v.detach().clone() for k, v in net.state_dict().items()}
    net.load_state_dict(best_state)
    net.eval()

    class WithSoftmax(nn.Module):           # the engine reads probabilities: softmax inside the graph
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, x):
            return torch.softmax(self.m(x), dim=1)

    out.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(WithSoftmax(net).cpu(), torch.zeros(1, 3, 96, 96), str(out), input_names=["crops"], output_names=["probs"],
                      dynamic_axes={"crops": {0: "n"}, "probs": {0: "n"}}, opset_version=17, dynamo=False)
    return {"val_top1": round(best, 4), "train_crops": int(counts.sum()), "per_class_train": dict(zip(CLASSES, counts.tolist()))}


def evaluate_classifier(net, loader) -> dict:
    import torch
    net.eval()
    conf = np.zeros((len(CLASSES), len(CLASSES)), dtype=int)
    with torch.no_grad():
        for x, y in loader:
            p = net(x.cuda()).argmax(1).cpu().numpy()
            for a, b in zip(y.numpy(), p):
                conf[a, b] += 1
    return {"top1": float(np.trace(conf) / max(1, conf.sum())), "confusion": conf.tolist()}


def onnx_classifier_heldout(model: pathlib.Path, crops: pathlib.Path) -> dict:
    """The exported ONNX file itself (not the PyTorch module) on held-out crops, through ONNX Runtime."""
    import onnxruntime as ort
    s = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
    mean, std = np.array([0.485, 0.456, 0.406], np.float32), np.array([0.229, 0.224, 0.225], np.float32)
    conf = np.zeros((len(CLASSES), len(CLASSES)), dtype=int)
    for split in ("train", "val"):
        for ci, c in enumerate(CLASSES):
            files = sorted((crops / split / c).glob("*.jpg"))
            for i in range(0, len(files), 256):
                batch = np.stack([((cv2.cvtColor(cv2.imread(str(f)), cv2.COLOR_BGR2RGB).astype(np.float32) / 255 - mean) / std).transpose(2, 0, 1)
                                  for f in files[i:i + 256]])
                for p in s.run(None, {"crops": batch})[0].argmax(1):
                    conf[ci, p] += 1
    per = {c: round(float(conf[i, i] / max(1, conf[i].sum())), 4) for i, c in enumerate(CLASSES)}
    return {"crops": int(conf.sum()), "top1": round(float(np.trace(conf) / max(1, conf.sum())), 4), "per_class_recall": per,
            "confusion_rows_true_cols_pred": conf.tolist(), "classes": CLASSES}


def heldout_yolo_set(render: pathlib.Path, dst: pathlib.Path) -> pathlib.Path:
    """The held-out render as one YOLO folder (images/ + labels/ + data.yaml), the shape eval.py reads."""
    if not (dst / "data.yaml").exists():
        for k in ("images", "labels"):
            (dst / k).mkdir(parents=True, exist_ok=True)
            for split in ("train", "val"):
                for f in (render / "yolo" / k / split).glob("*"):
                    shutil.copy(f, dst / k / f.name)
        (dst / "data.yaml").write_text(f"path: {dst.resolve().as_posix()}\ntrain: images\nval: images\nnames:\n  0: hand\n  1: tool\n",
                                       encoding="utf-8")
    return dst


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--train", type=pathlib.Path, required=True)
    ap.add_argument("--heldout", type=pathlib.Path, required=True)
    ap.add_argument("--yolo-epochs", type=int, default=100)
    ap.add_argument("--cls-epochs", type=int, default=12)
    a = ap.parse_args(argv)
    runs = ROOT / "data" / "runs"
    crops, crops_h = ROOT / "data" / "crops" / "train", ROOT / "data" / "crops" / "heldout"
    t0 = time.time()
    report = {"trained_on": "HSGPC RTX 3060 Laptop (local, not Colab)", "seed": SEED,
              "train_render": a.train.name, "heldout_render": a.heldout.name}
    if not (crops / "done.json").exists():
        report["crops_train"] = make_crops(a.train, crops, jitter=True, seed=SEED)
        (crops / "done.json").write_text(json.dumps(report["crops_train"]) + "\n")
    if not (crops_h / "done.json").exists():
        report["crops_heldout"] = make_crops(a.heldout, crops_h, jitter=False, seed=99)
        (crops_h / "done.json").write_text(json.dumps(report["crops_heldout"]) + "\n")
    print("crops ready", flush=True)

    det_onnx = train_yolo(a.train, runs, a.yolo_epochs)
    cls_onnx = runs / "classifier" / "threaded_fastener.onnx"
    if not cls_onnx.exists():
        report["classifier_training"] = train_classifier(crops, cls_onnx, a.cls_epochs)

    models = ROOT / "models"
    shutil.copy(det_onnx, models / "yolo11n_wheel.onnx")
    shutil.copy(cls_onnx, models / "threaded_fastener.onnx")
    manifest = json.loads((models / "manifest.json").read_text(encoding="utf-8"))
    manifest["yolo11n_wheel.onnx"] = {"sha256": sha(models / "yolo11n_wheel.onnx"), "version": f"yolo11n-w14-s{SEED}",
                                      "classes": ["hand", "tool"], "input": [640, 640], "trained_from": a.train.name,
                                      "trained_on": "HSGPC RTX 3060 (local)", "accuracy_report": "docs/accuracy.synthetic.json"}
    manifest["threaded_fastener.onnx"] = {"sha256": sha(models / "threaded_fastener.onnx"), "version": f"mnv3s-tf-s{SEED}",
                                          "states": STATES, "outputs": CLASSES, "input": [96, 96], "trained_from": a.train.name,
                                          "trained_on": "HSGPC RTX 3060 (local)"}
    (models / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    sys.path.insert(0, str(ROOT / "training"))
    import eval as ev
    report["detector_synthetic_heldout"] = ev.evaluate(models / "yolo11n_wheel.onnx", heldout_yolo_set(a.heldout, ROOT / "data" / "synth" / "heldout"))
    report["classifier_synthetic_heldout"] = onnx_classifier_heldout(models / "threaded_fastener.onnx", crops_h)
    report["real"] = None
    report["limits"] = ev.LIMITS + ["No real-photo set exists yet (👤 Phase 3): the real half of I10 is not measured, so I10 is not met.",
                                    "Hands and the nutrunner are primitive proxies in the renders; transfer to real gloves is unmeasured."]
    report["minutes"] = round((time.time() - t0) / 60, 1)
    (ROOT / "docs" / "accuracy.synthetic.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("detector_synthetic_heldout", "classifier_synthetic_heldout")}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
