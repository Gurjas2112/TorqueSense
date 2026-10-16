// The demo, recorded: a scripted walk through the running station (spec Section 23), as one continuous
// video plus a screenshot at each step of the flow. Drives the real app against the real engine, PLC,
// Node-RED, API and Supabase — nothing here is mocked.
//
//   node scripts/record-demo.mjs --api http://127.0.0.1:8010 --out ../app_screenshots_screen_recording
import { chromium } from '@playwright/test';
import fs from 'node:fs';
import path from 'node:path';

const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const API = arg('--api', 'http://127.0.0.1:8000');
const OUT = path.resolve(arg('--out', '../app_screenshots_screen_recording'));
const VERCEL = arg('--vercel', 'https://dist-replay-one.vercel.app');
const ENGINE = 'http://127.0.0.1:8095';
const NODERED = 'http://127.0.0.1:1880/dashboard/tags';
fs.mkdirSync(OUT, { recursive: true });
const tmp = fs.mkdtempSync(path.join(OUT, '.video-'));

const b = await chromium.launch();
const ctx = await b.newContext({ viewport: { width: 1600, height: 900 }, recordVideo: { dir: tmp, size: { width: 1600, height: 900 } } });
const page = await ctx.newPage();
let n = 0;
const shot = async (name) => {
  const file = `${String(++n).padStart(2, '0')}_${name}.png`;
  await page.screenshot({ path: path.join(OUT, file) });
  console.log('screenshot', file);
};
const pause = (ms) => page.waitForTimeout(ms);
const state = async (port = 8095) => (await fetch(`http://127.0.0.1:${port}/state`)).json();
const waitFor = async (pred, timeoutMs, port) => {
  const end = Date.now() + timeoutMs;
  while (Date.now() < end) { try { if (pred(await state(port))) return true; } catch { /* starting */ } await pause(300); }
  return false;
};
// a title card between scenes, so the recording reads as a flow
const card = async (title, sub) => {
  await page.setContent(`<html><body style="margin:0;background:#0F1115;color:#E8EAED;font-family:IBM Plex Sans,Segoe UI,sans-serif;display:grid;place-items:center;height:100vh">
    <div style="text-align:center"><div style="font-size:54px;font-weight:700">${title}</div>
    <div style="font-size:22px;color:#9AA0A6;margin-top:14px;max-width:1100px">${sub}</div></div></body></html>`);
  await pause(2600);
};

await card('TorqueSense', 'Real-time assembly verification compiled from CAD — any assembly, as data. A walk through the running prototype: engine, soft PLC, Node-RED, Florence-2 sidecar, API, Supabase.');

// 1 · a correct cycle
await card('1 · A correct cycle', 'Station W-14 fits a 5-lug wheel. The star order 1 → 3 → 5 → 2 → 4 was derived from the bolt geometry, not typed.');
await fetch(`${ENGINE}/scenario/cycle_correct_01`, { method: 'POST' });
await page.goto(`${API}/`);
await waitFor((s) => s.scenario === 'cycle_correct_01' && (s.cycle_elapsed_ms ?? 0) > 4500, 45000);
await shot('dashboard_correct_cycle_in_progress');
await page.goto(`${API}/hmi/W-14`);
await pause(2500);
await shot('hmi_green_next_bolt');
await waitFor((s) => s.verdict?.kind === 'RELEASED', 20000);
await pause(800);
await shot('hmi_green_released');

// 2 · pick a fault
await card('2 · Pick a fault', 'The fault-injection harness changes the input at the next cycle boundary — never the logic.');
await page.goto(`${API}/harness`);
await pause(1500);
await shot('harness_pick_a_fault');
await page.click('[data-scenario="fault_interrupt_resume"]');
await pause(1200);
await shot('harness_fault_queued');

// 3 · the hero fault, caught
await card('3 · Interrupt and resume', 'Called away after bolts 1 and 3, the operator resumes from bolt 2 — sequentially. Nothing he can see is wrong.');
await page.goto(`${API}/hmi/W-14`);
await waitFor((s) => s.scenario === 'fault_interrupt_resume', 40000);
await waitFor((s) => s.verdict && s.verdict.kind === 'SEQUENCE', 30000);
await pause(700);
await shot('hmi_red_stop_bolt_2_corrective_order');
await page.goto(NODERED);
await pause(2500);
await shot('node_red_opcua_red_and_hold_request');
await page.goto(`${API}/`);
await pause(2500);
await shot('dashboard_violation_narrative_latency');
await page.locator('[data-kind="SEQUENCE"]').first().click();
await pause(3500);
await shot('anomaly_modal_clip_narrative_review');
await page.keyboard.press('Escape');
await page.mouse.click(10, 10);

// 4 · fail-visible
await card('4 · Fail-visible', 'Freeze the camera: frames stop, the heartbeat stops, and the PLC — not the engine — turns the station AMBER.');
await fetch(`${ENGINE}/freeze?on=1`, { method: 'POST' });
await page.goto(`${API}/hmi/W-14`);
await pause(2000);
await shot('hmi_inspection_offline');
await page.goto(NODERED);
await pause(3500);
await shot('node_red_plc_amber_inspection_offline');
await fetch(`${ENGINE}/freeze?on=0`, { method: 'POST' });
await pause(3000);
await page.goto(NODERED);
await pause(2500);
await shot('node_red_heartbeat_back_inspection_online');   // the held cycle's RED stays until the next part

// 5 · any assembly
await card('5 · Any assembly is a folder', 'Station D-07 runs the door-trim profile in the same engine binary, reading clip, connector and panel states from the pixels.');
await page.goto(`${API}/line?stations=W-14,D-07`);
await waitFor((s) => s.verdict && s.verdict.kind === 'PRECEDENCE', 45000, 8195);
await pause(1500);
await shot('line_two_stations_door_precedence');
await page.goto(`${API}/station/D-07?stations=W-14,D-07`);
await pause(2500);
await shot('dashboard_door_trim_panel_over_open_clip');

// 6 · the fleet watches itself
await card('6 · The inspector watches itself', 'One-sided EWMA charts per station and signal. Seeded synthetic history: W-16\'s lens degrades before any false alarm.');
await page.goto(`${API}/fleet`);
await pause(3000);
await shot('fleet_drift_lens_contamination');

// 7 · in the cloud
await card('7 · Recorded, and shared', 'Every verdict goes through the outbox to Supabase; the public page replays a recorded session with the read-only key.');
await page.goto(VERCEL);
await pause(9000);
await shot('vercel_public_replay');

await card('TorqueSense', 'Engine latency ≈ 1.7 ms, evidence ≈ 36 ms on the replay provider (docs/VALIDATION.md) — against a one-second requirement.');
await ctx.close();
await b.close();
const webm = fs.readdirSync(tmp).find((f) => f.endsWith('.webm'));
fs.renameSync(path.join(tmp, webm), path.join(OUT, 'torquesense_demo_full.webm'));
fs.rmSync(tmp, { recursive: true, force: true });
console.log('video torquesense_demo_full.webm');
