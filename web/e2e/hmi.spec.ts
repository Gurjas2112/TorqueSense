// The operator HMI's four states (spec Section 21; coding instructions Phase 8): GREEN, RED, AMBER
// and offline — driven by a mocked engine WebSocket.
import { expect, test, type Page } from '@playwright/test';

const ips = [
  { id: 'bolt-1', family: 'threaded_fastener', state: 'fastened', conf: 0.95, u: 960, v: 453, x_mm: 0, y_mm: 57.15, rank: 2, n_states: 3 },
  { id: 'bolt-2', family: 'threaded_fastener', state: 'fastened', conf: 0.95, u: 1042, v: 513, x_mm: 54.35, y_mm: 17.66, rank: 2, n_states: 3 },
  { id: 'bolt-3', family: 'threaded_fastener', state: 'fastened', conf: 0.95, u: 1010, v: 609, x_mm: 33.59, y_mm: -46.24, rank: 2, n_states: 3 },
  { id: 'bolt-4', family: 'threaded_fastener', state: 'seated', conf: 0.95, u: 909, v: 609, x_mm: -33.59, y_mm: -46.24, rank: 1, n_states: 3 },
  { id: 'bolt-5', family: 'threaded_fastener', state: 'seated', conf: 0.95, u: 877, v: 513, x_mm: -54.35, y_mm: 17.66, rank: 1, n_states: 3 },
];

function state(over: Record<string, unknown> = {}) {
  return JSON.stringify({
    station_id: 'W-14', frame_seq: 100, cycle_id: 'c1', step: 'torque', profile: 'wheel-fitment-rl', ips, next: ['bolt-5'],
    latency: { engine_ms_p50: 1.2, engine_ms_p99: 2.4, evidence_ms_p99: 35.8 }, verdict: null, plc_connected: true,
    steps: [{ id: 'hand_start', kind: 'COMPLETE', state: 'seated', done: 5, total: 5, status: 'COMPLETE' },
            { id: 'torque', kind: 'SEQUENCE', state: 'fastened', done: 2, total: 5, status: 'ACTIVE' }],
    ...over,
  });
}

/** A fake engine: pushes `doc()` every 66 ms (15 Hz) until `stop` is called. */
async function engine(page: Page, doc: () => string) {
  let timer: ReturnType<typeof setInterval> | undefined;
  await page.routeWebSocket('ws://127.0.0.1:8080/', (ws) => {
    timer = setInterval(() => ws.send(doc()), 66);
  });
  return () => clearInterval(timer);
}

test('GREEN: the next action, the part map, no text to read', async ({ page }) => {
  await engine(page, () => state());
  await page.goto('/hmi/W-14');
  await expect(page.getByTestId('hmi')).toHaveAttribute('data-mode', 'GREEN');
  await expect(page.getByTestId('hmi-headline')).toHaveText('NEXT: BOLT 5');
  await expect(page.locator('[data-ip="bolt-5"]')).toHaveClass(/pulse/);
});

test('RED: the screen floods and names the bolt; the corrective order draws', async ({ page }) => {
  await engine(page, () => state({ verdict: { kind: 'SEQUENCE', step: 'torque', observed_ip: 'bolt-2', expected_ip: 'bolt-5',
    narrative: 'Bolt 2 fastened before bolt 5.', corrective: ['bolt-5', 'bolt-2', 'bolt-4'] } }));
  await page.goto('/hmi/W-14');
  await expect(page.getByTestId('hmi')).toHaveAttribute('data-mode', 'RED');
  await expect(page.getByTestId('hmi-headline')).toHaveText('STOP — BOLT 2');
  await expect(page.locator('[data-ip="bolt-2"]')).toHaveClass(/strobe/);
});

test('AMBER: the PLC link is down', async ({ page }) => {
  await engine(page, () => state({ plc_connected: false }));
  await page.goto('/hmi/W-14');
  await expect(page.getByTestId('hmi')).toHaveAttribute('data-mode', 'AMBER');
  await expect(page.getByText('INSPECTION OFFLINE')).toBeVisible();
});

test('offline: a feed silent for one second goes amber without being told', async ({ page }) => {
  const stop = await engine(page, () => state());
  await page.goto('/hmi/W-14');
  await expect(page.getByTestId('hmi')).toHaveAttribute('data-mode', 'GREEN');
  stop();                                                   // the engine dies: nothing reports it
  await expect(page.getByTestId('hmi')).toHaveAttribute('data-mode', 'OFFLINE', { timeout: 2500 });
  await expect(page.getByText('INSPECTION OFFLINE')).toBeVisible();
});

test('the HMI accepts no input', async ({ page }) => {
  await engine(page, () => state());
  await page.goto('/hmi/W-14');
  await expect(page.getByTestId('hmi')).toHaveCSS('pointer-events', 'none');
});
