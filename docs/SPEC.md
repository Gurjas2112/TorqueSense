# TorqueSense — SPEC.md (v5.5, prototype edition)

> **What this file is.** The full engineering specification v5.5
> (`docs/TorqueSense_Engineering_Specification_v5.5.md`, reproduced unchanged below this preface)
> plus one preface that fixes the scope of the **prototype** build. Nothing below the preface was
> edited. Where the preface and the body differ, the preface states the difference and the ADR
> that records it.

## P.1 Scope of the prototype edition

| Area | Status in the prototype | Record |
|---|---|---|
| Engine, CAD compiler, procedure model, samples, soft PLC, heartbeat (Parts I–III) | **build** — unchanged | spec body |
| Phases 0–12 of the build plan (`.github/copilot-instructions.md`) | **build**, in order; 👤 steps stop and are handed to the human | spec body |
| Phase 13 — production track (spec Section 33: supervisor install, signed releases, `Jenkinsfile.release`, Autologon, `tsdemo` account, power-cut test, nightly backup) | ⏸ **deferred on purpose** — the starter-kit files stay in the repository, tested by CI, but nothing is installed under `C:\TorqueSense` | `docs/adr/0010-prototype-scope.md` |
| Appendix D — industrial camera, RKE2, Tier 2 | ⏸ deferred on purpose — unchanged from v5.5 | spec Appendix D |
| Supabase region | the project was created in **Oceania (Sydney), `ap-southeast-2`**, not South Asia (Mumbai) as Section 29 recommends. Nothing on the alert path uses the network, so the region affects only the outbox's flush latency | `docs/adr/0010-prototype-scope.md` |
| Deliverable recordings | the PS asks for a flow-wise set of screenshots and one full screen recording, kept in `app_screenshots_screen_recording/` — the only videos the repository carries (I8 exception, small files only) | `docs/adr/0010-prototype-scope.md` |

The production rules in Section 33 still govern anything that *is* built: no process reads a
user's `PATH` at run time, configuration and data never live inside a release folder, and no
release is published by hand. The prototype simply does not install releases yet — it runs from
the build tree with `scripts/start-dev.ps1`, as Section 32 allows for development.

## P.2 The machine, as checked on 16 October 2026

Read-only checks run on HSGPC before any build step. Every later gate is measured against this
table; where it differs from Section 26, this table is the fact and Section 26 the plan.

| Component | Spec wants | Found | Verdict |
|---|---|---|---|
| OS | Windows 11 Home 26H2, build 26300.9711 | Windows 11 Home 26H2, build 26300.9550 / 9711 reported | ✅ |
| CPU / RAM | i7-11800H 8C/16T · 16 GB | i7-11800H @ 2.30 GHz · 16 GB (15.7 usable) | ✅ |
| GPU / driver | RTX 3060 Laptop 6 GB, cc 8.6, ≥ R580 | RTX 3060 Laptop, 6144 MiB, cc 8.6, driver **596.21** | ✅ gate G1 |
| CUDA toolkit | 13.2 | nvcc 13.2.78; `CUDA_PATH=…\CUDA\v13.2`; `cudart64_13.dll`, `cublas64_13.dll` in `bin\x64` | ✅ |
| cuDNN 9 for CUDA 13 | on `PATH` | `cudnn64_9.dll` **not found** | 👤 install (Section 28 step 3) |
| ONNX Runtime 1.28.0 `gpu_cuda13` | `C:\dev\tools\onnxruntime` | `C:\dev\tools` **absent** | 👤 install (Section 28 step 3) |
| TensorRT 10.13.3.9 (CUDA 13.0 zip) | `C:\dev\tools\TensorRT` | **absent** | 👤 install — needed only for Phase 12 |
| C++ compiler / debugger | MSYS2 UCRT64 GCC 16.2 / GDB 18.1 | `C:\msys64\ucrt64\bin\g++` 16.2.0 (Rev4), GDB 18.1; `hello.cpp` builds and runs | ✅ |
| CMake + Ninja | from MSYS2 `pacman` | CMake **3.26.3** (`C:\Program Files (x86)`), Ninja 1.13 (pip); UCRT64 `cmake`, `ninja`, OpenCV, OCCT, spdlog, nlohmann-json, gtest, benchmark **not installed** | ⚠️ `pacman -S` list in Section 28 step 4 |
| Python | python.org 3.12, latest patch | **3.12.0** via `py -3.12`; also uv CPython 3.14.4 and MSYS2 Python 3.14.7 | ⚠️ works; `winget upgrade Python.Python.3.12` recommended |
| Go | ≥ 1.24 | 1.27.2 | ✅ |
| Node | ≥ 22.12 | 26.0.0, npm 12.0.1 | ✅ |
| Docker | Desktop, WSL2 backend | 29.5.2 | ✅ (CI mode only) |
| ffmpeg | for camera listing and recordings | 8.1.2 (gyan.dev build) | ✅ |
| Project root | `C:\dev\torquesense` | moved there from `OneDrive\Desktop` (the space in the user path) | ✅ |

## P.3 What the prototype therefore proves, and what it cannot yet

- **Without the 👤 downloads:** the reference interpreter and the C++ `ProcedureEngine` agree on
  every sample trace (I16); the lock-free core, the soft PLC and its heartbeat watchdog (I13); the
  outbox to Supabase; the dashboard, HMI, harness, line and fleet views; the fleet agent and EWMA
  drift charts. The perception stage runs as `ReplayDetector` (Section 8), feeding recorded
  detections through the same pipeline.
- **Only after the 👤 downloads and training:** the GPU latency gate (I4), mAP figures (I10), VRAM
  (I7) and the TensorRT gate. Until then `docs/VALIDATION.md` says *not measured*, and no number
  is quoted anywhere (Section 7 of the coding instructions).

---

# TorqueSense — CAD-Compiled Assembly Verification, for Any Assembly

### Engineering, product and experience specification

**PS4 · Real-time Assembly Process Verification Using Vision LLMs · Tata Motors** — v5.5, production-ready prototype · universal profile engine · laptop webcam and phone · MSYS2 GCC · Supabase · TensorRT · supervised, released and gated

Scope: any assembly whose steps change something visible or signal the PLC · Reference profile: tyre fitting, 5-lug hub, 114.3 mm PCD · Target machine: **HSGPC**

> This specification is written for one machine — yours — and one build: the software-only
> prototype. Every version, path, memory figure and command has been chosen against the system
> details you supplied. It consolidates every decision taken so far (software-only build,
> CAD-generated training data, Florence-2 as a local adjudicator, CUDA 13.2, with TensorRT
> reached only through ONNX Runtime and a gate) and corrects the places where earlier drafts were wrong. Those corrections are listed
> in Appendix A rather than buried.
>
> **What v4.1 adds.** An operations track that treats the stations as a fleet of devices:
> per-station health telemetry with statistical drift detection, a fail-visible heartbeat, a
> Jenkins pipeline for the GPU-bound gates, container images and Kubernetes manifests tested in
> CI, and a small Go fleet agent. Each addition passes the scope test in Section 1 on
> engineering grounds, not as decoration. Most were checked before they were written down: the
> drift limits were set by simulation, the agent and the soft PLC's watchdog were compiled and
> run, and the manifests and pipelines were checked by real validators. That checking found an
> OPC-UA instruction in v4.0 that would have stopped the soft PLC from starting — Appendix A,
> item 18.
>
> **What v5.0 changes.** The engine is no longer shaped around one wheel. Every assembly is a
> *profile*: inspection points with ordered state families, steps with order, precedence,
> deadlines and evidence, compiled from CAD plus a short manifest (Sections 6–11). Tyre fitting
> stays the reference profile. The problem statement's other two examples — the door-panel harness
> clip and the windshield gasket — and a non-automotive electronics enclosure are written as
> profiles too, and all four run through one interpreter with no assembly-specific code. That
> interpreter was built and tested before this revision was written: 24 tests, two of which caught
> design errors in its first draft. Building it also exposed a resolution problem v4.1 never
> checked (Appendix A, item 20).
>
> **What v5.1 adds.** Two things the prototype could skip and a plant cannot. **Camera capture**
> (Section 8): one frame-source interface with file, webcam and industrial GigE implementations;
> a named production camera; a lens chosen by the compiler from the same 16 px rule; a link budget
> that decides the pixel format and the network card; zero-copy grabbing into the engine's own
> frame pool; and a commissioning checklist. **A production platform** (now Appendix D): one tool
> per layer — edge PC and GPU, RKE2, the NVIDIA GPU Operator, Flux, Harbor, cosign with Kyverno,
> CloudNativePG with TimescaleDB, Garage, and the platform's own observability — and the path a
> release takes from a pull request to a station. The lens and link arithmetic is in the reference
> implementation and tested (29 tests now); the fleet agent gained a dcgm-exporter GPU source; the
> manifests validate against Kubernetes 1.36. Researching the platform overturned three v5.0
> choices (Appendix A, items 24–26).
>
> **What v5.2 changes — the prototype edition.** Production is parked on purpose: the industrial
> camera, the platform and Tier 2 move intact to Appendix D, and nothing on the prototype path
> depends on them. Three things replace them in the main line:
>
> - **Your own toolchain.** The C++ build now uses the MSYS2 UCRT64 GCC 16.2 and GDB 18.1 already on
>   this laptop, with libraries from `pacman` and five small ones pinned through CMake. Visual
>   Studio Build Tools and vcpkg are gone (Section 28).
> - **Your own cameras.** The laptop webcam and your phone, with their resolution, calibration and
>   registration worked out (Section 8).
> - **A `samples/` folder** that makes "any assembly" testable: six assemblies — two of them built
>   at a desk with a ruler, no CAD — checked by one generic runner against one contract
>   (Section 12).
>
> The risky parts were tested before being written down. The engine's ONNX Runtime code was built
> with MinGW-w64 GCC against the official `gpu_cuda13` 1.28.0 package and ran a model correctly. The
> soft PLC was built with MinGW and passed the heartbeat test (AMBER 1.86 s after the last write).
> The five pinned libraries built and ran. The printed calibration sheets were detected back at
> scale. The reference suite is now 42 tests.
>
> **What v5.3 changes — Supabase.** The local TimescaleDB and Garage containers are replaced by one
> free Supabase project: Postgres for events and health, Storage for clips, and Realtime, so the
> Vercel dashboard is live rather than a replay. Supabase marks the `timescaledb` extension
> deprecated on Postgres 17 and ships only its Apache edition, which lacks continuous aggregates
> and retention policies. So the schema is now plain Postgres (`date_bin`, a view, `pg_cron`), and
> TimescaleDB is not used at all. Because Supabase is in the cloud, everything recorded now goes
> through a local outbox: the alert path never needs the network, and a dropped link loses
> nothing. The migrations and the outbox are tested against a real Postgres with Supabase's roles
> emulated (Section 16).
>
> **What v5.4 adds — TensorRT, through ONNX Runtime.** The detector can run on ONNX Runtime's
> TensorRT execution provider: the same model and session code as the CUDA path, FP16 on Ampere,
> an engine cache built once during setup, and a gate — output parity plus latency — that decides
> whether the demo uses it (Section 30). Your driver, 596.21, clears the R580 floor, so nothing needs
> upgrading. Two findings shaped the install. The provider inside ONNX Runtime 1.28.0's CUDA 13
> package imports `nvinfer_10.dll` — read from the DLL's import table — so TensorRT is pinned to
> 10.13.3.9, the release ONNX Runtime's own CUDA 13 pipeline uses; an unpinned
> `pip install tensorrt-cu13` now brings TensorRT 11, which that provider cannot load. And because
> ONNX Runtime loads TensorRT, the MSYS2 GCC build never links it: a MinGW-built check program
> appended the provider and failed loudly, naming the missing DLL, on a machine without CUDA. A new
> `gpu_preflight.py` checks the driver, the GPU and every DLL the engine loads; its tests run on your
> own `nvidia-smi` output (Appendix A, items 32–33).
>
> **What v5.5 changes — a production-ready prototype.** The same software now runs the way a
> station must: it starts itself after a power cut, restarts what crashes, ships as a verified
> release, is proven on the GPU before it goes live, rolls back in one command, and emails you
> when it is unwell (Section 33). The inference path is decided: ONNX Runtime's TensorRT provider
> in FP16, with the CUDA provider as its validated, visible fallback — chosen over native TensorRT,
> Triton, DeepStream and Windows ML for reasons specific to this build. New, and tested while
> writing:
>
> - **A Go supervisor.** It starts processes in dependency order, restarts them with backoff, and
>   holds them in a Windows job object so none outlives it. Its tests pass as a Linux binary and as
>   a Windows binary under Wine, and a `kill -9` left no child running on either.
> - **A release bundler.** It reads PE import tables, and run on ONNX Runtime's real DLLs it found
>   `MSVCP140_1.dll`, a dependency v5.4's checks missed.
> - **A release gate, an installer with automatic rollback, and an hourly watchdog.** The gate's
>   decision rules and the watchdog have their own tests.
>
> Docker leaves the run-time path: the API and Node-RED run natively, and the API serves the built
> dashboard (Appendix A, items 34–37).

## How to read this document

This is a **single recommended path**, not a menu. Every tool is the one pick for its layer, with
the reason it beat the alternatives. Parts I–II say what to build and why; Part III covers the
platform around the engine; Part IV is the build and the pitch; Part V takes the build from an
empty laptop to a running demo on *this* laptop.

Production is deliberately deferred. Appendix D keeps the industrial camera, the plant
platform and Tier 2 exactly as designed, so they can be picked up later without being rethought —
but nothing on the prototype path needs them.

When the platform and the verification engine compete for hours, **the engine wins**. A polished
dashboard around a detector that misreads part states loses to a plain one around a detector that
does not.

### Your machine at a glance

| | |
|---|---|
| Device | HSGPC |
| OS | Windows 11 **Home**, 26H2, build 26300.9711 |
| CPU | Intel Core i7-11800H — 8 cores / 16 threads, Tiger Lake, AVX2 + AVX-512 |
| RAM | 16 GB (15.7 GB usable) |
| GPU | NVIDIA GeForce RTX 3060 Laptop, 6 GB, Ampere, compute capability 8.6 · plus Intel UHD (display) |
| CUDA Toolkit | **13.2**, V13.2.78 (nvcc built 19 Mar 2026) |
| NVIDIA driver | **596.21**, WDDM — supports CUDA up to 13.2 |
| Python | **3.12.0**, python.org installer, per user — reached through the `py` launcher |
| C++ compiler | **GCC 16.2.0**, MSYS2 (`g++ (Rev4, Built by MSYS2 project)`) — on `PATH`, tested with `hello.cpp` |
| Debugger | **GDB 18.1**, MSYS2 |
| Cameras | built-in laptop webcam · Android phone |
| Storage | ~356 GB free of 954 GB |
| User path | `C:\Users\Gurjas Gandhi` — **contains a space** |

Section 26 turns this into a component-by-component verdict.

## Contents

**Part I — Position**

