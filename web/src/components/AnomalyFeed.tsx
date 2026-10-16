// Zone C (spec Section 21): cards with the looping clip, violation and station; a modal with the
// point-state strip, the narrative from the state machine, and Confirm / False positive — the review
// loop that turns a false alarm into a hard negative.
import { useEffect, useState } from 'react';
import { AlertTriangle, Check, Film, X } from 'lucide-react';
import { anomalies, clipUrl, onNewAnomaly, review } from '../lib/api';
import { REPLAY } from '../lib/config';
import type { AnomalyRow } from '../lib/types';

const kindColor: Record<string, string> = {
  SEQUENCE: 'var(--state-fault)', PRECEDENCE: 'var(--state-fault)', REGRESSION: 'var(--state-fault)',
  EVIDENCE_MISSING: 'var(--state-active)', STEP_TIMEOUT: 'var(--state-active)', INCOMPLETE_AT_RELEASE: 'var(--state-active)',
};

function time(ts: string) {
  const d = new Date(ts);
  return isNaN(+d) ? ts : d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

export function AnomalyFeed({ refreshKey }: { refreshKey?: unknown }) {
  const [rows, setRows] = useState<AnomalyRow[]>([]);
  const [open, setOpen] = useState<AnomalyRow | null>(null);
  useEffect(() => {
    let live = true;
    const load = () => anomalies(30).then((r) => live && setRows(r)).catch(() => {});
    load();
    const t = setInterval(load, 3000);
    const off = onNewAnomaly((a) => setRows((r) => [a, ...r].slice(0, 30)));
    return () => {
      live = false;
      clearInterval(t);
      off();
    };
  }, [refreshKey]);

  return (
    <div className="flex h-full flex-col">
      <div className="mb-2 flex items-center justify-between">
        <h2 className="zone-title">C · Anomaly feed</h2>
        <span className="mono text-muted">{rows.length} recent</span>
      </div>
      <div className="flex-1 space-y-2 overflow-auto pr-1" data-testid="anomaly-feed">
        {!rows.length && <div className="p-4 text-muted">No anomalies yet. Pick a fault in the harness.</div>}
        {rows.map((a) => (
          <button key={a.event_id} onClick={() => setOpen(a)} data-kind={a.violation_type}
            className="flex w-full items-start gap-3 rounded-md border border-line bg-surface-2 p-2.5 text-left hover:border-muted">
            <AlertTriangle size={16} style={{ color: kindColor[a.violation_type] }} className="mt-0.5 shrink-0" />
            <div className="min-w-0 flex-1">
              <div className="flex items-center justify-between gap-2">
                <span className="mono font-semibold" style={{ color: kindColor[a.violation_type] }}>{a.violation_type}</span>
                <span className="mono text-muted">{a.station_id} · {time(a.ts)}</span>
              </div>
              <div className="mt-0.5 truncate">{a.narrative}</div>
              <div className="mono mt-0.5 flex gap-3 text-[12px] text-muted">
                <span>{a.step_id}</span>
                <span>evidence {a.evidence_ms.toFixed(1)} ms</span>
                {clipUrl(a) && <span className="inline-flex items-center gap-1"><Film size={11} /> clip</span>}
                {a.false_positive === true && <span className="text-active">false positive</span>}
                {a.false_positive === false && <span className="text-ok">confirmed</span>}
                {a.synthetic && <span>synthetic</span>}
              </div>
            </div>
          </button>
        ))}
      </div>
      {open && <AnomalyModal a={open} onClose={() => setOpen(null)} onReviewed={(fp) => {
        setRows((r) => r.map((x) => (x.event_id === open.event_id ? { ...x, false_positive: fp } : x)));
        setOpen(null);
      }} />}
    </div>
  );
}

function AnomalyModal({ a, onClose, onReviewed }: { a: AnomalyRow; onClose: () => void; onReviewed: (fp: boolean) => void }) {
  const url = clipUrl(a);
  const detail = Object.entries(a.detail ?? {});
  return (
    <div className="fixed inset-0 z-50 grid place-items-center bg-black/70 p-4" onClick={onClose} role="dialog" aria-modal="true">
      <div className="panel w-full max-w-3xl p-5" onClick={(e) => e.stopPropagation()}>
        <div className="flex items-start justify-between">
          <div>
            <div className="mono text-lg font-semibold" style={{ color: kindColor[a.violation_type] }}>{a.violation_type} · {a.step_id}</div>
            <div className="mono text-muted">{a.station_id} · {a.profile} · cycle {a.cycle_id.slice(0, 8)} · {new Date(a.ts).toLocaleString()}</div>
          </div>
          <button onClick={onClose} aria-label="close" className="text-muted hover:text-ink"><X /></button>
        </div>
        <div className="mt-4 grid gap-4 md:grid-cols-[1.3fr_1fr]">
          <div className="aspect-video overflow-hidden rounded-md border border-line bg-black">
            {url ? <video src={url} autoPlay loop muted playsInline controls className="h-full w-full object-contain" />
              : <div className="grid h-full place-items-center text-muted">no clip for this event</div>}
          </div>
          <div className="space-y-3">
            <p className="text-[15px] leading-snug">{a.narrative}</p>
            <p className="text-[12px] text-muted">That sentence comes from the state machine, not a language model — it is a fact, not a guess.</p>
            <div className="mono rounded-md border border-line bg-surface-2 p-2 text-[12px]">
              {detail.map(([k, v]) => <div key={k}><span className="text-muted">{k}</span> {JSON.stringify(v)}</div>)}
              <div><span className="text-muted">engine</span> {a.engine_ms.toFixed(2)} ms · <span className="text-muted">evidence</span> {a.evidence_ms.toFixed(2)} ms</div>
              <div><span className="text-muted">adjudicated</span> {a.adjudicated ? 'yes (Florence-2)' : 'no'}</div>
            </div>
          </div>
        </div>
        {!REPLAY && (
          <div className="mt-4 flex justify-end gap-2">
            <button onClick={() => review(a.event_id, true).then(() => onReviewed(true))}
              className="rounded-md border border-active/60 px-3 py-1.5 text-active hover:bg-active/10">False positive</button>
            <button onClick={() => review(a.event_id, false).then(() => onReviewed(false))}
              className="inline-flex items-center gap-1 rounded-md bg-ok px-3 py-1.5 font-medium text-bg"><Check size={16} /> Confirm</button>
          </div>
        )}
      </div>
    </div>
  );
}
