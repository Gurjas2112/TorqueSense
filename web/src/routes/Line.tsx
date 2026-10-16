// The station grid (spec Section 21): one tile per station — state, cycle progress, live latency — and
// one summary line for the line.
import { Link } from 'react-router-dom';
import { PartMap } from '../components/PartMap';
import { stationIds } from '../lib/config';
import { hmiView } from '../lib/hmi';
import { useStation } from '../lib/live';

const color = { GREEN: 'var(--state-ok)', RED: 'var(--state-fault)', AMBER: 'var(--state-active)', OFFLINE: 'var(--state-active)' } as const;

function Tile({ id }: { id: string }) {
  const f = useStation(id);
  const s = f.state;
  const v = hmiView(s, f.silentMs);
  const steps = s?.steps ?? [];
  const done = steps.reduce((n, x) => n + x.done, 0);
  const total = steps.reduce((n, x) => n + x.total, 0) || 1;
  return (
    <Link to={`/station/${id}`} className="panel block p-3 hover:border-muted" data-station={id} data-mode={v.mode}>
      <div className="flex items-center justify-between">
        <span className="text-lg font-semibold">{id}</span>
        <span className="mono rounded px-2 py-0.5 text-bg" style={{ background: color[v.mode] }}>{v.mode}</span>
      </div>
      <div className="my-2 flex justify-center"><PartMap ips={s?.ips ?? []} next={s?.next ?? []} strobe={v.strobe} size={180} className="h-[180px] w-[180px]" labels={false} /></div>
      <div className="h-2 overflow-hidden rounded bg-bg"><div className="h-full bg-ok" style={{ width: `${(100 * done) / total}%` }} /></div>
      <div className="mono mt-2 flex justify-between text-muted">
        <span>{v.headline}</span>
        <span>evidence p99 {s?.latency.evidence_ms_p99 ? s.latency.evidence_ms_p99.toFixed(1) : '—'} ms</span>
      </div>
    </Link>
  );
}

export function Line() {
  const ids = stationIds();
  return (
    <div className="p-4">
      <h1 className="mb-3" style={{ font: 'var(--t-h1)' }}>Line · {ids.length} station{ids.length > 1 ? 's' : ''}</h1>
      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">{ids.map((id) => <Tile key={id} id={id} />)}</div>
      <p className="mt-3 text-muted">Add stations with <span className="mono">?stations=W-14,W-15,W-16,W-17</span> — each station's engine adds 100 to its ports.</p>
    </div>
  );
}
