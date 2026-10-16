// The operator HMI (spec Section 21): a full-screen kiosk route. The operator reads nothing — one
// colour, one line, the part map — and it accepts no input. If the feed is silent for 1 s it goes
// amber by itself: it does not wait to be told, and the PLC's own AMBER follows within 2 s.
import { useParams } from 'react-router-dom';
import { PartMap } from '../components/PartMap';
import { StepRail } from '../components/bits';
import { stationIds } from '../lib/config';
import { hmiView } from '../lib/hmi';
import { useStation } from '../lib/live';

const bg = { GREEN: 'var(--bg)', RED: 'var(--state-fault)', AMBER: 'var(--state-active)', OFFLINE: 'var(--state-active)' } as const;

export function Hmi() {
  const { station = stationIds()[0] } = useParams();
  const feed = useStation(station);
  const s = feed.state;
  const v = hmiView(s, feed.silentMs);
  const takt = (s?.takt_s ?? 58) * 1000;
  const elapsed = s?.cycle_elapsed_ms ?? 0;
  const late = elapsed > 0.8 * takt;
  const flooded = v.mode !== 'GREEN';
  return (
    <div data-testid="hmi" data-mode={v.mode}
      className="pointer-events-none fixed inset-0 flex select-none flex-col p-6"
      style={{ background: bg[v.mode], color: flooded ? '#0f1115' : 'var(--text)' }}
      onKeyDown={(e) => e.preventDefault()}>
      {v.mode === 'GREEN' && <StepRail s={s} big />}
      <div className="flex flex-1 items-center justify-center">
        {v.mode === 'OFFLINE' || v.mode === 'AMBER' ? (
          <div className="text-center">
            <div style={{ font: 'var(--t-hmi-hero)' }}>INSPECTION OFFLINE</div>
            <div className="mt-4" style={{ font: 'var(--t-hmi-label)' }}>{v.sub}</div>
          </div>
        ) : (
          <PartMap ips={s?.ips ?? []} next={s?.next ?? []} strobe={v.strobe} corrective={v.corrective}
            size={600} className="h-[60vh] w-[60vh]" />
        )}
      </div>
      {v.mode !== 'OFFLINE' && v.mode !== 'AMBER' && (
        <div className="text-center" data-testid="hmi-headline" style={{ font: 'var(--t-hmi-hero)' }}>{v.headline}</div>
      )}
      <div className="mono absolute bottom-4 right-6 text-2xl" style={{ color: late && !flooded ? 'var(--state-active)' : undefined }}>
        {station} · {(elapsed / 1000).toFixed(0)} s
      </div>
    </div>
  );
}