- [1. Scope and scoring logic](#1-scope-and-scoring-logic)
- [2. What is already solved, and what is not](#2-what-is-already-solved-and-what-is-not)
- [3. Users and scenarios](#3-users-and-scenarios)

**Part II — System**

- [4. System architecture](#4-system-architecture)
- [5. End-to-end verification flow](#5-end-to-end-verification-flow)
- [6. The procedure model — any assembly, as data](#6-the-procedure-model--any-assembly-as-data)
- [7. The CAD compiler and pattern solvers](#7-the-cad-compiler-and-pattern-solvers)
- [8. Camera capture — laptop webcam and phone](#8-camera-capture--laptop-webcam-and-phone)
- [9. Perception for any part](#9-perception-for-any-part)
- [10. Evidence beyond the camera](#10-evidence-beyond-the-camera)
- [11. Onboarding a new assembly](#11-onboarding-a-new-assembly)
- [12. Sample assemblies — the conformance suite](#12-sample-assemblies--the-conformance-suite)
- [13. Latency and capacity model](#13-latency-and-capacity-model)

**Part III — Platform**

- [14. Process and thread model](#14-process-and-thread-model)
- [15. Identity and access](#15-identity-and-access)
- [16. Data architecture](#16-data-architecture)
- [17. Verification and validation](#17-verification-and-validation)
- [18. Industrial and safety parameters](#18-industrial-and-safety-parameters)
- [19. Fleet health and reliability](#19-fleet-health-and-reliability)

**Part IV — Build**

- [20. Tool register](#20-tool-register)
- [21. Interface design](#21-interface-design)
- [22. Prototype build plan](#22-prototype-build-plan)
- [23. Demo script](#23-demo-script)
- [24. Risk register](#24-risk-register)

**Part V — Setup and operations, on HSGPC**

- [25. Setup roadmap](#25-setup-roadmap)
- [26. Your machine — compatibility verdict](#26-your-machine--compatibility-verdict)
- [27. Compute — what the Colab T4 is for](#27-compute--what-the-colab-t4-is-for)
- [28. Installation](#28-installation)
- [29. Configuration](#29-configuration)
- [30. Integration](#30-integration)
- [31. Testing](#31-testing)
- [32. Deployment](#32-deployment)
- [33. Production readiness — the prototype that runs unattended](#33-production-readiness--the-prototype-that-runs-unattended)
- [34. Caching strategy](#34-caching-strategy)

**Appendices**

- [\1. Corrections to earlier drafts](#appendix-a--corrections-to-earlier-drafts)
- [\1. Coding-agent kit deltas](#appendix-b--coding-agent-kit-deltas)
- [\1. Portfolio and interview mapping](#appendix-c--portfolio-and-interview-mapping)
- [\1. Production, deferred on purpose](#appendix-d--production-deferred-on-purpose)

---

# Part I — Position

## 1. Scope and scoring logic

PS4 grades a working verification system, not a platform. The rubric is explicit:

| Criterion | Weight | What scores 5 |
|---|---|---|
| Innovation | 20% | Strong novelty: CV + LLM integration for smart inspection |
| Technical Execution | 30% | High accuracy, robust anomaly and process validation |
| Business Relevance | 25% | Strong alignment with Tata Motors' quality operations |
| Visualization & UX | 15% | Clear video overlays and an intuitive inspection dashboard |
| Presentation & Demo | 10% | Smooth demo, strong business-impact storytelling |

The problem statement also names its own acceptance criterion: capture, detect a missing step, and
trigger an alert at the station **in under one second**. That sentence is the system's contract.

### The two tests

Every element must either **feed a graded criterion** or be **required by the one-second
contract**. Anything failing both was cut.

| Element | Verdict | Reason |
|---|---|---|
| CAD-compiled procedure | ✅ **build** | The innovation claim. Without it this is a hand-written rule engine. |
| Order, precedence and omission checks | ✅ **build** | The technical claim. Presence detection is commodity; order, precedence and omission are not. |
| Profile engine — any assembly as data | ✅ **build** | The problem statement names three different assemblies. A system that hard-codes one has answered a third of it. This is also the strongest innovation claim. |
| Three more profiles from the problem statement's own examples | ✅ **build**, staged | The door trim runs end to end; windshield and enclosure are compiled and verified at the procedure level — proof the engine is general without building four perception stacks (Section 22). |
| Lock-free C++ pipeline | ✅ **build** | Required by the contract, and it is what makes the latency number defensible. |
| Synthetic data from the same CAD | ✅ **build** | Turns the no-hardware constraint into a second innovation claim. |
| Real OPC-UA alert path | ✅ **build** | Business relevance: the alert speaks the protocol the plant already runs. |
| Florence-2 local adjudicator | ✅ **build** | The "LLM" half of the rubric's "CV + LLM", sized to fit the GPU. |
| Engineer dashboard + operator HMI | ✅ **build** | 15% of the score. |
| Fault-injection harness | ✅ **build** | Turns the demo from a recording into something a judge can break. |
| Four-station mode | ✅ **build** | Business relevance — scale shown, not asserted. |
| Station-fleet health monitoring | ✅ **build** | A camera inspector degrades silently: a smeared lens, a knocked mount, a throttling GPU. Catching that before it produces false alarms is the second question any plant asks, and the answer is business relevance. |
| Production-ready operation — supervisor, signed releases, release gate, rollback, watchdog | ✅ **build** | A demo that needs its author at the keyboard is not a verification system. Each piece removes a way the system fails on the day without anyone touching it (Section 33). |
| Fail-visible heartbeat | ✅ **build** | A quality gate that silently stops checking is worse than none. Cheap, and the line's own PLC enforces it. |
| Jenkins pipeline on the laptop | ✅ **build** | The latency and accuracy gates need the GPU, so hosted CI cannot run them. Without automation they get run by hand, then forgotten. |
| Container images + Kubernetes manifests | ⏸ **deferred**, kept | Not part of the demo or the prototype. Kept so they do not rot: a manual workflow still proves them in a kind cluster (Appendix D). They make Tier 2 concrete, and CI proves they work in a throwaway kind cluster at no cost to the laptop's 16 GB. |
| Go fleet agent | ✅ **build** | The engine cannot report its own death. A watcher must live outside the process it watches, and a static binary with no runtime is the right shape for an edge PC. |
| Anomaly log with clips | ✅ **build** | The PS asks for it by name: timestamp, station ID, video snippet. |
| Excel shift report | ⚠️ **reframe** | Not a feature for its own sake — it is how a quality engineer actually consumes this data on a Tata line. Built in C++, which also serves the Sun Fibo JD. |
| Login, accounts, roles | ❌ **cut** | Zero judge value on a single-laptop demo. Section 15 says what production needs. |
| Prometheus, Grafana, tracing | ❌ **cut** | Fleet health is stored in the same Supabase Postgres that holds the events, and drawn by the dashboard. A second metrics stack would duplicate it for roughly 0.5 GB of RAM this machine cannot spare. In the plant, a platform stack would watch the cluster itself (Appendix D); station health stays in Postgres. |
| Camera capture — laptop webcam and phone | ✅ **build** | Every verdict starts with a frame. The cameras you own run the desk samples live; the arithmetic that says whether they can see each point is tested (Section 8). |
| Sample assemblies — the conformance suite | ✅ **build** | "Universal" has to be testable. Six assemblies, two of them at a desk with no CAD, pass one contract through one runner (Section 12). |
| Production platform, industrial camera, Tier 2 | ⏸ **deferred on purpose** | Designed and kept intact in Appendix D. Picked up after the prototype proves itself, not before. |
| ESP32, tower light | ❌ **cut** | Nothing is bought for the demo: the cameras are the ones you own. Section 12 of the hardware addendum is the upgrade path. |
| Qt operator HMI | ❌ **cut** | A second UI toolchain on a 16 GB machine. The HMI becomes a kiosk route of the same web app (Section 21). |

> **The governing trade.** When the platform track and the engine track compete for hours, the
> engine wins. Judges forgive a plain dashboard. They do not forgive a red light that fires on the
> correct sequence.

---

## 2. What is already solved, and what is not

A Tata Motors judge knows line-side quality tooling. Open with what exists, or they will.

### Solved elsewhere

| Capability | How it is done today | Limitation |
|---|---|---|
| Torque value per fastening | DC electric nutrunners with controllers record torque and angle for every rundown and flag out-of-spec values | Counts and values events. Has no geometric notion of *which* bolt was fastened. |
| Fastener sequence enforcement | Position-sensing torque reaction arms with encoders, or socket trays that release one socket at a time | Per-station hardware and fixtures, re-engineered for every wheel variant. Expensive to retrofit across a line. |
| Part presence | Rule-based machine vision at end-of-line | Confirms five bolts exist. Says nothing about the order they were tightened in. |
| Operator guidance | Pick-to-light, work-instruction screens | Tells the operator what to do. Does not verify what they did. |

### Not solved — the gap TorqueSense fills

| Gap | Why it matters | TorqueSense module |
|---|---|---|
| Sequence verification **without fixtures** | Position arms work, but cost scales per station and per variant | Camera-only verification, retrofittable to any station with a view of the work |
| Rules that follow design changes | Every hand-written rule set goes stale when engineering revises the part | **CAD compiler** — the procedure is derived from the assembly model |
| Training data that follows design changes | A new variant or a new assembly means a new labelled dataset | **Synthetic renders from the same CAD** — zero manual labelling |
| Explanation tied to geometry | A red light alone does not tell an engineer which part | Evidence linked to CAD inspection-point identities, ghost overlay, deterministic narrative |
| One system for every station | Each existing tool is engineered per station: a torque arm for wheels, a fixture for clips, a vision recipe per part | **Profiles** — one engine, one data file per assembly (Section 6) |
| Knowing the inspector itself is healthy | Camera systems fail quietly: a smeared lens lowers detection confidence for days before it causes a false alarm | **Fleet health** — per-station control charts that flag drift before it reaches the line (Section 19) |

> **Use this as your opening rebuttal.** "We already have torque arms" is the first objection a
> line engineer raises. The answer: torque arms solve sequence per station, with hardware, for one
> variant. TorqueSense solves it with a camera that reconfigures itself from CAD — and it does not
> replace the nutrunner controller, it sits beside it.

### What TorqueSense deliberately does not claim

**It does not measure torque.** A camera cannot see clamp load. The torque value stays the
nutrunner controller's job. TorqueSense verifies **sequence and completeness** — which bolts, in
what order, all of them. Integrating the controller's per-rundown result over OPC-UA is the Tier 2
step (Section 32). Say this before a judge asks; it is the most likely question from anyone who has
worked a line, and volunteering it is worth more than any feature.

**"Any assembly" has a definition.** TorqueSense verifies steps that change something visible at a
known point, or that the plant can signal. Cable routing, cosmetic defects and dimensional metrology
are different problems. Section 11 has the five-question check that says whether a station
qualifies — and the compiler refuses a station whose parts its cameras cannot resolve.

---

## 3. Users and scenarios

Six people. Every screen exists because one of them needs it. Demo them in this order.

### A — Rajesh, operator, Station W-14, 58-second takt

Gloves on, three metres from a screen, sodium lighting. He mounts the rear-left wheel, hand-starts
five M12 bolts, and torques them in the star pattern. Mid-cycle his supervisor calls him away. He
returns and resumes — from bolt 2, sequentially. Nothing he can see is wrong. **About seventy
milliseconds** after the out-of-order bolt is tightened, his screen floods red and draws the
corrective path across a hub schematic that matches the wheel in front of him, bolt for bolt. He
reads nothing.

*Forces:* zero-reading HMI, spatial correspondence between screen and wheel, an alert faster than
the next action.

### B — Priya, process engineer, end of shift

Forty anomalies across four stations. She opens the dashboard, sorts by station, and opens one.
Six-second clip, the frame-by-frame bolt-state strip, the CAD view with the offending bolt in red,
and a one-line explanation generated from the state machine's own facts. She marks two as false
positives — each writes a hard-negative sample back to the training set. She exports the shift to
Excel for the morning quality meeting.

*Forces:* evidence linked to geometry, a disagreement loop, the export format the plant actually uses.

### C — Arvind, manufacturing engineer, new wheel variant

Engineering releases a four-lug hub for a new model. Arvind opens the revised assembly in FreeCAD
and re-runs the compiler. The procedure regenerates — sequence 1, 3, 2, 4, derived from geometry —
and the synthetic training set regenerates from the same model overnight. No rules typed, no images
labelled.

*Forces:* the CAD compiler, the render pipeline, the claim that reconfiguration is a file, not a project.

### D — The judge

Handed the laptop and asked: *"Pick a fault. Any fault."* They click one. The system catches it.
Then they watch four stations run at once on the same machine.

*Forces:* a fault-injection harness that cannot influence the verdict, and a multi-station mode that
fits in 6 GB of VRAM.

### E — Meera, controls engineer, twelve stations' edge PCs

Monday, 07:40. Nothing is alarming. But the fleet view shows W-16's detector-confidence chart in
amber since Saturday night, with its ambiguous-detection rate creeping up beside it and inference
latency flat. The playbook pairs that pattern with lens contamination or a lighting change, not a
software fault. She cleans the lens at the 09:00 break; within the hour both charts are back
inside their limits. No false alarm ever reached an operator. That afternoon she rolls a retrained
model onto W-15 alone, compares it with its neighbours for one shift, and promotes it to the rest
of the line the next morning. When W-12's PC crashed last month, its station went amber in two
seconds and the line lead knew not to trust it.

*Forces:* drift detection that fires before failure, a playbook from signal pattern to likely
cause, per-station rollout and rollback, a station that goes visibly amber the moment its
inspector stops.

### F — Neha, industrial engineer, the door line

A different line, a different assembly: front-door trim, six harness clips, a twelve-pin connector,
then the trim panel. Neha has the door CAD from engineering and the station's PLC tags from
controls. She writes a forty-line manifest and compiles it. The compiler refuses: four clips at the
ends of the door span 15 px from cameras mounted at 500 mm. She lowers the camera bar to 420 mm and
recompiles clean. No model is trained — snap clips and connectors are already in the family
library. After a validation shift the station goes live on the same engine as the wheel line, and
its first real catch is a trim panel pressed home over an open clip.

*Forces:* profiles as data, a compiler that refuses unresolvable rigs, a reusable family library,
precedence checks, onboarding measured in days.

---

# Part II — System

## 4. System architecture

One C++ engine that runs any profile, one local vision-language sidecar, and for each assembly one
CAD model feeding both the rules and the training data. Nothing runs in the cloud at demo time.

```mermaid
flowchart TB
  subgraph DT["Design time — runs when the CAD changes"]
    CAD["Assembly CAD<br/>any product"] --> STEP["STEP AP214"]
    MAN[/"manifest.yaml<br/>groups · steps · evidence"/] --> CC
    CAD --> GLTF["glTF"]
    STEP --> CC["CAD compiler · C++ · OCCT<br/>IPs · pattern solvers · coverage"]
    CC --> PJ[("profile<br/>procedure.json · coverage.json")]
    GLTF --> BP["BlenderProc · seeded<br/>domain randomisation"]
    BP --> DS[("auto-labelled crops per family<br/>+ scenario videos")]
    DS --> COL["Colab T4<br/>family classifiers · hands/tools detector"]
    COL --> ONNX[("ONNX models<br/>+ SHA-256 manifest")]
  end
  subgraph RT["Runtime — HSGPC · RTX 3060 6 GB"]
    FS["IFrameSource<br/>file · laptop webcam · phone stream"] --> CAP["capture"]
    CAP -- "SPSC ring" --> INF["inference<br/>detector + IP-state classifiers"]
    INF -- "SPSC ring" --> VER["verify<br/>ProcedureEngine · any profile"]
    VER -- "SPSC ring" --> ALR["alert<br/>OPC-UA client"]
    VER -- "SPSC ring" --> IO["io<br/>WebSocket · events · narrator"]
    CAP --> RING[("SharedClipRing<br/>Win32 shared memory · 46 MB")]
  end
  subgraph SC["Local sidecar — Python · same GPU"]
    FL["Florence-2-base<br/>phrase grounding · OCR"]
    CW["clip writer<br/>JPEG ring → MP4"]
  end
  subgraph PL["Platform — native processes under the supervisor"]
    API["FastAPI<br/>events · health · EWMA drift job"]
    OB[("outbox<br/>SQLite")]
    NR["Node-RED<br/>tag monitor"]
  end
  PLC["Soft PLC · open62541 server<br/>:4840 · heartbeat watchdog"]
  AGT["fleet-agent · Go<br/>host health · engine liveness"]
  UI["React dashboard + operator HMI<br/>served by the API :8000 · /fleet"]
  PJ --> VER
  ONNX --> INF
  ALR --> PLC
  PLC --> NR
  IO -- "ambiguous IP" --> FL
  PLC -. "evidence · release request" .-> VER
  RING --> FL
  RING --> CW
  CW --> API
  IO --> API
  API --> OB
  OB -. "in order, when online" .-> SB[("Supabase<br/>Postgres · Storage · Realtime")]
  IO --> UI
  API --> UI
  AGT -. "probe :8095" .-> IO
  AGT --> API
  classDef key fill:#E6F1F2,stroke:#0E7C86,stroke-width:1.5px;
  class CC,VER,FL key;
```

*Figure 1 — System architecture. The teal elements are load-bearing: the CAD compiler is the
innovation, the verify thread is the contract, and Florence-2 is the reasoning layer. Note that the
CAD model feeds both the left branch (rules) and the right branch (training data). Colab appears
only at design time. The fleet agent and the PLC's heartbeat watchdog sit outside the engine on
purpose: both exist to notice when the engine has stopped, which the engine cannot do itself. Swap the profile and the same runtime verifies a different
assembly.*

### Why the core is C++, and the reasoning layer is not

The hot path must allocate nothing, lock nothing, and finish in a fraction of a frame. That is a C++
job. Florence-2 is a 230-million-parameter PyTorch model invoked a few times a minute; rewriting its
inference in C++ buys nothing and costs days. The boundary between them is a shared-memory ring and
one HTTP call on localhost, never a blocking call from the verify thread.

### Why the vision-language model is off the critical path

Florence-2 takes 200–400 ms per call on this GPU. The frame budget is 33 ms. It reasons about the
frames the detector is unsure of; it never decides an alert. This is the architectural answer to the
rubric's "CV + LLM" — the integration is real, and it is placed where a 230M model is actually good.

---

## 5. End-to-end verification flow

Five lanes, two loops. The adjudication loop resolves ambiguity; the review loop makes the detector
better.

```mermaid
flowchart TB
  subgraph L1["Operator — any station"]
    A1["Fit parts"] --> A2["Fasten · clip · mate · seat"] --> A3["Request release"]
    A9["Sees HMI: green, or red with corrective path"]
  end
  subgraph L2["Engine — C++ · 30 fps"]
    B1["Hands and tools · IP states"] --> B2{"State change<br/>committed?"}
    B3{"Confidence in<br/>ambiguous band?"}
    B4["Interpreter<br/>order · precedence · evidence · deadlines"] --> B5{"Violation?"}
  end
  subgraph L3["Sidecar — Florence-2"]
    C1["Ground the IP's state in its crop<br/>confirm or reject"]
  end
  subgraph L4["Plant interface"]
    D1["OPC-UA: Andon.Red · Conveyor.Hold"]
    D2["Event + 6 s clip → outbox → Supabase"]
  end
  subgraph L5["Process engineer — end of shift"]
    E1["Dashboard: clip · IP strip · CAD view"] --> E2{"Agree?"}
    E3["False positive → hard negative"]
    E4["Excel shift report"]
  end
  A3 --> B1
  B1 --> B3
  B3 -- "yes" --> C1
  C1 -. "adjudication loop" .-> B2
  B3 -- "no" --> B2
  B2 -- "yes" --> B4
  B5 -- "yes" --> D1
  B5 -- "yes" --> D2
  B5 -- "no" --> A9
  D1 --> A9
  D2 --> E1
  E2 -- "no" --> E3
  E3 -. "review loop" .-> B1
  E2 -- "yes" --> E4
  classDef refer fill:#FBECEB,stroke:#B3261E;
  classDef routine fill:#EAF4EE,stroke:#1B7F4C;
  classDef retake fill:#FBF3E6,stroke:#B26B00;
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class D1 refer;
  class C1,E3 retake;
  class B4 key;
```

*Figure 2 — End-to-end flow. The amber paths are the two loops most implementations omit. The
adjudication loop is asynchronous: the interpreter never waits for it, and an inspection point in
the ambiguous band is held in its previous committed state until either the detector or Florence-2 resolves it.*

### Decision logic for one inspection-point event

```mermaid
flowchart TD
  S(["Frame arrives"]) --> D1{"IP state differs<br/>from committed state?"}
  D1 -- "no" --> N(["No event"])
  D1 -- "yes" --> D2{"Seen for 2 consecutive frames?"}
  D2 -- "no" --> N
  D2 -- "yes" --> D3{"Detector confidence ≥ 0.65?"}
  D3 -- "no, 0.35–0.65" --> AJ["Hold previous state<br/>enqueue Florence adjudication"] --> N
  D3 -- "yes" --> CM["Commit transition"]
  CM --> D4{"Reaches or leaves<br/>a step's state?"}
  D4 -- "no" --> OK(["Update state, no alert"])
  D4 -- "yes" --> D5{"Order, precedence<br/>and evidence hold?"}
  D5 -- "yes" --> ADV(["Advance the step"])
  D5 -- "no" --> V["SEQUENCE · PRECEDENCE<br/>EVIDENCE_MISSING · REGRESSION"] --> AL(["Alert + event"])
  classDef refer fill:#FBECEB,stroke:#B3261E;
  classDef routine fill:#EAF4EE,stroke:#1B7F4C;
  classDef retake fill:#FBF3E6,stroke:#B26B00;
  class V,AL refer;
  class ADV routine;
  class AJ retake;
```

*Figure 2b — Per-inspection-point decision logic. Two choices are visible. A state change is committed only after
two consecutive frames (debounce), which stops single-frame detector flicker from firing alerts. And an
ambiguous detection never commits — it holds the last known state, so ambiguity can delay an alert by
one adjudication but can never cause a false one.*

Deadlines run separately: each step has a `max_duration_s`, and a step that exceeds it raises
`STEP_TIMEOUT` whatever else is happening. The release gate is the last deadline: when the PLC asks
to let the part go, anything incomplete raises `INCOMPLETE_AT_RELEASE` and the part is held. That is
how a part that is never fitted is caught — absence produces no event, so it must be caught by time
(Section 6).

---

## 6. The procedure model — any assembly, as data

The engine knows nothing about wheels. Wheel fitment is one **profile**: a data file the engine
loads. A door-trim station, a windshield cell and an electronics bench are other profiles, run by the
same binary with no code changed. This section defines the vocabulary every profile is written in.

The problem statement itself demands this. It names three different checks — *"Is the wiring harness
clip for the door panel fastened? Has the torque wrench been applied to all four wheel bolts? Is the
weather-sealing gasket seated correctly before the windshield is installed?"* — and a system that
hard-codes one of them has answered a third of the question.

### Five ideas cover every assembly step

| Idea | What it is | Examples |
|---|---|---|
| **Inspection point (IP)** | A place on the assembly where a verifiable change happens. Position and surface normal come from CAD; it belongs to a state family; its distinguishing feature has a size | a bolt, a snap clip, a connector, a gasket segment, a label, a panel |
| **State family** | An *ordered* list of states. Reaching a later state implies the earlier ones | `threaded_fastener`: absent → seated → fastened |
| **Step** | A constraint over IPs: `complete` (all of them, any order) or `sequence` (an exact order) | "all six clips closed"; "bolts in star order" |
| **Precedence** | `requires`: a step may not make progress until other steps are complete | "gasket seated before the glass goes in" |
| **Deadline and release gate** | Every step has `max_duration_s`; the cycle ends at the PLC's release request, when anything incomplete holds the part | "all bolts torqued before the vehicle moves on" |

Plus one modifier: **evidence**. A step may demand that a visual transition be corroborated by a
plant signal within a time window — the nutrunner's OK, a connector's lock switch (Section 10).

### State families — the library

Each family is learned once and reused by every profile that contains that kind of part.

| Family | States, in order | Distinguishing feature |
|---|---|---|
| `threaded_fastener` | absent → seated → fastened | head height and the witness mark on the head face |
| `snap_clip` | open → closed | the latch tab's position |
| `connector` | unmated → mated | gap between housings; lock lever position |
| `seal_segment` | unseated → seated | lip contact along the flange |
| `panel` | absent → placed → installed | the panel edge against its datum |
| `label` | absent → applied | the label's outline in its target box |
| `presence` | absent → present | the part's silhouette |

**Why order matters — found by testing, not by design.** The first version of the reference
interpreter treated the families as unordered sets. Its first run flagged every bolt that went from
`seated` to `fastened` as a *regression* of the hand-start step. With ordered families, reaching a
later state satisfies earlier steps, and only a move to a *lower* state is a regression — a loosened
bolt, a clip that sprang open.

### Six ways a step can fail

| Violation | Raised when | Example |
|---|---|---|
| `SEQUENCE` | a target reaches the state out of the listed order | bolt 2 torqued when bolt 3 was due |
| `PRECEDENCE` | a step makes progress before a step it requires is complete | trim panel installed over an open clip |
| `EVIDENCE_MISSING` | a transition is not corroborated by its plant signal in time | connector looks mated; lock switch never closed |
| `REGRESSION` | a completed target falls back to a lower state | a torqued bolt loosened: the whole pattern must be redone |
| `STEP_TIMEOUT` | an active step exceeds its deadline | five bolts started, one never torqued |
| `INCOMPLETE_AT_RELEASE` | the PLC asks to release the part with a required step unfinished | the label was never applied |

The last two are how **omissions** are caught. A missing step produces no event — there is nothing to
see — so it can only be caught at a deadline. That is why every required step must have one (I15):
either its own `max_duration_s`, or at the latest the release gate. Wrong *order* is caught at the
instant it happens; a wrong *absence* is caught when time runs out. The problem statement's
"before the vehicle moves to the next station" is literally the release gate.

### A profile, written out

The wheel profile's manifest — the human-authored half — is ten lines of intent:

```yaml
# samples/wheel-hub-5lug/profile.yaml
profile: wheel-fitment-rl
groups:
  - id: bolt
    family: threaded_fastener
    part_number: BLT-M12-1.25-45        # joins this group to its STEP occurrences
    feature_mm: 17                      # M12 head face, which carries the witness mark
steps:
  - { id: hand_start, group: bolt, kind: complete, state: seated, max_duration_s: 20 }
  - { id: torque, group: bolt, kind: sequence, order: derive, state: fastened,
      requires: [hand_start], max_duration_s: 16,
      evidence: { tag: Station.W14.Nutrunner.ResultOK, equals: true, within_ms: 1500 } }
```

The door-trim profile, for the problem statement's harness-clip question, uses the same vocabulary
for a different assembly:

```yaml
# samples/door-trim-harness/profile.yaml
profile: door-trim-harness-fl
groups:
  - { id: clip,      family: snap_clip, part_number: CLP-HRN-08,  feature_mm: 4 }
  - { id: connector, family: connector, part_number: CON-DR-12P,  feature_mm: 6 }
  - { id: panel,     family: panel,     part_number: TRM-DR-FL,   feature_mm: 60 }
steps:
  - { id: clips, group: clip, kind: complete, state: closed, max_duration_s: 40 }
  - { id: connector, group: connector, kind: complete, state: mated, max_duration_s: 20,
      evidence: { tag: Station.D07.ConnectorLock, equals: true, within_ms: 1000 } }
  - { id: trim_panel, group: panel, kind: complete, state: installed,
      requires: [clips, connector], max_duration_s: 30 }
```

The compiler (Section 7) joins each manifest to its STEP file, places every inspection point,
derives `order: derive` sequences from geometry, checks that a camera can resolve each point, and
writes `procedure.json`. The engine reads only `procedure.json`.

### The executable specification

The interpreter was written first as a small Python reference model, `reference/tsref/procedure.py`
— about 150 lines with no knowledge of any assembly — and tested against four profiles before a word
of this section was written:

| Profile | Source | Scenarios tested |
|---|---|---|
| `wheel-fitment-rl` | PS4 reference process | correct star; adjacent order; missing bolt (timeout and release); torqued-looking with no nutrunner OK; loosened bolt; torquing before every bolt is started |
| `door-trim-harness-fl` | PS4: "harness clip … fastened?" | correct; panel installed over an open clip; connector without its lock signal |
| `windshield-bond` | PS4: "gasket seated … before the windshield" | correct; glass placed with one segment unseated |
| `enclosure-cover` | non-automotive electronics | correct crisscross then label; label forgotten |

Forty-two tests in all — the interpreter, the solvers, optics, the camera arithmetic of Section 8 and the samples contract of Section 12 — including pattern solvers, optics and load-time rejection of a state that
is not in a part's family. All pass. Every violation kind in the table above is produced by at least
one scenario.

The C++ `ProcedureEngine` is the production implementation of the same model, and it is
**differentially tested** against the reference (I16): every scenario's event stream is fed to both,
and their violation lists must be identical. The reference is the specification; the C++ is the
fast copy. When they disagree, the reference wins until a test proves it wrong.

**Keeping it allocation-free.** A profile is compiled into flat arrays at load time: a rank table per
inspection point, one 64-bit mask per step for its targets, a fixed-size pending-evidence ring. A
committed transition touches only the steps that target that one point. Limits that follow: 256
inspection points per profile and 64 targets per step — far beyond one station's work content.
Invariant I1 holds for every profile, not just the wheel.

---

## 7. The CAD compiler and pattern solvers

Two inputs, one output. **Geometry** comes from the STEP file, because geometry is the one thing every
CAD system exports reliably. **Intent** — which parts are inspection points, which family, which
constraints — comes from the manifest, because custom attributes do not survive every CAD export and
should be reviewed like code anyway. The compiler joins them by part number.

```mermaid
flowchart LR
  STEP[/"assembly.step"/] --> R1["STEP reader<br/>OCCT XCAF"]
  MAN[/"manifest.yaml"/] --> J
  R1 --> R2["DFS over the assembly tree<br/>every occurrence · its transform"]
  R2 --> J["join by part number<br/>→ inspection points"]
  J --> P["pattern solvers<br/>circle · line · field"]
  J --> O["optics check<br/>resolution · frame · facing"]
  J --> X["occlusion ray-cast<br/>OCCT"]
  P --> PJ[("procedure.json")]
  O --> CR[("coverage.json")]
  X --> CR
  CR --> G{"every IP resolved?"}
  G -- "no" --> F["refuse to deploy<br/>name the points and the shortfall"]
  G -- "yes" --> OK(["profile ready for rendering"])
```

*Figure 3 — The compiler. It refuses as well as produces: a profile with an inspection point no camera
can resolve never reaches a station (I17).*

### Pattern solvers — the order is derived, not typed

When a manifest says `order: derive`, the compiler classifies the fasteners' layout and picks the
solver:

| Layout | Detected when | Solver | Example result |
|---|---|---|---|
| **Circle** | all points equidistant from the centroid | star: odd N strides ⌊N/2⌋; even N fastens diametric pairs | 5-bolt hub: 1, 3, 5, 2, 4 |
| **Line** | points collinear | centre-out, alternating sides | 7-screw seam: 4, 5, 3, 6, 2, 7, 1 |
| **Field** | anything else | centre-out; each next point the one most nearly opposite the last | 2×3 cover: 2, 5, 3, 4, 6, 1 |

Results on a circle:

| N | Sequence | Note |
|---|---|---|
| 4 | 1, 3, 2, 4 | opposite pairs |
| 5 | 1, 3, 5, 2, 4 | single cycle, stride 2 |
| 6 | 1, 4, 2, 5, 3, 6 | opposite pairs, rotating |
| 8 | 1, 5, 3, 7, 2, 6, 4, 8 | cross pattern |

An explicit `order: [ … ]` in the manifest overrides derivation when the OEM's work instruction
mandates something else; `procedure.json` records which it was, `derived` or `specified`.

```cpp
// cad-extract/src/StarPatternSolver.cpp
// Returns 1-based IDs in fastening order. `by_angle` is sorted by angle about the centroid,
// rotated to start at member 1, and oriented so it runs in the direction of the numbering.
std::vector<int> StarPatternSolver::solve(std::span<const Bolt> by_angle) {
  const int n = static_cast<int>(by_angle.size());
  std::vector<int> order;
  order.reserve(n);

  if (n % 2 == 1) {
    // Odd N: stride floor(N/2) is always coprime with N, so one cycle visits every bolt.
    for (int k = 0, i = 0; k < n; ++k, i = (i + n / 2) % n)
      order.push_back(by_angle[i].id);
    return order;
  }

  // Even N: fasten diametrically opposite pairs (j, j + N/2). Choose the order of the
  // pair starts by striding N/4 through the half-circle, falling back to the first
  // unvisited start when the stride cycles early (N = 8 would otherwise loop 0,2,0,2).
  const int half = n / 2;
  const int step = std::max(1, n / 4);
  std::vector<bool> seen(half, false);
  for (int s = 0, placed = 0; placed < half; ++placed) {
    if (seen[s]) s = static_cast<int>(std::find(seen.begin(), seen.end(), false) - seen.begin());
    seen[s] = true;
    order.push_back(by_angle[s].id);
    order.push_back(by_angle[s + half].id);
    s = (s + step) % half;
  }
  return order;
}
```

Each solver is tested against properties, not hard-coded answers: every fastener appears exactly
once; on a circle each consecutive pair obeys the spacing rule; on a line the distance from the
centre never decreases and sides alternate; every solver returns a permutation on 200 random
layouts. Three bugs were caught this way across revisions: the star solver looped forever on N = 8;
it started at whichever bolt happened to sit nearest the x-axis, giving 2, 5, 3, 1, 4 — a valid star,
but not the documented one; and the field solver paired corners across the short axis instead of
the diagonal until "most nearly opposite" replaced "on the other side".

### Can a camera see it? — the resolution check

For every inspection point and every camera, the compiler computes the projected size of the point's
distinguishing feature with a pinhole model:

$$f_{px} = \frac{W/2}{\tan(\mathrm{HFOV}/2)}, \qquad \text{feature}_{px} = f_{px}\cdot\frac{\text{feature}_{mm}}{d_{mm}}\cdot\cos\theta$$

where $W$ is the width of the frame the crop is taken from, $d$ the distance and $\theta$ the angle
between the feature's normal and the line of sight. A point counts as resolved only if it is inside
the frame, faces the camera (θ < 70°) and spans at least **16 px** — a starting threshold that each
profile's real-image validation confirms or raises (Section 11).

These figures are from the compiler's reference implementation, run on the four profiles:

| Rig | Smallest feature | Unresolved |
|---|---|---|
| Wheel, crops from the 960 px downscale | 12.8 px | 5 of 5 |
| **Wheel, crops from the 1,920 px frame** | **25.6 px** | **0** |
| Door, one camera at 1.2 m | 5.8 px | 7 of 8 |
| Door, three cameras at 500 mm | 15.1 px | 4 of 8 |
| **Door, three cameras at 420 mm** | **17.4 px** | **0** |
| **Windshield, four cameras at 800 mm** | **16.6 px** | **0** |
| Enclosure, one camera at 400 mm | 15.9 px | 4 of 8 |
| **Enclosure, one camera at 350 mm** | **17.8 px** | **0** |

The first row is a correction to v4.1, which processed every frame at 960×540. At that resolution the
wheel's bolt-head face spans under 13 px: too few to trust a state judgement. The detector still runs
on the downscale; inspection-point crops now come from a small pool of full-resolution frames
(Section 9). Nothing in v4.1 checked this. The compiler now checks it for every point of every profile.

**Occlusion** is a separate test, run on the CAD itself: a ray from each camera's centre to each
inspection point, intersected with the assembly's other bodies using OCCT's
`IntCurvesFace_ShapeIntersector`. If the first surface hit is not the point's own part, that camera
cannot see it. The reference model does not include this test, and it has not been run yet; it is the
C++ compiler's job.

The compiler writes `coverage.json` — every point, its best camera, its feature size, its occlusion
result — and refuses to emit a deployable profile if any point is unresolved (I17).

---

## 8. Camera capture — laptop webcam and phone

The prototype runs on the cameras you already own: the laptop's built-in webcam and your phone.
Everything downstream of the capture thread is the same whatever the camera, because frames enter
through one interface, `IFrameSource`. The industrial GigE camera, its lens and its network are
designed and parked on purpose for later (Appendix D.1).

| Source | Used for | Library | `kind:` in the station config |
|---|---|---|---|
| `FileSource` | rendered scenarios; recordings in `samples/*/media/`; the demo fallback | OpenCV, FFmpeg backend | `file` |
| `UvcSource` | the laptop's built-in webcam, or any USB webcam | OpenCV, DirectShow backend | `uvc` |
| `StreamSource` | the phone's camera over Wi-Fi, as an MJPEG stream | OpenCV, FFmpeg backend | `stream` |

A fourth, `ReplayDetector`, skips frames altogether and feeds recorded detections. The sanitizer
builds and CI use it, and the sample traces (Section 12) go one step further and skip perception
too. Nothing in the test suite needs a camera.

**Which camera does what.** The phone goes on a stand looking straight down at the desk. It has the
better sensor, and most assemblies are worked from above. The laptop webcam looks straight ahead,
so it takes assemblies that stand upright and face you. A station config can name both. The
compiler then gives each inspection point to whichever camera resolves it best, exactly as it does
for the wheel's fixed cameras.

### Will this camera see it? — the same 16 px rule

A fixed lens has no focal length to choose. What you can choose is the resolution the source asks
for and how far away the camera stands, so the arithmetic in `reference/tsref/capture.py` runs the
other way round:

| Camera, resolution requested | Distance | Smallest feature | Pixels across it | |
|---|---|---|---|---|
| Laptop webcam, 1280 × 720 | 400 mm | 8 mm clip flap | 18.3 | ✅ |
| The same webcam, left at OpenCV's default 640 × 480 | 400 mm | 8 mm clip flap | 9.1 | ❌ |
| Phone, 1920 × 1080 | 350 mm | 7 mm screw head | 27.9 | ✅ |
| Phone, 1280 × 720 (if Wi-Fi forces it down) | 350 mm | 7 mm screw head | 18.6 | ✅ |

The webcam must stand within **457 mm** of an 8 mm feature, and the phone within **611 mm** of a
7 mm one. The fields of view used here (about 70° for the webcam, 69° for the phone) are
placeholders. Calibration replaces them with the measured focal length, and the compiler re-runs.
The second row is the important one: a webcam opened without asking for a size quietly delivers
640 × 480. That halves every pixel count, and nothing warns you. So `UvcSource` asks for the size
and then **reads it back** (below).

The compiler also accounts for viewing angle. The outermost clips of the desk harness sample sit
126 mm off-axis, so they measure 16.6 px against 18.3 px in the centre. That is just over the
limit, and the sample's own notes say to move the camera closer, not farther.

### Laptop webcam — `UvcSource`

```cpp
// control-app/src/sources/UvcSource.cpp
cv::VideoCapture cap(cfg.device, cv::CAP_DSHOW);                 // DirectShow: works in a MinGW build
cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));  // before the size: raw YUY2
cap.set(cv::CAP_PROP_FRAME_WIDTH, cfg.width);                     // at 720p rarely reaches 30 fps
cap.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.height);
cap.set(cv::CAP_PROP_FPS, cfg.fps);
cap.set(cv::CAP_PROP_AUTOFOCUS, 0);
if (cfg.exposure) cap.set(cv::CAP_PROP_EXPOSURE, *cfg.exposure);  // DirectShow units: log2 seconds, −7 ≈ 8 ms

const Readback got{cap.get(cv::CAP_PROP_FRAME_WIDTH), cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                   cap.get(cv::CAP_PROP_FPS), cap.get(cv::CAP_PROP_FOURCC)};
if (!got.matches(cfg))                       // e.g. 640x480 YUY2: refuse to verify, say why
  return SourceStatus::settings_mismatch(got.describe());
```

Three things trip people up on Windows, so they are part of commissioning:

1. **Camera privacy.** *Settings → Privacy & security → Camera → Let desktop apps access your
   camera* must be **on**. When it is off, the camera opens and every read fails.
2. **Which device number.** `ffmpeg -hide_banner -list_devices true -f dshow -i dummy` lists the
   cameras in the order DirectShow numbers them. `ffmpeg` is already in your MSYS2 install, because
   OpenCV depends on it. The built-in webcam is usually `0`.
3. **Settings the driver ignores.** Many laptop webcams ignore manual exposure. The read-back
   records what the camera actually did. `cap.set(cv::CAP_PROP_SETTINGS, 1)` opens the driver's
   own settings dialog, which is the quickest way to see what the hardware allows.

### Phone — `StreamSource` over the phone's own hotspot

The phone runs the **IP Webcam** app (Android) and serves its camera as an MJPEG stream. Connect the
laptop to the **phone's hotspot**, with mobile data off. That gives a private link with no router
and no internet, so the demo stays offline and nobody else's network is involved.

| Setting in the app | Value |
|---|---|
| Video resolution | 1920 × 1080 |
| Orientation | landscape |
| Focus | locked once the assembly is in frame |
| Exposure and white balance | locked once lit |
| Power | phone on charge — streaming drains it |

The app shows its address, for example `http://192.168.43.1:8080`. Recent Android versions pick
the hotspot's subnet themselves, so copy the address the app actually shows. The stream is that
address followed by `/video`:

```yaml
# config/station_desk1.yaml
sources:
  phone: { kind: stream, url: "http://192.168.43.1:8080/video", width: 1920, height: 1080, fps: 30 }
```

```cpp
// control-app/src/sources/StreamSource.cpp — set once, before the first open
_putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "fflags;nobuffer|flags;low_delay");   // no buffering: newest frame
cv::VideoCapture cap(cfg.url, cv::CAP_FFMPEG);
```

**Latency belongs to the camera now.** The engine's clock starts when a frame arrives, so Wi-Fi and
JPEG time before that are invisible to `latency_test`. The one-second contract runs from the event
to the alert. So measure the camera's share once per camera:

```powershell
torquesense --measure-glass --source phone
```

That shows a millisecond counter full-screen, and you point the phone at it. The engine compares
the counter value it reads in each frame with the time that frame arrived. Record the median in
`cameras.yaml`. The alert budget for the station is one second minus that figure minus the
engine's ≈ 70 ms.

**If the phone is an iPhone.** IP Webcam is Android-only. `StreamSource` takes any URL FFmpeg can
open, so any iPhone app that serves MJPEG or RTSP on the local network does the same job.

### Settings are configuration, not defaults

Resolution, frame rate, pixel format, focus and exposure are fixed at commissioning and written
to the profile's `cameras.yaml`. On every start the source reads them back from the camera, and a
mismatch refuses to verify: the station shows AMBER and says why. A camera that silently fell back
to 640 × 480, or a phone that refocused on the operator's hand, is the kind of fault that otherwise
looks like a model problem for a week.

### Time, failure, recovery

- **Timestamps.** Every frame is stamped with the host's monotonic clock when the grab completes.
  `/health` reports each source's real frame rate and inter-frame jitter. A phone on a congested
  hotspot shows up there first.
- **Two cameras.** They are not synchronised, and they do not need to be. Steps are seconds apart,
  and one frame period of skew cannot reorder them.
- **Failure.** A failed read marks the source lost and stops it publishing. The verify thread's
  frame counter stops, so the heartbeat stops, so the PLC turns the station AMBER within 2 s.
  There is no camera-specific alarm path (Section 19). A reconnect loop retries every 2 s and
  re-runs the settings check before the first frame is trusted. Pulling the phone's Wi-Fi in
  rehearsal is a good test.

### Calibration and registration — printed, not bought

`samples/_print/` holds three PDFs drawn to exact scale. Print at **100 %** ("Actual size", never
"Fit to page") and check the 50 mm bar with a ruler.

| Sheet | Used for |
|---|---|
| `charuco_7x5_30mm_a4.pdf` | **Intrinsics.** Glue it flat to card. `python scripts\calibrate.py --camera phone` takes 20 poses and writes `fx, fy, cx, cy` and distortion into `cameras.yaml`. A reprojection error above 0.5 px RMS sends you back for more poses. |
| `placement_sheet_desk_cover.pdf` | **Registration**, desk sample 1: an ArUco marker 90 mm from a crosshair. Put the box on the crosshair, and every inspection point's position is known from then on. |
| `marker_desk_harness.pdf` | **Registration**, desk sample 2: a marker to stick on the clip board. |

The registration markers come from `DICT_4X4_50` and the ChArUco board from `DICT_5X5_100`, so the
calibration board can never be mistaken for a registration marker. The marker's pose comes from
`solvePnP` every frame. The sheets were checked by rendering them at 300 dpi and detecting them
back: the marker measured 39.9 mm and sat exactly 90 mm below the crosshair, and all 24 ChArUco
corners were found at a 30.07 mm pitch.

### Commissioning a desk station

| Step | Done when |
|---|---|
| Camera privacy allows desktop apps | the webcam opens and frames arrive |
| Device found | `ffmpeg … -list_devices` lists the webcam; the phone's URL plays in a browser |
| Resolution read back | the engine logs `webcam 1280x720@30 MJPG` and `phone 1920x1080@30`, not 640 × 480 |
| Intrinsics calibrated | reprojection error ≤ 0.5 px RMS |
| Marker in view | anchor residual stable for two minutes |
| Glass-to-arrival measured | median recorded in `cameras.yaml` |
| The sample passes with the calibrated numbers | `python samples\run_samples.py <sample>` says PASS |

---

## 9. Perception for any part

Two stages, split by what generalises.

| Stage | Question | Model | Input | Trained |
|---|---|---|---|---|
| **Scene** | Where are the hands and tools? | YOLO11n, ONNX Runtime CUDA EP | 960×540 downscale | once, shared by every profile |
| **Inspection point** | Which of its two or three states is this point in? | one small classifier per state family (MobileNetV3-Small, 96×96 crop) | crops at CAD-projected positions, from the full-resolution frame | once per family, reused by every profile with that family |

The inspection-point stage is what makes the system universal. At an inspection point the question
is never *"what is this object?"* — an open problem — but *"which of these two or three known states
is it in?"* That is a closed, small classification problem, and it can be learned from renders of
one part family and reused wherever that family appears. A snap-clip classifier trained for the door
also serves a dashboard harness, a seat-wiring loom and a battery-tray cover.

All the crops visible in a frame are classified in one batch. Committing, debouncing and adjudication
are unchanged from the wheel design (Figure 2b): two consecutive frames, a 0.65 confidence floor, and
an ambiguous band that holds the last state and asks Florence-2.

### Where are the inspection points in the image? — registration

| Station | Registration | How |
|---|---|---|
| Fixed fixture, assembly always in the same place | fixed | camera extrinsics from calibration, once |
| Moving carrier, pallet or conveyor | fiducials | AprilTag markers on the fixture, located every frame with OpenCV's ArUco module, pose by `solvePnP` |

Either way, every inspection point's pixel position is the CAD position projected through the current
pose. The mean distance between where a part is detected and where it should be — the anchor residual
— is the fleet-health signal that catches a knocked camera or a mis-seated fixture (Section 19).

### Training data for a new family

The same pipeline as the wheel: the part's CAD, rendered by BlenderProc in every state of its family
with domain randomisation, labels written automatically. A new profile built only from known families
needs **no new training at all** — only its own scenario fixtures for validation. A new family costs
one render run and one classifier training run on Colab.

### Cold start: Florence-2, advisory only

Before a new family's classifier exists, Florence-2 can ground a plain description — *"closed plastic
clip"* — in each crop, zero-shot. That is useful for trying a profile out and for seeding labels. It
is never allowed to raise an alert: a zero-shot model has not passed a validation gate, so in cold
start the station shows its verdicts as advisory and the PLC is not written to. The same rule as
everywhere else in this design: anything that has not been validated may inform, never decide.

### Memory: a full-resolution pool for crops

The detector's frame pool stays at 12 frames of 960×540 (18.7 MB). Crops need the full-resolution
frame for one frame period, so a second pool holds 4 full-resolution frames (4 × 6.2 MB = 25 MB),
leased and returned exactly like the first. Both pools are allocated at start-up; I1 holds. On a
production camera the second pool would be the camera driver's own buffer set — eight raw Bayer
frames written in place by the driver (Appendix D.1).

### What this stage cannot do

It classifies states at points. It does not trace a cable's route between clips, judge cosmetic
damage, or measure a gap to tenths of a millimetre. Route verification needs segmentation along a
path, and metrology needs calibrated optics. Both are different problems, and this design says so
rather than stretch.

---

## 10. Evidence beyond the camera

Some facts no camera can establish. A universal system has to accept evidence from the plant as well
as from pixels.

| Fact | Why vision cannot establish it | Evidence source |
|---|---|---|
| A fastener reached its torque | clamp load is invisible | nutrunner controller: per-rundown OK, torque and angle over OPC-UA |
| A connector's latch engaged inside its housing | the latch is hidden | connector-position-assurance switch, or a continuity test |
| The right part variant was fitted | many variants look alike | barcode or RFID scan, compared with the vehicle's bill of materials |
| A press-fit reached its force | force is invisible | press controller's force-window result |
| A fluid was filled | volume is hidden | fill machine's result |

A profile uses this in two ways.

**Corroboration.** A visual transition must be matched by a plant signal within a time window: the
wheel bolt looks torqued *and* the nutrunner reported OK within 1.5 s. Either alone is weaker. A
witness mark can be painted on a bolt that was never torqued; a nutrunner OK can belong to a different
bolt. Together, one says *which* bolt and the other says *it reached torque*. The reference tests
include both failures: five bolts that look torqued with no nutrunner result raise five
`EVIDENCE_MISSING`; a connector that looks mated with no lock signal raises one.

**Signal-only steps.** An inspection point can be of family `signal`, with no camera at all: its state
comes from a tag mapping — `press.ForceOK = true → done`. The interpreter does not care where a state
came from, so precedence, deadlines and the release gate apply to it exactly as to a visual point.
That is how a station whose critical step is invisible still gets omission detection.

Plant signals reach the interpreter as ordinary events. The alert thread's OPC-UA client subscribes
to the profile's evidence and release tags; each change is timestamped on receipt and handed to the
verify thread over its own SPSC ring. The verify thread still never touches the network.

### Mixed-model lines — the variant decides the profile

Tata lines build several variants on one line. At cycle start the engine reads the variant code the
PLC publishes for the arriving vehicle and selects that variant's compiled procedure. Every variant
for a station is compiled and loaded at start-up, so switching is an index change, not a file load:
no allocation, no latency. A variant code with no compiled profile holds the station AMBER instead of
guessing.

---

## 11. Onboarding a new assembly

A new station is a new profile directory, not a new program.

```mermaid
flowchart LR
  A["1 · Author manifest<br/>groups · steps · evidence"] --> B["2 · Compile<br/>orders · coverage"]
  B --> C{"coverage clean?"}
  C -- "no" --> R["move or add cameras<br/>change lens or distance"] --> B
  C -- "yes" --> D["3 · Render<br/>scenario fixtures · new families only"]
  D --> E["4 · Train<br/>only families not yet in the library"]
  E --> F["5 · Validate<br/>scenarios · real photos · gate"]
  F --> G{"gate passed?"}
  G -- "no" --> D
  G -- "yes" --> H["6 · Canary<br/>one station, one shift, vs peers"]
  H --> I(["Live"])
  classDef gate fill:#FBF3E6,stroke:#B26B00;
  class C,G gate;
```

*Figure 3b — Onboarding. Two gates, both before anything reaches the line: one on geometry, one on
evidence.*

### What it costs — estimates, to be replaced by measurements

| Work | Known families only | With one new family |
|---|---|---|
| Manifest: groups, steps, evidence tags | 2–4 h | 2–4 h |
| Compile and fix coverage | under 1 h, plus any camera moves | the same |
| Render scenario fixtures | about 30 min of machine time | plus the family's training renders |
| Train | nothing | one Colab run, 2–3 h |
| Real validation set: photograph and label | about 2 h | about 3 h |
| Gate and canary | one shift | one shift |
| **Engineer time** | **about one day** | **two to three days** |

These are planning estimates. The first real onboarding should time every row and replace them.

### The profile directory

```
samples/door-trim-harness/
├── profile.yaml         the manifest — authored and reviewed in a pull request
├── assembly.step        from engineering; its revision is the profile's CAD revision
│                        (none for `geometry: measured` profiles: positions come from a ruler)
├── cameras.yaml         intrinsics, poses, registration method
├── traces/              correct.yaml + one per fault, each with its expected violations
├── media/               renders or recordings named after the traces (DVC, not git)
├── procedure.json       compiled — never edited by hand
├── coverage.json        compiled — resolution, framing, occlusion for every point
├── narratives.yaml      one sentence template per violation kind and step
├── playbook.yaml        fleet-health patterns specific to this station, if any
└── VALIDATION.md        gate results, signed off before going live
```

The whole directory is versioned as one unit; its hash is the `profile_version` stamped on every
anomaly row and health bucket, next to `cad_revision` and `model_version`.

### The gate (I14)

A profile goes live only when:

1. `coverage.json` has no unresolved inspection point;
2. every scenario fixture produces exactly its expected verdict, in both the reference interpreter
   and the C++ engine;
3. each family's per-state recall on that station's real photographs is reported with a confidence
   interval, and the recall on the *bad* states — open clip, unseated gasket — meets the plant's
   threshold;
4. a canary shift on one station shows no more false alarms than its peers.

### Is a station a candidate at all? — the applicability check

Before promising a station, answer five questions. Any "no" means vision verification is the wrong
tool for that step, not that the step cannot be verified.

| Question | If "no" |
|---|---|
| Does every step to be verified change something visible at a point, or produce a plant signal? | a signal-only step, or out of scope |
| Is each visible change at least 16 px at a camera position you can actually mount? | different lens, closer camera, a second camera |
| Is each point unoccluded by the operator for at least part of its step? | a second viewpoint, or corroborating evidence |
| Does the cycle have a release signal the engine can read? | add one to the PLC; without it omissions are caught only by timeouts |
| Can the assembly's pose be known — a fixed fixture or fiducials? | add fiducials to the fixture |

Out of scope, whatever the answers: cable routing between clips, cosmetic inspection, dimensional
metrology, and anything whose only evidence is inside a closed part with no sensor.

---

## 12. Sample assemblies — the conformance suite

"Universal" is a claim, and `samples/` is how it is tested. Every folder in it is an assembly the
engine verifies with no assembly-specific code. Each folder holds three things: what to look at,
in what order, and what a right and a wrong cycle look like. One generic runner checks every folder
against the same contract. If a new assembly can be written as a folder that passes, the engine
accepts it without a code change. That is the standard every future assembly is held to, and the
one a judge or reviewer can check for themselves in a few seconds.

| Folder | Assembly | Camera | Geometry from | What it proves |
|---|---|---|---|---|
| `wheel-hub-5lug` | tyre fitting — the PS4 reference | rendered video | CAD | star order derived from a bolt circle; nutrunner evidence; all six violation kinds in one profile |
| `door-trim-harness` | clips, connector, trim panel — PS4 example | traces only | CAD | precedence: a panel cannot hide an open clip; a connector lock signal |
| `windshield-gasket` | gasket before glass — PS4 example | traces only | CAD | a seal handled as eight segments |
| `electronics-enclosure` | connector, six screws, label — not automotive | traces only | CAD | field order, centre-out; an omission only the release gate can see |
| `desk-cover-4screw` | **any box with a four-screw lid** | **phone**, top-down | **a ruler** | runs at a desk; diagonal order derived from four measured points |
| `desk-harness-clips` | **cable, five clips, plug, cover** | **laptop webcam** | **a ruler** | the door problem at desk scale, on the camera you already have |
| `_template` | — | — | — | the starting point for the next one |

The two desk samples matter most for the prototype. They need no CAD, no render farm and no plant.
Their geometry is measured with a ruler from a printed crosshair, so anyone can build one in an
afternoon. That makes "any assembly" something a judge can see working on their own table.

### A sample folder

```
samples/desk-cover-4screw/
  profile.yaml            cameras, inspection points, steps — the manifest of Section 6
  traces/
    correct.yaml          the right cycle: must release cleanly with no violation
    round_the_box.yaml    one wrong cycle per file, and exactly what it must raise
    screw_left_out.yaml
    screw_backed_out.yaml
  media/                  recordings (or renders) named after the traces; not committed
```

A **trace** is the sequence of committed states the perception stage would produce, with times. It
tests the procedure without a camera, in milliseconds, on any machine:

```yaml
# Tightened clockwise round the box, 1-2-3-4, instead of across the diagonals.
trace: round_the_box
events:                # t: ms from cycle start · ip+state: a committed state · tag+value: a plant signal
  - {t: 2000, ip: lid-1, state: placed}
  - {t: 4000, ip: screw-1, state: seated}
  # … screws 2–4 seated …
  - {t: 10000, ip: screw-1, state: fastened}
  - {t: 13000, ip: screw-2, state: fastened}
  - {t: 16000, ip: screw-3, state: fastened}
  - {t: 19000, ip: screw-4, state: fastened}
  - {t: 25000, ip: label-1, state: applied}
release_at: 30000
expect:
  release_ok: false
  violations:
    - {kind: SEQUENCE, step: screws, observed: screw-2, expected: screw-3}
    - {kind: SEQUENCE, step: screws, observed: screw-3, expected: screw-2}
```

The `expect:` block is the single statement of what should happen, and it is used three times:

1. by `samples/run_samples.py`, against the Python reference interpreter;
2. by the C++ engine, `torquesense --sample samples\desk-cover-4screw --replay-trace traces\round_the_box.yaml`,
   which makes the samples the corpus for the differential test (I16);
3. by the C++ engine on camera, `torquesense --sample … --source media\round_the_box.mp4`. A
   recording must raise the same violations as its trace.

### The contract

| | Rule | Why |
|---|---|---|
| C1 | the profile compiles, and every inspection point is at least 16 px on some camera | a point the camera cannot resolve cannot be verified (I17) |
| C2 | every step has `max_duration_s` | without a deadline a skipped step is never caught (I15) |
| C3 | `traces/correct.yaml` exists, releases cleanly and raises nothing | a profile that fails the right cycle is wrong |
| C4 | at least one fault trace, and every fault trace expects the release to be refused | a profile nobody has tried to break is untested |
| C5 | every trace raises exactly the violations it expects, in order | the profile means what its author thinks |

The runner knows no assembly by name. Its output today:

```
> python samples\run_samples.py
sample                    IPs  min px   traces  derived order
desk-cover-4screw           6    27.2    4/4    screws: circle 1,3,2,4  PASS
desk-harness-clips          7    16.6    3/3    —  PASS
door-trim-harness           8    17.4    3/3    —  PASS
electronics-enclosure       8    17.8    2/2    cover_screws: field 2,5,3,4,6,1  PASS
wheel-hub-5lug              5    25.6    6/6    torque: circle 1,3,5,2,4  PASS
windshield-gasket           9    16.6    2/2    —  PASS

violation kinds exercised: 6/6
```

Twenty fault and correct traces across six assemblies raise every one of the six violation kinds.
The contract also has its own tests. Each one breaks a copy of a sample on purpose and checks that
the runner refuses it under the right rule:

- a 2 mm screw head (C1);
- the webcam moved back to 600 mm (C1);
- a step with no deadline (C2);
- a missing `correct` trace (C3);
- a wrong expectation (C5).

Those tests, the template and every sample run in the reference suite — 42 tests, all passing.

### The desk samples — what to buy and build

| Item | For | Notes |
|---|---|---|
| A small box with a four-screw lid | `desk-cover-4screw` | a 100 × 100 mm PVC electrical junction box from any hardware shop is ideal |
| A small screwdriver and a sheet of labels | the same | the label is the last step |
| Five adhesive cable clips, a USB cable, a USB socket or hub | `desk-harness-clips` | clips and socket stuck to a stiff board about 350 × 200 mm |
| A piece of card | the harness cover | |
| A phone stand, or a stack of books | the phone, top-down at 350 mm | |
| The three PDFs in `samples/_print/` | registration and calibration | print at 100 % |

The desk cover's order is not typed anywhere. Four points measured at (±40, ±40) mm are equidistant
from their centroid, so the compiler classifies them as a circle and solves for **1, 3, 2, 4**:
across the diagonals, which is how any work instruction tightens a square lid.

### Adding your own assembly — no code

1. Copy `samples/_template` to `samples/<your-assembly>/`.
2. Print a placement sheet or marker from `_print/`. Put the assembly on it, or stick the marker
   beside it.
3. Measure each inspection point from the crosshair in millimetres, +x right and +y away from the
   marker. Number fasteners the way a work instruction would.
4. Give each group a state family and the size of the smallest thing that tells its states apart.
5. Write the steps. Leave fastener order as `order: derive`; the runner prints the order it chose,
   so check it against your work instruction.
6. Write `traces/correct.yaml` and one trace per mistake you care about.
7. Run `python samples\run_samples.py <your-assembly>` until it says PASS. A C1 failure names the
   point and its pixel size: move the camera closer, ask for a higher resolution, or add a camera.
8. Record `media/correct.mp4` and the faults, and check them through the engine.

### What the traces do not prove — teaching a family on real crops

A trace proves the *procedure*. Whether the camera reads the states correctly is the perception
stage's job, and that is per family (Section 9).

- **Rendered parts.** The wheel's classifiers are trained on renders of the wheel's own CAD.
- **Desk parts.** A desk box's Phillips screws are not wheel bolts, and the desk samples have no
  CAD to render.
- **Teach mode.** `scripts/teach.py --sample desk-cover-4screw --camera phone` shows the live view
  with every inspection point boxed, projected from the marker. You put the points into each state
  and press that state's key, and it saves 96 × 96 crops, about 150 per state from varied hands and
  light. It then fine-tunes that family's MobileNetV3-Small on the laptop's own GPU, in minutes
  rather than a Colab session. The new classifier passes the same validation gate as every other
  model, and recordings in `media/` are its test set.
- **Before a family is taught.** Florence-2 can run the station in advisory mode only. It never
  writes to the PLC.

**Status.** Teach mode is designed, not built. Everything else in this section is built and tested.

---

## 13. Latency and capacity model

### Define the clock before quoting the number

Two latencies, both measured with HdrHistogram over 100 runs, reported at p50/p99/p99.9:

| Latency | From | To | What it means |
|---|---|---|---|
| **Engine latency** | frame in which the transition **commits** | OPC-UA write acknowledged | how fast the software reacts |
| **Evidence latency** | **first** frame showing the torqued bolt | OPC-UA write acknowledged | what the operator experiences |

Evidence latency includes the debounce frame. Quote both, and say which is which. A judge who has
measured anything will ask.

### Budget at 30 fps, single station

| Stage | Expected |
|---|---|
| Frame source (decode from file) | 2 ms |
| Letterbox + normalise to 640² | 2 ms |
| YOLO11n, ONNX Runtime CUDA EP, FP32 | 14 ms |
| *or* YOLO11n, TensorRT EP, FP16 — only if the gate in Section 30 adopts it | measured, not estimated |
| NMS + ByteTrack + CAD identity + state vector | 3 ms |
| IP crops from the full-resolution frame, family classifiers batched | ≈ 4 ms, estimate |
| Interpreter evaluate | < 0.1 ms |
| SPSC hop to alert thread | < 0.1 ms |
| OPC-UA write to localhost soft PLC | 3 ms |
| **Engine latency, p99** | **≈ 35 ms** |
| Debounce (one additional frame) | 33 ms |
| **Evidence latency, p99** | **≈ 70 ms** |

The crop stage is the one stage that grows with the number of inspection points in view, and its
figure is an estimate until `latency_test` measures it.

**Invariant I4:** evidence latency p99 ≤ 150 ms at 30 fps single-station. Measured with GPU clocks
locked, on AC power. Against the problem statement's one second, that is a fourteen-fold margin.
Against a real PLC, add roughly 15 ms for the fieldbus — say so.

### VRAM — single station, demo mode

The Intel UHD drives the display, so the RTX 3060's 6 GB is almost entirely available for compute.

```
RTX 3060 Laptop — 6 GB
  torquesense.exe  CUDA context                 ~0.35 GB
                   ORT CUDA arena (capped)       1.00 GB
  sidecar.py       CUDA context + torch          ~0.45 GB
                   Florence-2-base fp16 weights  ~0.46 GB
                   activations, capped           ~0.60 GB
  ───────────────────────────────────────────────
  Total                                          ~2.9 GB  ✓  3 GB headroom
```

Under the TensorRT provider, add the engine and its activations, which the ORT arena cap does not
bound. YOLO11n's FP16 engine is small, but this figure is measured before I7 is claimed (Section 30).

### Four-station mode — honest figures

Four `torquesense.exe` processes, one shared sidecar. Each process carries its own CUDA context.

| | Per station | ×4 |
|---|---|---|
| CUDA context | 0.35 GB | 1.40 GB |
| ORT arena, capped | 0.40 GB | 1.60 GB |
| Sidecar (shared) | — | 1.50 GB |
| **Total** | | **~4.5 GB / 6 GB** ✓ |

**Frame rate drops to 15 fps per station.** Four processes time-slice one GPU; 60 inferences per
second aggregate is comfortable, 120 is not. Fastening a bolt takes seconds, so 15 fps loses nothing
for verification. Evidence latency rises to roughly 180 ms — still six times inside the one-second
requirement, but outside the 150 ms single-station invariant. **I4 is defined for the single-station
hero mode; multi-station mode reports its own measured figure against the problem statement's
1 second.** If GPU utilisation exceeds 80% in `nvidia-smi`, drop to three stations rather than
lowering the frame rate further.

Earlier drafts claimed four stations at 30 fps under 40% GPU. That was not measured and is
withdrawn (Appendix A).

### Adjudication capacity

Florence-2 serves one request at a time at 200–400 ms, so ~3 per second. A bolt enters the
ambiguous band perhaps once per cycle under good rendering, and cycles are 58 seconds long. Four
stations generate well under one request per second. The bounded queue (depth 16) drops the oldest
request on overflow; a dropped adjudication delays a state commit, it never produces an alert.

---

# Part III — Platform

## 14. Process and thread model

Seven kinds of process on the host, all started and watched by one supervisor (Section 33). Docker runs
only Jenkins, and only in CI mode. Every boundary exists for a reason that can be stated in one sentence.

| Process | Language | Port | Why it is separate |
|---|---|---|---|
| `torquesense.exe` (one per station) | C++20 | 8080 WS · 8090 MJPEG · 8095 control | The hot path. Nothing else may share its address space. |
| `softplc.exe` | C++ · open62541 server | 4840 | Stands in for the line PLC. Separate so the alert path crosses a real network boundary. |
| `sidecar` | Python · PyTorch | 8001 | Florence-2 and the clip writer. A stall here must never stall the engine. |
| `fleet-agent.exe` (one per station) | Go | none — push only | Reports host health and whether the engine is alive. It cannot live in the process it watches, and it listens on no port. |
| API | Python · FastAPI | 8000 | Events in, history out, the outbox, and the built dashboard and HMI (`web\dist`). The database and clip store are the Supabase project. |
| Node-RED | Node | 1880 | The tag monitor, pinned with a lockfile. |
| `supervisor.exe` | Go | 8099, loopback | Starts, restarts and stops all of the above. It must outlive any of them, so it cannot be one of them. |

### Threads inside `torquesense.exe`

```mermaid
flowchart LR
  subgraph P["Pinned — physical cores 4 to 7 · HIGH_PRIORITY_CLASS"]
    C["capture<br/>logical CPU 8"] -- "SPSC 32" --> I["inference<br/>logical CPU 10"]
    I -- "SPSC 32" --> V["verify<br/>logical CPU 12"]
    V -- "SPSC 16" --> A["alert<br/>logical CPU 14<br/>OPC-UA write · heartbeat"]
  end
  subgraph U["Unpinned — normal priority"]
    O["io<br/>WebSocket · HTTP · narrator · logging"]
    K["control<br/>scenario switch · health"]
  end
  V -- "SPSC 64" --> O
  A -- "SPSC 16 · ack + latency" --> O
  C -- "JPEG" --> R[("SharedClipRing")]
  A --> PLC["OPC-UA :4840"]
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class V,A key;
```

*Figure 4 — Thread model. The alert thread exists for one reason: the OPC-UA write is blocking I/O,
and blocking I/O must never run on the verify thread. Giving the alert its own pinned thread keeps the
verify thread allocation-free and non-blocking while keeping the alert path short. Everything slow —
string formatting, JSON, HTTP, logging — lives on the unpinned io thread.*

### The heartbeat — liveness tied to work, not to a thread

Every 500 ms the alert thread increments `Station.W14.Heartbeat` — but only if the verify thread's
frame counter has moved since the last beat. A frozen camera, a hung inference thread and a crashed
process all look the same to the PLC: the counter stops. The PLC turns that into AMBER (Section 19).

```cpp
// control-app/src/AlertThread.cpp — the beat is tied to frames actually verified
if (now >= next_beat_) {
  const auto verified = frames_verified_.load(std::memory_order_relaxed);  // a counter: relaxed is enough
  if (verified != last_verified_) {
    last_verified_ = verified;
    plc_.writeUInt32(heartbeat_node_, ++beat_);  // blocking I/O: one reason this is its own thread
  }
  next_beat_ = now + std::chrono::milliseconds{500};
}
```

The alert thread also returns each acknowledgement and its measured latency to the io thread, over
its own SPSC ring. That is how an anomaly row learns its `engine_ms` and `evidence_ms`.

The i7-11800H exposes physical cores as logical CPU pairs (0,1), (2,3) … (14,15). Pinning to the
even logical CPUs 8, 10, 12, 14 puts each pipeline thread on its own physical core and leaves the
hyperthread siblings idle, so no pipeline thread shares execution resources with the OS or the
browser. `SetThreadAffinityMask` returns 0 on failure; check it, because a silently failed pin makes
every latency figure meaningless.

### Windows-specific scheduling — three things a Linux-trained engineer misses

**Timer resolution.** `Sleep` and `std::this_thread::sleep_for` on Windows wake on the system timer,
historically 15.6 ms. Replaying video at 30 fps with that timer produces uneven frame pacing that
looks like a detector problem. The capture thread paces itself with a high-resolution waitable timer:

```cpp
// control-app/src/Affinity.cpp — one of the two files allowed platform #ifdefs
#ifdef _WIN32
HANDLE make_frame_timer() {
  return CreateWaitableTimerExW(nullptr, nullptr,
                                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}
#endif
```

**Power throttling.** Windows 11 applies EcoQoS to processes it considers background, lowering clock
speed on a laptop. Opt the engine out explicitly at startup:

```cpp
PROCESS_POWER_THROTTLING_STATE s{};
s.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
s.StateMask   = 0;   // 0 = throttling OFF for the controlled policy
SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);   // never REALTIME_PRIORITY_CLASS
```

**`REALTIME_PRIORITY_CLASS` is forbidden.** It can preempt the input stack and the disk cache
manager, and a demo laptop that stops responding to the mouse is worse than any latency figure.

---

## 15. Identity and access

**Cut for the prototype, specified for production.** Applying the scope test from Section 1: login
earns no rubric points on a single-laptop demo and costs a day.

What the prototype does instead:

| Measure | Implementation |
|---|---|
| Nothing listens on the network that does not need to | Every service binds `127.0.0.1`. The soft PLC still binds all interfaces (open62541's default, Section 29), but with Node-RED out of Docker nothing outside the laptop needs it, so there is no firewall rule letting anything in |
| Write endpoints are not open | FastAPI `POST` routes require an `X-API-Key` header read from `.env` |
| The fault harness cannot be mistaken for production | Separate route, styled as a test tool, and the control port refuses requests not from localhost |

What production needs — say this in the pitch, do not build it: OIDC for the engineer dashboard
(plant SSO), no login on the operator HMI (it is a kiosk at a station, identified by station ID, not
by person), and OPC-UA security mode `SignAndEncrypt` with certificates between the engine and the
PLC. Operator identity, where needed, comes from the plant's existing badge system via MES — the
camera system should not become a second identity authority on the shop floor.

---

## 16. Data architecture

Pixels never enter the relational store. Events are rows; clips are objects; the row holds the
object key.

### Where the data lives — one free Supabase project

The system of record is a hosted **Supabase** project on the free plan. One service gives three
things this design needs: Postgres for events and health; Storage for clips, reached through its
S3-compatible API so the sidecar's `boto3` code is unchanged; and Realtime, so the public
dashboard on Vercel shows new verdicts live (Section 32). It replaces the local TimescaleDB and
Garage containers, which frees about 1.2 GB of the 4 GB that WSL is allowed.

**Why there is no `timescaledb` extension.** Supabase's own documentation marks the extension
deprecated on Postgres 17, keeps it only on Postgres 15 projects, and ships only its Apache 2
edition. That edition has hypertables but not the features this design used them for: continuous
aggregates, compression and retention policies. So the schema is plain Postgres:

- **Tables.** Ordinary tables with primary keys and the indexes the dashboard's queries need.
- **Time buckets.** `time_bucket` becomes core Postgres's `date_bin`.
- **The hourly roll-up.** The continuous aggregate becomes a plain view, because at a few thousand
  rows a day it computes in milliseconds.
- **Retention.** Retention policies become two `pg_cron` jobs.

Supabase's own replacement for hypertables is native partitioning with `pg_partman`. It is worth
adding only if a table passes about ten million rows, which this prototype will not.

**The network is never on the alert path.** The alert still goes engine → soft PLC → HMI, entirely
on the laptop. What goes to Supabase is the *record*, and it goes through an **outbox**:

- **Store first.** The API writes every anomaly, cycle and health sample to a SQLite file on the
  laptop (`api/outbox.py`).
- **Forward in order.** A flusher sends them to Supabase in order, through the session pooler
  (IPv4 on every plan).
- **A dropped link costs nothing.** A transient failure stops the flush and the same record goes
  first next time.
- **Duplicates are harmless.** Every record carries its own primary key and every insert is
  `on conflict do nothing`.
- **Bad records don't block.** A record the database rejects as invalid goes to a dead-letter
  table instead of holding up everything behind it.

So when the venue Wi-Fi drops mid-demo, alerts keep firing, nothing is lost, and the cloud
dashboard catches up when the link returns.

### Schema — `supabase/migrations/`

Applied with the Supabase CLI (`npx supabase db push`, Section 29). These three files are tested.
The tests run against a real Postgres with Supabase's roles, publication and Storage table
emulated, and check four things:

- the migrations apply and the retention jobs are scheduled;
- a record delivered twice lands once, and an invalid one is refused;
- the public role reads the showcase tables and the hourly view;
- the same role cannot read host or chart-state data, insert, or delete.

```sql
-- supabase/migrations/20261016000001_events.sql
-- Verification events: what the engine decided, one row per cycle and per violation.
-- Plain Postgres. Supabase deprecates the timescaledb extension on Postgres 17 and ships only its
-- Apache edition (no continuous aggregates, compression or retention policies), so nothing here
-- depends on it. At prototype scale — thousands of rows a day — plain tables with the right
-- indexes are faster to reason about than partitions; add pg_partman if a table passes ~10M rows.

create table public.anomalies (
  event_id        uuid        primary key,         -- minted at the source: replays are idempotent
  ts              timestamptz not null,
  station_id      text        not null,
  cycle_id        uuid        not null,
  step_id         text        not null,
  profile         text        not null,            -- e.g. wheel-fitment-rl, desk-cover-4screw
  profile_version text        not null,            -- hash of the profile folder
  violation_type  text        not null check (violation_type in
                    ('SEQUENCE','PRECEDENCE','EVIDENCE_MISSING','REGRESSION','STEP_TIMEOUT','INCOMPLETE_AT_RELEASE')),
  observed_ip     text,
  expected_ip     text,
  detail          jsonb       not null default '{}',
  engine_ms       real        not null,
  evidence_ms     real        not null,
  clip_key        text,                            -- object key in the Storage bucket `clips`
  narrative       text        not null check (narrative <> ''),
  adjudicated     boolean     not null default false,
  cad_revision    text        not null,
  model_version   text        not null,
  reviewed_at     timestamptz,
  false_positive  boolean
);
create index anomalies_station_ts on public.anomalies (station_id, ts desc);
create index anomalies_type_ts    on public.anomalies (violation_type, ts desc);

create table public.cycles (
  cycle_id    uuid        primary key,
  ts          timestamptz not null,
  station_id  text        not null,
  profile     text        not null,
  passed      boolean     not null,
  duration_s  real        not null
);
create index cycles_station_ts on public.cycles (station_id, ts desc);

-- First-time-right per station per hour. A plain view: date_bin is core Postgres (14+), and at this
-- volume computing it on read costs milliseconds. security_invoker makes it obey the caller's RLS.
create view public.ftr_hourly with (security_invoker = true) as
select date_bin('1 hour', ts, timestamptz '2000-01-01') as bucket,
       station_id,
       avg(passed::int)::real as first_time_right,
       count(*)               as cycles
from public.cycles
group by 1, 2;
```

```sql
-- supabase/migrations/20261016000002_fleet_health.sql
-- Fleet health: the engine's health buckets and the fleet agent's host samples.

create table public.station_health (
  station_id          text        not null,
  bucket_start        timestamptz not null,
  bucket_s            smallint    not null,           -- 300 normally, 5 in demo mode
  fps                 real        not null,
  dropped_frame_rate  real        not null,
  ip_conf_mean        real,
  ambiguous_rate      real        not null,
  anchor_residual_px  real,
  evidence_p99_ms     real,
  camera_temp_c       real,                           -- NULL for file, webcam and phone sources
  packets_resent      integer,                        -- industrial GigE only (Appendix D.1)
  adjudications       integer     not null default 0,
  model_version       text        not null,
  cad_revision        text        not null,
  primary key (station_id, bucket_start)
);

create table public.host_health (                     -- every 10 s from the fleet agent
  station_id       text        not null,
  node             text        not null,
  ts               timestamptz not null,
  engine_up        boolean     not null,
  gpu_temp_c       real,
  sm_clock_ratio   real,
  gpu_mem_mib      real,
  disk_free_bytes  bigint,
  primary key (station_id, node, ts)
);
create index host_health_ts on public.host_health using brin (ts);   -- the retention delete's index

create table public.health_events (                   -- drift episodes, offline periods, baseline resets
  event_id    uuid        primary key,
  ts          timestamptz not null,
  station_id  text        not null,
  kind        text        not null check (kind in
                ('DRIFT','DRIFT_CLEARED','ENGINE_OFFLINE','ENGINE_ONLINE','BASELINE_RESET')),
  metric      text,
  detail      jsonb       not null default '{}'
);
create index health_events_station_ts on public.health_events (station_id, ts desc);

create table public.health_chart_state (              -- EWMA state: a 24 h baseline survives a restart
  station_id  text        not null,
  metric      text        not null,
  state       jsonb       not null,
  updated_at  timestamptz not null,
  primary key (station_id, metric)
);
```

```sql
-- supabase/migrations/20261016000003_access_and_jobs.sql
-- Who may read what, live updates for the public dashboard, the clip bucket, and retention.
--
-- Writes never use the Data API. The local FastAPI service connects to Postgres as the project
-- owner through the session pooler and inserts; the browser only ever holds the publishable key,
-- which acts as `anon`. So: RLS on every table, explicit grants, and read policies only on what
-- the public showcase may show.

alter table public.anomalies          enable row level security;
alter table public.cycles             enable row level security;
alter table public.station_health     enable row level security;
alter table public.health_events      enable row level security;
alter table public.host_health        enable row level security;   -- no policy: invisible to the API
alter table public.health_chart_state enable row level security;   -- no policy: invisible to the API

revoke all on public.anomalies, public.cycles, public.station_health, public.health_events,
              public.host_health, public.health_chart_state, public.ftr_hourly
  from anon, authenticated;
grant select on public.anomalies, public.cycles, public.station_health, public.health_events,
                public.ftr_hourly
  to anon, authenticated;

create policy showcase_read on public.anomalies      for select to anon, authenticated using (true);
create policy showcase_read on public.cycles         for select to anon, authenticated using (true);
create policy showcase_read on public.station_health for select to anon, authenticated using (true);
create policy showcase_read on public.health_events  for select to anon, authenticated using (true);

-- Live updates: the Vercel dashboard subscribes to new anomalies and cycles over Realtime.
alter publication supabase_realtime add table public.anomalies, public.cycles;

-- Clips: public read, because the showcase plays them and they show only your own hands at a desk
-- or rendered ones. The day real workers are filmed, make it private and serve signed URLs (spec: "Retention and privacy").
insert into storage.buckets (id, name, public)
values ('clips', 'clips', true)
on conflict (id) do nothing;

-- Retention, in the database itself. Clips are pruned by the API through the Storage API instead:
-- deleting rows from storage.objects would orphan the files.
create extension if not exists pg_cron with schema pg_catalog;
select cron.schedule('host-health-30d',    '17 3 * * *',
  $$delete from public.host_health    where ts           < now() - interval '30 days'$$);
select cron.schedule('station-health-1y',  '27 3 * * *',
  $$delete from public.station_health where bucket_start < now() - interval '365 days'$$);
```

`cad_revision` and `model_version` on every row are not decoration. When engineering revises the
CAD or the detector is retrained, those two columns are how an engineer separates "the line got
worse" from "the system changed."

`station_health` is kept for a year. One row per station per five minutes is about 105,000 rows a
year per station, which is nothing. `host_health` samples every ten seconds and is kept for thirty
days. The free plan's 500 MB database holds all of it with room to spare; `select
pg_size_pretty(pg_database_size(current_database()));` in the SQL editor shows where you stand.

### Who can see what

| Key or credential | Lives in | Can do |
|---|---|---|
| `DATABASE_URL` (session pooler, database password) | `.env`, used by the API only | everything — it is the owner |
| S3 access key pair | `.env`, used by the API and the sidecar only | read and write every bucket; bypasses RLS |
| Publishable key `sb_publishable_…` | `web/.env.local` and Vercel's environment | read the showcase tables and the hourly view, and subscribe to new rows; nothing else |

The secret key `sb_secret_…` is not used at all. Writes go through Postgres, not the Data API,
which leaves one fewer key to leak. Nothing secret ever goes into `web/`: Vite inlines every
`VITE_` variable into the public bundle.

### Object layout in Storage — bucket `clips`

```
clips/
  clips/{station_id}/{yyyy}/{mm}/{dd}/{cycle_id}.mp4     6 s, H.264, 960x540
  cad/{cad_revision}/view_{cycle_id}.png                   CAD view with the offending point highlighted
  hardneg/{model_version}/{cycle_id}_{frame}.jpg           engineer-flagged false positives
```

The sidecar uploads a clip *before* its row is inserted, so a row never points at nothing. The
free plan allows 1 GB of files, which holds several hundred six-second clips.

### Retention and privacy

Clips show workers' hands and sometimes faces. Under India's DPDP Act 2023, identifiable video of
employees is personal data. Three design rules, all cheap: the camera frames the **hub region of
interest** rather than the operator; clips keep **30 days** by default, then are deleted by the API's daily prune
through the Storage API; and hard-negative frames are **cropped to the hub** before they leave the plant for
retraining. Say this in the business section — it is the question an HR or union representative
would raise before any Tata plant installed a camera at a station.

---

## 17. Verification and validation

Three questions, three kinds of evidence.

### Is the engine correct? — the invariants

| # | Invariant | Enforced by |
|---|---|---|
| I1 | **Zero heap allocation** in the steady-state hot path | `NoAllocationTest`, counting allocator, 10,000 frames |
| I2 | **No mutex** in `perception/` or `procedure/` | review + TSan suite in WSL2 |
| I3 | **Florence-2 never blocks an alert** | `AdjudicationNeverBlocksVerify`, circuit-breaker tests |
| I4 | **Evidence latency p99 ≤ 150 ms**, single station, 30 fps | `latency_test`, hard CI gate on the laptop |
| I5 | **Clean** under `-Wall -Wextra -Wpedantic -Werror` with GCC 16 on Windows and Clang on Linux; ASan, UBSan and TSan on Linux | CI matrix |
| I6 | **Tests before implementation** in `procedure/` | review rule |
| I7 | **Peak VRAM < 3.2 GB single-station, < 5 GB four-station** | `MemoryCeiling` soak test |
| I8 | **Never commit** weights, renders, videos, `.env`, `web/.env.local`, `var/outbox.db` | `.gitignore` + DVC |
| I9 | **Renders are reproducible**: identical labels, pixels within PSNR ≥ 45 dB | `test_render_reproducible.py` |
| I10 | **Synthetic and real mAP reported separately**, always | `training/eval.py` |
| I11 | **Florence-2 is unreachable from `Pipeline::tick()`** | link-time test: the engine library does not link the HTTP client |
| I12 | **Model artefacts are verified before load**: the ONNX file's SHA-256 matches `models/manifest.json`; Florence-2 is pinned to a commit `revision` | the engine refuses to start on a mismatch; `ManifestMismatchTest` |
| I13 | **Fail visible**: if the engine's heartbeat stops for 2 s, the station shows AMBER and `InspectionOnline` goes false — decided by the PLC | `tests/plc/test_heartbeat.py`; the kind smoke test scales the engine to zero |
| I14 | **No profile goes live without passing its gate**: coverage clean, every scenario verdict exact in both implementations, per-family recall on real photos reported with confidence intervals, one canary shift | `VALIDATION.md` in the profile directory; `profile_gate.py` in CI |
| I15 | **Every required step has a deadline** — its own `max_duration_s`, or the release gate | the profile loader rejects a profile otherwise |
| I16 | **The C++ engine and the reference interpreter agree** on every scenario's violation list | differential test over every profile's fixtures |
| I17 | **No unresolved inspection point**: each one in frame, facing a camera, unoccluded, and at least 16 px across | the compiler refuses to emit the profile |

I9 was tightened from "byte-identical" in an earlier draft. Cycles renders with a fixed seed are
reproducible on one GPU and driver, but GPU path-tracing is not guaranteed bit-exact across driver
versions. Identical labels and a PSNR threshold is the honest contract.

I11 is enforced structurally rather than by review: the verify thread's library, `sunverify_procedure`,
does not link `cpp-httplib`. If someone writes a direct call to the sidecar from the hot path, it
fails to link.

I13 was tested against a soft PLC built on open62541 1.4.21 — the vendored amalgamation, built
both with GCC on Linux and with MinGW-w64 for Windows: AMBER before the first heartbeat, cleared while the counter moved, and back
2.2 s after the last beat (the 2 s rule plus the 250 ms check interval). Section 31 has the test.

### Is the detector accurate? — the two numbers

`training/eval.py` reports mAP50 and mAP50-95 on two sets, never one:

| Set | Size | What it proves | What it cannot prove |
|---|---|---|---|
| Synthetic held-out | 400 frames, unseen seeds | the model learned the rendered classes | that it works on real pixels |
| Real validation | 40 phone photos of real wheel hubs, labelled in Label Studio | `hub`, `wheel`, `bolt_absent`, `bolt_seated` transfer to real imagery | `bolt_torqued` — real hubs do not carry your yellow witness marks |

That last cell is the honest limit, and you should state it on the slide. The torque-mark class is
validated on synthetic data only. Closing that gap is one afternoon with a real hub, a paint pen,
and forty photos — it is Tier 2 work, not a flaw in the method.

### Is the procedure right? — the scenario test matrix

Twenty-five table-driven cases minimum, run on rendered scenario videos with `.expected.json`
siblings. The two that matter most are the ones that must **not** alert: hand occlusion mid-sequence,
and a second person entering the frame. False alarms on a production line get a system unplugged
faster than missed defects get it noticed.

---

## 18. Industrial and safety parameters

### OPC-UA — governs the interface

The alert is an OPC-UA write (IEC 62541), the protocol most modern PLCs and MES layers expose. The
prototype's soft PLC publishes:

```
ns=2;s=Station.W14.Andon.Red          Boolean
ns=2;s=Station.W14.Andon.Amber        Boolean
ns=2;s=Station.W14.Andon.Green        Boolean
ns=2;s=Station.W14.Conveyor.HoldReq   Boolean      ← a request, not a command
ns=2;s=Station.W14.LastViolation      String
ns=2;s=Station.W14.CycleState         Int32
ns=2;s=Station.W14.VariantCode        String       ← selects the variant's procedure at cycle start
ns=2;s=Station.W14.ReleaseRequest     Boolean      ← the release gate: may this part leave?
```

### Functional safety — what this system is not

TorqueSense is a **quality** system, not a **safety** function. It is not designed or claimed to
IEC 61508 or ISO 13849, and it must never be the only thing standing between an operator and a
hazard. That is why the conveyor tag is `HoldReq` — a *request*. The PLC owns the conveyor and
decides whether to honour the request according to its own validated logic. A camera system that
directly commanded machinery would be a safety function and would need certification it does not
have.

This distinction is worth one sentence in the pitch. It tells a Tata engineer you know where the
boundary is.

### Torque practice — governs the claim

Star or cross-pattern tightening spreads clamp load evenly across the hub flange. Witness marks
show which fasteners have been torqued. The nutrunner controller remains the authority on torque
value. TorqueSense adds what neither provides without fixtures: geometric verification of sequence
and completeness, derived from the CAD.

### DPDP Act 2023 — governs the video

Covered in Section 16: region-of-interest framing, 30-day retention, hub-cropped retraining data.

---

## 19. Fleet health and reliability

The verification engine answers *was this wheel fitted correctly?* This section answers the
question a plant asks next: *is the inspector itself still working?*

Camera systems rarely fail loudly. A smeared lens, a mount knocked two degrees, a fan clogged with
dust — each lowers performance gradually, and the first symptom an operator sees is a false alarm,
by which point trust is already spent. So the stations are treated as a fleet of devices and
watched the way a storage team watches a drive fleet: telemetry from every device, statistical
drift detection against each device's own normal, and a playbook from signal to likely cause.

### Two tiers, two speeds

| Tier | Detects | Speed | Who acts | Effect on the line |
|---|---|---|---|---|
| **Fail-visible** | engine dead, camera frozen, pipeline hung | 2 s | the PLC, automatically | AMBER, `InspectionOnline` false; the PLC decides whether parts may pass |
| **Drift** | slow degradation | minutes to hours | a maintenance engineer | none — a ticket |

Health rules never raise or suppress a quality alert. Keeping the two systems apart means a noisy
health signal can never stop the line, and a healthy-looking station can never mask a real defect.

### Fail-visible — the PLC owns it

```mermaid
sequenceDiagram
  participant V as verify thread
  participant A as alert thread
  participant P as PLC watchdog, every 250 ms
  participant L as Andon and HMI
  loop every 500 ms, while frames are being verified
    V->>A: frames_verified has moved
    A->>P: Heartbeat = n + 1
    P->>L: InspectionOnline true · Amber off
  end
  Note over V,A: camera freezes, a thread hangs, or the process dies
  P->>P: Heartbeat unchanged for 2 s
  P->>L: InspectionOnline false · AMBER
```

*Figure 4b — The heartbeat. The engine proves it is working by changing a number; the PLC notices
when it stops. Nothing has to report a failure, which is the point: the failures that matter are
the ones that cannot report themselves.*

The watchdog lives in the PLC, not in the fleet agent, because the PLC owns the line, runs on its
own hardware, and keeps running when the PC does not. This is the standard industrial pattern for a
supervised device. The soft PLC's version, as built and tested:

```c
/* softplc/src/watchdog.c — compiled and run against open62541 1.4.21 */
#define STALE_MS 2000
static UA_UInt32  last_beat   = 0;
static UA_DateTime last_change = 0;            /* 0 = never seen: inspection not ready */

static void watchdog(UA_Server *s, void *data) {          /* registered every 250 ms */
    UA_Variant v; UA_Variant_init(&v);
    if (UA_Server_readValue(s, UA_NODEID_STRING(ns, "Station.W14.Heartbeat"), &v) != UA_STATUSCODE_GOOD)
        return;
    UA_UInt32 beat = *(UA_UInt32 *)v.data;
    UA_Variant_clear(&v);
    UA_DateTime now = UA_DateTime_nowMonotonic();
    if (beat != last_beat) { last_beat = beat; last_change = now; }
    UA_Boolean online = last_change != 0 && (now - last_change) < STALE_MS * UA_DATETIME_MSEC;
    set_bool(s, "Station.W14.InspectionOnline", online);
    set_bool(s, "Station.W14.Andon.Amber", !online);
}
/* in main(), once per configured station (W-14 shown; the soft PLC reads the station list
   from its command line, so four-station mode gets four independent watchdogs):
   UA_Server_addRepeatedCallback(server, watchdog, station, 250, NULL); */
```

A station is AMBER from the moment the soft PLC starts until the engine's first beat. That is
intended: *not ready* is not *OK*. During the demo, start-up takes a few seconds and the HMI says why.

### The six signals

Five come from the engine's io thread, aggregated per bucket; one comes from the fleet agent.

| Signal | Source | Bad direction | What moves it | σ floor |
|---|---|---|---|---|
| `ip_conf_mean` | engine | down | lens contamination, lighting, focus | 0.005 |
| `ambiguous_rate` | engine | up | the same causes, usually earlier | 0.002 |
| `anchor_residual_px` | engine | up | camera knocked or mount loosened | 0.5 px |
| `evidence_p99_ms` | engine | up | thermal throttling, GPU contention | 2 ms |
| `dropped_frame_rate` | engine | up | capture link, decode or disk stalls | 0.001 |
| `sm_clock_ratio` | fleet agent | down | throttling | 0.01 |

`anchor_residual_px` exists only because of this design. Inspection points come from projecting CAD
positions into the image, so the engine knows where every part *should* appear. The mean distance
between detections and those projections is close to constant — until someone knocks the camera.
It rises long before detection fails.

The σ floor stops a near-constant baseline from producing an estimated spread of zero, which would
make the smallest blip look like a catastrophe.

### Drift detection — limits set by simulation, not by habit

Each station runs one **one-sided EWMA control chart** per signal, on bucket means (five minutes in
production). One-sided, because confidence rising is never a problem; the chart only watches the
bad direction, which halves false alarms for free.

$$z_t = \lambda u_t + (1-\lambda) z_{t-1}, \qquad u_t = \pm\frac{x_t-\hat\mu_0}{\hat\sigma_0}, \qquad \text{drift when } z_t > L\sqrt{\tfrac{\lambda}{2-\lambda}\left(1-(1-\lambda)^{2t}\right)}$$

$\hat\mu_0$ and $\hat\sigma_0$ come from a **phase I baseline**: the median and the scaled median
absolute deviation of the first 288 buckets — 24 hours — after a deploy. Median and MAD rather than
mean and standard deviation, so one bad hour in the baseline does not poison it.

**The budget.** At most one false drift ticket per station per month: six charts, 8,640 five-minute
buckets a month. Every figure below is from a Monte Carlo simulation of independent, normal bucket
means, run while writing this section: 4,000 simulated charts per estimated-baseline row, and 800
to 2,000 long in-control runs for the known-parameter rows.

| Configuration | False tickets, per station-month | Median delay to flag a shift of 0.5σ · 1σ · 2σ |
|---|---|---|
| Textbook λ 0.2, L 3, parameters *known* | ≈ 48 | — |
| λ 0.05, L 3.9, parameters *known* | ≈ 0.87 | mean run length 71 · 15 · 4.4 buckets |
| λ 0.05, L 3.9, *24 h estimated* baseline | ≈ 1.7 — over budget | — |
| **λ 0.05, L 4.2, 24 h estimated baseline — chosen** | **≈ 0.92** | **73 · 17 · 5 buckets (≈ 6 h · 85 min · 25 min)** |

The textbook setting has an in-control average run length of about 1,090 buckets — under four days
per chart, more than one false ticket a day per station. A maintenance engineer would mute that
within a week.

**What the simulation exposed.** With the parameters *known*, λ 0.05 and L 3.9 meets the budget. With
parameters *estimated* from a baseline — the only way they are ever obtained — it does not. An error
of a quarter of a sigma in $\hat\mu_0$ acts like a small permanent shift, and a chart tuned to catch
small shifts catches that too. The effect shrinks slowly with baseline length:

| Baseline length at L 3.9 | 2 h | 12 h | 24 h | 48 h | 96 h | known |
|---|---|---|---|---|---|---|
| Charts with a false ticket within a month | 46.9% | 34.1% | 28.4% | 24.1% | 20.3% | 14.5% |

So L is raised to 4.2 for the 24-hour baseline that is actually used. The first version of the unit
test, written with a two-hour baseline, failed for exactly this reason — the right outcome for a test.

**The assumption to state out loud.** The simulation assumes independent buckets. Real health data
is autocorrelated — GPU temperature especially — which shortens in-control run lengths. Bucket means
of thousands of frames are close to normal, but not independent. So every ticket's outcome is logged
(`DRIFT`, then confirmed or dismissed by the engineer), and L is recalibrated after two weeks of
real data. That makes the false-alarm budget a measured number rather than a promise.

**Baselines reset on redeploy.** A new model or a new CAD revision legitimately changes confidence
and residuals. When `model_version` or `cad_revision` changes, that station's charts return to phase
I and a `BASELINE_RESET` event is logged. Without this, every deployment would look like drift.

```python
# api/fleet/ewma.py — the core of the chart; tests in api/tests/test_ewma.py
@dataclass
class Chart:
    metric: str
    direction: Literal["up", "down"]      # which way is bad
    sigma_floor: float
    lam: float = 0.05
    L: float = 4.2                        # set by simulation for a 288-bucket estimated baseline
    baseline_n: int = 288                 # 24 h at 5-minute buckets; shorter inflates false alarms
    clear_n: int = 6                      # buckets back inside limits before a drift episode closes

    def update(self, x: float) -> tuple[State, bool]:
        if self.state is State.BASELINE:
            self._phase1.append(x)
            if len(self._phase1) >= self.baseline_n:
                self.mu0 = statistics.median(self._phase1)
                mad = statistics.median(abs(v - self.mu0) for v in self._phase1)
                self.sigma0 = max(1.4826 * mad, self.sigma_floor)
                self.state = State.IN_CONTROL
            return self.state, False
        sign = 1.0 if self.direction == "up" else -1.0
        self.t += 1
        self.z = self.lam * sign * (x - self.mu0) / self.sigma0 + (1 - self.lam) * self.z
        out = self.z > self.limit()
        if self.state is State.IN_CONTROL and out:
            self.state, self._inside = State.DRIFT, 0
            return self.state, True                       # a new episode: raise one ticket
        if self.state is State.DRIFT:
            self._inside = 0 if out else self._inside + 1
            if self._inside >= self.clear_n:
                self.state = State.IN_CONTROL             # hysteresis: no flapping tickets
        return self.state, False
```

The drift job runs in the API once per bucket: it loads each chart's state from
`health_chart_state`, feeds it the new bucket, writes the state back, and inserts a `health_events`
row when an episode opens or closes. It is plain Python; at four stations and six charts it does 24
updates every five minutes.

```mermaid
flowchart LR
  E["engine io thread<br/>bucket aggregates"] --> SH[("station_health")]
  G["fleet-agent<br/>every 10 s"] --> HH[("host_health")]
  SH --> J["drift job · each bucket<br/>EWMA per station × signal"]
  HH --> J
  J <--> CS[("health_chart_state")]
  J --> HE[("health_events")]
  HE --> F["fleet view · attention list"]
  SH --> F
```

*Figure 4c — Health data path. Two writers, one job, one reader. Chart state is persisted every
bucket, so a restart costs nothing.*

### From signal to cause — the playbook

The fleet view does not stop at "W-16 is amber". It shows which signals moved together and what that
pattern usually means.

| Signals moving together | Likely cause | Action |
|---|---|---|
| `ip_conf_mean` ↓ and `ambiguous_rate` ↑, latency flat | lens contamination, lighting change | clean the lens or check lighting at the next break |
| `anchor_residual_px` ↑ | camera knocked, mount loose | check the ghost overlay alignment; re-tighten and re-calibrate |
| `ambiguous_rate` ↑ on one bolt position only | partial occlusion: a cable or fixture in view | inspect the station |
| `evidence_p99_ms` ↑ and `sm_clock_ratio` ↓ | thermal throttling | check fans, filters, cabinet temperature |
| `evidence_p99_ms` ↑, clocks normal | contention: another process on the GPU or the pinned cores | check what else is running |
| `dropped_frame_rate` ↑ | capture link, decode or disk stalls | check the camera cable and the clip writer's free disk |
| everything shifts just after a deploy | not drift — a new model | baselines reset; if the shift is bad, the canary has failed |

### Canary, promote, roll back

Drift charts judge a station against its own past. A canary must be judged against its **peers over
the same shift**: they answer different questions, and a canary's own charts are still learning a
new baseline anyway.

1. Deploy the new model to one station. Its charts reset to phase I, as expected.
2. Over one shift, compare it with the other stations on first-time-right rate, engineer-confirmed
   false positives, ambiguous rate and evidence p99.
3. Promote it to the rest of the line, or roll back.

On the laptop, a deploy is restarting one station's engine with a different `perception.model_path`.
In Tier 2 it is `kubectl set image` on one Deployment, and roll-back is `kubectl rollout undo`
(Section 32). Either way the model's SHA-256 is checked against `models/manifest.json` before the
engine will load it (I12):

```json
{
  "yolo11n_wheel.onnx": {
    "sha256": "<64 hex characters>",
    "version": "yolo11n-w14-s20260721",
    "trained_from": "data/synth/3f2a91c0b7de-g3-s20260721",
    "accuracy_report": "docs/accuracy.json"
  }
}
```

The hash is computed with picosha2, a header-only library, so the check adds no platform code.

### The fleet agent

One per station, every ten seconds: probe the engine's `/health`; sample the GPU — through
`nvidia-smi` on the laptop, through the node's dcgm-exporter on a station (`-gpu-source=dcgm`,
Appendix D.3); read free disk space; push one JSON record to the API. Written in Go using only the
standard library — a single static binary of about 6 MB, cross-compiled for Windows and Linux from
either, with nothing third-party to patch on an edge PC.

When the API is down it spools to a capped JSONL file (5 MiB, so a dead API cannot fill the disk).
Every record goes *through* the spool, oldest first:

```go
// fleet-agent/main.go — one path for every record
line, _ := json.Marshal(rec)
if err := sp.Append(line); err != nil {
    slog.Error("spool append failed; record lost", "err", err) // full or unwritable: say so
}
sent, pending, err := sp.Flush(func(b []byte) error { return post(ctx, client, *apiURL, apiKey, b) })
if pending > 0 {
    slog.Warn("api unreachable; records held in spool", "pending", pending, "sent", sent)
}
```

The first version sent the new record directly and replayed the spool afterwards. An outage test
caught it: after the API came back, records arrived out of order — and the drift job reads buckets
in time order. Routing everything through the spool fixed it. Re-run, 17 records crossed a simulated
outage and arrived complete and in order.

The agent also reports `engine_up`. That is how the fleet view knows a station is offline even
though its engine — the thing that would normally report — is the thing that died.

### Security, as a fleet property

| Concern | Prototype | Tier 2 |
|---|---|---|
| A tampered or wrong model | SHA-256 against the manifest before load (I12) | the same, with the manifest signed |
| An agent as an attack surface | listens on no port; push only | the same |
| Certificate expiry — the classic fleet-wide outage | — | OPC-UA certificate expiry dates reported by the agent as a health signal, ticketed 30 days out |
| Images from the internet on the line | — | in-plant registry, pinned tags (Section 32) |

---

# Part IV — Build

## 20. Tool register

One pick per layer, each chosen because it fits something already in the stack *and* this machine.
Every item is free and open source, or free tier.

### CAD and synthetic data

| Layer | Pick | Reason |
|---|---|---|
| CAD modelling | [FreeCAD](https://www.freecad.org/) 1.0 or later, Assembly workbench | Free, exports STEP and glTF from the same document. |
| CAD kernel, C++ | [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT) from MSYS2 (`opencascade` 7.9) | The kernel FreeCAD is built on. Reading STEP assemblies through `XCAFDoc_ShapeTool` is CAD-kernel work in C++ — the same class of work as NX Open. |
| Rendering + auto-labels | [BlenderProc](https://github.com/DLR-RM/BlenderProc) | Built for exactly this problem: domain-randomised renders with COCO annotations. Downloads and manages its own Blender, so there is no Blender version to get wrong. |
| Label review, real set | [Label Studio](https://github.com/HumanSignal/label-studio) in Docker | Only for the 40 real validation photos. Run it when needed, stop it after. |
| Dataset versioning | [DVC](https://dvc.org/) | Renders and weights are never committed (I8); DVC records which seed produced which model. |

### Perception and reasoning

| Layer | Pick | Reason |
|---|---|---|
| Detector | [Ultralytics YOLO11n](https://github.com/ultralytics/ultralytics) | Hands and tools, shared by every profile. 2.6M parameters, 14 ms on this GPU through ONNX Runtime. |
| Inspection-point states | MobileNetV3-Small from [torchvision](https://pytorch.org/vision/), one per state family, exported to ONNX | A closed two- or three-way choice on a 96×96 crop: small enough to batch every point in view in a few milliseconds, and reusable wherever the family appears. |
| Registration | AprilTag fiducials through OpenCV's ArUco module | Places inspection points on a moving carrier every frame. |
| Training | [Google Colab](https://colab.research.google.com/) T4 | Section 27 explains why it is used for this and nothing else. |
| Inference runtime, C++ | [ONNX Runtime](https://github.com/microsoft/onnxruntime) ≥ 1.27, **`gpu_cuda13`** build, CUDA execution provider | Matches your CUDA 13.2 toolkit. The baseline every other provider is measured against. |
| Inference fast path | [TensorRT](https://developer.nvidia.com/tensorrt) **10.13.3.9**, CUDA 13.0 build, through ONNX Runtime's TensorRT execution provider | The release ONNX Runtime's CUDA 13 provider links and is tested with. Detector only, FP16, engine cached; used only if its gate passes (Section 30). `trtexec` from the same zip is the standalone benchmark. GCC never links it. |
| GPU libraries | [cuDNN](https://developer.nvidia.com/cudnn) 9.x **for CUDA 13** | Required by the ONNX Runtime CUDA EP. The CUDA 13 build, not the CUDA 12 one. |
| Tracking | ByteTrack, reimplemented (~200 lines) | IoU association with a Kalman predictor. Reimplementing avoids a Python dependency on the hot path; bolt identity comes from CAD anchoring anyway. |
| Vision-language model | [Florence-2-base](https://huggingface.co/florence-community/Florence-2-base) via [transformers](https://github.com/huggingface/transformers) | 230M parameters, fits beside the detector in 6 GB. Phrase grounding is the exact task adjudication needs. |
| PyTorch | [PyTorch](https://pytorch.org/) ≥ 2.11, **cu130** wheels | The first line with CUDA 13 wheels. Florence-2 only; the C++ engine does not use it. |

### Engine, C++

| Layer | Pick | Reason |
|---|---|---|
| Compiler | **GCC 16.2, MSYS2 UCRT64** — already installed | The compiler on this laptop. UCRT64 links the same Universal C Runtime as ONNX Runtime's binaries, and ORT's C API is a plain C ABI, so GCC links its official package directly — built and run while writing this. |
| Debugger | **GDB 18.1**, MSYS2, through VS Code's C/C++ extension | Matches the compiler; `windows-debug` builds with `-Og -g3` for it. |
| Build | [CMake](https://cmake.org/) + [Ninja](https://ninja-build.org/) from MSYS2; libraries from `pacman`; five small ones by CMake `FetchContent` at pinned tags (`cmake/deps.cmake`) | Prebuilt packages install in minutes, where vcpkg compiled OpenCV and OCCT for an hour. One file lists every source dependency and its tag. |
| Video I/O | [OpenCV](https://opencv.org/) 5.0 from MSYS2 — DirectShow and FFmpeg backends | Laptop webcam (`CAP_DSHOW`), phone stream and file replay (`CAP_FFMPEG`), JPEG encode for the clip ring. |
| Phone as a camera | IP Webcam (Android), over the phone's own hotspot | MJPEG at a URL OpenCV opens directly; no driver on the laptop, no router, no internet. |
| Camera calibration | OpenCV ChArUco + `calibrateCamera`, on printed sheets from `samples/_print/` | Intrinsics with a 0.5 px RMS gate, written into `cameras.yaml`. |
| OPC-UA | [open62541](https://github.com/open62541/open62541) 1.4.21, vendored as its single-file amalgamation | Client in the engine, server in the soft PLC. The exact version the watchdog was tested on, built with MinGW too. MSYS2 packages 1.5, which nothing here has been tested against. |
| WebSocket server | [IXWebSocket](https://github.com/machinezone/IXWebSocket) | Small, Windows-friendly, no libuv dependency. |
| HTTP | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | MJPEG stream, control endpoint, sidecar client. Header-only. |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | Off the hot path only. |
| Excel | [OpenXLSX](https://github.com/troldal/OpenXLSX) | Shift report written from C++. |
| Logging | [spdlog](https://github.com/gabime/spdlog) | Async logger on the io thread. |
| Tests | [GoogleTest](https://github.com/google/googletest) · [Google Benchmark](https://github.com/google/benchmark) | Unit, property, and microbenchmark. |
| Latency histograms | [HdrHistogram_c](https://github.com/HdrHistogram/HdrHistogram_c) via FetchContent | p99 and p99.9 without storing every sample. |

### Platform and front end

| Layer | Pick | Reason |
|---|---|---|
| Database | [Supabase](https://supabase.com/) — hosted Postgres, free plan, region South Asia (Mumbai) | Events, health and the EWMA state in plain Postgres; `pg_cron` for retention. The `timescaledb` extension is deprecated there on Postgres 17 and is not used (Section 16). |
| Migrations | Supabase CLI — `npx supabase db push` | `supabase/migrations/` is the only place the schema changes. |
| Store-and-forward | SQLite outbox in the API — `api/outbox.py` | In order, idempotent, with dead letters: the cloud is never on the alert path. |
| Object store | Supabase Storage, bucket `clips`, through its S3-compatible API | The sidecar's `boto3` code is unchanged; the free plan holds 1 GB. |
| Live public dashboard | Supabase Realtime + publishable key, on Vercel | New verdicts appear as the outbox delivers them; RLS limits the key to the showcase. |
| API | [FastAPI](https://fastapi.tiangolo.com/) | Events in, history out. |
| Tag monitor | [Node-RED](https://nodered.org/) + [node-red-contrib-opcua](https://github.com/mikakaraila/node-red-contrib-opcua) | Shows real OPC-UA tags as industrial indicators — proof the alert is a real protocol write, not a coloured `div`. |
| Front end | React + [Vite](https://vite.dev/) + TypeScript + [Tailwind](https://tailwindcss.com/) + [shadcn/ui](https://ui.shadcn.com/) | Dashboard and the operator HMI from one codebase. |
| Charts · icons | [Recharts](https://recharts.org/) · [Lucide](https://lucide.dev/) | — |
| Typography | IBM Plex Sans and Plex Mono via [Fontsource](https://fontsource.org/) | Tabular figures for latency readouts; self-hosted, works offline. |
| End-to-end tests | [Playwright](https://playwright.dev/) | Dashboard flows and the fault harness. |
| Containers | Docker Desktop, WSL2 backend | The only backend Windows 11 Home supports. |

### Operations

| Layer | Pick | Reason |
|---|---|---|
| CI for the GPU gates | [Jenkins](https://www.jenkins.io/) LTS (`jenkins/jenkins:lts-jdk21`) in Docker, with an inbound agent on Windows | Hosted CI has no GPU. GitHub recommends self-hosted runners only for private repositories, because a pull request from a fork can run code on the runner. Jenkins polling this repository's `main` branch never runs fork code, and exposes nothing on the laptop. |
| Java for the agent | [Eclipse Temurin](https://adoptium.net/) 21 | The Jenkins agent is a Java process. |
| Fleet agent | [Go](https://go.dev/), standard library only | One static binary, cross-compiled for Windows and Linux, no runtime to install. |
| Process supervisor | `supervisor.exe` — the same Go module, standard library only | Dependency order, readiness, backoff, crash-loop detection, a Windows job object, rotating logs, maintenance mode. A service wrapper would run it as a Windows service in session 0, where the webcam and the kiosk browser are not available (Section 33). |
| Start at boot | Task Scheduler at sign-in + Sysinternals [Autologon](https://learn.microsoft.com/sysinternals/downloads/autologon) | Built into Windows 11 Home; runs in the interactive session the camera and kiosk need; restarts the supervisor if it exits. |
| Release packaging | `scripts/release/bundle.py`, standard library only | Follows PE import tables, so the release carries exactly the DLLs it needs and fails to build if one is missing. |
| Releases and provenance | GitHub Releases + [artifact attestations](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds) (`actions/attest`), verified with `gh attestation verify` | Free for a public repository; ties every installed zip to the workflow run and commit that built it. |
| Release gate | `Jenkinsfile.release` + `scripts/release/gates.py` on HSGPC | The GPU-bound checks a hosted runner cannot do, decided by one tested rule set, before a release is published. |
| Python lock | [uv](https://docs.astral.sh/uv/) `pip compile --generate-hashes` (already on this laptop), installed by `pip --require-hashes` | A release's Python environment is exact and tamper-evident. |
| Database backup | `pg_dump` from MSYS2's PostgreSQL package, nightly | The free Supabase plan keeps no backups. |
| Station script lint | [PSScriptAnalyzer](https://github.com/PowerShell/PSScriptAnalyzer), with 5.1 syntax compatibility | The station runs Windows PowerShell 5.1, which is not PowerShell 7. |
| Model integrity | [picosha2](https://github.com/okdshin/PicoSHA2) v1.0.1 via FetchContent | Header-only SHA-256: the manifest check needs no platform code. |
| Orchestration, Tier 2 | deferred — Appendix D | |
| Manifests | [Kustomize](https://kustomize.io/), built into `kubectl` | One base, two overlays (`kind`, `edge`). Hashed ConfigMaps roll a station when its procedure changes. |
| Manifest testing | [kind](https://kind.sigs.k8s.io/) in GitHub Actions + [kubeconform](https://github.com/yannh/kubeconform) | A real cluster in CI at no cost to the laptop's RAM; schemas checked strictly. |
| OPC-UA test client | [asyncua](https://github.com/FreeOpcUa/opcua-asyncio) | Python; reads and writes PLC tags in the heartbeat and cluster smoke tests. |
| Decision records | Architecture Decision Records in `docs/adr/` | Every decision this spec argues for, in one numbered file each, where a reviewer can find it. |
| Reference interpreter | Python, `reference/tsref` | The executable specification of the procedure model; the C++ engine is differentially tested against it (I16). |
| Occlusion test | OCCT `IntCurvesFace_ShapeIntersector` | Ray-casts camera-to-point lines against the assembly's own bodies at compile time. |

---

## 21. Interface design

### The design thesis

Two audiences, two surfaces, one codebase. **The operator reads nothing; the engineer reads
everything.** The HMI is a full-screen kiosk route of the same React app as the dashboard, so one
WebSocket, one set of tokens, one build.

#### Colour tokens

```css
/* web/src/styles/tokens.css */
:root {
  --bg:            #0F1115;   /* shop-floor dark: no glare on a station screen */
  --surface:       #171A21;
  --surface-2:     #1F232C;
  --text:          #E8EAED;
  --text-muted:    #9AA0A6;
  --line:          #2A2F3A;

  --state-ok:      #22C55E;   /* complete */
  --state-active:  #F5A524;   /* in progress, next bolt */
  --state-fault:   #EF4444;   /* violation */
  --state-info:    #0E7C86;   /* CAD provenance, links */

  --focus:         #7DD3FC;
  --radius:        8px;
}
```

The three state colours differ in luminance as well as hue, so they remain distinguishable in
greyscale and for red-green colour blindness. Check this on a projector before demo day; projectors
wash out amber first.

#### Type scale

| Token | Size / weight | Used for |
|---|---|---|
| `--t-hmi-hero` | 96 px / 700 | "NEXT: BOLT 3" on the operator HMI |
| `--t-hmi-label` | 40 px / 600 | step labels at three metres |
| `--t-h1` | 24 px / 600 | dashboard zone titles |
| `--t-body` | 14 px / 400 | dashboard text |
| `--t-mono` | 13 px / 500, Plex Mono, tabular | latency badge, timestamps, tag values |

### The signature element: the part map

```
        ╭───────────╮
      ╭─┤     1     ├─╮
    ╭─┤ ●         ○ ├─╮      ● torqued    ○ seated
    │ 5 ●         ○ 2 │      ◉ next, pulsing at 1 Hz
    ╰─┤ ●       ◉ ├─╯
      ╰─┤ 4     3 ├─╯        positions are the CAD bolt angles,
        ╰───────────╯        rotated to the camera's view
```

The part map is generated from the profile: the assembly's outline as the camera sees it, with a
marker at every inspection point, coloured by state. For the wheel it is the hub schematic above;
for the door it is the trim outline with six clip markers and a connector. Positions come from
`procedure.json` through the camera pose, so on-screen positions correspond to the physical part.
On violation the screen floods `--state-fault`, the offending point strobes, and for a sequence the
corrective order draws as a numbered arc. The operator never reads a sentence.

### The second invention: the ghost overlay

A semi-transparent CAD wireframe of the *correct* assembly state, projected onto the live feed with
the same extrinsics. In the software-only build the extrinsics are exact — Blender's camera is
known — so the overlay registers perfectly. When reality and CAD diverge, the divergence is visible
as a shape.

### Screen specifications

#### Operator HMI — `/hmi/:station`, Chrome kiosk

| Element | Rule |
|---|---|
| Step rail | five steps, top, 40 px labels; completed steps filled |
| Part map | centre, 60% of viewport height; generated from the profile |
| Next action | one line, 96 px |
| Cycle timer | bottom right, mono, turns amber past 80% of takt |
| Violation | full-bleed red, corrective arc, no text beyond "STOP — BOLT 4" |
| Interaction | none. The HMI accepts no input. |
| Inspection offline | if the WebSocket is silent for 1 s, full-bleed amber and "INSPECTION OFFLINE". The HMI does not wait to be told; the PLC's own AMBER follows within 2 s |

#### Engineer dashboard — `/`

Four zones, one screen, no tabs:

| Zone | Content |
|---|---|
| **A — Live feed** (top-left, 60%) | MJPEG from the engine on `:8090`, canvas overlay with boxes and CAD bolt IDs, ghost overlay toggle on `G`, and a **latency badge** showing engine and evidence latency, updating per frame |
| **B — Procedure timeline** (top-right) | steps bound to the interpreter, elapsed against allotted; a **CAD provenance chip**: `RL_WHEEL_ASSY_REV_C.step · compiled 09:14` |
| **C — Anomaly feed** (bottom-left) | cards with looping clip, violation type, station; modal with the bolt-state strip, CAD view, narrative, and **Confirm / False positive** |
| **D — Shift analytics** (bottom-right) | most-violated steps, first-time-right by hour, **Export to Excel** served by the engine's `XlsxReporter` |

#### Fault-injection harness — `/harness`

Styled deliberately as a test tool — monospace, muted border, "TEST HARNESS" in the header — so
nobody mistakes it for production UI. It selects which pre-rendered scenario feeds `IFrameSource`
at the next cycle boundary. It cannot influence the verdict: it changes the input, never the logic.

| Button | Scenario file |
|---|---|
| Correct cycle | `cycle_correct_01.mp4` |
| Skip bolt 3 | `fault_missing_bolt.mp4` |
| Adjacent order | `fault_adjacent_order.mp4` |
| Interrupt and resume | `fault_interrupt_resume.mp4` — the hero fault |
| Wheel off-axis | `fault_wheel_off_axis.mp4` |
| Tool never picked | `fault_no_tool.mp4` |
| Hand occlusion | `edge_hand_occlusion.mp4` — must **not** alert |
| Second person | `edge_second_person.mp4` — must **not** alert |
| Switch profile | loads the door-trim profile and its scenarios into the same running engine |

#### Station grid — `/line`

Four tiles, each with station state, cycle progress, and live latency. One summary line: first-time
right for the line, and the most-missed step.

#### Node-RED tag monitor — `localhost:1880/ui`

Three lamp indicators and a conveyor-hold indicator bound to the soft PLC's OPC-UA tags. Displayed
beside the dashboard during the demo. It is visually plain on purpose: it should look like the
SCADA screen a line engineer already knows.

#### Fleet view — `/fleet`

| Element | Rule |
|---|---|
| Station matrix | one row per station: engine status, model version, CAD revision, then the six signals |
| Signal cell | 24 h sparkline with the centre line and control limit drawn. Colour carries state only: neutral in control, amber in drift, dotted grey while learning a baseline, red when offline |
| Attention list | open drift episodes, oldest first, each with the playbook's likely cause and action |
| Canary tag | on any station running a model version the rest of the line is not |
| History banner | seeded synthetic history is labelled as synthetic, on screen, whenever it is shown |

A cell never shows a raw number without its limit. A value means nothing to a maintenance engineer
until it is drawn against that station's own normal.

---

## 22. Prototype build plan

### Demo topology

```
HSGPC (AC power, clocks locked) — everything below started by supervisor.exe (:8099, Section 33)
├─ softplc.exe            opc.tcp://127.0.0.1:4840
├─ api (outbox)           http://127.0.0.1:8000     /  /hmi/W-14  /harness  /line — the built dashboard
├─ node-red               http://127.0.0.1:1880/ui
├─ sidecar                http://127.0.0.1:8001     Florence-2 + clip writer
├─ torquesense.exe --station W-14   ws://127.0.0.1:8080  mjpeg :8090  control :8095
└─ fleet-agent.exe -station W-14    pushes to the API; probes :8095; listens on nothing
Projector: dashboard on screen 1 · HMI kiosk on screen 2 if available
```

### The engine contract

Everything crossing a process boundary is defined once in `contracts/`, as JSON Schema, and
generated into C++ structs and TypeScript types. Contract drift becomes a compile error.

```jsonc
// contracts/state.schema.json — engine → dashboard, ~15 Hz over WebSocket
{
  "station_id": "W-14",
  "frame_seq": 18233,
  "cycle_id": "3f9c…",
  "step": "S4",
  "profile": "wheel-fitment-rl",
  "ips": [ { "id": "bolt-1", "family": "threaded_fastener", "state": "fastened", "conf": 0.97, "u": 824, "v": 576 }, … ],
  "next": ["bolt-3"],
  "latency": { "engine_ms_p50": 24.9, "engine_ms_p99": 34.6, "evidence_ms_p99": 69.3 },
  "verdict": null
}

// contracts/anomaly.schema.json — engine → API, once per violation
{
  "ts": "2026-…Z", "station_id": "W-14", "cycle_id": "3f9c…",
  "profile": "wheel-fitment-rl", "step_id": "torque", "violation_type": "SEQUENCE",
  "observed_ip": "bolt-2", "expected_ip": "bolt-3",
  "engine_ms": 33.1, "evidence_ms": 67.8,
  "ring_end_seq": 18233,
  "narrative": "Bolt 2 torqued before bolt 3. Expected 1 → 3 → 5 → 2 → 4. …",
  "cad_revision": "C", "model_version": "yolo11n-w14-s20260721"
}
```

### Forty-hour build order — one engineer

| Hours | Work | Gate |
|---|---|---|
| 0–5 | FreeCAD assembly; STEP + glTF export; MSYS2 `pacman` install of the C++ libraries (minutes) | five fasteners at 114.3 mm PCD; `cmake --preset windows-release` configures |
| 5–10 | `synth/generate.py`; 4,000 renders + 10 scenarios; 40 real photos labelled | I9 reproducibility test passes |
| 10–14 | Colab training: hands/tools detector and the `threaded_fastener` classifier; ONNX export; local ORT smoke test | both mAP figures reported (I10); CUDA EP confirmed |
| 14–20 | `SpscRingBuffer`, both frame pools, `StateVector`; OCCT reader, manifest join, pattern solvers, resolution check | TSan torture test; solver property tests; wheel `coverage.json` clean |
| 20–26 | Generic `ProcedureEngine` loading `procedure.json`; differential test against `reference/tsref`; `OpcUaAlerter`; soft PLC | reference and C++ agree on every wheel scenario (I16); `NoAllocationTest` green |
| 26–30 | Pipeline orchestration, pinning, HdrHistogram; `SharedClipRing` | **I4 latency gate** |
| 30–34 | Sidecar: Florence-2, clip writer; Docker stack; FastAPI | anomaly row + clip land end to end |
| 34–38 | Dashboard four zones, HMI, harness, station grid, Node-RED flow | demo runbook passes once |
| 38–40 | Four-station mode; rehearse four times; record fallback capture | GO on the demo checklist |

Hour 0 used to start an hour-long vcpkg build of OpenCV and Open CASCADE. MSYS2 installs both
prebuilt, so the first configure happens in the first hour instead of the second.

### Operations track — twelve more hours

Build the hackathon core first. The operations track must never delay the demo; it can be built
before or after it.

| Hours | Work | Gate |
|---|---|---|
| 40–43 | Engine health buckets; heartbeat on the alert thread; soft PLC watchdog; HMI offline state | I13: AMBER within 2.5 s of the last beat |
| 43–46 | Health tables, EWMA drift job, `/fleet`, seeded history with one drifting station | 7 EWMA tests; drift visible on `/fleet` |
| 46–48 | Go fleet agent with spool | `go test -race`; outage test delivers every record in order |
| 48–50 | Jenkins controller, Windows agent, `Jenkinsfile` | one green Jenkins build with the latency histogram archived |
| 50–52 | Dockerfiles, Kustomize base and overlays, `k8s-smoke` job | green in GitHub Actions |

Write the ADR for each decision as you make it, not afterwards — `docs/adr/0001-remove-tensorrt.md`,
`0002-garage-not-minio.md`, `0003-florence-local.md`, `0004-jenkins-for-gpu-gates.md`,
`0005-ewma-limits-by-simulation.md`, `0006-plc-owns-fail-visible.md`,
`0007-soft-plc-default-server-url.md`, `0008-supabase-without-timescaledb.md`,
`0009-tensorrt-through-ort-gated.md`. Each one is a page: context, decision, consequences. ADR 0009
supersedes 0001 rather than editing it: the record of why TensorRT left, and why it came back, is
the point.

### Profiles track — ten more hours

Proves the engine is general. Build it after the hackathon core, never instead of it.

| Hours | Work | Gate |
|---|---|---|
| 52–55 | Door-trim CAD in FreeCAD — panel, six clips, connector; manifest; compile | `coverage.json` clean |
| 55–58 | `snap_clip` and `connector` renders and classifiers; door scenario fixtures | both mAP figures for each new family |
| 58–60 | The door profile end to end in the same engine; harness "switch profile" | panel over an open clip caught as `PRECEDENCE` |
| 60–62 | Windshield and enclosure: manifests, compile, scenario event streams through both interpreters | the two implementations agree; no perception models built |

The last row is deliberately cheap. Two more assemblies verified at the procedure level show the
engine is not wheel-shaped, without the cost of two more perception stacks. Say exactly that if asked.

### TensorRT track — three more hours

Optional, and after the hackathon core: the demo meets its budget on the CUDA provider.

| Hours | Work | Gate |
|---|---|---|
| +1 | TensorRT zip, `PATH`, `gpu_preflight.py`, `trt_check.exe`, `trtexec` on the stock detector | gate G2, TensorRT half |
| +2 | `execution_provider: tensorrt` in the detector; `--build-trt-cache`; `/health` reports the provider | engine starts from the cache; refuses to start without one |
| +3 | `ProviderParity`; `latency_test` once per provider; VRAM under TensorRT | the gate decided either way, and recorded in `VALIDATION.md` |

### Production track — eight more hours

Turns the demo into a station that runs without you (Section 33). Build it last, after the demo
works by hand; it changes how the software runs, never what it decides.

| Hours | Work | Gate |
|---|---|---|
| +2 | `supervisor.json` for this laptop; `register-station.ps1`; Autologon | power-cut test: green with no key pressed |
| +1 | API serves `web\dist`; Node-RED native from `tools\node-red` | demo works with Docker Desktop quit |
| +2 | `requirements.lock`; tag a release; `release.yml` green with its attestation | draft release with zip and `SHA256SUMS` |
| +2 | `Jenkinsfile.release` on the laptop; install and roll back once | release published with `gates.json`; rollback in about a minute |
| +1 | watchdog variables; nightly backup; one restore drill | watchdog green; a restored dump |

### Desk track — the prototype on your own cameras, ten hours

This is what turns "any assembly" from a slide into something a judge can try on the table. If the
demo is live, build it before the operations track.

| Hours | Work | Gate |
|---|---|---|
| 62–64 | `UvcSource` and `StreamSource` with settings read-back; `scripts/calibrate.py`; print the sheets | both cameras calibrated, ≤ 0.5 px RMS; the engine logs the resolutions it asked for |
| 64–66 | `torquesense --conformance samples`: every trace of every sample through the C++ engine | C++ and reference agree on all 20 traces (I16) |
| 66–70 | Build both desk kits; `scripts/teach.py`; teach `threaded_fastener`, `snap_clip`, `connector`, `panel`, `label` on real crops | each taught family passes its gate on held-out crops |
| 70–72 | Record every trace on camera into `media/`; `--measure-glass` for both cameras | each recording raises exactly its trace's violations |

### The demo pack

| Item | Location |
|---|---|
| 10 scenario videos + `.expected.json` | `data/scenarios/` |
| Frozen ONNX model, tagged | `models/yolo11n_wheel.onnx` |
| Wheel profile, revision C | `samples/wheel-hub-5lug/` |
| Door-trim profile with its traces | `samples/door-trim-harness/` |
| Desk kit: the junction box, screwdriver, labels; the clip board, card cover | in the bag, with the printed sheets |
| Phone: charged, IP Webcam installed, hotspot named, focus locked at 350 mm; a stand | `config/station_desk1.yaml` holds its URL |
| Four-lug revision for the live reconfiguration | `cad/wheel_assembly_4lug.FCStd` |
| Fallback screen recording of a perfect run | `data/demo_fallback.mp4` |
| Seeded 30-day history for the analytics zone | `scripts/seed_history.py` |

### Offline insurance

Verification never needs the internet. The camera, the engine, the soft PLC, the HMI and
Florence-2 are all on the laptop. The weights are cached in `%HF_HOME%`, fonts are self-hosted,
and Docker images are pulled in advance. The network carries only the record to Supabase, and the
outbox holds it while the network is away. Prove both in the final rehearsal:

1. **Pull the network mid-cycle.** Alerts and the HMI carry on, and `/health` shows the outbox
   count rising.
2. **Restore the network.** The count drains to zero, and the Vercel page fills in the gap in order.

### Repository layout

```
C:\dev\torquesense\
├── AGENTS.md                          spec for coding agents (Appendix B)
├── CMakeLists.txt  CMakePresets.json  cmake\deps.cmake
├── .env.example    docker-compose.yml    Jenkinsfile
├── .github\
│   ├── workflows\ci.yml
│   └── pull_request_template.md       build phase · invariants touched · gate output pasted
├── cad\
│   ├── wheel_assembly.FCStd           5-lug, revision C
│   ├── wheel_assembly_4lug.FCStd      for the live reconfiguration demo
│   └── export\                        .step  .gltf  (generated)
├── contracts\                         JSON Schemas → C++ and TS types
├── samples\                           one folder per assembly · run_samples.py · _template\ · _print\ (Section 12)
├── third_party\open62541\            win32\ and posix\ amalgamations of 1.4.21
├── reference\                         Python · tsref: interpreter, compiler, pattern solvers, optics
├── cad-extract\                       C++ · OCCT reader, StarPatternSolver, ProcedureEmitter
├── perception\                        C++ · SpscRingBuffer, FramePool, YoloOnnxDetector, Tracker
├── procedure\                         C++ · ProcedureEngine, Verdict, IAlerter, OpcUaAlerter,
│                                            TemplateNarrator, XlsxReporter
├── ipc\                               C++ · SharedClipRing (Win32 + POSIX backends)
├── control-app\                       C++ · main, Pipeline, AlertThread (heartbeat), Affinity,
│                                            IFrameSource, ReasoningClient, HealthBuckets
├── softplc\                           C++ · open62541 server, heartbeat watchdog · node-red\flows.json
├── fleet-agent\                       Go · main.go, internal\probe (engine, gpu, disk, spool)
├── sidecar\                           Python · Florence-2 adjudicator, clip writer, FastAPI
├── synth\                             Python · BlenderProc generation, scenario sequences
├── training\                          Python · colab_train.ipynb, eval.py, dataset.yaml
├── api\                               Python · FastAPI, fleet\ewma.py + job.py
├── docker\                            engine · api · fleet-agent Dockerfiles
├── deploy\k8s\                        base\  overlays\kind\  overlays\edge\
├── supabase\
│   ├── config.toml                    from `npx supabase init`
│   └── migrations\                    events · fleet_health · access_and_jobs (Section 16)
├── web\                               React · dashboard, /hmi, /harness, /line, /fleet
├── tests\
│   ├── latency\  soak\  scenarios\    C++ integration tests
│   ├── fixtures\detections\           per-scenario detector output, for ReplayDetector
│   ├── plc\test_heartbeat.py          I13
│   ├── k8s\smoke.py                   run by CI against kind
│   └── e2e\                           Playwright
├── scripts\                           bootstrap.ps1  start-dev.ps1  start-jenkins-agent.ps1  watchdog.py
│   ├── station\                       install-release · rollback-release · register-station · kiosk · backup
│   └── release\                       bundle.py  gates.py
│                                      seed_history.py  gen-contracts.sh  gpu_preflight.py
├── data\                              DVC-tracked: synth\  scenarios\  real_val\
├── models\                            DVC-tracked · manifest.json committed
└── docs\
    ├── VALIDATION.md                  both mAP figures, latency histograms, scenario matrices, drift calibration
    ├── adr\                           0001-remove-tensorrt.md … 0009-tensorrt-through-ort-gated.md
    ├── functional-specification.md
    └── test-scenarios.md
```

---

## 23. Demo script

Five minutes, rehearsed to the second.

| Time | On screen | Line |
|---|---|---|
| 0:00 | Slide: the warranty claim | "Steering shudder eleven months after assembly. Root cause: five bolts torqued in the wrong order. The nutrunner logged five good rundowns. Every check passed." |
| 0:25 | Slide: torque arms | "Position arms solve this — per station, per variant, with hardware. We solve it with a camera that reconfigures from CAD." |
| 0:45 | FreeCAD → run the compiler | "We don't write the inspection rules. We compile them from the assembly." |
| 1:05 | `samples/wheel-hub-5lug` → `procedure.json` | "Star pattern derived from bolt geometry. Nobody typed this." |
| 1:15 | Grid of rendered frames | "The same model generates the training data. Four thousand frames, zero manual labels." |
| 1:35 | Correct cycle; HMI green; Node-RED green | *Silence. Let it run.* |
| 2:10 | **Hand the laptop to a judge**: "Pick a fault." | — |
| 2:20 | Red floods the HMI; Node-RED `HoldReq` true | "Seventy milliseconds from the first frame showing that bolt. And that's real OPC-UA — point it at the station PLC and the same binary drives the tower." |
| 2:45 | Anomaly card | Read the narrative aloud. "That sentence comes from the state machine, not a language model — it's a fact, not a guess." |
| 3:05 | Modal: CAD view, bolt strip | "Florence-2 runs locally beside the detector, confirming bolts the detector isn't sure about. Nothing here touches the cloud." |
| 3:20 | `/fleet`, four stations | "One laptop, four stations — and it watches itself. This is a month of seeded history: W-16's lens degraded, and its confidence chart crossed the control limit before a single false alarm." |
| 3:40 | Close W-15's engine window | "A quality gate that silently stops checking is worse than none." W-15's lamp in Node-RED goes AMBER in about two seconds — set by the PLC, not the engine — and `/fleet` marks it offline. |
| 4:00 | **Live, on the table:** the phone on its stand over the junction box. Hand the judge the screwdriver | "Your problem statement asked about any assembly. This one we measured with a ruler this morning — no CAD." The judge tightens round the box; `SEQUENCE`, red, on a real camera. "Same binary. A new assembly is a folder." |
| 4:20 | Slide: business | "We verify sequence, not torque — the nutrunner keeps that job. We sit beside it." |
| 4:40 | Close | "The CAD model is the single source of truth — for what the line should do, and for what the camera should see." |

### What this does not do yet

State these before the judges find them. It converts weaknesses into a roadmap.

1. **It does not measure torque.** Tier 2 reads the nutrunner's per-rundown result over OPC-UA and
   joins it to the bolt identity.
2. **The torque-mark class is validated on synthetic data only.** Real validation needs forty photos
   of a real hub with witness marks.
3. **It has run on one GPU class.** Edge deployment on an industrial PC or Jetson is designed (the
   Linux build exists for the sanitizers) but not demonstrated.
4. **The drift limits are calibrated on simulated data.** Real health data is autocorrelated, so
   the false-alarm rate is re-measured, and L recalibrated, after two weeks on a real line.
5. **Two of the four profiles stop at the procedure level.** Windshield and enclosure are compiled
   and verified by both interpreters on scripted event streams; their perception models are not built.
6. **The occlusion test is designed, not run.** The resolution, framing and facing checks are tested;
   ray-casting against the CAD is the C++ compiler's job and has not been exercised.
7. **The live cameras are a phone and a webcam.** Both are rolling-shutter. The phone's Wi-Fi adds
   latency the engine's own clock does not see, so it is measured separately (`--measure-glass`).
   Their accuracy is stated per camera, never borrowed from the rendered set. The industrial camera
   is designed and deferred (Appendix D.1).

---

## 24. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| ONNX Runtime silently falls back to CPU | **High** on CUDA 13 | Detector at ~3 fps, no error | Download the `gpu_cuda13` asset; startup check fails loudly if `CUDAExecutionProvider` is missing (Section 30) |
| TensorRT 11 installed instead of 10 | **High** with an unpinned `pip install tensorrt-cu13` | TensorRT provider fails to load | Install the 10.13.3.9 zip; `gpu_preflight.py` fails if `nvinfer_10.dll` is not on `PATH` (Section 28) |
| TensorRT engine built on stage | Certain without a cache | A start-up of minutes, in front of judges | `--build-trt-cache` during setup; a missing cache refuses to start (Section 30) |
| FP16 changes a verdict | Low | A different event from the same frames | Parity gate: identical scenario events under both providers, or the demo stays on CUDA |
| TensorRT memory outside the arena cap | Medium in four-station mode | VRAM over the I7 budget | Workspace capped at 256 MB; VRAM re-measured under TensorRT before I7 is claimed |
| Power telemetry wrong on this driver | **Certain** — 752 W reported against 130 W | A misleading figure in `VALIDATION.md` | Record clocks and temperature, not power; `gpu_preflight.py` flags it |
| cuDNN DLL not found | Medium | CUDA EP fails to load | `where cudnn64_9.dll` in the gate; add its directory to `PATH` |
| First inference takes ~1 s | **Certain** | Pollutes latency stats; first demo frame stalls | 20 warm-up inferences before the engine reports ready (Section 34) |
| Laptop GPU throttles | High | p99 doubles | AC power, clocks locked, cooling pad; `ThrottleDetection` warns if median inference exceeds 20 ms |
| Space in user path | **Certain** if ignored | CMake, MSYS2 tools, Docker mounts fail with misleading errors | Everything under `C:\dev\torquesense` |
| `python` on `PATH` is MSYS2's, not python.org's | **High** — GDB depends on MSYS2's Python | Packages installed into the wrong Python; torch without CUDA | Create the venv with `py -3.12`; only ever run the venv's `python` (Section 26) |
| Windows Defender scans 4,000 renders | High | Rendering and builds 2–3× slower | Exclusion for `C:\dev\torquesense` (Section 28) |
| WSL2 takes half the RAM | High | Pipeline pages to disk | `.wslconfig` cap at 4 GB |
| Node-RED cannot reach the soft PLC | Low | Tag monitor blank | Node-RED is native now: connect it to `opc.tcp://127.0.0.1:4840` and leave the server URL at its default — the server echoes it back (Section 29). No firewall rule is needed |
| Florence-2 OOM beside the detector | Low | Sidecar crashes | ORT arena capped; torch memory fraction capped; circuit breaker keeps the engine running |
| Sim-to-real gap | Medium | Judge asks about real pixels | Report both mAP figures; state the torque-mark limit honestly |
| BlenderProc phase overruns | Medium | Compresses everything after | Render 1,000 frames first, train, then render the rest while integrating |
| Colab disconnects mid-training | Medium | Lost epochs | `save_period=5`, train to Drive, resume from `last.pt` |
| Four stations exceed GPU | Medium | Frame drops | 15 fps per station; fall back to three stations |
| Firewall prompt on first start of `softplc.exe` | Medium | A dialog over the kiosk | Answer it during setup (Cancel: every client is local); never for the first time on stage |
| Projector washes out amber | Medium | States indistinguishable | Check tokens on the venue projector |
| Overriding the soft PLC's server URL | — | Server binds nothing and exits | Leave `serverUrls` at its default (Section 29; Appendix A, item 18) |
| Jenkins on port 8080 | Certain if defaulted | Collides with the engine's WebSocket | Jenkins mapped to `127.0.0.1:8081` |
| GitHub self-hosted runner on a public repo | — | A fork's pull request runs code on the laptop | Not used: Jenkins polls `main` only |
| Jenkins plus the platform exceed the WSL cap | Medium | Containers OOM-killed | Jenkins is a compose profile, run in CI mode with the platform stopped (Section 26) |
| Running the Jenkins agent as Administrator to lock clocks | Medium | Every build step gets admin rights | Agent runs as your user; the pipeline records clocks and power instead |
| False drift tickets | Medium | Engineers stop trusting `/fleet` | Limits sized by simulation for the real baseline length; every ticket's outcome logged; L recalibrated after two weeks |
| Chart baselines lost on an API restart | Certain without persistence | 24 h of learning lost | State persisted every bucket; round-trip tested |
| AMBER during engine start-up | Certain | Looks like a fault on stage | Intended — not ready is not OK. Start the stack before the audience is seated |
| An inspection point too small to judge | Certain on some rigs | Unreliable verdicts | The compiler refuses the profile and names the points and the shortfall (I17) |
| A mis-authored manifest | Medium | A wrong order or a missing step enforced at a station | Manifests reviewed in pull requests; schema validation; the scenario gate (I14) |
| A new part family with no training data | Certain for new assemblies | No classifier for its states | Florence-2 cold start, advisory only: the station cannot write to the PLC until the family passes the gate |
| The wrong variant's procedure on a mixed-model line | Medium | False alarms or missed steps | Variant code read at cycle start; an unknown code holds the station AMBER |
| The operator's body hides a point for a whole step | Medium | The step cannot be seen | A second camera position, or corroborating plant evidence (Section 10) |
| The webcam opens at 640 × 480 | **Certain** if not asked | Every inspection point at half its pixels; nothing warns | `UvcSource` asks for 1280 × 720 MJPG and reads it back; a mismatch holds the station AMBER (Section 8) |
| Windows camera privacy blocks desktop apps | Medium | Webcam opens; every read fails | First row of the desk commissioning checklist |
| Phone stream drops — sleep, battery, Wi-Fi | Medium | Frames stop mid-demo | Phone on charge; reconnect every 2 s; the heartbeat makes it AMBER, never silent; the laptop webcam station and the rendered video are the fallbacks |
| Venue network down during the demo | Medium | No cloud dashboard updates | Nothing on the alert path uses the network; the outbox holds every record and drains in order on reconnect |
| Free Supabase project paused after a week idle | Low | Dashboard empty, outbox cannot drain | The hourly watchdog's read keeps it active; the outbox keeps everything until it resumes |
| A process crashes mid-demo | Medium | That station AMBER | The supervisor restarts it with backoff; the HMI is green again in seconds (Section 33) |
| An orphaned engine holds the camera or GPU after a crash | Medium without a job object | The restarted engine cannot open its camera | Every child in a kill-on-close job object; tested with `kill -9` |
| A release that works on the build machine but not on the laptop | Medium | The station does not start after an upgrade | Bundle from import tables; `gpu_preflight.py --bundle` on the supervisor's `PATH`; the switch rolls itself back if `/health` is not 200 in four minutes |
| A tampered or wrong zip installed | Low | Unknown code on the station | SHA-256 plus `gh attestation verify` against this repository; models checked against the attested manifest |
| A release published without GPU validation | — | A slower or different verdict ships | Releases start as drafts; only the laptop's gate publishes them, and the installer refuses a missing or failed `gates.json` |
| Windows Update restarts the laptop on the day | Medium | Demo interrupted | Updates paused (35 days, renewable on this build); and if it restarts anyway, Autologon and the supervisor bring the station back without a key pressed |
| Running degraded on CUDA without noticing | Low | A slower path than intended | DEGRADED banner, `/health` field and fleet record; only gate-validated providers are ever tried |
| Free plan limits — 500 MB database, 1 GB files | Low | Inserts or uploads refused | Retention jobs and clip pruning; check the size query weekly; demo-mode buckets are short-lived |
| A server credential copied into `web/` | Medium if careless | Anyone can write or delete data | Only `VITE_SUPABASE_URL` and the publishable key go in `web/.env.local`; RLS tested to refuse writes from that key |
| Schema edited in the Supabase dashboard | Medium | Repository and database drift apart | Schema changes only as migrations; `npx supabase db diff` before every demo shows nothing |
| Phone or webcam refocuses or re-exposes on a hand | Medium | States misread for seconds | Focus and exposure locked at commissioning; settings read back where the device reports them |
| Rolling-shutter phone and webcam images | Certain | Moving hands skew | Judged parts are stationary when committed (two-frame debounce); accuracy figures stated per camera, not borrowed from renders |
| Hand-built Windows code paths differ under GCC | Low | A Win32 call that compiled with MSVC does not with MinGW | The engine's Windows calls — affinity, timer resolution, EcoQoS opt-out, named shared memory — were compiled with MinGW-w64 while writing this; the `windows-mingw` CI job builds every push |
| A sample that passes as traces fails on camera | Medium | The desk demo misreads | Recordings in `media/` must raise their trace's violations; teach the family on real crops first (Section 12) |

### Turning this into a portfolio artifact

1. **`docs/VALIDATION.md`** — both mAP figures with the sets described, latency histograms at
   p50/p99/p99.9 with the clock defined, the scenario test matrix, and the conditions of measurement.
   Almost nobody writes this file, which is exactly why it is the strongest hiring signal in the repo.
2. **A green CI badge** over the sanitizer matrix. "TSan-clean lock-free ring buffer" is a claim; a
   badge is evidence.
3. **A four-minute engineering walkthrough**, not the pitch: why the alert thread exists, why
   TensorRT is gated rather than assumed, why the narrative is deterministic, why I9 is a PSNR bound and not byte
   equality.

---

# Part V — Setup and operations, on HSGPC

## 25. Setup roadmap

Six phases, five gates. Do not start a phase until the gate before it passes — every gate here
exists because skipping it produces a failure that surfaces hours later looking like a different
problem.

```mermaid
flowchart LR
  P1["1 · Prepare machine<br/>C:/dev path · driver r580+<br/>long paths · Defender exclusion<br/>WSL2 + Docker · .wslconfig"] --> G1{"GPU ok?<br/>nvidia-smi · cc 8.6"}
  G1 -- "fail" --> P1
  G1 -- "pass" --> P2["2 · Install<br/>MSYS2 pacman · cmake/deps.cmake<br/>cuDNN 9 cu13 · ORT gpu_cuda13<br/>TensorRT 10.13 zip<br/>Python 3.12 venv · torch cu130<br/>Go · Temurin 21"]
  P2 --> G2{"GPU stack live?<br/>gpu_preflight · ort_check · trt_check<br/>trtexec · torch.cuda"}
  G2 -- "fail" --> P2
  G2 -- "pass" --> P3["3 · Configure<br/>.env · compose · Supabase db push<br/>station_w14.yaml · soft PLC hostname"]
  P3 --> P4["4 · Integrate<br/>contracts → types · shared ring<br/>sidecar · OPC-UA · WebSocket"]
  P4 --> G3{"Hour-one contract green?"}
  G3 -- "fail" --> P4
  G3 -- "pass" --> P5["5 · Test<br/>GoogleTest · WSL2 TSan/ASan · go test<br/>latency gate · pytest · Playwright<br/>Jenkins GPU run · kind smoke in CI"]
  P5 --> G4{"All gates pass?"}
  G4 -- "fail" --> P5
  G4 -- "pass" --> P6["6 · Deploy<br/>freeze model · tag · rehearse<br/>fallback capture · network pulled"]
  P6 --> T0(["Tier 0 — laptop demo"])
  P6 --> T1(["Tier 1 — public replay link"])
  P6 -.-> T2(["Tier 2 — station edge · pitched"])
  classDef gate fill:#FBF3E6,stroke:#B26B00;
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class G1,G2,G3,G4 gate;
  class P6,T0 key;
```

*Figure 5 — Setup roadmap. Gate G2 is the one specific to your CUDA 13.2 installation: it proves
the ONNX Runtime CUDA execution provider — and TensorRT beside it — actually load and run on the GPU,
because the most likely failure on this machine is a build that silently runs on the CPU instead.*

---

## 26. Your machine — compatibility verdict

Every component clears its requirement. Five need action before you start — Java, Go, TensorRT, the
venv's Python and the project path — and none needs an upgrade.

| Component | Yours | Requirement | Verdict |
|---|---|---|---|
| OS | Windows 11 Home 26H2, build 26300.9711 | Docker Desktop on Home uses the WSL2 backend; WSL2 available | ✅ **pass** |
| CPU | i7-11800H, 8C/16T, AVX2 + AVX-512 | Pipeline needs 4 free physical cores | ✅ **pass** cores 4–7 pinned |
| RAM | 16 GB (15.7 usable) | Develop mode ~12 GB | ⚠️ **tight** see memory plan |
| GPU | RTX 3060 Laptop, 6 GB, Ampere cc 8.6 | CUDA 13 supports Ampere | ✅ **pass** VRAM budgeted in §13 |
| NVIDIA driver | **596.21**, reports CUDA 13.2 | CUDA 13.x and TensorRT's CUDA 13 build require **R580 or later** | ✅ **pass** |
| TensorRT | not installed | 10.13.3.9, CUDA 13.0 zip — the release ONNX Runtime's TensorRT provider links | ⚠️ **install** (Section 28) |
| CUDA Toolkit | 13.2, V13.2.78 | ONNX Runtime CUDA 13 builds need ≥ 13.0 | ✅ **keep** do not downgrade |
| Python | 3.12.0, python.org, per user | PyTorch cu130 wheels support 3.12 | ✅ **pass** upgrade the patch |
| C++ compiler | GCC 16.2.0, MSYS2 | C++20; must link ONNX Runtime's MSVC-built package | ✅ **keep** — that link was built and run while writing this |
| Debugger | GDB 18.1, MSYS2 | DWARF debug info from GCC | ✅ **keep** |
| `python` on `PATH` | probably MSYS2's own (GDB depends on it) | the venv must be python.org's 3.12 | ⚠️ **use `py -3.12`** |
| Java | not installed | Jenkins inbound agent needs Java 17 or later | ⚠️ **install** Temurin 21 |
| Go | not installed | Fleet agent's module declares Go 1.24 or later | ⚠️ **install** current Go |
| kubectl, kind | ships with Docker Desktop / not needed | the kind smoke test runs in GitHub Actions, not on the laptop | ✅ **nothing to do** |
| Storage | ~356 GB free | MSYS2 packages ~5 GB · renders ~6 GB · Docker ~5 GB · Florence ~1 GB | ✅ **pass** |
| Cameras | built-in webcam · Android phone | `UvcSource` at 1280 × 720 MJPG; `StreamSource` at 1080p over the phone's hotspot | ✅ **use both** — Section 8 |
| User path | `C:\Users\Gurjas Gandhi` | — | ❌ **move** the space breaks tools |

### Five things specific to this machine

**1 · The space in your user path will bite you.** CMake, some MSYS2 tools, Docker bind mounts
and shell scripts mishandle `C:\Users\Gurjas Gandhi`. Put the project at `C:\dev\torquesense`,
BlenderProc's Blender at `C:\dev\tools\blender`, and the
Hugging Face cache at `C:\dev\hf-cache`. Every tool that would default into your home directory gets
an explicit path in Section 29. This costs nothing now and an afternoon later.

**2 · Keep CUDA 13.2 — and add TensorRT through ONNX Runtime, not beside it.** The ecosystem
supports CUDA 13 now: ONNX Runtime ships stable CUDA 13 builds from 1.27, and PyTorch publishes
cu130 wheels from 2.11. Downgrading buys nothing — CUDA 13's improvements target Blackwell, and your
Ampere GPU runs identically on either branch. Your driver, 596.21, clears the R580 floor that CUDA 13
and TensorRT's CUDA 13 build share, so nothing on this machine needs upgrading.

TensorRT comes in through ONNX Runtime's TensorRT execution provider, which the CUDA 13 package you
already install contains. That fixes the version: the provider links TensorRT **10**, so the install
is TensorRT 10.13.3.9 for CUDA 13.0 — not the TensorRT 11 an unpinned pip install now brings. It also
keeps TensorRT out of your GCC build entirely. The CUDA provider's ~14 ms for YOLO11n already meets
the budget, so TensorRT is adopted only if a parity and latency gate says it earns its place
(Section 30).

> **Your `nvidia-smi` shows 752 W against a 130 W limit.** A laptop GPU cannot draw that; the sensor
> reading is wrong, not the GPU. Treat power figures from this driver as unreliable: the latency
> pipeline records clocks and temperature, and `gpu_preflight.py` flags any reading above the
> board's own limit.

> **The most likely install failure on this machine.** The CUDA 13 ONNX Runtime asset is named
> `onnxruntime-win-x64-gpu_cuda13-<ver>.zip`. The similarly named `…-gpu-<ver>.zip` is the CUDA 12
> build. It loads, cannot find the CUDA 12 runtime, and the detector runs on the CPU. Gate G2 exists
> to catch this.

**3 · Python 3.12.0 is the first 3.12 release.** It works, but install the latest 3.12 patch for
bug and security fixes. Same minor version, so every wheel in this document still applies. Do not
jump to 3.13 or 3.14 mid-project.

**4 · This is a hybrid-graphics laptop — which helps.** The Intel UHD drives the display, so the
desktop, the browser and the projector output do not consume the RTX 3060's VRAM. Nearly all 6 GB
is available for compute. What matters is power: laptop GPUs throttle heavily on battery and when
hot. Every latency number in this document assumes AC power, Windows power mode **Best performance**,
and GPU clocks locked (Section 29).

**5 · Windows 11 Home has no Group Policy editor, and does not need one.** Everything this build
configures — long paths, Defender exclusions, power mode, camera privacy — is done from PowerShell
or Settings.

**6 · There are two Pythons on this laptop.** MSYS2's GDB depends on MSYS2's own Python, so
`C:\msys64\ucrt64\bin\python.exe` is very likely on your `PATH`, possibly ahead of the python.org
3.12 in your Start menu. `where.exe python` shows which comes first. It does not matter which,
provided you never install packages into either directly. Create the venv with the `py` launcher,
which only knows python.org installs (`py -3.12 -m venv .venv`), and from then on use only the
venv's `python`. uv has added a third, CPython 3.14.4 (`py -0p` lists it as `-V:Astral/CPython3.14.4`);
`py -3.12` still selects the right one, so name the version every time.

### Memory plan — four operating modes

16 GB holds the whole stack, but not the whole stack *and* a render or a local training run. Run
one mode at a time. Figures are estimates; confirm in Task Manager.

| Mode | Running | Stopped | Approx. RAM |
|---|---|---|---|
| **Render** | BlenderProc, one terminal | Docker, sidecar, browser, IDE | Blender 4–6 GB |
| **Develop** | VS Code, engine (debug), sidecar, Docker at 4 GB cap, Vite, one browser | — | 12–13 GB |
| **Demo** | The installed release under its supervisor: engine, soft PLC, sidecar, API, Node-RED; Edge kiosk and one browser tab | Docker, IDE, chat apps, OneDrive sync | ~8–9 GB — no WSL2 VM |
| **Line** | Demo, with four engine instances and four fleet agents | — | ~11.5 GB |
| **CI** | Jenkins controller (Docker, 1 GB cap), Jenkins agent, one build-and-test run | Platform containers, browser, IDE | ~7–9 GB while compiling |

> **The single biggest saving: Docker's cap.** Without `.wslconfig`, WSL2 may claim up to half your
> RAM, and Docker Desktop will hold it even when containers are idle. Capping WSL2 at 4 GB is the
> difference between Develop mode fitting and Windows paging the engine to disk mid-benchmark.

---

## 27. Compute — what the Colab T4 is for

**Training YOLO11n. Nothing else.** Not inference, not the vision-language model, not rendering.

### Why train on Colab when the 3060 could do it

The 3060 *can* train YOLO11n — at 640 px and batch 16 it fits comfortably in 6 GB. An earlier draft
said the laptop GPU would thrash; that was wrong, and it is corrected in Appendix A. The real
reasons to use Colab are scheduling and heat, not capability:

| Reason | Detail |
|---|---|
| **Parallelism** | While Colab trains for two to three hours, the laptop renders scenario videos and you write C++. On one GPU those two jobs serialise. |
| **Thermals** | A multi-hour training run on a laptop holds the GPU at its thermal limit. You want the laptop cool and its clocks stable for latency measurement afterwards. |
| **Throughput is comparable** | A T4 and an RTX 3060 Laptop are in the same FP16 class for a model this small. Colab is not faster; it is *elsewhere*. |

If Colab is unavailable, train locally overnight with `batch=16, imgsz=640, workers=4` — same
result, same `ultralytics` command.

### Why the vision-language model does not run on Colab

An earlier version of this design served Qwen2.5-VL from a Colab T4 through a tunnel. Florence-2
replaced it precisely because it fits locally: a 230M-parameter model needs roughly 0.46 GB for
fp16 weights and about 1.2–1.5 GB as a whole process. Removing the tunnel removed the single
largest demo-day risk — a free Colab session idling out in the middle of a presentation.

### The Colab training notebook

```python
# training/colab_train.ipynb — run on a T4 runtime
!pip -q install ultralytics
from google.colab import drive; drive.mount('/content/drive')

ROOT = '/content/drive/MyDrive/torquesense'
!cp {ROOT}/synth_w14_s20260721.zip /content/ && unzip -q /content/synth_w14_s20260721.zip -d /content/data

from ultralytics import YOLO
model = YOLO('yolo11n.pt')
model.train(
    data='/content/data/dataset.yaml',
    epochs=100, imgsz=640, batch=32,        # T4 has 16 GB; 32 fits
    close_mosaic=10, patience=25,
    cache='disk',                            # decode JPEGs once, not every epoch
    project=f'{ROOT}/runs', name='w14',
    save_period=5,                           # survive a Colab disconnect
    seed=20260721, deterministic=True,
)

best = YOLO(f'{ROOT}/runs/w14/weights/best.pt')
best.export(format='onnx', opset=17, imgsz=640, simplify=True, dynamic=False)
```

If the session disconnects, resume with `YOLO(f'{ROOT}/runs/w14/weights/last.pt').train(resume=True)`.
Copy `best.onnx` to `C:\dev\torquesense\models\yolo11n_wheel.onnx`. The ONNX file is portable across
GPUs; there is no engine to rebuild on the laptop.

---

## 28. Installation

In this order. Each step's verification is the gate for the next. All blocks are PowerShell.

### 1 · Machine preparation — PowerShell as Administrator

```powershell
# Project root outside the user folder (the space in "Gurjas Gandhi" breaks tools)
New-Item -ItemType Directory -Force C:\dev\torquesense, C:\dev\tools, C:\dev\hf-cache

# Long paths: node_modules and CMake's FetchContent trees exceed 260 characters
New-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem `
  -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force

# Defender: real-time scanning of 4,000 renders and a C++ build costs hours
Add-MpPreference -ExclusionPath C:\dev

# WSL2 for Docker and the sanitizer builds
wsl --install -d Ubuntu-24.04      # skip if already installed
wsl --update

# GPU and driver — gate G1
nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv
```

Expect `NVIDIA GeForce RTX 3060 Laptop GPU`, a driver of **580.xx or higher**, and `8.6`. Yours
reported 596.21 on 16 October 2026: gate G1 passes. If the
driver is older, install the current Game Ready or Studio driver from NVIDIA before anything else. A
CUDA 13 toolkit on a pre-580 driver fails at runtime with errors that look like library problems.

Then set Windows **Settings → System → Power → Power mode → Best performance**.

### 2 · Toolchain — PowerShell as a normal user

```powershell
winget install --id Git.Git -e
winget install --id OpenJS.NodeJS.LTS -e
winget install --id Docker.DockerDesktop -e
winget install --id FreeCAD.FreeCAD -e
winget upgrade --id Python.Python.3.12 -e          # 3.12.0 → latest 3.12 patch

git config --global core.longpaths true
node --version          # needs 22.12 or newer for current Vite
py -3.12 --version
```

Start Docker Desktop once, accept the WSL2 backend (the only option on Windows Home), then close it
until Section 29 caps its memory.

**The C++ toolchain is already here.** Your `g++` 16.2.0 and `gdb` 18.1 come from MSYS2. Confirm
which MSYS2 environment they belong to, because the libraries must come from the same one:

```powershell
where.exe g++          # expect C:\msys64\ucrt64\bin\g++.exe
g++ -dumpmachine       # x86_64-w64-mingw32
```

If the path says `mingw64` rather than `ucrt64`, install the UCRT64 toolchain and put
`C:\msys64\ucrt64\bin` first on your user `PATH` instead. UCRT64 is MSYS2's default environment,
and it uses the same C runtime as ONNX Runtime's Windows binaries. Do not mix the two
environments: their DLLs share names.

### 3 · CUDA 13 runtime libraries

You already have the CUDA 13.2 toolkit. Two more pieces:

**cuDNN 9.x for CUDA 13** — from <https://developer.nvidia.com/cudnn>, choose Windows, x86_64, and
the **CUDA 13** variant. Then find where the DLL landed and confirm it is on `PATH`:

```powershell
Get-ChildItem "C:\Program Files\NVIDIA\CUDNN" -Recurse -Filter cudnn64_9.dll | Select FullName
where.exe cudnn64_9.dll        # must print a path; if not, add that folder to the user PATH
where.exe cudart64_13.dll      # CUDA 13 runtime — from your toolkit
where.exe cublas64_13.dll
```

If `cudart64_13.dll` is not found, add the directory that contains it under `%CUDA_PATH%` to your
user `PATH`. Find it with `Get-ChildItem $env:CUDA_PATH -Recurse -Filter cudart64_13.dll`.

**ONNX Runtime, CUDA 13 build** — from <https://github.com/microsoft/onnxruntime/releases>, take the
newest release that has an asset named **`onnxruntime-win-x64-gpu_cuda13-<version>.zip`** — 1.28.0
was the one tested with GCC while writing this:

```powershell
$ort = "1.28.0"   # tested with MinGW-w64 GCC; a newer gpu_cuda13 release is fine
$zip = "onnxruntime-win-x64-gpu_cuda13-$ort.zip"
Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$ort/$zip" -OutFile "$env:TEMP\$zip"
Expand-Archive "$env:TEMP\$zip" C:\dev\tools
Rename-Item "C:\dev\tools\onnxruntime-win-x64-gpu_cuda13-$ort" C:\dev\tools\onnxruntime
```

Not the `…-gpu-<version>.zip` asset. That one is the CUDA 12 build.

**TensorRT 10.13.3.9, CUDA 13.0 build — for the TensorRT execution provider.** ONNX Runtime's CUDA 13
package already contains `onnxruntime_providers_tensorrt.dll`. That DLL links the TensorRT **10** ABI
(`nvinfer_10.dll`, `nvonnxparser_10.dll`), and ONNX Runtime's own Windows CUDA 13 pipeline builds and
tests it against TensorRT 10.13.3.9 for CUDA 13.0. Install exactly that release. It runs on your 13.2
toolkit under CUDA's minor-version compatibility, and your driver (596.21) clears its R580 floor.

From <https://developer.nvidia.com/nvidia-tensorrt-10x-download> (free NVIDIA developer sign-in),
download **`TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip`**, then:

```powershell
Expand-Archive "$env:USERPROFILE\Downloads\TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip" C:\dev\tools
Rename-Item C:\dev\tools\TensorRT-10.13.3.9 C:\dev\tools\TensorRT
# PATH is set with the other user variables in Section 29; open a new terminal afterwards
where.exe nvinfer_10.dll                 # C:\dev\tools\TensorRT\lib\nvinfer_10.dll
trtexec --help | Select-Object -First 1  # the bundled benchmark tool, from C:\dev\tools\TensorRT\bin
```

> **Do not `pip install tensorrt-cu13` for this.** Unpinned, it installs TensorRT **11.4**, whose
> libraries are `nvinfer_11.dll`. ONNX Runtime's TensorRT provider cannot load them (tracked upstream
> as onnxruntime#32278), and the engine is C++, so it never imports the Python package anyway. If you
> later want the Python bindings for scripting, pin them to the same release in a separate venv:
> `pip install tensorrt-cu13==10.13.3.9`.

Nothing here is compiled against TensorRT. GCC links only ONNX Runtime's import library, as it
already does; ONNX Runtime loads `onnxruntime_providers_tensorrt.dll`, and that DLL loads TensorRT.
So the MSVC-built TensorRT needs no MinGW build of its own, and the C API boundary that Section 28
already proved for the CUDA provider is the same boundary here.

**Gate G2, TensorRT half** — three checks, on the stock detector before your own model exists:

```powershell
cd C:\dev\torquesense; .\.venv\Scripts\Activate.ps1
python scripts\gpu_preflight.py                  # driver, SM, power sensor, every DLL on PATH

$o = "C:\dev\tools\onnxruntime"
cd tools\check-mingw
g++ -std=c++20 -O2 -I"$o\include" trt_check.cpp "$o\lib\onnxruntime.lib" -o trt_check.exe
.\trt_check.exe mul2.onnx      # expect: TensorRT EP appended · CUDA EP appended behind it · out[0]=2.000000
cd ..\..

yolo export model=yolo11n.pt format=onnx imgsz=640 opset=17                 # writes yolo11n.onnx
trtexec --onnx=yolo11n.onnx --fp16 --saveEngine="$env:TEMP\probe.engine"   # ends &&&& PASSED
```

`gpu_preflight.py` prints one PASS, WARN or FAIL line per check and exits non-zero on any FAIL; its
DLL list was read from the import table of 1.28.0's `onnxruntime_providers_tensorrt.dll`
(`nvinfer_10.dll`, `nvonnxparser_10.dll`, `cudnn64_9.dll`, `cublas64_13.dll`, and the MSVC runtime).
`trt_check.cpp` uses the engine's exact provider calls. It was built with MinGW-w64 GCC against
1.28.0 while writing this and run on a machine with no GPU, where it failed the way the engine must:

```text
TensorRT EP unavailable: … Error loading "…\onnxruntime_providers_tensorrt.dll"
which depends on "cublas64_13.dll" which is missing. (Error 126: "Module not found.")
```

On your laptop the same message names whichever DLL is missing — most likely `nvinfer_10.dll` if the
TensorRT `lib` folder is not on `PATH`. `trtexec`'s `GPU Compute Time` line is the first TensorRT
figure for this GPU: write it next to the CUDA provider's ~14 ms, but adopt nothing yet; the
decision is the gate's (Section 30).

GCC links this MSVC-built package through `lib/onnxruntime.lib`, its import library. That works
because everything the engine calls goes through ORT's C API, which is a plain C ABI; the C++
header is a thin inline wrapper compiled by GCC itself. While writing this, a program using
the engine's calls was built with MinGW-w64 GCC against 1.28.0:

- **Calls.** Session options, the CUDA execution provider's V2 options, a session, a run.
- **Run.** It loaded `onnxruntime.dll` and ran a model correctly.
- **Missing CUDA.** On a machine with no CUDA it raised the "CUDA EP unavailable" exception that
  Section 30's startup check catches, which proves exceptions cross the boundary as designed.

That program ships in the starter kit as `tools/check-mingw/ort_check.cpp`. Building it on this
laptop is the C++ half of gate G2:

```powershell
$o = "C:\dev\tools\onnxruntime"
cd C:\dev\torquesense\tools\check-mingw
g++ -std=c++20 -O2 -I"$o\include" ort_check.cpp "$o\lib\onnxruntime.lib" -o ort_check.exe
$env:Path = "$o\lib;$env:Path"   # this window only; Section 29 sets it for good
.\ort_check.exe mul2.onnx      # expect: ORT 1.28.0, CUDA EP appended · out[0]=2.000000
```

If it prints `CUDA EP missing`, the message names the DLL it could not load — usually cuDNN or the
CUDA runtime not on `PATH` (step 3 above).

### 4 · C++ libraries — MSYS2 packages, plus five pinned sources

Open the **MSYS2 UCRT64** shell from the Start menu. Its prompt says `UCRT64`. Then install
everything the engine links, prebuilt:

```bash
pacman -Syu                     # once; it may close the shell to update itself — reopen and repeat
pacman -S --needed git \
  mingw-w64-ucrt-x86_64-toolchain  mingw-w64-ucrt-x86_64-cmake  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-opencv     mingw-w64-ucrt-x86_64-opencascade \
  mingw-w64-ucrt-x86_64-spdlog     mingw-w64-ucrt-x86_64-nlohmann-json \
  mingw-w64-ucrt-x86_64-gtest      mingw-w64-ucrt-x86_64-benchmark
```

| Package | Version at time of writing | Notes |
|---|---|---|
| `opencv` | 5.0.0 | pulls in FFmpeg, which the phone stream and file replay need, and `ffmpeg.exe` for listing cameras |
| `opencascade` | 7.9.3 | the STEP reader for the CAD compiler |
| `spdlog` · `nlohmann-json` · `gtest` · `benchmark` | 1.17 · 3.12 · 1.18 · 1.9.5 | |
| `toolchain` | GCC 16.2.0, GDB 18.1 | the versions you already have |

OpenCV 5 moved some of `calib3d` into new modules. `calibrateCamera` is now in `calib`, and
`solvePnP` in `geometry`. Function signatures are unchanged and `opencv2/calib3d.hpp` still
includes them all, so the engine includes that one header.

Five small libraries that MSYS2 does not package are fetched by CMake at configure time, at pinned
tags, from one file — `cmake/deps.cmake`:

| Library | Tag | For |
|---|---|---|
| IXWebSocket | v12.1.0 | the dashboard's WebSocket |
| cpp-httplib | v0.61.0 | MJPEG, the control endpoint, the sidecar client |
| HdrHistogram_c | 0.12.0 | p99 / p99.9 latency |
| PicoSHA2 | v1.0.1 | the model manifest check |
| OpenXLSX | v0.5.1 | the shift report (Windows build only) |

open62541 is not fetched. Its 1.4.21 amalgamation is committed under `third_party/open62541/`,
generated once per platform (`-DUA_ENABLE_AMALGAMATION=ON -DUA_ARCHITECTURE=win32`). That is the
exact code the soft PLC's watchdog was tested on. MSYS2 packages 1.5, which has not been.

All five fetched libraries and the open62541 amalgamation were built with MinGW-w64 GCC while this
was written. The test program served HTTP, wrote an `.xlsx`, recorded a histogram and hashed a
file, and the soft PLC passed the heartbeat test. The Linux sanitizer and container builds take
the same five from the same file, with the rest from `apt`. There is no vcpkg anywhere, and no
hour-long first build.

The first configure (Section 29) downloads the fetched sources once into the build tree. After
that it needs no network.

### 5 · Python environment

```powershell
cd C:\dev\torquesense
py -3.12 -m venv .venv                       # the py launcher: python.org's 3.12, never MSYS2's
.\.venv\Scripts\Activate.ps1
# if blocked: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

python -m pip install --upgrade pip

# 1. torch FIRST, from the cu130 index. Installing anything that depends on torch before
#    this pulls the CPU-only wheel from PyPI and nothing will tell you.
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu130

# 2. everything else
pip install transformers accelerate pillow `
            fastapi "uvicorn[standard]" httpx pydantic `
            boto3 "psycopg[binary]" av numpy opencv-python `
            ultralytics onnx onnxruntime `
            blenderproc dvc pytest pyyaml

# Gate G2, Python half
python -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.get_device_name(0))"
python -c "from transformers import Florence2ForConditionalGeneration; print('florence ok')"
```

Expect a `+cu130` torch version, `True`, and `NVIDIA GeForce RTX 3060 Laptop GPU`. If `is_available()`
prints `False`, a CPU wheel won: `pip uninstall -y torch torchvision` and repeat step 1.

The Florence-2 import needs a `transformers` release that includes the model natively. If that import
fails, `pip install -U transformers`. The `timm` and `einops` packages are **not** needed for the
native class — they were only required by the older remote-code checkpoint.

`onnxruntime` (CPU) in the venv is for `training/eval.py` sanity checks only. The engine uses the
C++ CUDA 13 build from step 3.

### 6 · BlenderProc and Blender

```powershell
blenderproc quickstart --blender-install-path C:\dev\tools\blender
```

BlenderProc downloads its own Blender into the path you give it. Without `--blender-install-path`
it installs under your home directory, which contains the space. The quickstart renders one test
scene; open the output to confirm it works.

### 7 · Front end

```powershell
cd C:\dev\torquesense
npm create vite@latest web -- --template react-ts
cd web
npm install
npm install recharts lucide-react react-router-dom `
            @fontsource/ibm-plex-sans @fontsource/ibm-plex-mono
npm install -D tailwindcss @tailwindcss/vite vitest @playwright/test `
               quicktype json-schema-to-typescript
npx shadcn@latest init
npx playwright install chromium
npm run dev
```

If `npm install` warns that install scripts were skipped and `npm run dev` then fails with an
esbuild or missing-binary error, approve the package using the command npm prints and reinstall.

### 8 · Docker images, pulled in advance

```powershell
docker pull nodered/node-red:latest
docker pull heartexlabs/label-studio:latest
docker pull jenkins/jenkins:lts-jdk21
```

Pull them now, on good Wi-Fi. The demo runs offline (Section 22).

### 9 · Operations tooling

```powershell
winget install --id GoLang.Go -e
winget install --id EclipseAdoptium.Temurin.21.JDK -e
go version; java -version

# build the fleet agent (from the repository root; -C runs the build inside fleet-agent\)
go build -C fleet-agent -trimpath -ldflags="-s -w" -o ..\build\fleet-agent\fleet-agent.exe .
```

The agent's module asks for Go 1.24 or later, so any current Go release builds it. It uses only the
standard library, so `go build` downloads nothing.

---

## 29. Configuration

One `.env` as the single source of truth. Never committed.

### WSL2 memory cap

Create `%USERPROFILE%\.wslconfig` (this file *must* live in your home folder; the space is harmless
here because nothing parses it as a path):

```ini
[wsl2]
memory=4GB
processors=4
swap=2GB
```

Then `wsl --shutdown` and restart Docker Desktop. Four logical processors for WSL leaves twelve for
Windows — including the four the engine pins.

### Environment variables — user scope, set once

```powershell
[Environment]::SetEnvironmentVariable("HF_HOME", "C:\dev\hf-cache", "User")
[Environment]::SetEnvironmentVariable("ORT_ROOT", "C:\dev\tools\onnxruntime", "User")
# add ONNX Runtime's DLLs to PATH so torquesense.exe finds them
$p = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$p;C:\dev\tools\onnxruntime\lib", "User")
# TensorRT: DLLs for the TensorRT provider, and trtexec
$p = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$p;C:\dev\tools\TensorRT\lib;C:\dev\tools\TensorRT\bin", "User")
```

### .env — repository root

```bash
# ── generate secrets:  python -c "import secrets; print(secrets.token_hex(32))"

# Supabase — the system of record (Section 16). Server-side only: never copy these into web\
# Connect → Session pooler; the user is postgres.<project-ref>, the password is the database password
DATABASE_URL=postgresql://postgres.<project-ref>:<db-password>@<pooler-host>:5432/postgres
# Storage → S3: endpoint, region and an access key pair (full access, bypasses RLS)
S3_ENDPOINT=https://<project-ref>.storage.supabase.co/storage/v1/s3
S3_REGION=ap-south-1
S3_BUCKET=clips
S3_ACCESS_KEY=from-supabase-storage-s3-keys
S3_SECRET_KEY=from-supabase-storage-s3-keys
OUTBOX_PATH=/var/torquesense/outbox.db

# API
API_KEY=generate-me
API_URL=http://127.0.0.1:8000

# sidecar
SIDECAR_URL=http://127.0.0.1:8001
FLORENCE_MODEL=florence-community/Florence-2-base
TORCH_MEMORY_FRACTION=0.30
```

### docker-compose.yml

```yaml
name: torquesense
services:
  api:                                          # events in, history out; owns the outbox
    build: ./api
    env_file: .env
    ports: ["127.0.0.1:8000:8000"]
    volumes: ["./var:/var/torquesense"]           # outbox.db survives container restarts
    mem_limit: 384m

  node-red:
    image: nodered/node-red:latest                # pin after first pull
    ports: ["127.0.0.1:1880:1880"]
    volumes: ["./softplc/node-red:/data"]
    extra_hosts: ["host.docker.internal:host-gateway"]
    mem_limit: 256m

  jenkins:
    image: jenkins/jenkins:lts-jdk21
    profiles: ["ci"]                              # not started by a plain `docker compose up`
    ports: ["127.0.0.1:8081:8080"]                # 8080 is the engine's WebSocket
    environment:
      JAVA_OPTS: "-Xmx512m"
    volumes: ["jenkins-home:/var/jenkins_home"]
    mem_limit: 1g

volumes: { jenkins-home: {} }
```

Limits sum to about 0.6 GB, well inside the 4 GB WSL cap: the database and the clip store now live in Supabase. Jenkins adds 1 GB but sits in the
`ci` profile: it starts only with `docker compose --profile ci up -d jenkins`, in CI mode, with the
demo platform stopped. After the first successful
`docker compose up`, run `docker compose images` and replace every `latest` with the exact tag shown.
Unpinned images are how a working demo breaks the night before.

### Supabase — one free project, set up once

1. At supabase.com, create a project named `torquesense` in **South Asia (Mumbai)**, the region
   nearest Pune. Generate a database password and keep it in your password manager. Run `show
   server_version;` in the SQL editor and note the version; nothing here depends on it.
2. **Connect → Session pooler.** Copy the connection string into `DATABASE_URL` in `.env`. Its user
   is `postgres.<project-ref>`. The session pooler works over IPv4 on every plan; the direct
   connection does not.
3. **Storage → S3.** Create an access key pair, and copy it with the endpoint and region into the
   `S3_*` lines of `.env`. These keys bypass RLS, so they are server-side only.
4. **Project Settings → API Keys.** Copy the publishable key (`sb_publishable_…`) into
   `web\.env.local`. That is the only Supabase value the browser ever sees:

```ini
# web/.env.local — public by design; RLS decides what it can read
VITE_SUPABASE_URL=https://<project-ref>.supabase.co
VITE_SUPABASE_PUBLISHABLE_KEY=sb_publishable_...
```

5. Apply the schema from the repository. The CLI asks for the database password when it pushes:

```powershell
cd C:\dev\torquesense
npx supabase@latest login
npx supabase@latest init                # once: writes supabase\config.toml beside the migrations
npx supabase@latest link --project-ref <project-ref>
npx supabase@latest db push             # applies supabase\migrations\*.sql in order
npx supabase@latest db diff             # later, before a demo: must print no changes
```

6. Open **Advisors → Security** in the dashboard. It should report no table without RLS. Then check
   **Storage** for the `clips` bucket and **Integrations → Cron** for the two retention jobs.

### The soft PLC — leave its server URL at the default

OPC-UA clients first ask a server for its endpoints, then connect. v4.0 of this document warned that
the soft PLC would advertise the machine name `HSGPC`, which a Docker container cannot resolve, and
told you to override `serverUrls` with `host.docker.internal`. Both halves were wrong. Tested against
open62541 1.4.21, the version vendored in `third_party/open62541/`:

- With the default configuration the server **echoes the URL each client used** in its GetEndpoints
  reply. With the server running under a hostname that does not resolve, asyncua, the open62541
  client and node-opcua — the library under Node-RED's OPC-UA nodes — all connected, read and wrote.
- In 1.4, `serverUrls` also decides **which address the server binds to**. A name that is not a local
  interface — `host.docker.internal`, or a Kubernetes Service name — leaves the server with no socket.
  It logs "The server has no server socket" and exits.

So the soft PLC calls `UA_ServerConfig_setMinimal(cfg, 4840, NULL)` and nothing more. Clients connect
with whatever name reaches it: `localhost` from the engine and from Node-RED, which runs natively
from v5.5 (Section 33), `host.docker.internal` from a container in development, `softplc` inside
Kubernetes.

When Windows Firewall prompts for `softplc.exe`, choose **Cancel**: from v5.5 every client is on this
machine, and loopback traffic does not pass the firewall. In development with Node-RED in Docker,
allow private networks instead, and remove that rule before the station goes live (Section 33).

### config/station_w14.yaml

```yaml
station:
  id: W-14
  name: "Wheel & Tyre Fitment, Line 2"
  takt_seconds: 58

source:
  kind: file                                  # file | uvc | stream  (Section 8)
  path: data/scenarios/cycle_correct_01.mp4
  # kind: uvc     → device: 0, width: 1280, height: 720, fps: 30, fourcc: MJPG     # laptop webcam
  # kind: stream  → url: "http://192.168.43.1:8080/video", width: 1920, height: 1080 # phone, IP Webcam
  fps: 30                                     # 15 in four-station mode
  process_width: 960
  process_height: 540

threads:                                      # logical CPUs: one per physical core 4..7
  capture: 8
  inference: 10
  verify: 12
  alert: 14
  priority_class: high                        # never realtime

perception:
  model_path: models/yolo11n_wheel.onnx
  model_manifest: models/manifest.json        # SHA-256 checked before load (I12)
  execution_providers: [tensorrt, cuda]       # in order; only those the release's gates.json validated (Section 33)
  gpu_mem_limit_mb: 1024                      # 400 in four-station mode; bounds the CUDA arena only
  tensorrt:                                   # read only when execution_provider is tensorrt
    fp16: true
    workspace_mb: 256
    cache_dir: cache/trt                      # + /<TensorRT version>/<model SHA-256, first 8 hex>
    allow_build: false                        # true only for `--build-trt-cache`
  warmup_runs: 20
  input_size: 640
  conf_commit: 0.65                           # at or above: commit
  conf_floor: 0.35                            # 0.35–0.65: ambiguous → adjudicate
  nms_iou: 0.45
  debounce_frames: 2
  crop_pool_size: 4                           # full-resolution frames kept for inspection-point crops
  crop_px: 96

profile:
  dir: samples/wheel-hub-5lug              # procedure.json, coverage.json, narratives.yaml
  variant_tag: Station.W14.VariantCode        # selects a variant's procedure at cycle start
  release_tag: Station.W14.ReleaseRequest     # the release gate
  advisory_only: false                        # true during a cold start: verdicts shown, PLC never written

alert:
  backend: opcua
  endpoint: opc.tcp://127.0.0.1:4840            # the soft PLC on this machine; no Docker name
  namespace: 2
  prefix: Station.W14

clip_ring:
  name: Local\torquesense_W-14
  slots: 180
  slot_bytes: 262144
  jpeg_quality: 80

sidecar:
  url: http://127.0.0.1:8001
  timeout_ms: 1500
  failure_threshold: 3
  circuit_open_seconds: 60
  queue_depth: 16

io:
  websocket_port: 8080                        # W-15 adds 100 to all three ports, W-16 adds 200, …
  mjpeg_port: 8090
  control_port: 8095
  api_url: http://127.0.0.1:8000
  event_spool: data/spool/W-14.jsonl          # events survive an API outage

health:
  bucket_s: 300                               # 5 in demo mode
  heartbeat_ms: 500
  heartbeat_node: Station.W14.Heartbeat
```

### CMakePresets.json

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" }
    },
    {
      "name": "windows", "hidden": true, "inherits": "base",
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" },
      "cacheVariables": { "CMAKE_C_COMPILER": "gcc", "CMAKE_CXX_COMPILER": "g++" }
    },
    {
      "name": "windows-release", "inherits": "windows",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "SUNVERIFY_WITH_CUDA": "ON",
        "ONNXRUNTIME_ROOT": "$env{ORT_ROOT}"
      }
    },
    {
      "name": "windows-debug", "inherits": "windows",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_FLAGS_DEBUG": "-Og -g3",
        "SUNVERIFY_WITH_CUDA": "ON",
        "ONNXRUNTIME_ROOT": "$env{ORT_ROOT}"
      }
    },
    {
      "name": "windows-ci", "inherits": "windows",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "SUNVERIFY_WITH_CUDA": "OFF" }
    },
    {
      "name": "linux-asan", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug", "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1",
        "SUNVERIFY_WITH_CUDA": "OFF", "SUNVERIFY_HEADLESS": "ON"
      }
    },
    {
      "name": "linux-tsan", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug", "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_CXX_FLAGS": "-fsanitize=thread -g -O1",
        "SUNVERIFY_WITH_CUDA": "OFF", "SUNVERIFY_HEADLESS": "ON"
      }
    },
    {
      "name": "linux-headless", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "SUNVERIFY_WITH_CUDA": "OFF", "SUNVERIFY_HEADLESS": "ON"
      }
    }
  ],
  "buildPresets": [
    { "name": "windows-release", "configurePreset": "windows-release" },
    { "name": "windows-debug",   "configurePreset": "windows-debug" },
    { "name": "windows-ci",      "configurePreset": "windows-ci" },
    { "name": "linux-asan",      "configurePreset": "linux-asan" },
    { "name": "linux-tsan",      "configurePreset": "linux-tsan" },
    { "name": "linux-headless",  "configurePreset": "linux-headless" }
  ],
  "testPresets": [
    { "name": "windows-release", "configurePreset": "windows-release", "output": { "outputOnFailure": true } },
    { "name": "windows-ci",      "configurePreset": "windows-ci",      "output": { "outputOnFailure": true } },
    { "name": "linux-tsan",      "configurePreset": "linux-tsan",      "output": { "outputOnFailure": true } }
  ]
}
```

Build from any PowerShell window with `C:\msys64\ucrt64\bin` on `PATH` — the same `PATH` that
already finds your `g++` — or from the MSYS2 UCRT64 shell. `cmake --preset windows-debug` is the
everyday build, for stepping through in GDB (VS Code: the C/C++ extension with `"MIMode": "gdb"` and
`"miDebuggerPath": "C:/msys64/ucrt64/bin/gdb.exe"`). `windows-release` is what the latency gate
measures. The `linux-*` presets run identically in WSL2 and in GitHub Actions. Sanitizers live
there because GCC on Windows has no AddressSanitizer or ThreadSanitizer.

The engine finds its DLLs through `PATH`. MSYS2's `ucrt64\bin` supplies GCC's runtime, OpenCV and
FFmpeg; `C:\dev\tools\onnxruntime\lib` supplies ONNX Runtime; the CUDA and cuDNN directories
supply the rest. For a copy that runs on another machine, `cmake --install` plus `ldd` from the
MSYS2 shell lists exactly which DLLs to ship beside the executable.

### Jenkins — controller in Docker, agent on Windows

The controller is a container; the work happens on an agent on the laptop itself, because that is
where the GPU, the MSYS2 toolchain and the CUDA DLLs are.

```powershell
docker compose --profile ci up -d jenkins
docker compose exec jenkins cat /var/jenkins_home/secrets/initialAdminPassword
```

Open `http://localhost:8081`, install the suggested plugins, create your admin user, then:

1. **Manage Jenkins → System → Jenkins URL:** `http://localhost:8081/`.
2. **Manage Jenkins → Nodes → New node** `hsgpc-gpu`: permanent agent, label `hsgpc-gpu`, one
   executor, remote root `C:\dev\jenkins-agent`, launched by connecting it to the controller, with
   **Use WebSocket** ticked. Save the secret it shows into `C:\dev\jenkins-agent\secret`.
3. **Manage Jenkins → Credentials:** add a *Secret text* with ID `torquesense-api-key`.
4. **New Item → Pipeline** `torquesense-gpu`: *Pipeline script from SCM*, Git, your repository URL,
   branch `*/main`, script path `Jenkinsfile`.

Start the agent as **your own user** — never as Administrator, and not as a Windows service under
LocalSystem. It must inherit your `PATH` (MSYS2's `ucrt64\bin`, ONNX Runtime and cuDNN DLLs), `ORT_ROOT` and
`HF_HOME`, and a build step should never hold admin rights:

```powershell
# scripts\start-jenkins-agent.ps1
New-Item -ItemType Directory -Force C:\dev\jenkins-agent | Out-Null
Invoke-WebRequest http://localhost:8081/jnlpJars/agent.jar -OutFile C:\dev\jenkins-agent\agent.jar
java -jar C:\dev\jenkins-agent\agent.jar -url http://localhost:8081/ -name hsgpc-gpu `
     -secret "@C:\dev\jenkins-agent\secret" -webSocket -workDir C:\dev\jenkins-agent
```

Locking GPU clocks needs Administrator, so the pipeline does not lock them. It records the clocks,
temperature and power source in `conditions.json` beside every result instead, and marks the build
UNSTABLE on battery. The 150 ms budget is about twice the expected 70 ms, so only real throttling
fails it — which is a result worth having.

### GPU clock lock — before every measurement

```powershell
# PowerShell as Administrator
nvidia-smi -pm 1                                   # may report "not supported" on Windows; harmless
nvidia-smi --lock-gpu-clocks=1200,1200
nvidia-smi --query-gpu=clocks.sm,clocks.max.sm,temperature.gpu,power.draw --format=csv
# after measuring:
nvidia-smi --reset-gpu-clocks
```

Persistence mode is a Linux feature and is often reported unsupported on Windows; the clock lock is
what matters. Ignore `power.draw` on this laptop: driver 596.21 has reported 752 W against a 130 W
limit, so clocks and temperature are the recorded conditions. If `--lock-gpu-clocks` itself is refused by the laptop's driver, measure anyway with
clocks unlocked and report the clocks observed alongside the histogram — a disclosed condition is
still a valid measurement; an undisclosed one is not.

---

## 30. Integration

One rule governs everything here: **every process boundary is a schema, and every schema is
generated into code.** Hand-written structs on two sides of a boundary drift; generated ones fail
to compile.

### The contract, generated

```powershell
# contracts/ holds state, anomaly, adjudication, health and host_health schemas (*.schema.json)
npx quicktype -s schema contracts\*.schema.json -l c++ --namespace sunverify::contracts `
  --code-format with-struct --include-location global-include -o control-app\include\contracts.hpp
npx json2ts -i "contracts/*.schema.json" -o web\src\contracts\
```

`quicktype` emits C++ structs with nlohmann/json serialisers; `json-schema-to-typescript` emits the
dashboard's types. Run both from one script; CI fails if the generated files differ from what is
committed.

These structs are used **only on the io thread**. The hot path carries `StateVector`, a fixed-size
POD, and the io thread converts it to the contract type. JSON never touches a pinned thread.

### The shared clip ring — one layout, two languages

The engine writes JPEG frames; the sidecar reads them for adjudication crops and clip assembly. No
image ever crosses HTTP. The layout is fixed and documented, because a Python reader parses it with
`struct` offsets:

```
Offset     Size        Field
0          4           magic 'TSCR'
4          4           version = 1
8          4           slots = 180
12         4           slot_bytes = 262144
64         8           write_seq          (own cache line; producer-owned)
128        180 × 32    slot table:  seq u64 · ts_ns u64 · frame_seq u64 · len u32 · pad u32
8192       180 × 256K  payloads
Total      47,194,112 bytes  (~45 MiB)
```

Each slot is a **seqlock**. The writer marks the slot odd while writing and even when done; a reader
that sees the sequence change, or sees it odd, discards the copy and retries.

```cpp
// ipc/src/SharedClipRing.cpp — producer, called from the capture thread
bool SharedClipRing::publish(std::span<const std::byte> jpeg,
                             std::uint64_t ts_ns, std::uint64_t frame_seq) noexcept {
  if (jpeg.size() > kSlotBytes) return false;                 // never truncate, never block
  const std::uint64_t n = header_->write_seq.load(std::memory_order_relaxed);
  SlotMeta& m = slots_[n % kSlots];
  m.seq.store(2 * n + 1, std::memory_order_relaxed);          // odd: write in progress
  std::atomic_thread_fence(std::memory_order_release);        // odd seq visible before payload
  std::memcpy(payload(n % kSlots), jpeg.data(), jpeg.size());
  m.ts_ns = ts_ns; m.frame_seq = frame_seq;
  m.len   = static_cast<std::uint32_t>(jpeg.size());
  m.seq.store(2 * n + 2, std::memory_order_release);          // even: payload complete
  header_->write_seq.store(n + 1, std::memory_order_release); // publish
  return true;
}
```

The Windows backend maps it with `CreateFileMappingW` under the name `Local\torquesense_W-14`.
Python's standard `mmap` module can open a named Windows mapping directly through its `tagname`
argument — no extension module needed:

```python
# sidecar/clipring.py
import mmap, struct

SLOTS, SLOT_BYTES = 180, 262_144
TABLE, PAYLOAD, TOTAL = 128, 8192, 8192 + 180 * 262_144

class ClipRing:
    def __init__(self, station: str):
        self.m = mmap.mmap(-1, TOTAL, tagname=f"Local\\torquesense_{station}")
        assert self.m[0:4] == b"TSCR", "engine not running, or layout mismatch"

    def write_seq(self) -> int:
        return struct.unpack_from("<Q", self.m, 64)[0]

    def read(self, n: int) -> bytes | None:
        base = TABLE + (n % SLOTS) * 32
        for _ in range(3):                                    # seqlock retry
            s1, ts, fseq, ln = struct.unpack_from("<QQQI", self.m, base)
            if s1 & 1:                                        # writer mid-copy: retry
                continue
            if s1 != 2 * n + 2:                               # slot holds a different frame:
                return None                                   # not yet written, or overwritten
            off = PAYLOAD + (n % SLOTS) * SLOT_BYTES
            data = bytes(self.m[off: off + ln])
            if struct.unpack_from("<Q", self.m, base)[0] == s1:
                return data
        return None
```

> **An honest platform assumption.** The Python reader has no explicit memory fences. It is correct
> on x86-64, where loads are not reordered with other loads, and the seqlock re-check catches torn
> copies. On an ARM edge device the reader must move to C++ with acquire loads. This is noted in
> `docs/architecture.md` rather than discovered on a Jetson.

### ONNX Runtime — fail loudly, then warm up

```cpp
// perception/src/YoloOnnxDetector.cpp
std::expected<YoloOnnxDetector, Error>
YoloOnnxDetector::create(const Config& c, Ort::Env& env) {
  Ort::SessionOptions so;
  so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  so.SetIntraOpNumThreads(1);                      // the inference thread owns this session

  OrtCUDAProviderOptions cuda{};
  cuda.device_id              = 0;
  cuda.gpu_mem_limit          = std::size_t{c.gpu_mem_limit_mb} << 20;   // 1024 or 400
  cuda.arena_extend_strategy  = 1;                 // kSameAsRequested: no greedy doubling
  cuda.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
  cuda.do_copy_in_default_stream = 1;
  try {
    so.AppendExecutionProvider_CUDA(cuda);
  } catch (const Ort::Exception& e) {
    // Typical on this machine: the CUDA 12 ORT asset looking for cudart64_12.dll,
    // or cudnn64_9.dll missing from PATH.
    spdlog::critical("CUDA EP failed to load: {} — use the gpu_cuda13 ONNX Runtime asset "
                     "and check `where cudnn64_9.dll`", e.what());
    return std::unexpected(Error::kCudaUnavailable);
  }

  YoloOnnxDetector d{env, c, so};
  // Warm-up: the first runs pay for cuDNN's exhaustive algorithm search and lazy
  // allocation — often ~1 s. They must never land in a latency histogram.
  for (int i = 0; i < c.warmup_runs; ++i) d.runDummy();
  const double median_ms = d.timeDummyRuns(10);
  if (median_ms > 40.0) {
    spdlog::critical("Median inference {:.1f} ms after warm-up — expected ~14 ms. "
                     "Nodes are probably running on CPU, or the GPU is throttled.", median_ms);
    return std::unexpected(Error::kTooSlow);
  }
  return d;
}
```

Earlier drafts checked `Ort::GetAvailableProviders()`. That only lists providers compiled into the
library, not whether they load — so it passes on exactly the misconfiguration it was meant to catch.
The exception plus a timed warm-up is the check that actually works (Appendix A).

### TensorRT — the fast path, adopted only through a gate

The detector can run on ONNX Runtime's **TensorRT execution provider** instead of the CUDA one. It is
the same `.onnx` file, the same session API and the same output tensors; TensorRT compiles the graph
into an engine tuned for this GPU, in FP16 on Ampere's tensor cores. The CUDA provider is registered
after it, so any node TensorRT declines still runs on the GPU.

**Which models.** The detector only. Its input is fixed at 640², which is what TensorRT optimises
best. The family classifiers see a different number of crops every frame and take ≈ 4 ms together;
dynamic-shape profiles are not worth their complexity for that, so they stay on the CUDA provider.
Florence-2 stays on PyTorch.

```cpp
// perception/src/YoloOnnxDetector.cpp — provider selection
void appendTensorRt(Ort::SessionOptions& so, const Config& c) {
  const OrtApi& api = Ort::GetApi();
  OrtTensorRTProviderOptionsV2* raw = nullptr;
  Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&raw));
  std::unique_ptr<OrtTensorRTProviderOptionsV2, decltype(api.ReleaseTensorRTProviderOptions)>
      opts{raw, api.ReleaseTensorRTProviderOptions};

  // cache\trt\<TensorRT version>\<first 8 hex of the model's manifest SHA-256>\ — a new model or a
  // new TensorRT can never pick up a stale engine (I12)
  const std::string cache = c.trt_cache_dir.string();
  const std::string ws    = std::to_string(std::size_t{c.trt_workspace_mb} << 20);
  const char* keys[] = {"device_id", "trt_fp16_enable", "trt_max_workspace_size",
                        "trt_engine_cache_enable", "trt_engine_cache_path",
                        "trt_timing_cache_enable", "trt_timing_cache_path"};
  const char* vals[] = {"0", c.trt_fp16 ? "1" : "0", ws.c_str(),
                        "1", cache.c_str(), "1", cache.c_str()};
  Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(opts.get(), keys, vals, std::size(keys)));
  so.AppendExecutionProvider_TensorRT_V2(*opts);
}

// in create(), replacing the single AppendExecutionProvider_CUDA call
try {
  if (c.execution_provider == Provider::kTensorRt) {
    if (!c.allow_trt_build && !std::filesystem::exists(c.trt_cache_dir)) {
      spdlog::critical("No TensorRT engine cache at {} — run `torquesense --build-trt-cache` "
                       "first; an engine is never built during a demo or a shift", c.trt_cache_dir.string());
      return std::unexpected(Error::kTrtCacheMissing);
    }
    appendTensorRt(so, c);           // first: TensorRT claims every node it can
  }
  so.AppendExecutionProvider_CUDA(cuda);   // then CUDA, for anything TensorRT declines
} catch (const Ort::Exception& e) {
  // Typical: nvinfer_10.dll not on PATH, or TensorRT 11 installed instead of 10
  spdlog::critical("{} provider failed to load: {} — run scripts\\gpu_preflight.py",
                   to_string(c.execution_provider), e.what());
  return std::unexpected(Error::kProviderUnavailable);
}
```

Three rules keep it honest:

1. **Only a measured provider runs, and never silently.** The engine tries the providers the
   release gate validated, in the configured order. A fall from TensorRT to CUDA is reported as
   DEGRADED on `/health`, the dashboard and the fleet record; none loading means the engine does not
   start. There is never a drop to the CPU. (v5.4 refused to start on any failure; Section 33
   explains why the validated fallback is the production rule.)
2. **The engine is built once, off stage.** The first session with an empty cache compiles the
   engine, which takes from tens of seconds to minutes. `torquesense --build-trt-cache --config …`
   does it during setup, with `allow_trt_build` true for that run only. Afterwards a missing cache
   is a start-up failure, not a slow start. In four-station mode the four processes share one cache.
3. **The engine's memory sits outside the ORT arena cap.** `gpu_mem_limit` bounds the CUDA
   provider's arena, not TensorRT's engine and activations. `trt_max_workspace_size` (256 MB here)
   bounds the build, and the VRAM figures in Section 13 are re-measured under TensorRT before I7 is
   claimed.

**The gate — adopt TensorRT only if both hold:**

| Check | Command | Passes when |
|---|---|---|
| Parity | `ctest -R ProviderParity` — the validation clip's 200 frames through both providers | every detection at or above the 0.35 floor in one has a same-class match in the other with IoU ≥ 0.9 and confidence within 0.05; and the full scenario suite produces identical events under both (as I16 does for the interpreter) |
| Latency | `latency_test` twice, clocks locked, once per provider | TensorRT's evidence p99 is no worse than the CUDA provider's |

If both pass, `execution_provider: tensorrt` becomes the demo configuration. If either fails, the
demo stays on CUDA and `VALIDATION.md` records the measured gap and the reason. That outcome is a
result, not a failure: the latency budget in Section 13 is met with the CUDA provider alone, and the
point of the gate is that the choice is made by measurement.

### The sidecar — Florence-2 adjudication

```python
# sidecar/florence.py
import io, torch
from PIL import Image
from transformers import AutoProcessor, Florence2ForConditionalGeneration

class FlorenceAdjudicator:
    MODEL = "florence-community/Florence-2-base"
    TASK  = "<CAPTION_TO_PHRASE_GROUNDING>"

    def __init__(self, memory_fraction: float = 0.30):
        torch.cuda.set_per_process_memory_fraction(memory_fraction)   # share the GPU politely
        self.model = Florence2ForConditionalGeneration.from_pretrained(
            self.MODEL, torch_dtype=torch.float16).to("cuda").eval()   # pinned; never device_map="auto"
        self.proc = AutoProcessor.from_pretrained(self.MODEL)
        self._warm()

    @torch.inference_mode()
    def ground(self, crop: Image.Image, phrase: str) -> list[list[float]]:
        crop = crop.convert("RGB")                                     # RGBA crops crash the processor
        x = self.proc(text=self.TASK + phrase, images=crop,
                      return_tensors="pt").to("cuda", torch.float16)   # casts pixel_values only
        ids = self.model.generate(input_ids=x["input_ids"], pixel_values=x["pixel_values"],
                                  max_new_tokens=128, num_beams=1, do_sample=False)
        text = self.proc.batch_decode(ids, skip_special_tokens=False)[0]
        out = self.proc.post_process_generation(text, task=self.TASK,
                                                image_size=(crop.width, crop.height))
        return out[self.TASK].get("bboxes", [])

    def bolt_is_torqued(self, crop: Image.Image, u: float, v: float, r: float) -> bool:
        """True if a grounded 'yellow marked bolt head' box is centred within r px of
        the CAD-projected bolt position (u, v) in crop coordinates."""
        for x0, y0, x1, y1 in self.ground(crop, "yellow marked bolt head"):
            if ((x0 + x1) / 2 - u) ** 2 + ((y0 + y1) / 2 - v) ** 2 <= r * r:
                return True
        return False

    def _warm(self):
        self.ground(Image.new("RGB", (256, 256)), "bolt")
```

`max_new_tokens=128, num_beams=1` rather than the tutorial's 1024 and 3: a bolt crop needs few
tokens, and greedy decoding roughly triples throughput. `.to("cuda")` rather than
`device_map="auto"`: with the detector already resident, automatic placement can silently spill
weights to the CPU and turn a 300 ms call into three seconds.

> **Validate the adjudicator, do not assume it.** Florence-2 was never trained on your yellow
> witness marks; grounding "yellow marked bolt head" is zero-shot. Render 200 deliberately ambiguous
> crops (motion blur, partial occlusion, glancing light) with known labels and report its agreement
> rate in `docs/VALIDATION.md`. If it agrees less often than the detector alone, the adjudicator is
> making things worse and should be turned off — which the config allows without code changes.

### The engine's sidecar client — the circuit breaker

`ReasoningClient` lives on the io thread, owns a bounded drop-oldest queue of depth 16, and opens a
circuit after three consecutive failures or timeouts for sixty seconds. While the circuit is open,
ambiguous bolts simply stay in their last committed state until the detector resolves them. Nothing
about an open circuit can produce an alert; it can only delay a state commit.

### Integration checkpoints

| Hour (of 40) | Checkpoint | Proves |
|---|---|---|
| 21 | Stub engine publishes canned `StateVector` JSON over the WebSocket into all four dashboard zones | every interface exists; integration is never a cliff |
| 26 | The real procedure engine drives the soft PLC; Node-RED lamp turns red on the adjacent-order scenario | engine → OPC-UA → container round trip |
| 30 | Sidecar reads the clip ring, writes an MP4, queues it in the outbox; it reaches Supabase Storage and the anomaly row has a `clip_key` | shared memory across processes and languages |
| 33 | Ambiguous scenario triggers an adjudication; the anomaly card shows `adjudicated: true` | the CV + LLM loop is live |
| 38 | Four engines on `/line` | multi-station mode inside VRAM budget |
| 41 | Close an engine; its Node-RED lamp turns amber within 2.5 s | heartbeat → PLC watchdog, end to end |
| 45 | The seeded drifting station is amber on `/fleet`; the attention list names lens contamination | drift job and playbook, end to end |
| 47 | Stop the API for a minute; restart it; no fleet-agent record is missing or out of order | spool and in-order flush |
| 52 | `k8s-smoke` green in GitHub Actions | the images and manifests work in a real cluster |

---

## 31. Testing

Eighteen layers. Each answers a different question. The latency gate and the two-number accuracy report
outrank the rest, because they are the claims you will say out loud.

| Layer | Tool | Proves | Runs |
|---|---|---|---|
| Unit | GoogleTest | ring buffer, frame pools, interpreter, pattern solvers, narrator, profile loading | laptop, CI |
| **Reference model** | pytest, `reference/tests` | 42 tests: one interpreter on every sample, every violation kind, pattern solvers, the resolution check, webcam and phone arithmetic, and the samples contract proven to refuse what it should | laptop, CI |
| **Differential** | GoogleTest + reference | the C++ engine and the reference produce identical violation lists on every profile's scenarios (I16) | laptop, CI |
| Profile gate | `profile_gate.py` | coverage clean, scenarios exact, recall reported, canary signed (I14) | CI, per profile |
| Concurrency | GoogleTest under **TSan** | SPSC ring and clip ring are race-free | WSL2, CI (Linux) |
| Memory | GoogleTest under ASan + UBSan | no overflow, use-after-free, UB | WSL2, CI (Linux) |
| Allocation | counting allocator | zero allocations in `tick()` over 10,000 frames (I1) | laptop, CI |
| Scenario | 10 rendered videos + `.expected.json` | every fault caught; both edge cases **not** flagged | laptop |
| **Latency** | `latency_test` + HdrHistogram | evidence latency p99 ≤ 150 ms (I4) | laptop, GPU, AC power |
| **Accuracy** | `training/eval.py` | synthetic and real mAP, reported separately (I10) | laptop |
| Reproducibility | pytest | same seed → same labels, PSNR ≥ 45 dB (I9) | laptop |
| Sidecar + API | pytest, Florence mocked in CI | schema validation, circuit breaker, event spool replay | laptop, CI |
| End to end | Playwright | harness selects a fault → anomaly card appears; HMI floods red | laptop, CI (stub engine) |
| **Fail-visible** | soft PLC + asyncua stand-in engine | AMBER before the first beat, clear while beating, back within 2.5 s of the last beat (I13) | laptop, CI (Linux) |
| Drift detection | pytest | EWMA quiet in control, fast on a shift, direction-aware, floored, hysteretic, restart-safe | laptop, CI |
| Fleet agent | `go test -race` | GPU parsing, in-order spool flush, spool cap; cross-compiles for Windows and Linux | CI |
| Orchestration | kind + `tests/k8s/smoke.py` | engine → API → PLC inside a cluster; scaling the engine to zero turns the lamp amber | CI |
| GPU gates | Jenkins on HSGPC | latency, accuracy, scenarios and the nightly soak, with conditions recorded | laptop |

### The procedure engine — table-driven

```cpp
// procedure/tests/ProcedureEngineTest.cpp
// Protects: I6 (written before ProcedureEngine), I4 (evaluation must stay O(1) per frame).
struct Case { const char* name; std::vector<int> torque_order; std::optional<Violation> expect; };

class Sequences : public ::testing::TestWithParam<Case> {};

TEST_P(Sequences, MatchesExpectedVerdict) {
  const auto& c = GetParam();
  ProcedureEngine fsm{loadProfile("samples/wheel-hub-5lug")};
  StateVectorBuilder sv;                       // starts with 5 seated bolts
  std::optional<Violation> got;
  for (int bolt : c.torque_order)
    for (int f = 0; f < 3; ++f)                // 3 frames: survives the 2-frame debounce
      if (auto v = fsm.evaluate(sv.torque(bolt).frame()); v.isViolation()) got = v.violation();
  EXPECT_EQ(got, c.expect) << c.name;
}

INSTANTIATE_TEST_SUITE_P(W14, Sequences, ::testing::Values(
  Case{"star",               {1,3,5,2,4}, std::nullopt},
  Case{"adjacent",           {1,2,3,4,5}, Violation{.observed = 2, .expected = 3}},
  Case{"resume_from_two",    {1,3,2},     Violation{.observed = 2, .expected = 5}},
  Case{"double_torque_ok",   {1,1,3,5,2,4}, std::nullopt}
  /* … 21 more, including timeouts, occlusion, second person … */
));
```

### Zero allocation — the counting allocator

```cpp
// perception/tests/support/CountingAllocator.cpp — linked only into the allocation test binary
#include <atomic>
#include <cstdlib>
#include <new>
std::atomic<std::size_t> g_allocs{0};
thread_local bool g_counting = false;

void* operator new(std::size_t n) {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
```

```cpp
TEST(Pipeline, NoAllocationInSteadyState) {      // I1
  Pipeline p{testConfig(), std::make_unique<ReplayDetector>("data/scenarios/cycle_correct_01.json")};
  for (int i = 0; i < 100; ++i) p.tick();          // warm-up: pools, reserves, first cycle
  g_allocs = 0; g_counting = true;
  for (int i = 0; i < 10'000; ++i) p.tick();
  g_counting = false;
  EXPECT_EQ(g_allocs.load(), 0u);
}
```

This binary is excluded from the ASan presets: ASan replaces `operator new` itself, and the two
replacements conflict. That is expected, and the exclusion is commented in CMake so nobody "fixes" it.

### Latency — the gate

```powershell
# after locking clocks (Section 29), on AC power
.\build\windows-release\tests\latency_test.exe `
    --scenario data\scenarios\fault_interrupt_resume.mp4 `
    --runs 100 --budget-evidence-p99-ms 150 --histogram docs\latency.hgrm
```

The test prints p50/p99/p99.9 for both engine and evidence latency, the execution provider, the GPU
clocks and temperature observed during the run, and exits non-zero if evidence p99 exceeds the
budget. `--provider tensorrt` runs the same test on the TensorRT path; the two histograms side by
side are half of the TensorRT gate, and `ctest -R ProviderParity` is the other half (Section 30). The `.hgrm` file goes
into `docs/` and its percentiles into `VALIDATION.md`.

### Reproducibility and accuracy

```python
# synth/tests/test_render_reproducible.py — I9
def test_same_seed_same_render(tmp_path):
    a, b = render(seed=7, frames=10, out=tmp_path/"a"), render(seed=7, frames=10, out=tmp_path/"b")
    assert a.labels == b.labels                         # labels must be identical
    for fa, fb in zip(a.images, b.images):
        assert psnr(fa, fb) >= 45.0                     # pixels: near-identical, not bit-exact
```

```powershell
python training\eval.py --model models\yolo11n_wheel.onnx `
    --synthetic data\synth\heldout --real data\real_val --out docs\accuracy.json
```

`eval.py` refuses to write a single combined figure. Its output has two blocks and a `limits`
field stating that `bolt_torqued` has no real-set coverage.

### Differential — the reference is the specification

Every trace in `samples/` is replayed as committed state and tag events through both the Python
reference interpreter and the C++ engine. Each must produce exactly the violations in the trace's
`expect:` block — kind, step, inspection point and order — so the two implementations agree with
the trace and with each other. A disagreement fails CI and names the first event at which an
implementation diverged.

```bash
python samples/run_samples.py --template                       # the reference, every sample
./build/linux-asan/control-app/torquesense --conformance samples  # the C++ engine, the same traces
```

Both tools are built in hours 20–26. The reference model's own suite ran while this revision was
written: 42 tests, all passing, after its first run caught a design error — unordered state families
(Section 6).

### Fail-visible — the heartbeat test

The soft PLC is tested on its own, with asyncua standing in for the engine. This is the test that
was run while writing this revision, against open62541 1.4.21:

```python
# tests/plc/test_heartbeat.py — the script form; the pytest version asserts ≤ 2.5 s
import asyncio, time
from asyncua import Client, ua

async def main(url="opc.tcp://127.0.0.1:4840"):
    async with Client(url=url) as c:
        node = lambda n: c.get_node(f"ns=2;s=Station.W14.{n}")
        print("before first beat -> Amber:", await node("Andon.Amber").read_value())
        hb = node("Heartbeat")
        for i in range(1, 8):                               # the engine, beating every 400 ms
            # open62541 rejects a write that carries a status code unless the node grants
            # StatusWrite; asyncua sends one by default, so send the bare value
            await hb.write_value(ua.DataValue(ua.Variant(i, ua.VariantType.UInt32),
                                              StatusCode=None, SourceTimestamp=None, ServerTimestamp=None))
            await asyncio.sleep(0.4)
        print("while beating     -> Amber:", await node("Andon.Amber").read_value())
        t0 = time.monotonic()                               # the engine dies here
        while not await node("Andon.Amber").read_value():
            await asyncio.sleep(0.05)
        print(f"engine stopped    -> Amber after {time.monotonic() - t0:.2f} s")

asyncio.run(main())
```

```
before first beat -> Amber: True
while beating     -> Amber: False
engine stopped    -> Amber after 1.79 s
```

The last beat was written 0.4 s before the clock started, so AMBER arrived 2.2 s after the last
heartbeat: the 2 s rule plus the 250 ms check interval. The engine's own client never trips the
status-code issue — `UA_Client_writeValueAttribute` sends a bare value, and was tested writing the
heartbeat the same way.

### Drift — the control-chart tests

Seven tests in `api/tests/test_ewma.py`, all with fixed seeds so they are deterministic: no ticket
over 5,000 in-control buckets; a 2σ drop flagged within 12 buckets; a rise on a "bad when falling"
chart ignored; a flat baseline floored rather than given zero spread; a drift episode that clears
with hysteresis and can fire again; a reset to baseline on redeploy; and a chart restored from its
JSON state that behaves identically to the original. The first draft used a two-hour baseline, and
the in-control test failed — Section 19 explains why that failure was the correct outcome.

### Scenario and soak

The ten scenario videos each have a sibling `.expected.json` naming the violation type and the
frame window in which it must fire. `ctest -R Scenario` runs all ten. A separate
`soak_test --minutes 10 --stations 4` runs the line mode, samples `nvidia-smi` every second, and
fails if VRAM exceeds 5 GB (I7) or any station drops below 14 fps.

### Continuous integration

```yaml
# .github/workflows/ci.yml — passes actionlint
name: ci
on: [push, pull_request]
permissions: { contents: read }

jobs:
  linux-sanitizers:
    runs-on: ubuntu-24.04
    strategy: { matrix: { preset: [linux-asan, linux-tsan] } }
    steps:
      - uses: actions/checkout@v4
      - run: |
          sudo apt-get update
          sudo apt-get install -y clang ninja-build libgtest-dev libgmock-dev libbenchmark-dev \
                                  nlohmann-json3-dev libspdlog-dev
      - run: cmake --preset ${{ matrix.preset }} && cmake --build --preset ${{ matrix.preset }}
      - run: ctest --test-dir build/${{ matrix.preset }} --output-on-failure --repeat until-fail:5

  windows-mingw:                       # the same compiler as the laptop: MSYS2 UCRT64 GCC
    runs-on: windows-latest
    defaults: { run: { shell: "msys2 {0}" } }
    steps:
      - uses: actions/checkout@v4
      - uses: msys2/setup-msys2@v2
        with:
          msystem: UCRT64
          update: true
          install: git
          pacboy: >-
            toolchain:p cmake:p ninja:p opencv:p opencascade:p
            spdlog:p gtest:p benchmark:p nlohmann-json:p
      - run: cmake --preset windows-ci && cmake --build --preset windows-ci
      - run: ctest --preset windows-ci

  samples:                             # the conformance suite: every assembly, one contract
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.12" }
      - run: pip install pyyaml pytest
      - run: python samples/run_samples.py --template
      - run: pytest reference/tests

  fleet-agent:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-go@v5
        with: { go-version-file: fleet-agent/go.mod }
      - working-directory: fleet-agent
        run: |
          test -z "$(gofmt -l .)"
          go vet ./...
          go test -race ./...
          GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -o /dev/null .
          GOOS=linux   GOARCH=amd64 CGO_ENABLED=0 go build -o /dev/null .

  python:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.12" }
      - run: pip install -r sidecar/requirements-ci.txt -r api/requirements.txt pytest
      - run: pytest sidecar/tests api/tests      # includes the EWMA drift tests; Florence is mocked

  web:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: 22 }
      - run: cd web && npm ci && npx vitest run
      - run: cd web && npx playwright install --with-deps chromium && npx playwright test

  contracts:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: ./scripts/gen-contracts.sh && git diff --exit-code
```

No GPU runs in hosted CI. The `windows-mingw` job installs the same MSYS2 UCRT64 packages as the
laptop and builds with `SUNVERIFY_WITH_CUDA=OFF`, so GCC compiles every line except the ONNX
Runtime adapter. The Linux jobs carry the sanitizers, with their libraries from `apt`. The
`samples` job runs the conformance suite and the reference tests on every push.

The `k8s-smoke` job is production work, so it moved to `.github/workflows/deferred-production.yml`.
That workflow runs only on demand (`workflow_dispatch`): it builds the three images, side-loads
them into a kind cluster, applies the `kind` overlay and runs `tests/k8s/smoke.py`. Both workflows
pass `actionlint` cleanly.

The manifests are checked before they ever reach a cluster: both overlays render with Kustomize and
pass `kubeconform -strict` against Kubernetes 1.36 (12 objects for `kind`, 11 for `edge`). The
Dockerfiles were written but not built while drafting this revision — there was no Docker daemon
available — so their first build is the `k8s-smoke` job's first run.

### The GPU pipeline — Jenkins on the laptop

What hosted CI cannot run, Jenkins runs on HSGPC: the build with CUDA, the scenario suite, the
latency gate, the accuracy report and, nightly, the four-station soak.

```groovy
// Jenkinsfile: the GPU gates, on HSGPC.
// GitHub Actions runs everything that needs no GPU. These stages need the RTX 3060, so
// they run on a Jenkins agent on the laptop. The controller polls GitHub; nothing on the
// laptop is exposed to the internet, and builds run only from this repository's main
// branch, never from fork pull requests.
pipeline {
  agent { label 'hsgpc-gpu' }
  options {
    timeout(time: 2, unit: 'HOURS')
    disableConcurrentBuilds()                 // one GPU: concurrent builds would corrupt latency figures
    buildDiscarder(logRotator(numToKeepStr: '30'))
    timestamps()
  }
  triggers {
    pollSCM('H/15 * * * *')                   // laptop pulls; no inbound webhook
    cron('H 2 * * *')                         // nightly run adds the four-station soak
  }
  environment {
    MSYS2  = 'C:\\msys64\\ucrt64\\bin'           // GCC, CMake, Ninja, OpenCV and their DLLs
    PY     = 'C:\\dev\\torquesense\\.venv\\Scripts\\python.exe'
    API_KEY = credentials('torquesense-api-key')
  }
  stages {
    stage('Conditions') {
      steps {
        // On battery the GPU clocks down and p99 roughly doubles. Record the conditions,
        // and mark the build UNSTABLE rather than trusting numbers taken on battery.
        catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
          powershell '''
            $b  = Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue | Select-Object -First 1
            $ac = if ($b) { [bool]$b.PowerOnline } else { $true }
            $g  = nvidia-smi --query-gpu=clocks.sm,clocks.max.sm,temperature.gpu,driver_version --format=csv,noheader
            @{ ac_power = $ac; gpu = "$g"; at = (Get-Date).ToString("o") } | ConvertTo-Json | Set-Content conditions.json
            if (-not $ac) { Write-Error "On battery: latency figures from this run are not comparable"; exit 3 }
          '''
        }
      }
    }
    stage('Data') {
      steps { bat '"%PY%" -m dvc pull' }      // scenarios, real validation set, model: from the local DVC store
    }
    stage('Build') {
      steps { bat 'set "PATH=%MSYS2%;%PATH%" && cmake --preset windows-release && cmake --build --preset windows-release' }
    }
    stage('Unit and scenarios') {
      steps { bat 'set "PATH=%MSYS2%;%PATH%" && ctest --preset windows-release --output-junit "%WORKSPACE%\\ctest.xml"' }
      post { always { junit testResults: 'ctest.xml' } }
    }
    stage('Samples, both engines') {
      // every sample through the reference interpreter, then every trace through the C++ engine (I16)
      steps {
        bat '"%PY%" samples\\run_samples.py --template'
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\control-app\\torquesense.exe --conformance samples'
      }
    }
    stage('Latency gate') {
      steps {
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\tests\\latency_test.exe --scenario data\\scenarios\\fault_interrupt_resume.mp4 --runs 100 --budget-evidence-p99-ms 150 --histogram docs\\latency.hgrm'
      }
    }
    stage('Accuracy report') {
      steps {
        bat '"%PY%" training\\eval.py --model models\\yolo11n_wheel.onnx --synthetic data\\synth\\heldout --real data\\real_val --out docs\\accuracy.json'
      }
    }
    stage('Soak, four stations') {
      when { triggeredBy 'TimerTrigger' }
      steps {
        bat 'set "PATH=%MSYS2%;%PATH%" && build\\windows-release\\tests\\soak_test.exe --minutes 10 --stations 4 --vram-limit-gb 5 --min-fps 14'
      }
    }
  }
  post {
    always {
      archiveArtifacts artifacts: 'conditions.json, docs/latency.hgrm, docs/accuracy.json', allowEmptyArchive: true
    }
  }
}
```

The file passes `npm-groovy-lint` with no errors; the only warnings are long command lines.
Archiving `conditions.json` beside every histogram is what makes a latency number from this pipeline
reviewable: a figure taken at 65 °C on AC power is a different claim from one taken throttled.

**Why Jenkins, and not a GitHub self-hosted runner.** A self-hosted runner would also give hosted CI
the laptop's GPU. GitHub recommends against using them on public repositories, because a pull request
from a fork can run arbitrary code on the runner — here, on your own laptop. Jenkins polling `main`
never builds fork code and needs no inbound connection. It is also the CI server most likely to
already exist inside a manufacturing or supply-chain IT department.

---

## 32. Deployment

Three tiers. You ship Tier 0, you share Tier 1, you pitch Tier 2.

```mermaid
flowchart TB
  subgraph T0["Tier 0 — the installed release · HSGPC · alerts need no network"]
    SUP0["supervisor.exe<br/>Task Scheduler at sign-in"] -.-> E0
    E0["torquesense.exe ×1 or ×4<br/>RTX 3060 · cores 4–7"] --> S0["softplc.exe :4840"]
    E0 --> SC0["sidecar · Florence-2"]
    E0 --> D0["API + outbox :8000<br/>serves the dashboard · Node-RED"]
    D0 --> W0["Edge kiosk HMI"]
  end
  subgraph T1["Tier 1 — public showcase · synthetic data only"]
    S1[("Supabase · free project<br/>Postgres · Storage · Realtime")]
    V1["Vercel · dashboard<br/>live from Supabase · replay as fallback"]
    S1 --> V1
    G1["GitHub · CI badge · VALIDATION.md"]
  end
  subgraph T2["Tier 2 — production · deferred on purpose · Appendix D"]
    CAM["Basler a2A1920-51gcBAS<br/>GigE · PoE · own NIC port"] --> E2["engine pod · Recreate<br/>pinned to its station's node"]
    AG2["fleet-agent DaemonSet<br/>one per node"] -.-> E2
    E2 -- "OPC-UA SignAndEncrypt · heartbeat" --> PLC2["Line PLC · Andon · conveyor<br/>heartbeat watchdog"]
    NR2["Nutrunner controller"] -- "per-rundown result" --> E2
    E2 --> H2[("Plant historian / MES<br/>in-plant object store")]
  end
  D0 -. "outbox, when online" .-> S1
  T0 ~~~ T1
  T1 ~~~ T2
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class E0,E2 key;
```

*Figure 6 — Deployment tiers. The same engine source runs in Tier 0 and Tier 2; the Linux build that
runs the sanitizers in Tier 0 is the build that would run on a station edge PC. Tier 2 adds the one
input the prototype lacks — the nutrunner's torque result — joined to the camera's bolt identity.*

### Tier 0 — the installed release, and the development launcher

The demo runs from an installed release under its supervisor: `install-release.ps1`, then sign in
as the station account and the HMI comes up by itself (Section 33). The script below is what it
replaced. It stays for development — a debug engine from the build tree, the Vite dev server with
hot reload, the API in Docker — and is renamed so nobody runs it on stage.

```powershell
# scripts/start-dev.ps1 — development only; run from C:\dev\torquesense with C:\msys64\ucrt64\bin on PATH
param([int]$Stations = 1)
$ErrorActionPreference = "Stop"
$env:API_KEY = (Select-String -Path .env -Pattern '^API_KEY=(.*)$').Matches[0].Groups[1].Value

docker compose up -d                                    # the platform; Jenkins stays in profile "ci"
docker compose ps                                       # all four healthy or running

Start-Process .\build\windows-release\softplc\softplc.exe
.\.venv\Scripts\Activate.ps1
Start-Process powershell -ArgumentList '-NoExit','-Command',
  '.\.venv\Scripts\uvicorn.exe sidecar.main:app --host 127.0.0.1 --port 8001'
while (-not (Invoke-RestMethod http://127.0.0.1:8001/health -ErrorAction SilentlyContinue)) { Start-Sleep 1 }

$ids = @("W-14","W-15","W-16","W-17")[0..($Stations-1)]
for ($i = 0; $i -lt $ids.Count; $i++) {
  $id = $ids[$i]; $control = 8095 + 100 * $i            # each station's ports are offset by 100
  Start-Process .\build\windows-release\control-app\torquesense.exe `
    -ArgumentList "--config config\station_$($id.Replace('-','').ToLower()).yaml"
  Start-Process .\build\fleet-agent\fleet-agent.exe `
    -ArgumentList "-station $id -engine http://127.0.0.1:$control/health -api http://127.0.0.1:8000/v1/health/host -disk C:\dev"
}

Start-Process powershell -ArgumentList '-NoExit','-Command','cd web; npm run dev'
Start-Sleep 4
Start-Process msedge -ArgumentList '--kiosk','http://127.0.0.1:5173/hmi/W-14',
  '--edge-kiosk-type=fullscreen','--user-data-dir=C:\dev\tools\edge-kiosk'
Start-Process msedge -ArgumentList 'http://127.0.0.1:5173/'
```

**Desk mode — the live segment.** Phone on its stand over the junction box, the laptop's webcam on
the clip board, both stations beside the rendered ones:

```powershell
Start-Process .\build\windows-release\control-app\torquesense.exe -ArgumentList "--config config\station_desk1.yaml"   # phone
Start-Process .\build\windows-release\control-app\torquesense.exe -ArgumentList "--config config\station_desk2.yaml"   # webcam
```

Join the phone's hotspot before starting (mobile data off). If the phone fails on stage, its
station goes AMBER, as designed, and the webcam station or the rendered wheel carries the demo.

Microsoft Edge is used for the kiosk because it ships with Windows 11 — one less install. The
separate `--user-data-dir` keeps the kiosk window from attaching to your normal browser session.
Press `Alt+F4` to leave kiosk mode.

### Tier 1 — public showcase

A judge or recruiter should be able to see the system without your laptop.

**Live, from Supabase.** The Vercel build reads Supabase directly with the publishable key:

- **Recent data.** Recent cycles, anomalies with their clips, first-time-right per hour and the
  fleet charts.
- **New verdicts.** Each one appears the moment the outbox delivers it, through a Realtime
  subscription.

Set two environment variables in the Vercel project, `VITE_SUPABASE_URL` and
`VITE_SUPABASE_PUBLISHABLE_KEY`. Both are public by design: RLS limits that key to reading the
showcase (Section 16).

**Replay, as the fallback.** When the laptop is off, the live page simply shows the last data it
has. For a page that always plays a full cycle, build **replay mode**: the WebSocket client is
swapped for a reader that plays back a recorded session.

```powershell
# record one session during a rehearsal
.\build\windows-release\control-app\torquesense.exe --config config\station_w14.yaml `
    --record web\public\replay\session.jsonl
# copy the referenced clips from Supabase Storage (bucket clips) into web\public\replay\clips\

cd web
$env:VITE_REPLAY = "1"; npm run build
npx vercel --prod
```

The replay is labelled as one on screen. Only synthetic renders are published — there is no real
worker video to protect, which is a side benefit of the software-only build.

### Tier 2 — production, deferred on purpose

The station pod on RKE2 with the GPU Operator, the GitOps release path, the plant platform and the
industrial camera are designed and kept unchanged in Appendix D. What the prototype already
contributes to them is real: the same engine source, the same contracts and tag names, and
container images and manifests that a manual workflow still proves in a kind cluster.

### Go-live checklist — the night before

| Check | How |
|---|---|
| Model frozen and tagged | `git tag demo-v1`; no retraining after this |
| Both mAP figures in `VALIDATION.md` match `docs/accuracy.json` | `python training\eval.py --check` |
| Latency gate passes on this laptop today | `latency_test` after locking clocks |
| All ten scenarios fire correctly, both edge cases silent | `ctest -R Scenario` |
| Alerts need no network | pull the network mid-cycle: alerts and HMI continue; the outbox drains when it returns |
| Supabase project awake and under quota | the watchdog's last run is green; database well under 500 MB |
| The demo runs from an installed release | `C:\TorqueSense\current` points at a release whose `gates.json` passed; `http://127.0.0.1:8099/health` returns 200 |
| Node-RED lamps respond | the supervisor's `node-red` process is ready; no firewall rule is needed any more |
| The release checklist is complete | Section 33: rollback rehearsed, power-cut test, watchdog armed, backup fresh |
| Four-lug CAD recompiles to 1, 3, 2, 4 | Run the compiler once |
| Samples pass in both engines | `python samples\run_samples.py` PASS; `torquesense --conformance samples` agrees |
| Desk stations live | phone hotspot joined, mobile data off; both desk configs start and log the resolutions they asked for |
| Fail-visible and self-healing work | End `torquesense.exe` in Task Manager: its lamp is AMBER within 2.5 s, then green again as the supervisor restarts it |
| Fleet charts are out of baseline | `/fleet` shows no dotted "learning" cells — the 24-minute demo-mode baseline was captured in rehearsal and persisted |
| Every fleet agent reporting | `/fleet` shows host health for each station |
| Fallback recording present and plays | `data\demo_fallback.mp4` open in a second window |
| Projector colour check | Red, amber, green distinguishable from the back row |
| No secrets committed | `git log -p \| Select-String -Pattern "password|secret|token"` returns nothing |
| AC adapter packed | Not a joke. On battery, p99 doubles. |

---

## 33. Production readiness — the prototype that runs unattended

Until v5.4 the demo was a script you ran: eight windows, a dev server, Docker, and you watching.
This section makes the **same software** run the way a station must: it starts itself after a power
cut, restarts what crashes, ships as a verified release that can be rolled back in a minute, is
checked on the GPU before it goes live, and tells you when it is unwell while you are elsewhere.
Nothing here adds a feature a judge sees. It removes the ways a working demo fails on the day.

### The bar — what "production-ready" means here

| The station must… | How | Proven while writing this |
|---|---|---|
| start by itself after a reboot or power cut | Autologon into a standard account; Task Scheduler starts the supervisor at sign-in and restarts it if it exits | registration script parsed and linted for Windows PowerShell 5.1 |
| survive any one process crashing | the supervisor restarts it with backoff (1 s doubling to 30 s) and flags a crash loop | Go tests, run as a Linux binary and as a Windows binary under Wine |
| never leave an orphan holding the camera, GPU or a port | every child in a Windows job object with *kill on close* (Linux: `Pdeathsig`) | supervisor killed with `kill -9`: every child gone, on Linux and under Wine |
| start in the right order | each process waits until what it needs answers on its port or `/health` | Go test: the engine started after the PLC was ready, though defined first |
| run the same bits on every install | a release bundle staged from PE import tables; children get an explicit `PATH`, never the user's | the bundler run on ONNX Runtime 1.28.0's real DLLs; `PATH` test |
| be traceable to a commit and verifiable | a tag builds the release in GitHub Actions with a build-provenance attestation and SHA-256 | `actionlint` clean |
| be validated on the GPU before anyone installs it | a release gate on the laptop: samples, provider parity, latency per provider, four-station soak → `gates.json`; the installer refuses a release without a passing one | 5 tests of the decision rules |
| upgrade without risk, roll back in one command | releases side by side; a junction names the live one; an unhealthy switch reverts itself | PowerShell parsed and linted |
| degrade visibly, never silently | TensorRT falls back to CUDA only between providers the gate validated, and says so | — design; see below |
| be watched when you are not there | an hourly GitHub Actions watchdog emails you when health data goes stale, and keeps Supabase awake | 2 tests, one against a local HTTP server |
| lose no verdicts, and recover the database | the outbox (Section 16) plus a nightly `pg_dump`, 14 kept | script parsed and linted |
| run with the least privilege that works | a standard account, a read-only config folder, loopback-only ports, no inbound firewall rule | — |

### One inference path for production — and why it is this one

**ONNX Runtime with the TensorRT execution provider, FP16, with the CUDA provider as its validated
fallback.** This is the v5.4 path, promoted from "adopted if the gate passes" to "the production
path, released only through the gate". The decision is final for this prototype; the alternatives
lose for reasons specific to it:

- **Native TensorRT C++.** Its API is C++ classes compiled with MSVC; the engine is built with MSYS2
  GCC, and a C++ ABI does not cross compilers. ONNX Runtime's C API does, which is why the provider
  route works (Section 28).
- **Triton Inference Server.** It puts an HTTP or gRPC hop and a second server inside a 150 ms
  evidence budget, and its home is Linux.
- **DeepStream.** It is Linux and Jetson only.
- **Windows ML.** It downloads and updates its execution providers through Windows itself, so the
  exact TensorRT the gate measured is not guaranteed to be the one that runs. A gate is worthless
  if what ships can change after it.
- **The Python TensorRT plan.** It pins the wrong major version (Appendix A, item 33), and the engine
  is C++.

**The fallback rule replaces v5.4's "load or do not start".** The station config lists providers
in preference order — `execution_providers: [tensorrt, cuda]` — and the engine reads the release's
`gates.json` and drops any provider the gate did not validate. It tries them in order:

- **The first one loads.** Normal operation.
- **A later one loads.** The engine runs, and `/health` reports `degraded: "tensorrt unavailable:
  <the loader's message>"`. The dashboard shows a DEGRADED banner and the fleet view records it.
  The PLC stays green: the CUDA path passed the same parity gate, so its verdicts are equally
  valid. This is a visible condition, not a silent one.
- **None loads.** The engine refuses to start. The heartbeat stops and the station goes AMBER
  (Section 19).

A provider the gate did not validate is never tried. That keeps the v5.4 principle intact: nothing
unmeasured decides.

### What runs, and what starts it

```mermaid
flowchart TB
  subgraph WIN["HSGPC — Windows 11, account tsdemo (standard), signed in by Autologon"]
    TS["Task Scheduler<br/>at sign-in · restart on failure"] --> SUP["supervisor.exe :8099<br/>job object · restart with backoff<br/>/status · /health · /maintenance"]
    TS --> K["kiosk.ps1<br/>Edge kiosk, kept open"]
    TS -. "02:30 daily" .-> BK["backup.ps1<br/>pg_dump · keep 14"]
    SUP --> PLC["softplc.exe :4840"]
    SUP --> API["API + outbox :8000<br/>serves web/dist"]
    SUP --> SC["sidecar :8001<br/>Florence-2, offline"]
    SUP --> NR["Node-RED :1880"]
    SUP --> ENG["torquesense.exe :8095<br/>TensorRT EP → CUDA EP"]
    SUP --> AG["fleet-agent.exe"]
    ENG -- "OPC-UA · heartbeat" --> PLC
    K --> API
  end
  API -. "outbox, when online" .-> SB[("Supabase<br/>Postgres · Storage · Realtime")]
  BK -.-> SB
  GH["GitHub Actions · hourly watchdog"] -. "keep awake · stale? → email" .-> SB
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class SUP,ENG key;
```

*Figure 8 — The station at run time. One scheduled task starts one supervisor; the supervisor owns
every TorqueSense process. Docker is not in the picture: the API and Node-RED run natively, so the
demo no longer depends on Docker Desktop starting, on WSL2's memory, or on a container network.*

Docker is now for development and for Jenkins in CI mode only. Running the API and Node-RED
natively frees the WSL2 virtual machine's memory in Demo and Line modes, and removes the one
component that needed the soft PLC to listen beyond loopback (Section 15).

### The supervisor

Part of the Go module that already holds the fleet agent: `fleet-agent/cmd/supervisor` and
`internal/supervise`, standard library only, one static binary. Its configuration is
`C:\TorqueSense\config\supervisor.json`, written once by `register-station.ps1` from the release's
template. An excerpt:

```json
{
  "vars":  { "release": "C:\\TorqueSense\\current", "config": "C:\\TorqueSense\\config",
             "data": "C:\\TorqueSense\\data", "cuda_bin": "…", "cudnn_bin": "…",
             "trt_lib": "C:\\dev\\tools\\TensorRT\\lib", "node": "C:\\Program Files\\nodejs\\node.exe" },
  "path":  ["{release}\\bin", "{cuda_bin}", "{cudnn_bin}", "{trt_lib}"],
  "log_dir": "{data}\\logs",
  "processes": [
    { "name": "softplc", "exe": "{release}\\bin\\softplc.exe", "args": ["4840"],
      "ready": "tcp://127.0.0.1:4840" },
    { "name": "engine-w14", "exe": "{release}\\bin\\torquesense.exe",
      "args": ["--config", "{config}\\station_w14.yaml", "--release", "{release}"],
      "env_file": "{config}\\.env", "ready": "http://127.0.0.1:8095/health", "ready_timeout": "120s",
      "stop_url": "http://127.0.0.1:8095/shutdown", "after": ["softplc", "api", "sidecar"] }
  ]
}
```

| Behaviour | Rule |
|---|---|
| Start order | Each process waits until everything in its `after` list is ready. Ready means a TCP connect to `tcp://…`, or `200` from `http://…`, inside `ready_timeout`; otherwise the process is killed and retried |
| Restart | Any exit restarts it after a backoff: 1 s doubling to 30 s. A run of 60 s resets the backoff. Five starts in five minutes marks it `crash_looping` in `/status`; it keeps retrying at the slowest rate |
| Dependencies after start-up | A dependency's later restart does not restart its dependents. The engine reconnects to the PLC by itself, so cascading restarts would only lengthen an outage |
| Stopping | Reverse order. Each process is first asked through its `stop_url`, then killed after `stop_grace` (5 s default) |
| If the supervisor dies | Windows closes its job object and terminates every child, grandchildren included. Task Scheduler restarts the supervisor a minute later |
| Environment | The parent environment, plus `env_file` (read at each start, so an edited `.env` applies on the next restart), plus `env`. `PATH` is replaced by `path` followed by the Windows directories, so nothing depends on what is on the user's `PATH` |
| Logs | One file per process in `data\logs`, rotated at 10 MB with five generations kept. A process that logs in a loop cannot fill the disk |
| No console windows | Children start with `CREATE_NO_WINDOW`: the screen shows the HMI, not eight terminals |
| Validation | `supervisor -config … -check` prints the start order. It refuses unknown keys, a missing dependency, a cycle, an unresolved `{var}`, an empty variable, and a bare program name that would be looked up on `PATH` |

| Endpoint (127.0.0.1:8099 only) | Answers |
|---|---|
| `GET /status` | every process: state, PID, starts, last exit, `crash_looping` |
| `GET /health` | `200` only when every process is ready; `503` naming the first that is not |
| `POST /maintenance?on=1` / `?on=0` | stops everything in order and holds it stopped; then starts it again. The release gate uses it to have the GPU to itself |
| `POST /shutdown` | an orderly stop of everything, then exit; the installer uses it before switching releases |

Ten tests cover the rules above, and they run against real child processes. The Windows build of
the test suite passes under Wine: job object, hidden windows, `PATH`, maintenance. Killing the
supervisor with `kill -9` left no child running on Linux or under Wine. Wine is not Windows, so the
first run on HSGPC repeats that check: end `supervisor.exe` in Task Manager and confirm that no
`torquesense.exe` survives it.

### A release: built, attested, gated, published

```mermaid
flowchart LR
  T["git tag v5.5.0<br/>git push --tags"] --> GA["GitHub Actions · release.yml<br/>MSYS2 build · tests without GPU<br/>bundle.py · web build · Node-RED"]
  GA --> AT["attestation<br/>SHA256SUMS"] --> DR[("draft release<br/>torquesense-v5.5.0-win64.zip")]
  DR --> JG["Jenkins on HSGPC · Jenkinsfile.release<br/>maintenance window · stage beside live<br/>samples · parity · latency ×2 · soak"]
  JG -- "pass" --> PUB[("published release<br/>+ gates.json + models-v5.5.0.zip")]
  JG -- "fail" --> X["stays a draft<br/>staged copy kept for inspection"]
  PUB --> INS["install-release.ps1<br/>verify · prove · switch · auto-rollback"]
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class JG,INS key;
```

*Figure 9 — Release flow. Hosted CI builds what needs no GPU and signs it; the laptop proves what
needs the GPU and publishes it. Nothing is installed that did not pass both.*

| Release asset | Made by | Contains | Checked by the installer |
|---|---|---|---|
| `torquesense-vX.Y.Z-win64.zip` | GitHub Actions | `bin\`: engine, soft PLC, supervisor, fleet agent, gate tools, MinGW and ONNX Runtime DLLs (all three providers), `bundle.json`; `web\dist`; `api\`, `sidecar\`; profiles, samples, scripts; `models\manifest.json`; `requirements.lock`; Node-RED with its `node_modules`; `release.json` | SHA-256 against `SHA256SUMS`, then `gh attestation verify` against this repository |
| `models-vX.Y.Z.zip` | the release gate, from the local DVC store | the ONNX files | each file's SHA-256 against `models\manifest.json` inside the attested zip (I12) |
| `gates.json` | the release gate | verdict, validated providers and default, latency and parity results, soak, conditions | `passed` is true and `version` matches |

**Building the bundle.** `scripts/release/bundle.py` reads each executable's import table,
including delay-loaded imports, and follows it recursively. It copies what it finds in MSYS2's
`ucrt64\bin` and ONNX Runtime's `lib`. The three ONNX Runtime providers are added explicitly,
because ONNX Runtime loads them with `LoadLibrary` and no import table names them. Every DLL is
sorted into one of four groups, and the build fails if any DLL fits none of them:

| Group | What it holds | What happens to it |
|---|---|---|
| bundled | MinGW runtime, ONNX Runtime | copied into `bin\` |
| GPU runtime | NVIDIA's CUDA, cuDNN and TensorRT DLLs | not shipped; recorded as requirements |
| MSVC runtime | the Visual C++ runtime ONNX Runtime was built with | not shipped; recorded as requirements |
| system | Windows | not shipped |

Run on ONNX Runtime 1.28.0's real files, it found what the TensorRT provider needs from the
machine: `nvinfer_10.dll`, `nvonnxparser_10.dll`, `cudnn64_9.dll`, `cublas64_13.dll`, and the MSVC
runtime including `MSVCP140_1.dll`. `gpu_preflight.py --bundle` then checks exactly that list on
exactly the supervisor's `PATH`.

**Attestation needs a public repository.** GitHub offers artifact attestations on its free plans
for public repositories only; a private one needs Enterprise Cloud. A portfolio repository is public
anyway. If it ever has to be private, drop the attestation step and keep the SHA-256 check.

**The Python environment is locked.** `requirements-runtime.in` lists what the API and sidecar
import. On HSGPC, `uv pip compile … --generate-hashes --python-platform x86_64-pc-windows-msvc`
turns it into `requirements.lock`, with the cu130 index for torch; the exact command is at the top
of the `.in` file. The lock is committed, and the installer installs it with
`pip install --require-hashes`, so a substituted wheel fails the install. Environments live in
`venvs\<first 12 hex of the lock's SHA-256>`, so releases that share a lock share one environment
and its 3 GB of torch. The command was checked for the Windows target without torch; the PyTorch
index was not reachable from where this was written.

### On disk

```text
C:\TorqueSense\
├── current\            → junction to the live release; Task Scheduler and supervisor.json point here
├── previous.txt        the release before it, for rollback
├── releases\
│   ├── v5.5.0\         one folder per release, never edited after install
│   │   ├── bin\        bundle.json · torquesense.exe · supervisor.exe · fleet-agent.exe · DLLs
│   │   ├── venv\       → junction to venvs\<lock hash>
│   │   ├── models\  web\dist\  api\  sidecar\  node-red\  profiles\  samples\  scripts\
│   │   └── gates.json  release.json
│   └── v5.4.2\
├── venvs\<lock hash>\  Python environments, shared by releases with the same lock
├── config\             machine-specific, survives every upgrade · read-only to tsdemo
│   ├── supervisor.json  station_w14.yaml  cameras.yaml  .env
└── data\               survives every upgrade · writable by tsdemo
    ├── outbox.db  spool\  logs\  trt-cache\<TensorRT version>\<model hash>\
    ├── hf-cache\  node-red\  edge-kiosk\  backups\
```

Configuration and data never live inside a release, so a rollback cannot lose either. The
TensorRT cache is keyed by TensorRT version and model hash, so two releases with the same model
share one engine.

### Install, switch, roll back

```powershell
# as Administrator, with gh signed in (gh auth login)
cd C:\TorqueSense\current\scripts\station          # or the repository's scripts\station the first time
.\install-release.ps1 -Version v5.5.0
```

What it does, in order — it stops at the first failure, and nothing live has changed until the
last step:

1. **Downloads** the zip, `SHA256SUMS`, `gates.json` and the model pack with `gh release download`.
2. **Verifies** the SHA-256, then the attestation; refuses a `gates.json` that is missing or failed.
3. **Unpacks** to `releases\vX.Y.Z` and checks every model file against the manifest.
4. **Python.** Reuses or creates the locked environment, and fetches Florence-2 once, at its pinned
   commit, into `data\hf-cache`. The sidecar runs with `HF_HUB_OFFLINE=1`, so a demo never downloads.
5. **Proves it on this machine.** Runs `gpu_preflight.py --bundle` on the supervisor's `PATH`, then
   every sample through this release's engine (`--conformance`). If TensorRT is validated, it builds
   the TensorRT engine cache.
6. **Switches.** It sends `POST /shutdown`, repoints `current`, starts the task, and waits up to four
   minutes for `/health`. If the new release is not healthy, it puts the previous one back by
   itself and exits with an error.

`.\rollback-release.ps1` swaps back to `previous.txt` in about a minute, with no download. The
release gate runs the same installer with `-StageOnly -SkipGates`, which installs beside the live
release and proves it, but never switches.

Two Windows details are handled in the module rather than left to chance:

- **The junction is removed with `cmd /c rmdir`.** In Windows PowerShell 5.1, `Remove-Item` on a
  junction can recurse into its target and delete the live release.
- **The scripts are pure ASCII.** Windows PowerShell 5.1 reads a file without a byte-order mark as
  ANSI, which corrupts any non-ASCII character. PSScriptAnalyzer checked all six files for 5.1 syntax
  compatibility.

### One-time setup of the station

| Step | How |
|---|---|
| A standard local account for the station | Settings → Accounts → Other users → `tsdemo`, standard user. The demo runs with no administrator rights |
| Register | `scripts\station\register-station.ps1 -User tsdemo`, as Administrator. It creates `C:\TorqueSense` with permissions (config read-only to `tsdemo`; releases staged by your own account), finds the CUDA and cuDNN DLL folders and writes them into `supervisor.json`, sets the power plan, and registers three tasks: **Supervisor** and **Kiosk** at sign-in (no time limit, restart every minute on failure, run on battery), **Backup** daily at 02:30 (run late if the laptop was off) |
| Sign in automatically | Sysinternals **Autologon** for `tsdemo`; it stores the password as an encrypted LSA secret, not in the registry in clear |
| Camera access for the account | signed in as `tsdemo`: Settings → Privacy & security → Camera → *Let desktop apps access your camera* on |
| First release | `install-release.ps1 -Version vX.Y.Z` |
| Prove a power cut | hold the power button until the laptop is off, switch it on, touch nothing: the HMI comes up green |

### Hardening the demo machine

| Risk on the day | Setting |
|---|---|
| Sleep, hibernate or lid close mid-demo | on AC: never sleep, never hibernate, display always on, lid close does nothing — set by `register-station.ps1` with `powercfg` |
| Windows Update restarts or throttles the laptop | Settings → Windows Update → pause, up to 35 days at a time and renewable on this Insider build; active hours cover the event |
| Notifications over the kiosk | Do not disturb on for `tsdemo`; Focus sessions off |
| A second user session takes the GPU | sign out your own account during the demo; Jenkins stays stopped |
| An unexpected inbound connection | no firewall rule for `softplc.exe` any more. Node-RED is native now, so nothing outside the laptop needs port 4840; remove the rule from v5.3's setup |
| Defender scanning a 3 GB venv at sign-in | the existing exclusion moves to `C:\TorqueSense` as well as `C:\dev\torquesense` |
| Secrets readable by anything | `config\.env` is readable only by `tsdemo` and Administrators. The publishable key in the dashboard is public by design; RLS limits it (Section 16) |

### Watching it

| Where | What it tells you |
|---|---|
| `http://127.0.0.1:8099/status` | every process: state, restarts, last exit code, crash loops |
| `C:\TorqueSense\data\logs\<process>.log` | each process's own output, rotated; the engine writes JSON lines |
| `/fleet` on the dashboard | the six drift charts and host health (Section 19); a DEGRADED banner when running on the fallback provider |
| Your email | the hourly **watchdog** workflow fails, and GitHub emails you, when the newest health bucket in Supabase is older than 30 minutes and the repository variable `TS_EXPECT_ONLINE` is `true`. Set it to `true` for demo days; `false` the rest of the time |

The watchdog reads one row with the public, RLS-limited key the Vercel page already uses, so the
workflow holds no new secret. The same request is the keep-alive that stops the free project
pausing after a week idle. GitHub disables scheduled workflows in a public repository after 60 days
without activity; any push resets that.

### Backups and recovery

| Data | Protection | Worst case |
|---|---|---|
| Verdicts and health not yet delivered | the outbox on the laptop's disk, drained in order when the link returns (Section 16) | none while the disk survives |
| The Supabase database | `backup.ps1` nightly: `pg_dump --format=custom` of `public`, newest 14 kept in `data\backups`; `last.json` records the result | up to 24 hours of rows — the free plan keeps no backups of its own |
| Clips in Supabase Storage | not backed up | synthetic renders and desk recordings, all reproducible; said plainly rather than solved |
| Configuration | `config\` is outside every release; copy it whenever it changes | — |

Restore drill, once before the event: `pg_restore --clean --no-owner -d <a scratch project's URL>
<newest dump>`, then point a local dashboard at it. A backup is real only after a restore has been
done.

### The release checklist — done means all of these

| Check | Evidence |
|---|---|
| CI green on the tagged commit, release draft created with attestation | GitHub Actions run |
| Release gate passed | `gates.json` on the release: `passed: true`, providers listed |
| Installed with the installer, not copied by hand | `C:\TorqueSense\current` → the new release; `/health` 200 |
| Rollback rehearsed on this release | `rollback-release.ps1`, then `install-release.ps1` again |
| Self-healing shown | end `torquesense.exe` from Task Manager: HMI AMBER within 2.5 s, back to green as the supervisor restarts it |
| Power-cut test | switch off at the button; on again; green with no key pressed |
| Watchdog armed | `TS_EXPECT_ONLINE=true`; one manual run of the workflow is green |
| Backup fresh and restorable | `data\backups\last.json` from last night; restore drill done once |

### When something goes wrong

| You see | First action |
|---|---|
| HMI AMBER, `/status` shows the engine `crash_looping` | read `data\logs\engine-w14.log`; if it started with this release, `rollback-release.ps1` |
| HMI AMBER, `/status` unreachable | Task Scheduler → *TorqueSense Supervisor* → last run result; start it by hand |
| DEGRADED banner | `/health` names the TensorRT loader error; run `gpu_preflight.py --bundle …` — usually a driver update moved a DLL |
| Watchdog email | open the dashboard: if the laptop is on and green, the outbox is draining after an outage; otherwise check power and network |
| Install refused | the message names the failed check; nothing live was touched |
| The switch rolled itself back | `/status` from the attempt is printed in the installer's output; the staged release stays in `releases\` for inspection |

### What is still not production — on purpose

This is a production-ready **prototype**: one laptop, run like a station. These remain Appendix D
material, and the pitch should say so:

- **Hardware.** One laptop is a single point of failure, and there is no standby.
- **Line and safety.** The PLC link is OPC-UA without `SignAndEncrypt` to a soft PLC, and there is
  no functional-safety rating.
- **Cameras.** The industrial camera is deferred.
- **Signing and backups.** The executables carry provenance attestations, not Authenticode
  signatures, so Windows SmartScreen still sees an unsigned program. Backups depend on the laptop
  being on at night.

---

## 34. Caching strategy

**Cache computation, never authority.** Every key is content-addressed, so nothing can go stale and
there is no invalidation logic to get wrong.

### Where caching pays, and where it does not

| Cache | Value | Why |
|---|---|---|
| ONNX session + warm-up | ✅ **required** | Creating a session and running cuDNN's exhaustive algorithm search costs about a second. Done once at startup, never per frame. |
| Florence-2 model load | ✅ **required** | Loading weights takes several seconds. Load once at sidecar start, warm up once. |
| Rendered dataset by seed | ✅ **high** | Re-rendering 4,000 frames is 30 minutes. Keyed by seed, generator version, and glTF hash, so a CAD change produces a new folder automatically. |
| YOLO decode cache on Colab | ✅ **high** | `cache='disk'` decodes each JPEG once rather than every epoch. |
| Adjudication results | ⚠️ **modest** | Keyed by crop hash. Helps only when the same ambiguous frame is asked about twice — retries and scenario replays. Do not claim it improves throughput. |
| Verdicts, alerts, anomaly rows | ❌ **never** | These are facts about one cycle at one time. A cached verdict is a wrong verdict. |
| A cache server (Redis) | ❌ **cut** | Nothing here needs one. On 16 GB, another service is a cost with no measurable gain. |

### What is cached, where, for how long

| Layer | Key | Lifetime | Invalidated by |
|---|---|---|---|
| ONNX session, CUDA arena | process | until engine restart | restart after a new model |
| Florence-2 weights in VRAM | process | until sidecar restart | restart |
| Florence-2 weights on disk | `HF_HOME=C:\dev\hf-cache` | indefinite | manual |
| Adjudication results | SHA-256 of crop bytes · model id · phrase | 256-entry in-process LRU | eviction |
| Rendered frames | `synth/{glTF-sha}-{generator-ver}-s{seed}/` | until generator or CAD changes | a new key simply creates a new folder |
| Compiled procedure | `procedure_{station}_{cad_revision}.json` | until the CAD revision changes | new revision, new file |
| EWMA chart state | `(station, metric)` in `health_chart_state` | until a redeploy | a `model_version` or `cad_revision` change resets it to baseline |
| Fetched C++ sources | `build\<preset>\_deps\` | until the build folder is deleted | CMake `FetchContent`; tags pinned in `cmake/deps.cmake` |
| MJPEG frames in the browser | — | **never cached** | `Cache-Control: no-store` on `:8090` |

### The render manifest is the cache key

```python
# synth/generate.py — the output folder name is the cache key
import hashlib, json, pathlib
GENERATOR_VERSION = "3"
def output_dir(gltf: pathlib.Path, seed: int) -> pathlib.Path:
    sha = hashlib.sha256(gltf.read_bytes()).hexdigest()[:12]
    d = pathlib.Path("data/synth") / f"{sha}-g{GENERATOR_VERSION}-s{seed}"
    if (d / "manifest.json").exists():
        print(f"cache hit: {d}"); raise SystemExit(0)    # same CAD, same code, same seed
    return d
```

Change the CAD and the hash changes; change the generator and bump `GENERATOR_VERSION`. Either way
the old folder stops matching. Nothing is ever deleted to "invalidate" it.

### The demo fallback

If anything fails on stage, `data\demo_fallback.mp4` is a screen recording of a perfect run, already
open in a second window. It is the one cache of a *result* this system permits — because it is
labelled as a recording and shown as one.

---

# Appendices

## Appendix A — Corrections to earlier drafts

Each of these was stated in an earlier version of this design. Each is wrong or superseded, and the
reason matters more than the fix.

| # | Earlier claim | Corrected | Why |
|---|---|---|---|
| 1 | MinIO for object storage | **Garage** | MinIO stopped publishing community Docker images in October 2025 and archived its repository in 2026. `docker pull minio/minio` no longer gets maintained images. |
| 2 | CUDA 12.1 exactly, TensorRT 8.6 | **CUDA 13.2, no TensorRT** | Your installed toolkit is supported everywhere now; TensorRT's five milliseconds are not needed and cost an exact version pin. |
| 3 | Qwen2.5-VL on Colab through a tunnel | **Florence-2-base, local** | Fits in 6 GB beside the detector; removes the tunnel as a demo-day risk. |
| 4 | `transformers==4.44.2`, plus `timm`, `einops` | **Current `transformers`**, neither extra | 4.44 predates the native `Florence2ForConditionalGeneration` class; the extras were for the remote-code checkpoint. |
| 5 | Check `Ort::GetAvailableProviders()` for CUDA | **Catch the append exception, then time a warm-up** | `GetAvailableProviders` lists compiled-in providers, not loadable ones, so it passes on the very failure it should catch. |
| 6 | Florence-2 needs ~1.6 GB for weights | **~0.46 GB weights; ~1.2–1.5 GB as a process** | 230M parameters × 2 bytes. The process total includes the CUDA context and activations. |
| 7 | Four stations at 30 fps under 40% GPU | **Four stations at 15 fps, measured** | The earlier figure was not measured. Four processes time-slice one GPU. |
| 8 | "≈ 25–30 ms" alert latency | **Engine ≈ 30 ms, evidence ≈ 65 ms** | The earlier figure omitted the debounce frame. Quote both, with the clock defined. |
| 9 | "6 GB will thrash" for YOLO training | **It fits; Colab is for parallelism and heat** | YOLO11n at batch 16 trains comfortably on 6 GB. |
| 10 | I9: byte-identical renders | **Identical labels, PSNR ≥ 45 dB** | GPU path tracing is not guaranteed bit-exact across driver versions. |
| 11 | Star solver for even N | **Fallback to first unvisited start** | The earlier version looped forever on N = 8. |
| 12 | Qt 6 operator HMI | **Kiosk route of the React app** | A second UI toolchain on a 16 GB machine for no rubric gain. |
| 13 | MediaMTX for live video | **MJPEG from the engine** | The clip ring already holds JPEG frames; streaming them costs no extra encode and no extra service. |
| 14 | OPC-UA write on the verify thread | **Dedicated alert thread** | It is blocking I/O, which the hot-path rules forbid on the verify thread. |
| 15 | Scenario implied the system catches 62 Nm vs 103 Nm | **It catches sequence, not torque value** | A camera cannot see clamp load. Torque value stays the nutrunner's job until Tier 2 joins the two. |
| 16 | Presets named `wsl-asan`, `wsl-tsan` | **`linux-asan`, `linux-tsan`** | The same presets run in GitHub Actions; they were never WSL-specific. |
| 17 | Python 3.11 | **Python 3.12, latest patch** | You already have 3.12, and the cu130 wheel line covers it. |
| 18 | Set the soft PLC's `serverUrls` to `host.docker.internal` so Docker clients can resolve it | **Leave `serverUrls` at its default** | Tested against open62541 1.4.21. The server already echoes each client's own URL in GetEndpoints; asyncua, the open62541 client and node-opcua all connected while it ran under an unresolvable hostname. And in 1.4 `serverUrls` also sets the bind address: a name that is not a local interface leaves the server with no socket, and it exits. The v4.0 advice would have broken the soft PLC. |
| 19 | A procedure engine shaped around one procedure: bolt rising edges, one star sequence | **A generic interpreter over profiles** | The problem statement names three assemblies; the v4 engine could check one of them. |
| 20 | Every frame processed at 960×540 | **Detector on the downscale; inspection-point crops from a full-resolution pool** | The new resolution check put the wheel's bolt face at 12.8 px at 960 px wide — below the 16 px threshold. |
| 21 | The star solver started wherever the angle sort began | **Start at member 1, run in the numbering's direction** | It produced 2, 5, 3, 1, 4 for the wheel: a valid star, but not the documented 1, 3, 5, 2, 4. |
| 22 | `observed_bolt`, `expected_bolt`, `TORQUE_SEQUENCE_VIOLATION` | **`observed_ip`, `expected_ip`, `profile`, `profile_version`; six generic violation kinds** | A door clip is not a bolt. |
| 23 | Evidence latency ≈ 65 ms | **≈ 70 ms, an estimate** | The crop-classification stage adds a few milliseconds; `latency_test` will settle it. |
| 24 | k3s for Tier 2 | **RKE2** | NVIDIA's GPU Operator documents RKE2 explicitly and not k3s; one plant cluster with station agents replaces one cluster per line. |
| 25 | The fleet agent reads `nvidia-smi`, injected by `NVIDIA_DRIVER_CAPABILITIES=utility` | **The node's dcgm-exporter, through a node-local Service** | Under the GPU Operator's CDI injection a pod that claims no GPU gets no driver utilities. The GPU Operator already runs dcgm-exporter on every GPU node. |
| 26 | `runtimeClassName: nvidia` on the engine pod | **Removed** | From GPU Operator 25.10, GPUs are injected through CDI from the resource request alone. |
| 27 | MSVC from Visual Studio Build Tools, libraries from vcpkg | **MSYS2 UCRT64 GCC 16.2 and GDB 18.1; `pacman`, plus five libraries through `FetchContent`** | It is the toolchain already on this laptop. ONNX Runtime's MSVC-built package links from GCC through its C API — built and run, not assumed. And prebuilt packages remove the hour-long first build. |
| 28 | open62541 from vcpkg | **Vendored 1.4.21 amalgamation** | It is the version the watchdog was tested on; MSYS2 ships 1.5, which has not been. Built with MinGW, the soft PLC passed the heartbeat test. |
| 29 | The webcam as a development-only source that "never ships"; Basler as the camera | **Laptop webcam and phone are the prototype's cameras; the industrial camera is deferred** | The demo is live on the cameras you own. Their limits — rolling shutter, Wi-Fi latency, resolution — are stated and measured, not hidden. |
| 30 | Production platform and Tier 2 in the main line; `k8s-smoke` on every push | **Appendix D, deferred on purpose; the cluster test runs on demand** | The prototype should spend its hours, and its CI minutes, on the engine. |
| 31 | TimescaleDB and Garage in local Docker containers | **One free Supabase project — Postgres, Storage, Realtime — fed through a local outbox** | Supabase marks `timescaledb` deprecated on Postgres 17 and offers only its Apache edition, which has no continuous aggregates or retention policies — so the schema became plain Postgres (`date_bin`, a view, `pg_cron`). The outbox keeps the alert path offline-safe, and Realtime makes the public dashboard live. |
| 32 | No TensorRT (item 2) | **TensorRT through ONNX Runtime's provider, adopted only through a parity and latency gate** | Item 2 rejected a standalone TensorRT with its own version pin. The provider already ships inside the ONNX Runtime package in use, so the cost is one zip and an engine cache, GCC never links TensorRT, and the gate keeps the CUDA path as the measured fallback. |
| 33 | A TensorRT prototype plan: `pip install tensorrt-cu13` into a venv under `%USERPROFILE%` | **TensorRT 10.13.3.9 zip under `C:\dev\tools`; Python bindings only if needed, pinned to 10.13.3.9** | Unpinned, that package is now TensorRT 11.4 (`nvinfer_11.dll`); ONNX Runtime's provider imports `nvinfer_10.dll`. The engine is C++ and needs the DLLs, not the Python module. And the user folder's space breaks tools (Section 26). |
| 34 | `start-demo.ps1` opening eight windows by hand | **A supervisor started by Task Scheduler at sign-in, from an installed release** | A demo that needs its author to start it, and that leaves processes dead when one crashes, is not production-ready. The script survives as `start-dev.ps1` for development. |
| 35 | The API and Node-RED in Docker at run time | **Native processes under the supervisor; Docker for Jenkins only** | Docker Desktop has to start with the user session, holds a WSL2 VM's memory, and was the only reason the soft PLC needed a firewall rule. |
| 36 | The Vite dev server for the dashboard and HMI | **The built dashboard, served by the API** | A dev server is not a production web server. The API's fallback route keeps HMI deep links working (`api/spa.py`, tested). |
| 37 | "A configured provider loads or the engine does not start" (v5.4) | **Only gate-validated providers, in order; a fallback is reported as DEGRADED** | Both providers pass the same parity gate, so falling back to CUDA keeps verdicts valid. Refusing to start would turn a driver update into an outage. |

## Appendix B — Coding-agent kit deltas

The `AGENTS.md` kits for Claude Code, Cursor and VS Code Copilot predate this document. Apply these
edits so the agents build what this spec says:

| Area | Change |
|---|---|
| Machine | Add CUDA 13.2, Python 3.12, Windows 11 Home, project root `C:\dev\torquesense` |
| Inference | ONNX Runtime `gpu_cuda13` is the only inference API in the engine. TensorRT is reached through its TensorRT provider, never linked or called directly; delete `.trt` artefacts from older kits. Pin TensorRT to 10.13.3.9; never add an unpinned `tensorrt` pip package. Engine caches live under `cache/trt/<TensorRT version>/<model hash>/` and are never committed. |
| Sidecar | Replace Qwen/vLLM/Colab with local Florence-2; delete SM 7.5, xformers and tunnel guidance |
| Storage | Supabase. Schema changes only as new files in `supabase/migrations/`, never in the dashboard. No TimescaleDB functions: `date_bin`, not `time_bucket`. Every write goes through `api/outbox.py`. `DATABASE_URL` and the S3 keys never appear under `web/` |
| Threads | Add the alert thread; the OPC-UA write is forbidden on the verify thread |
| Invariants | I4 restated as **evidence** latency; I7 restated as VRAM budgets; I9 restated as labels + PSNR; add I11 |
| HMI | Replace Qt rules with "operator HMI is the `/hmi` route; accepts no input" |
| Presets | `wsl-*` → `linux-*`; `windows-release`, `windows-debug`, `windows-ci` use MSYS2 GCC; no vcpkg anywhere — `pacman`/`apt` plus `cmake/deps.cmake` |
| Video | Replace MediaMTX with MJPEG on `:8090` |
| Hooks | The Claude Code guard hook's allowed `#ifdef _WIN32` paths are unchanged: `ipc/` and `control-app/src/Affinity.cpp` |
| Invariants | Add I12 (model manifest) and I13 (fail visible) |
| Operations rules | `fleet-agent/`: standard library only, `gofmt`, `go vet`, `go test -race`. `deploy/k8s/`: check with `kustomize build … \| kubeconform -strict`. `Jenkinsfile`: lint with `npm-groovy-lint`. Soft PLC: never set `serverUrls`. |
| Drift job | `api/fleet/ewma.py` parameters change only with a re-run of the simulation recorded in an ADR |
| Production | Every run-time process is started by the supervisor from an installed release; nothing reads the user's `PATH`. Station scripts stay ASCII-only and pass PSScriptAnalyzer with 5.1 compatibility. Never publish a release by hand: drafts are published only by `Jenkinsfile.release`. Configuration and data never go inside a release folder. |
| Profiles | Engine code may not name an assembly, a part or a family; everything specific lives in `samples/`. A change to the interpreter lands with the matching change to `reference/tsref` and passes the differential test. |
| Capture | `IFrameSource` is the only way frames enter: `FileSource`, `UvcSource`, `StreamSource`. Ask for a resolution and read it back. Capture arithmetic changes land with `reference/tsref/capture.py`. Production capture and platform code is deferred (Appendix D) — do not start it. |
| Samples | Every assembly is a folder in `samples/`. A change to the interpreter or to a profile passes `python samples/run_samples.py --template`; a new assembly is a new folder, never code. |

## Appendix C — Portfolio and interview mapping

### Resume bullets — update the numbers only after measuring

Your current resume quotes **45 ms p99** and describes a **remote** inference sidecar. Both are now
out of date. Once `latency_test` has run on this laptop, replace them with the measured figures, in
this shape:

> Built a five-thread C++20 pipeline (capture → inference → verification → alert → I/O) for
> real-time wheel-fitment verification, passing frames over cache-line-aligned lock-free SPSC ring
> buffers from a pre-allocated pool — zero heap allocation in the steady state, proven by a
> counting-allocator test — and measured **[N] ms p99 evidence latency** against a 1-second
> requirement under locked GPU clocks.

> Compiled the inspection procedure from CAD instead of hand-writing rules: a depth-first traversal
> of the STEP assembly tree with Open CASCADE extracts fastener geometry, a star-pattern solver
> derives the tightening order (property-tested for N = 4–12), and a deterministic state machine
> validates the observed sequence in O(1) per frame; the same CAD model generates the
> auto-labelled training set through BlenderProc.

> Designed a cross-process, cross-language shared-memory clip ring (Win32 named mapping, per-slot
> seqlocks, read from Python via `mmap`) so a local vision-language sidecar (Florence-2) can
> adjudicate ambiguous detections without copying frames or blocking the alert path; isolated it
> behind a circuit breaker, kept the build ASan/UBSan/TSan-clean in CI, and added TensorRT through
> ONNX Runtime only behind a gate — output parity on every scenario and a latency histogram per
> provider — so the faster path is adopted by measurement, not assumed.

Do not put a number on the resume that `docs/VALIDATION.md` does not contain.

### For Point Wild (C++, DSA, OS)

| Their assessment area | Where it is in this project |
|---|---|
| Processes and threads | five threads, seven processes, the reasons for each boundary (§14); a supervisor with dependency ordering, backoff and a Windows job object so no child outlives it (§33) |
| Synchronisation | SPSC memory ordering; seqlocks; why no mutex (§30, I2) |
| Memory management | frame pool, zero-allocation proof, VRAM budgets, arena caps (§13, §31) |
| IPC | named shared memory read from two languages (§30) |
| Scheduling | core pinning, priority classes, why not realtime, Windows timer resolution, EcoQoS (§14) |
| DSA | star solver, ring buffer, HdrHistogram, LRU adjudication cache |
| Debugging and optimisation | sanitizers, the latency clock definition, adopting TensorRT only by measurement (§30) |

### For Sun Fibo (CAD customisation, C++)

| JD line | Where it is |
|---|---|
| CAD API for features, bodies, properties | OCCT XCAF traversal of the STEP assembly — the same class of work as NX Open (§7) |
| DLL / class library development | `cad-extract`, `perception`, `procedure`, `ipc` as separate libraries behind interfaces |
| Assembly component automation | the procedure compiler reads the assembly and emits the station's procedure |
| Excel integration | `XlsxReporter` shift export, in C++ |
| Test scenarios and documentation | per-profile scenario matrices, `VALIDATION.md`, `functional-specification.md` |
| Two languages minimum | C++ core, Python sidecar and data pipeline, TypeScript front end |

### For Seagate (Software Development Intern, Supply Chain Operations and Services)

The group builds software that monitors storage fleets, improves operational visibility,
anticipates device issues, and supports redeployment, reliability and security decisions. The
operations track is the same work on a different fleet.

| What the role does | Where it is in this project |
|---|---|
| Monitor fleets; operational visibility | `/fleet`: telemetry from every station, six signals each drawn against that station's own normal (§19, §21) |
| Anticipate device issues | EWMA drift detection with limits set by simulation to a false-ticket budget; the anchor residual catches a knocked camera before detection fails (§19) |
| Redeployment and reliability decisions | Canary on one station, judged against its peers; promote or `kubectl rollout undo`; a fail-visible heartbeat (§19, §32) |
| Security | Model integrity checked before load; the agent listens on no port; certificate expiry as a health signal in Tier 2 (§19) |
| C/C++ and Python | C++ engine and soft PLC; Python API, drift job, data pipeline and tests |
| Automation scripts | the installer and its automatic rollback, the release bundler, the release gate, contract generation, the render pipeline, the drift job |
| CI/CD with Jenkins or GitLab | A `Jenkinsfile` for the GPU gates beside GitHub Actions for everything else (§31) |
| Containerisation and orchestration | Three Dockerfiles; Kustomize base and overlays; a kind smoke test kept as a manual workflow; the production target designed and deferred (Appendix D) |
| Linux/Unix | Linux sanitizer and headless builds, WSL2, Bash, the engine container |
| Git workflows and code review | Short-lived branches into `main`, a PR template that asks for the gate output, CI that fails on contract drift |
| Golang (a plus) | The fleet agent |
| Document learnings | `docs/VALIDATION.md`, `docs/adr/`, this specification |

**Timing.** The posting opened on 13 October 2026 and stays open for at least seven days. Apply with
the Seagate resume as it stands — it already describes TorqueSense as a monitoring and
anomaly-detection pipeline with Docker and CI. Do not wait for the operations track. If it is
finished before the interview, it becomes the thing you walk them through.

**Bullets to add once built** — replacing the third TorqueSense bullet on the Seagate resume, and
only after each part runs:

> Built the operations layer for a fleet of inspection stations: per-station health telemetry with
> one-sided EWMA control charts whose limits were set by simulation to at most one false
> maintenance ticket per station per month, flagging a degrading camera before it causes false
> alarms; a PLC-side heartbeat watchdog that turns a station amber within 2.5 s of its inspector
> stopping; and a dependency-free Go fleet agent with an in-order offline spool.

> Set up CI/CD across two systems: GitHub Actions for Windows (MSYS2 GCC) and Linux builds, sanitizer
> runs, Go, Python and Playwright tests, and a kind-based Kubernetes smoke test of the container
> images; plus a Jenkins pipeline on a GPU workstation for the latency and accuracy gates that
> hosted CI cannot run.

The sentence for this interview:

> *"I treated a set of camera inspection stations as a device fleet: every station reports its
> health, a control chart learns each one's normal and flags drift before it turns into false
> alarms, a dead inspector shows amber in two seconds, and a new model goes to one station first
> and rolls back with one command."*

The one sentence that covers all three interviews:

> *"I built a real-time assembly verification engine in C++ that checks any assembly described as
> data; its inspection procedure and its training data are both compiled from the CAD — and every latency number in it was
> measured under controlled conditions, with the clock defined."*

## Appendix D — Production, deferred on purpose

Everything in this appendix was designed in v5.1 and, where it could be, checked. The lens and link
arithmetic is tested, the manifests validate against Kubernetes 1.36, and the facts about each tool
were confirmed against its documentation. It is kept intact so it can be picked up once the
prototype has proved itself, without being rethought. **Nothing in the main line depends on it, and
none of it should be built before the prototype's gates pass.** One thing keeps running so it does
not rot: `.github/workflows/deferred-production.yml`, run by hand, still builds the images and
proves the manifests in a kind cluster.

The prototype keeps its data in a cloud Supabase project (Section 16). The plant design below
keeps data inside the plant, on CloudNativePG and Garage, because nothing on the shop floor may
depend on the internet. That choice is revisited only when production resumes.

### D.1 The industrial camera

One toolchain fact decides where this code is built. pylon's Windows SDK is a C++ API compiled with
MSVC, and a C++ ABI does not cross compilers the way ONNX Runtime's C API does: the MinGW GCC
prototype cannot link it. That costs nothing, because the station PCs in D.2 run Linux, where pylon
supports GCC natively. `PylonSource` is built there, behind `SUNVERIFY_WITH_PYLON`, and the Windows
laptop never needs the SDK.

#### The camera

The problem statement names Basler and FLIR. The pick is the **Basler ace 2 Basic a2A1920-51gcBAS**.

| Property | Value | Why it matters here |
|---|---|---|
| Sensor | Sony IMX392, **global shutter** | Hands and tools move during exposure. A rolling-shutter webcam skews them; a global shutter does not |
| Resolution | 1920 × 1200, 3.45 µm pixels | Enough to resolve every reference profile with a stock lens (below) |
| Frame rate | up to 51 fps | 30 fps runs with headroom |
| Interface | GigE, **PoE** or 12–24 VDC | One cable carries power and data up to 100 m — the standard on a shop floor |
| Lens mount | C-mount | The lens is chosen per station, by the compiler |
| I/O | 1 opto-isolated input, 2 GPIO, on an M8 6-pin connector | Strobe output for lighting; trigger input if ever needed |
| Operating temperature | −10 to 60 °C | A line-side enclosure, not an office |

The Pro model has the same sensor and frame rate; its extra features are not used by this workload,
so the Basic is the right buy. The SDK is pylon: a C++ API on Windows, Linux x86-64 and Linux ARM,
with GigE Vision and USB3 Vision drivers. It is proprietary and downloaded from Basler — read its
licence before shipping it in an image. `IFrameSource` keeps the engine free of it everywhere else.

#### Which lens? — the compiler picks it

A stock focal length must be **short enough** to cover the field the station needs and **long
enough** to resolve the smallest feature at 16 px. The compiler computes both bounds and takes the
longest stock lens inside the window — the most pixels per millimetre that still sees everything.
From the reference implementation, `reference/tsref/capture.py`, for this camera:

| Station | Distance | Field needed | Smallest feature | Lens | Feature in pixels | Field covered |
|---|---|---|---|---|---|---|
| Wheel | 900 mm | 500 mm | 17 mm bolt face | **8 mm** | 43.8 px | 745 mm |
| Door trim, per camera | 420 mm | 400 mm | 4 mm clip latch | **6 mm** | 16.6 px | 464 mm |
| Windshield, per camera | 800 mm | 700 mm | 10 mm gasket lip | **6 mm** | 21.7 px | 883 mm |
| Enclosure | 350 mm | 260 mm | 5 mm screw head | **8 mm** | 33.1 px | 290 mm |

The door's window is narrow — 5.8 to 7.0 mm, and only the 6 mm lens fits. When no stock lens fits,
the function returns nothing and the compiler says why: one camera covering the whole door from
1.2 m needs a lens shorter than 7.9 mm for the field and longer than 16.6 mm for the latch. That rig
cannot work, and it is better to learn so from a test than from a station.

#### Will the link carry it? — Bayer on the wire

| Pixel format on the wire | Bandwidth at 30 fps | Fits one GigE link? |
|---|---|---|
| **BayerRG8** | **581 Mbit/s** | **yes** |
| YCbCr422_8 | 1,161 Mbit/s | no |
| RGB8 | 1,742 Mbit/s | no |

So the camera sends raw Bayer data and the host demosaics it, once, on the capture thread. And two
cameras cannot share a gigabit port at this rate: each camera gets its own port, from a **4-port
PoE+ GigE network card** in the edge PC — no switch between camera and computer, one less device to
fail. The camera ports run jumbo frames (MTU 9000) with the camera's packet size set to the largest
the card carries; on Linux the socket receive buffers are raised so a burst never overflows the
kernel. These are node settings, applied by provisioning (D.3).

#### Zero-copy into the frame pool

Invariant I1 — no allocation per frame — has to hold from the camera inward. pylon lets an
application supply its own image buffers through `IBufferFactory`, installed with
`CInstantCamera::SetBufferFactory`; buffers are requested when grabbing starts. So the driver writes
every frame straight into the engine's pre-allocated full-resolution pool:

```cpp
// control-app/src/sources/PylonSource.cpp (production builds only)
class PoolBufferFactory final : public Pylon::IBufferFactory {
 public:
  explicit PoolBufferFactory(RawFramePool& pool) : pool_(pool) {}
  void AllocateBuffer(size_t bytes, void** buffer, intptr_t& context) override {
    auto slot = pool_.lendToDriver(bytes);         // pre-allocated at start-up; never grows
    *buffer = slot.data;
    context = slot.index;
  }
  void FreeBuffer(void* /*buffer*/, intptr_t context) override { pool_.returnFromDriver(context); }
  void DestroyBufferFactory() override {}          // owned by PylonSource, not by pylon
 private:
  RawFramePool& pool_;
};

// start-up, before grabbing — the factory must outlive every buffer it lent
camera_.SetBufferFactory(&factory_, Pylon::Cleanup_None);
camera_.MaxNumBuffer = 8;                                    // 8 raw Bayer frames: 8 × 2.3 MB
camera_.StartGrabbing(Pylon::GrabStrategy_LatestImageOnly);  // fall behind → drop, never queue
```

`GrabStrategy_LatestImageOnly` matches the rest of the pipeline: when the engine falls behind it
drops old frames rather than queueing them, and every drop is counted in `dropped_frame_rate`. In
production the full-resolution crop pool *is* this raw pool — 8 Bayer frames, about 18 MB — and
crops are cut from it at CAD-projected positions (Section 9).

#### Commissioning checklist — one station

| Step | Done when |
|---|---|
| Lens from `lens_for` mounted, focus locked | the compiler's chosen focal length is on the camera |
| Camera on its own NIC port, jumbo frames on | `pylon Viewer` or the engine reports zero resends over 10 minutes |
| Exposure ≤ 2 ms, strobe wired, settings saved to the user set | settings read-back matches `cameras.yaml` after a power cycle |
| Intrinsics calibrated | reprojection error ≤ 0.5 px RMS |
| Extrinsics fixed or fiducials visible | anchor residual stable for one shift |
| Coverage re-run with the installed camera's numbers | `coverage.json` clean |

### D.2 Tier 2 — the station on RKE2

Specified, and partly exercised. The base manifests and the `edge` overlay pass strict schema
validation, and the `kind` overlay runs in CI; the plant cluster itself is not built. This
subsection is the station's view; D.3 names the platform around it.

| Choice | Why |
|---|---|
| RKE2, one cluster per plant: servers in the server room, each station's edge PC an agent | NVIDIA documents its GPU Operator on RKE2; a running station keeps verifying if the server room is unreachable |
| One Deployment per station, `strategy: Recreate` | The camera and GPU are exclusive. A rolling update would start the new pod while the old one still holds them |
| `nodeSelector: torquesense.io/station: w-14` | W-14's engine runs on W-14's PC and nowhere else |
| Fleet agent as a DaemonSet | Exactly one watcher per machine, by construction |
| Station config and compiled profile in a hashed ConfigMap; family models in the image | Recompiling a profile — a new CAD revision or a new manifest — changes the hash, which rolls that station's pod: the change deploys itself |
| Images from an in-plant registry, pinned tags | Nothing is pulled from the internet on a production line |

```yaml
# deploy/k8s/base/engine-w14.yaml (excerpt)
apiVersion: apps/v1
kind: Deployment
metadata:
  name: engine-w14
  labels: { app.kubernetes.io/name: engine, torquesense.io/station: w-14 }
spec:
  replicas: 1
  strategy: { type: Recreate }          # never RollingUpdate: exclusive camera and GPU
  selector:
    matchLabels: { app.kubernetes.io/name: engine, torquesense.io/station: w-14 }
  template:
    metadata:
      labels: { app.kubernetes.io/name: engine, torquesense.io/station: w-14 }
    spec:
      securityContext: { runAsNonRoot: true, runAsUser: 10001 }
      containers:
        - name: engine
          image: torquesense/engine:ci
          args: ["--config", "/etc/torquesense/station.yaml"]
          ports: [{ name: ws, containerPort: 8080 }, { name: control, containerPort: 8095 }]
          readinessProbe: { httpGet: { path: /health, port: control }, periodSeconds: 2 }
          livenessProbe:
            httpGet: { path: /health, port: control }
            initialDelaySeconds: 20     # covers the ONNX warm-up
            periodSeconds: 5
          volumeMounts: [{ name: config, mountPath: /etc/torquesense, readOnly: true }]
      volumes:
        - name: config
          configMap: { name: station-w14 }   # Kustomize appends a content hash to this name
---
# deploy/k8s/overlays/edge/engine-gpu.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: engine-w14
spec:
  template:
    spec:
      nodeSelector: { torquesense.io/station: w-14 }
      containers:                         # no runtimeClassName: the GPU Operator injects GPUs via CDI
        - name: engine
          resources:
            limits: { nvidia.com/gpu: 1, memory: 2Gi }
```

The fleet agent's DaemonSet takes its node name from the downward API and, on GPU nodes, reads
the GPU from the node's own dcgm-exporter (`-gpu-source=dcgm`) through a Service with
`internalTrafficPolicy: Local`, without claiming a GPU. The node-local Service matters: a normal
Service would load-balance across every node's exporter and report some other station's GPU. With
no `nvidia-smi` in its container the agent's image is distroless *static*. The SM clock ceiling for
`sm_clock_ratio` is a flag set at commissioning from `nvidia-smi -q -d CLOCK`, because dcgm-exporter
does not export a maximum clock.

Deploying by hand, and the canary from Section 19 — on a plant cluster the same steps are pull
requests that Flux applies (D.3):

```bash
kubectl label node edge-w14 torquesense.io/station=w-14
kubectl apply -k deploy/k8s/overlays/edge

# canary: one station first
kubectl -n torquesense set image deployment/engine-w15 engine=registry.plant.local/torquesense/engine:1.5.0
kubectl -n torquesense rollout status deployment/engine-w15

# after one shift on /fleet: promote to every station…
kubectl -n torquesense set image deployments -l app.kubernetes.io/name=engine \
    engine=registry.plant.local/torquesense/engine:1.5.0
# …or roll back the canary
kubectl -n torquesense rollout undo deployment/engine-w15
```

The rest of Tier 2 is unchanged from v4.0: OPC-UA with `SignAndEncrypt` and certificates to the
line PLC; the nutrunner controller's per-rundown torque and angle joined to the camera's bolt
identity, so a single record says *bolt 4, out of sequence, 62 Nm against 103 Nm specified*; events
and clips to the plant's own historian and object store, never to an external cloud. Its credibility
comes from Tiers 0 and 1 already sharing the engine, the contracts, the tag names — and now the
container images.

### D.3 The platform and hosting

D.2 says what runs on a station. This section says where everything runs once the
system leaves the laptop, and names one tool per layer. Every pick serves one of three needs the
plant has and the hackathon did not: **nothing pulled from the internet at runtime**, **nothing
inbound to the shop floor**, and **every change through a reviewed pull request**.

#### Where everything runs

| Environment | What runs there | Hosted on |
|---|---|---|
| Development | everything, Tier 0 | HSGPC, this laptop |
| Continuous integration | builds, sanitizers, Go and Python tests, the kind smoke test; image signing | GitHub Actions |
| GPU gates | latency, accuracy, scenarios, nightly soak | Jenkins on HSGPC |
| Public showcase | the replay dashboard, Tier 1 | Vercel |
| Station | the engine and the fleet agent, beside its cameras | one industrial edge PC per station |
| Plant | the control plane, API, TimescaleDB, Garage, dashboard, registry, Git mirror | three servers in the plant's server room |

```mermaid
flowchart LR
  subgraph GH["GitHub — outside the plant"]
    PR["pull request"] --> CI["Actions: test · build<br/>cosign sign"] --> GHCR[("GHCR<br/>signed images")]
  end
  subgraph DC["Plant server room — RKE2 control plane on 3 servers"]
    HB[("Harbor<br/>pulls GHCR on a schedule · Trivy scan")]
    GIT[("Git mirror")]
    FX["Flux<br/>pulls Git · decrypts SOPS"]
    KY["Kyverno<br/>admits only cosign-signed images"]
    DB[("CloudNativePG<br/>TimescaleDB · 3 instances")]
    OBJ[("Garage<br/>3 nodes · replication 3")]
    WEB["API · dashboard<br/>Traefik · cert-manager · oauth2-proxy"]
    OBS["kube-prometheus-stack · Loki"]
  end
  subgraph LINE["Shop floor — station PCs join as RKE2 agents"]
    ST1["W-14 · RTX 2000 Ada<br/>engine pod · fleet agent<br/>GPU Operator · dcgm-exporter"]
    CAMS["Basler a2A1920-51gcBAS<br/>one NIC port each"]
    PLC["line PLC"]
  end
  GHCR -. "pull" .-> HB
  GIT --> FX --> KY
  HB --> ST1
  CAMS --> ST1
  ST1 -- "OPC-UA" --> PLC
  ST1 --> WEB --> DB
  WEB --> OBJ
  ST1 -. "metrics · logs" .-> OBS
  classDef key fill:#E6F1F2,stroke:#0E7C86;
  class FX,HB,ST1 key;
```

*Figure 7 — Production. The arrows that cross into the plant all start inside it: Harbor pulls
images, Flux pulls Git, station PCs connect out to the control plane. Nothing on the internet can
reach the shop floor, and a station keeps verifying if the server room drops off the network — a
running pod does not need its control plane.*

#### The register — one pick per layer

| Layer | Pick | Why this one |
|---|---|---|
| Station edge PC | x86 industrial PC: one low-profile GPU slot, one slot for the camera card, 32 GB RAM, NVMe | Same architecture as the laptop, so the Linux build that runs the sanitizers is the build that ships |
| Station GPU | NVIDIA **RTX 2000 Ada Generation** — 16 GB, 70 W, low-profile | Fits an industrial chassis without extra power; Ada is supported by the CUDA 13 line already in use. A Jetson would mean an ARM build and a JetPack-pinned CUDA: a second toolchain for no gain here |
| Camera network | **4-port PoE+ GigE NIC**, one port per camera | The link budget allows one camera per gigabit port (D.1) |
| Node OS | **Ubuntu Server 24.04 LTS** | Long support window; check it is on the supported-OS list of the GPU Operator release you deploy before ordering hardware |
| Provisioning | **Ansible** | One playbook: OS autoinstall, RKE2 join, NIC jumbo frames, socket buffer sysctls, node labels. A replaced PC is a playbook run, not a day |
| Kubernetes | **RKE2** 1.36, one cluster per plant: three servers in the server room, every station PC an agent | NVIDIA documents its GPU Operator for RKE2 explicitly; RKE2 1.36 ships containerd 2.3 and is security-hardened by default |
| GPUs | **NVIDIA GPU Operator** | Driver, container toolkit, device plugin and dcgm-exporter as one managed unit. From release 25.10 it injects GPUs through CDI, so pods need no runtime class |
| GitOps | **Flux** | Runs inside the cluster and *pulls* desired state, so nothing pushes into the plant. Decrypts SOPS natively |
| Secrets | **SOPS with age** | Secrets live encrypted in the same Git repository as the manifests and are reviewed the same way |
| Registry | **Harbor**, in the server room | Pull-based replication from GitHub Container Registry on a schedule, with Trivy scanning. The line never touches the internet to start a pod |
| Image signing | **cosign** in GitHub Actions, verified by **Kyverno** at admission | An image that was not built and signed by CI cannot run on a station |
| Ingress | **Traefik** — RKE2's default from 1.36 | ingress-nginx was retired upstream in March 2026; start new clusters on what is maintained |
| Certificates | **cert-manager** with the plant's internal CA | Every endpoint on HTTPS; renewal is automatic |
| Dashboard sign-in | **oauth2-proxy** in front of the dashboard, against the plant's OIDC identity provider | Engineers sign in with plant credentials; this system stores no passwords |
| Database | **CloudNativePG** with the TimescaleDB extension image, three instances | The operator manages failover and backups; TimescaleDB mounts as an extension image, which needs image volumes — stable and on by default from Kubernetes 1.36 |
| Database backups | CloudNativePG's Barman Cloud backups to Garage, with point-in-time recovery | Backups land in the same in-plant object store; restore is drilled quarterly |
| Object storage | **Garage**, three nodes, replication factor 3 | The prototype's store, now with no single point of failure |
| Platform observability | **kube-prometheus-stack** and **Loki**, with **Grafana Alloy** collecting from every node; dcgm-exporter for GPUs | Node and pod health, logs, GPU telemetry. Station health stays in TimescaleDB (Section 19) — this layer watches the platform, not the inspection |
| Showcase hosting | **Vercel**, replay mode | Unchanged from Tier 1 |

#### How a release reaches a station

1. A pull request merges to `main`. GitHub Actions runs the full suite, builds the three images,
   signs them with cosign and pushes them to GHCR.
2. Harbor's scheduled pull replication brings the signed images into the plant and scans them.
3. A second pull request changes the image tag in `deploy/k8s/overlays/plant/` for **one station** —
   the canary. Flux applies it; Kyverno admits it only if the signature verifies; the station's pod
   restarts on the new version (`Recreate`).
4. After one shift on `/fleet`, judged against its peers (Section 19), a third pull request
   promotes the tag to every station — or reverts the second.

Profiles travel the same road. A recompiled profile is a pull request that changes the station's
hashed ConfigMap, and the station rolls itself. Every change to what a station checks has an author,
a reviewer and a commit — which is also the audit trail a quality system needs.

#### The station pod on real hardware

Three things change from the kind overlay.

| Change | Why |
|---|---|
| `hostNetwork: true` on the engine pod | GigE Vision cameras are found by broadcast on the camera ports, which are host interfaces |
| The image adds the pylon runtime | `PylonSource` is compiled in production builds only |
| GPU via `nvidia.com/gpu: 1`; no `runtimeClassName` | the GPU Operator's CDI injection |

The fleet agent reads GPU figures from the node's own dcgm-exporter through a Service with
`internalTrafficPolicy: Local`, so each agent reports its own GPU. It no longer needs `nvidia-smi` in
its container, so its image is distroless *static*. Both overlays render with Kustomize and pass
`kubeconform -strict` against Kubernetes 1.36.

#### Running it — day two

| Task | How |
|---|---|
| Kubernetes upgrades | RKE2 minor versions one at a time: server nodes first, then station agents during a planned stop |
| GPU driver upgrades | through the GPU Operator; canary one station first, exactly as for a model |
| Certificates | cert-manager renews; Alertmanager warns 30 days before any certificate the plant issued by hand expires |
| Backups | nightly base backup plus continuous WAL to Garage; a restore into a scratch namespace every quarter |
| A station PC dies | swap the hardware, run the Ansible playbook, label the node; its Deployment schedules back onto it |
| Remote support | the plant's existing remote-access path — this system adds none |
