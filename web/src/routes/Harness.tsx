// The fault-injection harness (spec Section 21), styled as a test tool so nobody mistakes it for
// production UI. It selects which recorded scenario feeds the next cycle: it changes the input, never
// the logic. "Freeze camera" stops frames so the fail-visible path can be shown on demand.
import { useEffect, useState } from 'react';
import { useParams } from 'react-router-dom';
import { REPLAY, endpoints, stationIds } from '../lib/config';
import { useStation } from '../lib/live';

interface Scn { name: string; title: string; must_alert: boolean; seconds: number }

export function Harness() {
  const { station = stationIds()[0] } = useParams();
  const [list, setList] = useState<Scn[]>([]);
  const [log, setLog] = useState<string[]>([]);
  const feed = useStation(station);
  useEffect(() => {
    if (REPLAY) return;
    fetch(`${endpoints(station).control}/scenarios`).then((r) => r.json()).then((j) => setList(j.scenarios)).catch(() => {});
  }, [station]);
  const post = async (path: string, note: string) => {
    const r = await fetch(`${endpoints(station).control}${path}`, { method: 'POST' }).catch(() => null);
    setLog((l) => [`${new Date().toLocaleTimeString()}  ${note}  → ${r ? r.status : 'engine unreachable'}`, ...l].slice(0, 12));
  };
  return (
    <div className="mono mx-auto max-w-5xl p-6" style={{ font: 'var(--t-mono)' }}>
      <div className="mb-4 flex items-center justify-between rounded-md border border-dashed border-muted/60 px-4 py-2">
        <span className="tracking-widest text-active">TEST HARNESS · {station}</span>
        <span className="text-muted">current: {feed.state?.scenario ?? '—'} · {feed.state?.verdict?.kind ?? 'no verdict'}</span>
      </div>
      {REPLAY && <p className="text-muted">The harness drives a live engine; the public replay is read-only.</p>}
      <p className="mb-3 text-muted">Queues a recorded scenario for the next cycle boundary. The engine's logic is untouched.</p>
      <div className="grid grid-cols-2 gap-2 md:grid-cols-3">
        {list.map((s) => (
          <button key={s.name} data-scenario={s.name} onClick={() => post(`/scenario/${s.name}`, s.title)}
            className="rounded border border-line bg-surface p-3 text-left hover:border-muted">
            <div className="text-ink">{s.title}</div>
            <div className="mt-1 text-[11px] text-muted">{s.name}.mp4 · {s.seconds.toFixed(0)} s · {s.must_alert ? 'must alert' : 'must NOT alert'}</div>
          </button>
        ))}
      </div>
      <div className="mt-4 flex gap-2">
        <button onClick={() => post('/freeze?on=1', 'freeze camera')} className="rounded border border-active/60 px-3 py-2 text-active">Freeze camera</button>
        <button onClick={() => post('/freeze?on=0', 'unfreeze camera')} className="rounded border border-line px-3 py-2">Unfreeze</button>
      </div>
      <pre className="mt-4 rounded border border-line bg-surface p-3 text-[12px] text-muted">{log.join('\n') || 'no actions yet'}</pre>
    </div>
  );
}
