// What the operator sees, decided from the engine's state alone (spec Section 21, "Operator HMI").
// The operator reads nothing: one colour, one line, the part map. Pure, so it is unit-tested.
import type { LiveState } from './types';
import { OFFLINE_AFTER_MS } from './live';

export type HmiMode = 'GREEN' | 'RED' | 'AMBER' | 'OFFLINE';

export interface HmiView {
  mode: HmiMode;
  headline: string;          // the 96 px line
  sub: string;               // why, in a few words (AMBER and OFFLINE only)
  strobe: string[];          // inspection points to strobe
  corrective: string[];      // the order still to do, for the arc
}

/** "bolt-3" -> "BOLT 3"; "panel-1" -> "PANEL 1". No part is named in this code. */
export function label(ip: string | null | undefined): string {
  if (!ip) return '';
  const i = ip.lastIndexOf('-');
  return (i > 0 ? `${ip.slice(0, i)} ${ip.slice(i + 1)}` : ip).replace(/[_-]/g, ' ').toUpperCase();
}

export function hmiView(s: LiveState | null, silentMs: number): HmiView {
  if (!s || silentMs > OFFLINE_AFTER_MS)
    return { mode: 'OFFLINE', headline: 'INSPECTION OFFLINE', sub: 'no data from the engine', strobe: [], corrective: [] };
  if (s.plc_connected === false)
    return { mode: 'AMBER', headline: 'INSPECTION OFFLINE', sub: 'PLC link down', strobe: [], corrective: [] };
  const v = s.verdict;
  if (v && v.kind !== 'RELEASED') {
    const who = v.observed_ip ?? v.corrective?.[0] ?? null;
    return {
      mode: 'RED',
      headline: who ? `STOP — ${label(who)}` : 'STOP',
      sub: '',
      strobe: who ? [who] : [],
      corrective: v.corrective ?? [],
    };
  }
  if (v && v.kind === 'RELEASED') return { mode: 'GREEN', headline: 'RELEASED', sub: '', strobe: [], corrective: [] };
  const next = s.next?.[0];
  return { mode: 'GREEN', headline: next ? `NEXT: ${label(next)}` : 'READY', sub: '', strobe: [], corrective: [] };
}

/** Assembly-frame millimetres -> SVG coordinates in a size x size box, y up, with a margin. */
export function layoutPoints(pts: { id: string; x_mm?: number | null; y_mm?: number | null }[], size: number, margin = 0.16) {
  const xs = pts.map((p) => p.x_mm ?? 0);
  const ys = pts.map((p) => p.y_mm ?? 0);
  const cx = (Math.min(...xs) + Math.max(...xs)) / 2;
  const cy = (Math.min(...ys) + Math.max(...ys)) / 2;
  const span = Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...ys) - Math.min(...ys), 1);
  const k = (size * (1 - 2 * margin)) / span;
  return pts.map((p) => ({ id: p.id, x: size / 2 + ((p.x_mm ?? 0) - cx) * k, y: size / 2 - ((p.y_mm ?? 0) - cy) * k }));
}
