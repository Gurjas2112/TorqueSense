# Recordings for desk-cover-4screw

Record one clip per trace, named after it, with the phone on its stand at 350 mm, top-down:

    torquesense --config config\station_desk1.yaml --record samples\desk-cover-4screw\media\correct.mp4

| File | Perform |
|---|---|
| `correct.mp4` | lid on · start all four screws · tighten 1, 3, 2, 4 · label |
| `round_the_box.mp4` | the same, but tighten 1, 2, 3, 4 |
| `screw_left_out.mp4` | fit only screws 1–3, tighten 1, 3, 2, put the label on, wait 60 s |
| `screw_backed_out.mp4` | the correct cycle, then loosen screw 2 by a turn |

A recording is checked against the trace of the same name: `torquesense --sample
samples\desk-cover-4screw --source media\round_the_box.mp4` must raise exactly the violations listed
under `expect:` in `traces\round_the_box.yaml`. Recordings are not committed (DVC, I8).
