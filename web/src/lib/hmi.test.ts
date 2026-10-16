import { describe, expect, it } from 'vitest';
import { hmiView, label, layoutPoints } from './hmi';
import type { LiveState } from './types';

const base = (over: Partial<LiveState> = {}): LiveState => ({
  station_id: 'W-14', frame_seq: 1, cycle_id: 'c', step: 'torque', profile: 'wheel-fitment-rl',
  ips: [], next: ['bolt-3'], latency: { engine_ms_p50: 1, engine_ms_p99: 2, evidence_ms_p99: 35 },
  verdict: null, plc_connected: true, ...over,
});

describe('the operator HMI', () => {
  it('is green with the next action while the cycle is right', () => {
    expect(hmiView(base(), 50)).toMatchObject({ mode: 'GREEN', headline: 'NEXT: BOLT 3' });
  });
  it('floods red and names the point on a violation, with the corrective order', () => {
    const v = hmiView(base({ verdict: { kind: 'SEQUENCE' as never, step: 'torque', observed_ip: 'bolt-2', corrective: ['bolt-5', 'bolt-2', 'bolt-4'] } }), 50);
    expect(v).toMatchObject({ mode: 'RED', headline: 'STOP — BOLT 2', strobe: ['bolt-2'] });
    expect(v.corrective).toEqual(['bolt-5', 'bolt-2', 'bolt-4']);
  });
  it('goes offline after one silent second, without waiting to be told', () => {
    expect(hmiView(base(), 1200).mode).toBe('OFFLINE');
    expect(hmiView(null, 0).headline).toBe('INSPECTION OFFLINE');
  });
  it('is amber when the PLC link is down', () => {
    expect(hmiView(base({ plc_connected: false }), 50).mode).toBe('AMBER');
  });
  it('labels any inspection point without knowing the part', () => {
    expect(label('clip-5')).toBe('CLIP 5');
    expect(label('seal_segment-12')).toBe('SEAL SEGMENT 12');
  });
});

describe('the part map', () => {
  it('keeps the CAD geometry: centred, square, y up', () => {
    const p = layoutPoints([{ id: 'a', x_mm: 0, y_mm: 50 }, { id: 'b', x_mm: 0, y_mm: -50 }], 100);
    expect(p[0].x).toBeCloseTo(50);
    expect(p[0].y).toBeLessThan(p[1].y);
  });
});
