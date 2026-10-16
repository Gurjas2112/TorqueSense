"""The wheel assembly, modelled in FreeCAD from a script (spec Section 28, Phase 2).

    C:\\dev\\tools\\FreeCAD\\bin\\freecadcmd.exe cad\\build_wheel.py            # both revisions
    (FreeCADCmd runs the file as a macro; arguments come from TS_CAD_* variables, not argv)

Writes, per revision:
    cad/wheel_assembly.FCStd        5-lug, 114.3 mm PCD, revision C (the PS4 reference)
    cad/wheel_assembly_4lug.FCStd   4-lug, 100 mm PCD — the live reconfiguration demo
    cad/export/<name>.step          read by cad-extract (OCCT XCAF)
    cad/export/<name>.glb           read by synth/generate.py (BlenderProc)

The structure is what the compiler joins on (cad-extract/src/StepReader.cpp): an App::Part assembly
whose bolts are App::Link instances of ONE part labelled with its part number, BLT-M12-1.25-45. Each
bolt's local origin is the centre of its head's top face, with +Z out of the face, so an occurrence's
position is the point the camera inspects and its Z axis is the face normal. Bolts are linked in
member order: the compiler numbers them in STEP order, and the star order is derived from those
positions, never typed.
"""
import math
import os
import pathlib

import FreeCAD as App
import Import
import Part

HERE = pathlib.Path(os.environ.get("TS_CAD_DIR", r"C:\dev\torquesense\cad"))
EXPORT = HERE / "export"

BOLT_PN = "BLT-M12-1.25-45"
HEAD_AF, HEAD_H = 17.0, 8.0          # M12 hex head: 17 mm across flats (feature_mm in the profile)
SHANK_R, SHANK_L = 6.0, 45.0


def hex_prism(af, h, z0):
    r = af / math.sqrt(3.0)          # circumradius from across-flats
    pts = [App.Vector(r * math.cos(math.radians(30 + 60 * k)), r * math.sin(math.radians(30 + 60 * k)), z0)
           for k in range(6)]
    face = Part.Face(Part.makePolygon(pts + [pts[0]]))
    return face.extrude(App.Vector(0, 0, h))


def bolt_shape():
    head = hex_prism(HEAD_AF, HEAD_H, -HEAD_H)                         # top face at z = 0
    washer = Part.makeCylinder(11.0, 1.5, App.Vector(0, 0, -HEAD_H - 1.5))
    shank = Part.makeCylinder(SHANK_R, SHANK_L, App.Vector(0, 0, -HEAD_H - 1.5 - SHANK_L))
    return head.fuse(washer).fuse(shank).removeSplitter()


def hub_shape(pcd, n):
    top = -HEAD_H - 1.5                                                # the wheel face the bolts seat on
    wheel_centre = Part.makeCylinder(95.0, 12.0, App.Vector(0, 0, top - 12.0))
    bore = Part.makeCylinder(33.0, 12.0, App.Vector(0, 0, top - 12.0))
    disc = wheel_centre.cut(bore)
    for k in range(n):
        a = math.radians(90 + 360.0 * k / n)
        hole = Part.makeCylinder(7.0, 12.0, App.Vector(pcd / 2 * math.cos(a), pcd / 2 * math.sin(a), top - 12.0))
        disc = disc.cut(hole)
    hub = Part.makeCylinder(70.0, 30.0, App.Vector(0, 0, top - 42.0))
    cap = Part.makeCylinder(30.0, 6.0, App.Vector(0, 0, top - 12.0))    # centre cap, below the bolt faces
    return disc.fuse(hub).fuse(cap).removeSplitter()


def rim_shape():
    # rim barrel and tyre, outside every line of sight from the camera above the hub to a bolt face
    rim = Part.makeCylinder(205.0, 160.0, App.Vector(0, 0, -150.0)).cut(
        Part.makeCylinder(195.0, 160.0, App.Vector(0, 0, -150.0)))
    spokes = []
    for k in range(5):
        a = math.radians(126 + 72 * k)
        box = Part.makeBox(110.0, 22.0, 10.0, App.Vector(90.0, -11.0, -21.5))
        box.rotate(App.Vector(0, 0, 0), App.Vector(0, 0, 1), math.degrees(a))
        spokes.append(box)
    tyre = Part.makeTorus(265.0, 62.0, App.Vector(0, 0, -70.0))
    out = rim
    for s in spokes:
        out = out.fuse(s)
    return out.removeSplitter(), tyre


def build(name, revision, pcd, n):
    doc = App.newDocument(name)
    asm = doc.addObject("App::Part", "Assembly")
    asm.Label = f"RL_WHEEL_ASSY_REV_{revision}"

    bolt = doc.addObject("Part::Feature", "Bolt")
    bolt.Label = BOLT_PN
    bolt.Shape = bolt_shape()

    hub = doc.addObject("Part::Feature", "Hub")
    hub.Label = f"HUB-RL-{n}H-{int(round(pcd * 10))}"
    hub.Shape = hub_shape(pcd, n)
    rim, tyre = rim_shape()
    rim_obj = doc.addObject("Part::Feature", "Rim")
    rim_obj.Label = "RIM-17X7-ALLOY"
    rim_obj.Shape = rim
    tyre_obj = doc.addObject("Part::Feature", "Tyre")
    tyre_obj.Label = "TYR-215-60R17"
    tyre_obj.Shape = tyre
    asm.addObjects([hub, rim_obj, tyre_obj])

    for k in range(n):                                   # member order = STEP order = bolt-1..bolt-n
        a = math.radians(90 - 360.0 * k / n)             # clockwise from 12 o'clock, as the profile lists them
        link = doc.addObject("App::Link", f"Bolt{k + 1:03d}")
        link.LinkedObject = bolt
        link.Label = f"{BOLT_PN}#{k + 1}"
        link.Placement = App.Placement(App.Vector(round(pcd / 2 * math.cos(a), 2), round(pcd / 2 * math.sin(a), 2), 0),
                                       App.Rotation())
        asm.addObject(link)
    bolt.Visibility = False                              # the prototype is not itself an occurrence
    doc.recompute()
    for o in (bolt, hub, rim_obj, tyre_obj):             # glTF carries triangles: mesh the B-rep in place
        o.Shape.tessellate(0.1)

    EXPORT.mkdir(parents=True, exist_ok=True)
    doc.saveAs(str(HERE / f"{name}.FCStd"))
    Import.export([asm], str(EXPORT / f"{name}.step"))
    Import.export([asm], str(EXPORT / f"{name}.glb"))
    print(f"{name}: rev {revision}, {n} x {BOLT_PN} at {pcd} mm PCD -> {EXPORT / (name + '.step')}")
    App.closeDocument(doc.Name)


build("wheel_assembly", "C", 114.3, 5)
build("wheel_assembly_4lug", "D", 100.0, 4)
