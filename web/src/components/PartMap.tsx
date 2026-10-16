// The part map (spec Section 21): the assembly as the camera sees it, a marker at every inspection
// point coloured by state, positions from procedure.json. On a violation the offending point strobes
// and, for a sequence, the corrective order draws as a numbered arc. Generated from the profile:
// nothing here names a part.
import { layoutPoints } from '../lib/hmi';
import type { LiveState } from '../lib/types';

type Ip = LiveState['ips'][number] & { rank?: number | null; n_states?: number | null };

export function PartMap({ ips, next = [], strobe = [], corrective = [], size = 420, labels = true, className = '' }: {
  ips: Ip[];
  next?: string[];
  strobe?: string[];
  corrective?: string[];
  size?: number;
  labels?: boolean;
  className?: string;
}) {
  if (!ips.length) return <div className={className} />;
  const pts = layoutPoints(ips, size);
  const at = Object.fromEntries(pts.map((p) => [p.id, p]));
  const cx = pts.reduce((s, p) => s + p.x, 0) / pts.length;
  const cy = pts.reduce((s, p) => s + p.y, 0) / pts.length;
  const reach = Math.max(...pts.map((p) => Math.hypot(p.x - cx, p.y - cy)), size * 0.2);
  const r = Math.max(10, Math.min(28, size / (Math.sqrt(ips.length) * 5.5)));
  const arc = corrective.map((id) => at[id]).filter(Boolean);
  const outline = reach + r * 2.1;
  const pad = 6;   // the view fits the whole outline, whatever the points' spread
  const view = `${cx - outline - pad} ${cy - outline - pad} ${2 * (outline + pad)} ${2 * (outline + pad)}`;
  return (
    <svg viewBox={view} className={className} role="img" aria-label="part map">
      {/* the part: an outline around its inspection points, as the camera sees it from above */}
      <circle cx={cx} cy={cy} r={outline} fill="var(--surface-2)" stroke="var(--line)" strokeWidth={2} />
      <circle cx={cx} cy={cy} r={Math.max(reach * 0.42, r)} fill="none" stroke="var(--line)" strokeWidth={2} />
      {arc.length > 1 && (
        <polyline points={arc.map((p) => `${p.x},${p.y}`).join(' ')} fill="none" stroke="var(--state-fault)"
          strokeWidth={4} strokeDasharray="10 8" strokeLinejoin="round" opacity={0.9} />
      )}
      {ips.map((ip) => {
        const p = at[ip.id];
        const n = ip.n_states ?? 2;
        const rank = ip.rank ?? 0;
        const done = rank >= n - 1;
        const started = rank > 0;
        const isNext = next.includes(ip.id);
        const isFault = strobe.includes(ip.id);
        const fill = isFault ? 'var(--state-fault)' : done ? 'var(--state-ok)' : started ? 'var(--text-muted)' : 'transparent';
        const stroke = isFault ? 'var(--state-fault)' : isNext ? 'var(--state-active)' : done ? 'var(--state-ok)' : 'var(--text-muted)';
        const num = ip.id.slice(ip.id.lastIndexOf('-') + 1);
        return (
          <g key={ip.id} className={isFault ? 'strobe' : isNext ? 'pulse' : ''} data-ip={ip.id} data-state={ip.state}>
            <circle cx={p.x} cy={p.y} r={r} fill={fill} stroke={stroke} strokeWidth={isNext || isFault ? 5 : 3} />
            {ip.ambiguous && <circle cx={p.x} cy={p.y} r={r + 7} fill="none" stroke="var(--state-active)" strokeDasharray="4 4" strokeWidth={2} />}
            {labels && (
              <text x={p.x} y={p.y + r * 0.38} textAnchor="middle" fontSize={r * 1.05} fontWeight={700}
                fill={done || isFault ? '#0f1115' : 'var(--text)'} style={{ fontFamily: 'IBM Plex Sans' }}>
                {num}
              </text>
            )}
          </g>
        );
      })}
      {arc.map((p, i) => (
        <g key={`arc-${i}`}>
          <circle cx={p.x + r * 1.05} cy={p.y - r * 1.05} r={r * 0.55} fill="var(--state-fault)" />
          <text x={p.x + r * 1.05} y={p.y - r * 1.05 + r * 0.2} textAnchor="middle" fontSize={r * 0.6} fontWeight={700} fill="#fff">
            {i + 1}
          </text>
        </g>
      ))}
    </svg>
  );
}
