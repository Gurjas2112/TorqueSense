// Zone A (spec Section 21): MJPEG from the engine on :8090, an overlay with every inspection point at
// its CAD-projected position and its id, and the ghost overlay on G — the correct assembly drawn over
// the live frame with the same projection, so a divergence shows as a shape.
import { useEffect, useState } from 'react';
import { REPLAY, endpoints } from '../lib/config';
import type { LiveState } from '../lib/types';
import { LatencyBadge } from './bits';

export function LiveFeed({ s, station }: { s: LiveState | null; station: string }) {
  const [ghost, setGhost] = useState(false);
  const [ok, setOk] = useState(true);
  useEffect(() => {
    const k = (e: KeyboardEvent) => {
      if (e.key === 'g' || e.key === 'G') setGhost((g) => !g);
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, []);
  // projected points are in full-resolution pixels; the feed is the 960 x 540 downscale
  const W = 1920, H = 1080;
  const ips = s?.ips ?? [];
  return (
    <div className="relative aspect-video w-full overflow-hidden rounded-md border border-line bg-black">
      {!REPLAY && ok ? (
        <img src={endpoints(station).mjpeg} alt="live feed" className="absolute inset-0 h-full w-full object-contain" onError={() => setOk(false)} />
      ) : (
        <div className="absolute inset-0 grid place-items-center text-muted">
          {REPLAY ? 'replay: the live feed is on the station laptop' : 'no video from the engine'}
        </div>
      )}
      <svg viewBox={`0 0 ${W} ${H}`} className="absolute inset-0 h-full w-full">
        {ghost && ips.length > 1 && (
          <polygon points={ips.map((p) => `${p.u},${p.v}`).join(' ')} fill="rgba(14,124,134,0.12)" stroke="var(--state-info)" strokeWidth={3} strokeDasharray="12 8" />
        )}
        {ips.map((p) => {
          const done = (p as { rank?: number }).rank === ((p as { n_states?: number }).n_states ?? 2) - 1;
          const col = s?.verdict?.observed_ip === p.id ? 'var(--state-fault)' : done ? 'var(--state-ok)' : s?.next?.includes(p.id) ? 'var(--state-active)' : '#9AA0A6';
          return (
            <g key={p.id}>
              <rect x={p.u - 22} y={p.v - 22} width={44} height={44} fill="none" stroke={col} strokeWidth={3} rx={5} />
              <text x={p.u + 26} y={p.v + 8} fill={col} fontSize={20} fontFamily="IBM Plex Mono">{p.id.slice(p.id.lastIndexOf('-') + 1)}</text>
              {ghost && <circle cx={p.u} cy={p.v} r={22} fill="none" stroke="var(--state-info)" strokeWidth={3} />}
            </g>
          );
        })}
      </svg>
      <div className="absolute left-3 top-3"><LatencyBadge s={s} /></div>
      <div className="mono absolute bottom-2 right-3 rounded bg-black/60 px-2 py-0.5 text-[11px] text-muted">
        G ghost overlay {ghost ? 'on' : 'off'} · {s?.provider ?? '—'} · frame {s?.frame_seq ?? '—'}
      </div>
    </div>
  );
}
