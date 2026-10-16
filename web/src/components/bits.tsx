// Small shared pieces: the latency badge, the step rail, the provenance chip, the replay banner.
import { Activity, Boxes, Radio } from 'lucide-react';
import type { LiveState } from '../lib/types';

export function LatencyBadge({ s }: { s: LiveState | null }) {
  const l = s?.latency;
  const f = (v?: number | null) => (v == null || v === 0 ? '—' : v.toFixed(1));
  return (
    <div className="mono flex items-center gap-3 rounded-md border border-line bg-surface-2 px-3 py-1.5" title="engine: commit frame → OPC-UA write acknowledged · evidence: first frame showing it → OPC-UA write acknowledged">
      <Activity size={14} className="text-info" />
      <span><span className="text-muted">engine</span> {f(l?.last_engine_ms)} ms</span>
      <span><span className="text-muted">evidence</span> {f(l?.last_evidence_ms)} ms</span>
      <span className="text-muted">p99 {f(l?.evidence_ms_p99)}</span>
    </div>
  );
}

const statusColor: Record<string, string> = {
  complete: 'var(--state-ok)', active: 'var(--state-active)', violated: 'var(--state-fault)', waiting: 'var(--line)',
};

export function StepRail({ s, big = false }: { s: LiveState | null; big?: boolean }) {
  const steps = s?.steps ?? [];
  return (
    <div className="flex gap-2">
      {steps.map((st) => {
        const status = String(st.status).toLowerCase();
        const pct = st.total ? Math.round((100 * st.done) / st.total) : 0;
        return (
          <div key={st.id} className="flex-1 rounded-md border border-line bg-surface-2 p-2" data-step={st.id} data-status={status}>
            <div className={big ? 'truncate text-[40px] font-semibold leading-tight' : 'truncate text-sm font-medium'}>
              {st.id.replace(/_/g, ' ')}
            </div>
            <div className="mt-1.5 h-2 overflow-hidden rounded bg-bg">
              <div className="h-full transition-all" style={{ width: `${status === 'complete' ? 100 : pct}%`, background: statusColor[status] }} />
            </div>
            {!big && (
              <div className="mono mt-1 flex justify-between text-muted">
                <span>{st.done}/{st.total} {st.state}</span>
                <span>{st.allotted_ms ? `${Math.round(st.allotted_ms / 1000)} s` : ''}</span>
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}

export function ProvenanceChip({ s }: { s: LiveState | null }) {
  if (!s) return null;
  return (
    <span className="mono inline-flex items-center gap-1.5 rounded border border-info/50 px-2 py-0.5 text-[12px] text-info" title="the CAD revision and profile version every verdict is stamped with">
      <Boxes size={12} /> {s.profile} · cad {s.cad_revision} · {s.profile_version}
    </span>
  );
}

export function ReplayBanner({ show }: { show: boolean }) {
  if (!show) return null;
  return (
    <div className="flex items-center justify-center gap-2 bg-info/20 px-3 py-1 text-[12px] text-ink">
      <Radio size={12} /> REPLAY — a recorded session from the station engine, played back. Clips and history are synthetic renders and schematic replays only.
    </div>
  );
}
