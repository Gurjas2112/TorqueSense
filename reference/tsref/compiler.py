"""Assembly manifest + CAD geometry -> procedure profile. Geometry comes from the STEP file
(here: positions in the manifest); intent comes from the manifest; order comes from solvers."""
from __future__ import annotations
from . import patterns, optics

def compile_profile(manifest: dict, cameras: list[optics.Camera], min_feature_px: float):
    ips, report = [], []
    for g in manifest["groups"]:
        for i, m in enumerate(g["members"], 1):
            ip = {"id": f'{g["id"]}-{i}', "family": g["family"], "states": g["states"],
                  "position_mm": m["position_mm"], "normal": m.get("normal", g.get("normal", [0, 0, 1])),
                  "feature_mm": g["feature_mm"]}
            fp = optics.best(cameras, ip["position_mm"], ip["normal"], ip["feature_mm"])
            ip["camera"], ip["feature_px"] = fp.camera, round(fp.feature_px, 1)
            report.append((ip["id"], fp.camera, round(fp.feature_px, 1), fp.feature_px >= min_feature_px))
            ips.append(ip)
    steps = []
    for st in manifest["steps"]:
        g = next(x for x in manifest["groups"] if x["id"] == st["group"])
        ids = [f'{g["id"]}-{i}' for i in range(1, len(g["members"]) + 1)]
        step = {k: v for k, v in st.items() if k != "group"}
        if st["kind"] == "sequence" and st.get("order", "derive") == "derive":
            kind, order = patterns.solve([tuple(m["position_mm"][:2]) for m in g["members"]])
            step["targets"], step["pattern"] = [ids[i] for i in order], kind
            step["order_source"] = "derived"
        elif st["kind"] == "sequence" and isinstance(st.get("order"), list):
            # the OEM's work instruction mandates its own order: it overrides derivation (Section 7)
            if sorted(st["order"]) != list(range(1, len(ids) + 1)):
                raise ValueError(f'{st["id"]}: an explicit order must list every member once')
            step["targets"] = [ids[i - 1] for i in st["order"]]
            step["order_source"] = "specified"
        else:
            step["targets"] = ids
        step.pop("order", None)
        steps.append(step)
    unresolved = [r for r in report if not r[3]]
    return {"profile": manifest["profile"], "inspection_points": ips, "steps": steps}, report, unresolved
