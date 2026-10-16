# ADR 0011 — yaml-cpp for profiles, traces and station configs

- **Status:** Accepted
- **Date:** 2026-10-16
- **Spec:** Sections 7, 12, 29 (the formats); Section 20 (the tool register, which names no YAML
  library)

## Context

The C++ side reads three YAML formats the spec defines: a profile's `profile.yaml` (the CAD
compiler's manifest), a sample's `traces/*.yaml` (`torquesense --conformance samples` and the
differential test, I16), and a station's `config/station_*.yaml` (`torquesense --config`). The
tool register lists nlohmann/json for JSON and no YAML parser. Converting every YAML file to JSON
first would add a build step and a second copy of every profile, and the samples contract (C1–C5)
must read exactly the files a reviewer edits.

## Decision

Use **yaml-cpp** from the platform package manager: `mingw-w64-ucrt-x86_64-yaml-cpp` (0.9.0) on
MSYS2 UCRT64, `libyaml-cpp-dev` on Ubuntu 24.04. It is used **only at load time** — by
`cad_extract` (compiling `profile.yaml`), by the conformance runner and by the station config
loader. Nothing on a pinned thread touches it; the hot path reads the flat arrays the loader
builds (I1).

## Consequences

- One more `pacman` package in spec Section 28 step 4, and one more in CI's `pacboy` and `apt`
  lists (`.github/workflows/ci.yml`).
- `sunverify_procedure` does not link yaml-cpp: the engine library loads `procedure.json` with
  nlohmann/json, as the spec says. yaml-cpp is linked by `cad_extract` and `torquesense`.
- No new FetchContent pin: the five pinned sources in `cmake/deps.cmake` are unchanged.
