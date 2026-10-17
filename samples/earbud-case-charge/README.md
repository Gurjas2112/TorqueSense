# earbud-case-charge: a real assembly, filmed

Earbuds into their charging case, the lid closed, the cable plugged in, filmed top-down on a desk.
These are the project's first **real camera recordings**. Every other video in `samples/` is a
schematic drawing or a CAD render.

| Recording | Trace | What happens | TorqueSense's verdict | Earlier VLM tool (`baseline/`) |
|---|---|---|---|---|
| `recordings/golden.mp4` (20.5 s) | `correct` | every step, in order | releases, no violation | all six steps `done` |
| `recordings/test.mp4` (16.8 s) | `right_earbud_left_out` | right earbud never inserted | **PRECEDENCE at 3.0 s**, the moment the lid closes over the empty slot; then STEP_TIMEOUT and INCOMPLETE_AT_RELEASE | step 4 `missing` |
| `recordings/test1.mp4` (17.0 s) | `left_earbud_left_out` | left earbud never inserted | **PRECEDENCE at 3.5 s**, as the lid closes; then STEP_TIMEOUT and INCOMPLETE_AT_RELEASE | step 3 `missing` |
| `recordings/test2.mp4` (13.9 s) | `cable_never_plugged` | cable never plugged in | INCOMPLETE_AT_RELEASE at the release gate | step 6 `missing` |

Both tools find the same omission in every video. The difference is when it is found:
- **The earlier tool** reads sampled frames after the run and reports a step as missing once the whole
  video has been seen.
- **TorqueSense** raises the alarm the instant the next step would hide the omission. The lid closing
  over an empty slot is the door-trim problem in miniature: the station stops before the case moves on.

## What is in this folder

| Path | What |
|---|---|
| `profile.yaml` | the assembly as data: what is inspected, where, and the six steps with their order and deadlines |
| `procedure.json`, `coverage.json` | compiled by `cad-extract`; every point resolves at ≥ 35.9 px |
| `traces/*.yaml` | the four runs as committed state changes, each with the verdict it must produce |
| `recordings/` | the four videos (480 × 480, 30 fps) and close-ups of each part (`objects/`) |
| `baseline/out_*/verification_result.json` | the earlier VLM tool's per-step results for the same videos |

## The profile

| Group | Family (ordered states) | Points |
|---|---|---|
| `kit` | presence `[absent, present]` | case, left earbud, right earbud, cable |
| `lid` | hinged_lid `[shut, open, closed]` | the case |
| `left_slot`, `right_slot` | earbud_slot `[empty, seated]` | one each |
| `cable` | connector `[unmated, mated]` | the plug at the case |

Steps, in order:
1. `prepare`: the kit is present
2. `open_case`
3. `insert_left` and `insert_right`
4. `close_case`: **requires both inserts**
5. `plug_cable`

The lid's family has three states, because "closed again over loaded slots" is a later state than
"shut as delivered". Closing the lid is therefore progress, not a regression.

## How the traces were made, and what is not claimed

- **Where the times come from.** The times in each trace are the per-step timestamps the earlier tool
  reported for these videos. The golden run's times come from its evidence frames, which are every 8th
  frame at 30 fps. The positions were measured from the first frame of `golden.mp4` at about 3.6 px/mm.
- **What the traces test.** They test the procedure, and both engines agree on them: the reference
  interpreter and the C++ engine (`torquesense --conformance samples`).
- **Not yet:** the states read from these pixels by a trained model. No classifier has been trained for
  `hinged_lid` or `earbud_slot`. Four short videos are too few to train and validate one honestly, so
  no perception accuracy is claimed for this assembly. Labelling frames from more recordings would
  make it a full perception sample.
- **The LED.** It is about 2 mm across: roughly 7 px at this resolution, under the compiler's 16 px
  floor. So "LED on" is not an inspection point here, and the cable's `mated` state stands for the
  charge step. A closer camera or a higher capture resolution would let it be added.
