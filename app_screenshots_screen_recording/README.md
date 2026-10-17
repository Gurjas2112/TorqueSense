# TorqueSense — flow-wise screenshots and the full recording

Captured on 17 October 2026 from the running prototype on HSGPC (`scripts\start-dev.ps1 -Gpu -Door`):
station W-14 running the **trained models on the RTX 3060** (YOLO11n and the bolt classifier on ONNX
Runtime's CUDA provider, reading every frame of the rendered 1080p scenario videos), station D-07 (door
trim, schematic provider), the soft PLC (OPC-UA), Node-RED, the Florence-2 sidecar, the API with its
outbox to Supabase, and the public replay on Vercel. Nothing in these images is mocked. Produced by
`web/scripts/record-demo.mjs` (a scripted Playwright walk through the live app, recorded as one video).

**Video:** `torquesense_demo_full.mp4` — 120 s, H.264, 1600 × 900, the whole flow below in one take.

| # | Screenshot | What it shows |
|---|---|---|
| 01 | `01_dashboard_correct_cycle_in_progress.png` | Engineer dashboard, four zones: the rendered wheel with each bolt's state as the GPU classifier read it at its CAD projection, the latency badge, the procedure rail and part map (provider `cuda`, model `yolo11n-w14-s20260721+mnv3s-tf-s20260721`), the anomaly feed, shift analytics |
| 02 | `02_hmi_green_next_bolt.png` | Operator HMI, green: the next bolt pulses on the part map — the operator reads nothing |
| 03 | `03_hmi_green_released.png` | The release gate passes: every step complete, in the derived star order 1 → 3 → 5 → 2 → 4 |
| 04 | `04_harness_pick_a_fault.png` | The fault-injection harness, styled as a test tool: it changes the input, never the logic |
| 05 | `05_harness_fault_queued.png` | "Interrupt and resume" queued for the next cycle |
| 06 | `06_hmi_red_stop_bolt_2_corrective_order.png` | The hero fault caught: bolt 2 torqued when bolt 5 was due. Full-bleed red, the bolt strobes, the corrective order draws as a numbered arc |
| 07 | `07_node_red_opcua_red_and_hold_request.png` | Node-RED reading the soft PLC over OPC-UA: Andon RED and the conveyor hold *request* are set — a real protocol write |
| 08 | `08_dashboard_violation_narrative_latency.png` | The anomaly feed with the narrative from the state machine and the measured evidence latency |
| 09 | `09_anomaly_modal_clip_narrative_review.png` | The hero fault's anomaly: its clip from the clip ring (H.264, in Supabase Storage), the facts (evidence ≈ 59 ms, read from the pixels), Confirm / False positive |
| 10 | `10_hmi_inspection_offline.png` | Fail-visible: the camera frozen, the HMI goes amber after one silent second without being told |
| 11 | `11_node_red_plc_amber_inspection_offline.png` | The PLC's own watchdog: the heartbeat stopped, so Andon AMBER and InspectionOnline false — decided by the PLC, not the engine |
| 12 | `12_node_red_heartbeat_back_inspection_online.png` | Frames resume, the heartbeat moves, InspectionOnline returns (the held cycle's RED stays until the next part) |
| 13 | `13_line_two_stations_door_precedence.png` | The line: W-14 (wheel) and D-07 (door trim) in one view; D-07 has stopped on clip 4 |
| 14 | `14_dashboard_door_trim_panel_over_open_clip.png` | Any assembly is a folder: the door-trim profile in the same engine binary, states read from the pixels across three camera tiles — "Panel 1 installed before step 'clips' was complete" |
| 15 | `15_fleet_drift_lens_contamination.png` | Fleet health: each signal against its own EWMA limit; W-16's lens degrades and the playbook names the likely cause. Seeded synthetic history, labelled as such on screen |
| 16 | `16_vercel_public_replay.png` | The public page on Vercel: a recorded engine session replayed, anomalies read from Supabase with the read-only publishable key |

What these images are not:
- **Not camera footage.** The wheel frames are photoreal BlenderProc renders of the FreeCAD model,
  and the door frames are schematic drawings of recorded scenarios, stamped as such.
- **Not a real-camera accuracy claim.** The wheel's detector accuracy is measured on held-out renders
  only; accuracy on real photos is not measured yet.
- **The latency is the GPU detector's.** The W-14 latency on screen comes from the detector reading the
  renders: the 100-cycle latency test measured p99 62.8 ms. D-07's latency comes from the schematic
  provider.
- **The public replay (16) is older.** It still plays the session recorded on 16 October, before the
  GPU detector.

See `docs/VALIDATION.md` for what has and has not been measured.
