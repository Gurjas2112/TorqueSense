# samples — one folder per assembly

Every folder here is an assembly the engine verifies **with no assembly-specific code**. A folder
is data: what to look at, in what order, and what a right and a wrong cycle look like. If your
assembly can be written as a folder that passes `run_samples.py`, TorqueSense accepts it.

    python samples\run_samples.py                 # all samples, procedure level, no camera needed
    python samples\run_samples.py --template      # also the template

| Folder | Assembly | Camera | Geometry | What it proves |
|---|---|---|---|---|
| `wheel-hub-5lug` | tyre fitting, the PS4 reference | rendered video | CAD | star order derived from a bolt circle; plant evidence (nutrunner) |
| `door-trim-harness` | clips, connector, trim panel (PS4 example) | — traces only | CAD | precedence: a panel cannot hide an open clip |
| `windshield-gasket` | gasket before glass (PS4 example) | — traces only | CAD | a seal as eight segments |
| `electronics-enclosure` | connector, six screws, label — not automotive | — traces only | CAD | field order, centre-out; omission caught at release |
| `desk-cover-4screw` | any box with a four-screw lid | **phone**, top-down | **ruler** | runs at a desk; diagonal order derived from four measured points |
| `desk-harness-clips` | cable, five clips, plug, cover | **laptop webcam** | **ruler** | the door problem at desk scale, on the camera you already have |
| `earbud-case-charge` | earbuds into a charging case, cable plugged in | **real recordings**, top-down | **measured** | real footage: a lid closed over an empty slot caught as PRECEDENCE |
| `_template` | — | — | — | copy this to start |

## A folder

    my-assembly/
      profile.yaml        what to inspect: cameras, inspection points, steps
      traces/
        correct.yaml      the right cycle — must release cleanly, with no violation
        <fault>.yaml      one wrong cycle per file, and the exact violations it must raise
      media/              optional: recordings (or renders) named after the traces

A **trace** is the sequence of committed states the perception stage would produce, with times.
It tests the procedure without a camera, so it runs in milliseconds, in CI, on any machine. A
**recording** in `media/` with the same name is the same cycle on camera; it must raise the same
violations. The trace's `expect:` block is the single statement of what should happen, used by both.

## The contract — what `run_samples.py` checks

| | Rule | Why |
|---|---|---|
| C1 | the profile compiles, and every inspection point is at least 16 px on some camera | a point the camera cannot resolve cannot be verified (I17) |
| C2 | every step has `max_duration_s` | without a deadline a skipped step is never caught (I15) |
| C3 | `traces/correct.yaml` exists and releases cleanly | a profile that cannot pass the right cycle is wrong |
| C4 | at least one fault trace, and each expects the release to be refused | a profile nobody has tried to break is untested |
| C5 | every trace raises exactly its expected violations, in order | the profile means what its author thinks |

## Adding your own assembly — no code

1. Copy `_template` to `samples/<your-assembly>/`.
2. Print the placement sheet or a marker from `_print/` (100 %, check the 50 mm bar). Put the
   assembly on it, or stick the marker beside it.
3. Measure each inspection point from the crosshair with a ruler, in millimetres: +x right,
   +y away from the marker. Number fasteners the way a work instruction would.
4. Give each group a family from the list in the template and the size of the smallest thing that
   tells its states apart.
5. Write the steps. Leave fastener order as `order: derive` — the compiler chooses it from the
   geometry and prints it; check it is what your work instruction says.
6. Write `traces/correct.yaml` and one trace per mistake you care about.
7. `python samples\run_samples.py <your-assembly>` until it says PASS. If C1 fails, it names the
   point and its size in pixels: move the camera closer, ask the source for a higher resolution,
   or add a camera.
8. Record `media/correct.mp4` and the faults, and run them through the engine.

A family that no sample uses yet needs its classifier trained once (Section 9 of the spec); a
new assembly built from known families needs none.
