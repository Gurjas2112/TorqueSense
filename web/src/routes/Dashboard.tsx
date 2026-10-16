// The engineer dashboard (spec Section 21): four zones, one screen, no tabs.
import { useParams } from 'react-router-dom';
import { AnomalyFeed } from '../components/AnomalyFeed';
import { LiveFeed } from '../components/LiveFeed';
import { PartMap } from '../components/PartMap';
import { ProvenanceChip, StepRail } from '../components/bits';
import { ShiftAnalytics } from '../components/ShiftAnalytics';
import { stationIds } from '../lib/config';
import { hmiView } from '../lib/hmi';
import { useStation } from '../lib/live';

export function Dashboard() {
  const { station = stationIds()[0] } = useParams();
  const feed = useStation(station);
  const s = feed.state;
  const view = hmiView(s, feed.silentMs);
  const elapsed = s?.cycle_elapsed_ms ?? 0;
  return (
    <div className="grid h-full gap-3 p-3 lg:grid-cols-[1.5fr_1fr] lg:grid-rows-[auto_1fr]">
      <section className="panel p-3" aria-label="live feed">
        <div className="mb-2 flex items-center justify-between">
          <h2 className="zone-title">A · Live feed · {station}</h2>
          <span className="mono text-muted">{s?.scenario ? `scenario ${s.scenario}` : ''}</span>
        </div>
        <LiveFeed s={s} station={station} />
      </section>

      <section className="panel flex flex-col gap-3 p-3" aria-label="procedure timeline">
        <div className="flex items-center justify-between">
          <h2 className="zone-title">B · Procedure</h2>
          <ProvenanceChip s={s} />
        </div>
        <StepRail s={s} />
        <div className="flex items-center gap-4">
          <PartMap ips={s?.ips ?? []} next={s?.next ?? []} strobe={view.strobe} corrective={view.corrective} size={220} className="h-[220px] w-[220px] shrink-0" />
          <div className="min-w-0 flex-1 space-y-2">
            <div className="text-2xl font-bold" data-testid="verdict" style={{ color: view.mode === 'RED' ? 'var(--state-fault)' : view.mode === 'GREEN' ? 'var(--state-ok)' : 'var(--state-active)' }}>
              {view.headline}
            </div>
            {s?.verdict?.narrative && <p className="leading-snug">{s.verdict.narrative}</p>}
            <div className="mono text-muted">
              cycle {s?.cycle_id?.slice(0, 8) ?? '—'} · {(elapsed / 1000).toFixed(1)} s / takt {s?.takt_s ?? '—'} s
            </div>
            <div className="mono text-[12px] text-muted">
              PLC {s?.plc_connected ? 'connected' : 'down'} · provider {s?.provider ?? '—'} · {s?.model_version ?? ''}
            </div>
          </div>
        </div>
      </section>

      <section className="panel min-h-[280px] p-3" aria-label="anomaly feed"><AnomalyFeed /></section>
      <section className="panel min-h-[280px] p-3" aria-label="shift analytics"><ShiftAnalytics station={station} /></section>
    </div>
  );
}
