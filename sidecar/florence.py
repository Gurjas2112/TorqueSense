"""Florence-2 adjudication (spec Section 30): ground a phrase in an inspection point's crop and say
whether it sits on the point. Advisory to the committer only — it can resolve an ambiguous state, never
raise an alert (I3); the engine reaches it only through its circuit breaker (I11).

Pinned to a commit (I12): FLORENCE_REVISION must be a 40-character hash, fetched once by
sidecar/prefetch.py, and the sidecar runs with HF_HUB_OFFLINE=1.
"""
from __future__ import annotations
import hashlib
from collections import OrderedDict

# What each family's states look like, in words Florence-2 can ground. A family is data, not code
# (spec Section 6); a profile may add its own phrases.
PHRASES = {
    ("threaded_fastener", "fastened"): "yellow marked bolt head",
    ("threaded_fastener", "seated"): "bolt head",
    ("snap_clip", "closed"): "closed plastic clip",
    ("connector", "mated"): "mated electrical connector",
    ("seal_segment", "seated"): "rubber gasket seated in the channel",
    ("panel", "installed"): "trim panel",
    ("panel", "placed"): "panel",
    ("label", "applied"): "label",
    ("presence", "present"): "part",
}


def phrase_for(family: str, state: str) -> str:
    return PHRASES.get((family, state), f"{state} {family.replace('_', ' ')}")


class FlorenceAdjudicator:
    TASK = "<CAPTION_TO_PHRASE_GROUNDING>"

    def __init__(self, model: str, revision: str, memory_fraction: float = 0.30):
        import torch
        from transformers import AutoProcessor, Florence2ForConditionalGeneration
        self.torch = torch
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        if self.device == "cuda":
            torch.cuda.set_per_process_memory_fraction(memory_fraction)      # share the GPU politely
        dtype = torch.float16 if self.device == "cuda" else torch.float32
        self.model = Florence2ForConditionalGeneration.from_pretrained(
            model, revision=revision, torch_dtype=dtype).to(self.device).eval()  # pinned; never device_map="auto"
        self.proc = AutoProcessor.from_pretrained(model, revision=revision)
        self.dtype = dtype
        self.model_id, self.revision = model, revision
        self.cache: OrderedDict[str, list] = OrderedDict()                  # 256-entry LRU by crop hash (Section 34)
        self.ground(__import__("PIL.Image", fromlist=["Image"]).new("RGB", (256, 256)), "bolt")   # warm up once

    def ground(self, crop, phrase: str) -> list[list[float]]:
        key = hashlib.sha256(crop.tobytes() + phrase.encode() + self.revision.encode()).hexdigest()
        if key in self.cache:
            self.cache.move_to_end(key)
            return self.cache[key]
        with self.torch.inference_mode():
            crop = crop.convert("RGB")                                      # RGBA crops crash the processor
            x = self.proc(text=self.TASK + phrase, images=crop, return_tensors="pt").to(self.device, self.dtype)
            ids = self.model.generate(input_ids=x["input_ids"], pixel_values=x["pixel_values"],
                                      max_new_tokens=128, num_beams=1, do_sample=False)
            text = self.proc.batch_decode(ids, skip_special_tokens=False)[0]
            out = self.proc.post_process_generation(text, task=self.TASK, image_size=(crop.width, crop.height))
        boxes = out[self.TASK].get("bboxes", [])
        self.cache[key] = boxes
        if len(self.cache) > 256:
            self.cache.popitem(last=False)
        return boxes

    @staticmethod
    def on_point(boxes, u: float, v: float, r: float) -> bool:
        """True if a grounded box is centred within r px of the CAD-projected point (u, v)."""
        return any(((x0 + x1) / 2 - u) ** 2 + ((y0 + y1) / 2 - v) ** 2 <= r * r for x0, y0, x1, y1 in boxes)
