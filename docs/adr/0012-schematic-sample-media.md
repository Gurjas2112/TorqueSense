# ADR 0012 — Schematic sample media, committed, and verified frame by frame

- **Status:** Accepted
- **Date:** 2026-10-16
- **Spec:** Section 12 ("A recording must raise the same violations as its trace"); invariant I8;
  extends [ADR 0010](0010-prototype-scope.md)

## Context

Spec Section 12 uses a trace's `expect:` block three times: against the reference interpreter,
against the C++ engine on the trace, and against the engine *on camera* — `media/<trace>.mp4` must
raise the same violations. The owner asked for sample videos and images in every sample folder so
that third use can be exercised now, frame by frame. There is no trained classifier yet (Phase 3 is
👤) and no desk recordings, and I8 keeps videos out of git.

## Decision

1. `synth/sample_media.py` draws, for every trace of every sample, a **schematic** video and keyframe
   images: each camera a tile, each inspection point at its CAD position projected through its own
   camera, its colour encoding its rank in its ordered family (ring / grey / yellow; a glove means
   occluded). Every frame is stamped as a drawing, not a camera image.
2. `SchematicDetector` (perception/) reads those colours back from the pixels. `torquesense --sample
   <dir> --verify-media all` runs every frame through it, the 2-frame debounce, the committer and the
   interpreter, and requires each video to raise exactly its trace's violations; it also checks every
   keyframe against `frames.json`. ctest runs it as `MediaConformance.<sample>`.
3. **I8 exception, narrowed:** `samples/*/media/*.mp4` and `samples/*/media/*/*.png` are committed —
   about 30 MB in all, synthetic, regenerable. Weights, renders, scenario videos under `data/`, real
   recordings and TensorRT caches stay out of git.

## Consequences

- The media path — FFmpeg decode, pixel sampling, debounce, interpreter, release gate — is tested on
  every build, before any model or camera exists.
- SchematicDetector is valid only on schematic frames. No accuracy figure is ever taken from it; I10
  still needs the trained detector and real photographs.
- Real desk recordings, once made, replace the schematic videos under the same names and must pass
  the same `--verify-media` check with a taught classifier (Phase 11).
