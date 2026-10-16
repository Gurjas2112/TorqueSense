// Zone D (spec Section 21): most-violated steps, first-time-right by hour, and Export to Excel — the
// shift report the engine writes in C++ (XlsxReporter), served by its control port.
import { useEffect, useState } from 'react';
import { Bar, BarChart, CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { FileSpreadsheet } from 'lucide-react';
import { analytics } from '../lib/api';
import { REPLAY, endpoints } from '../lib/config';
import type { Analytics } from '../lib/types';

const axis = { stroke: 'var(--text-muted)', fontSize: 11, fontFamily: 'IBM Plex Mono' };

export function ShiftAnalytics({ station }: { station: string }) {
  const [a, setA] = useState<Analytics | null>(null);
  useEffect(() => {
    let live = true;
    const load = () => analytics(24).then((x) => live && setA(x)).catch(() => {});
    load();
    const t = setInterval(load, 5000);
    return () => {
      live = false;
      clearInterval(t);
    };
  }, []);
  const ftr = a && a.cycles ? (100 * a.passed) / a.cycles : null;
  const hours = (a?.ftr_hourly ?? []).map((h) => ({ ...h, hour: h.bucket.slice(11, 16), pct: Math.round(h.first_time_right * 100) }));
  return (
    <div className="flex h-full flex-col">
      <div className="mb-2 flex items-center justify-between">
        <h2 className="zone-title">D · Shift analytics</h2>
        {!REPLAY && (
          <a href={`${endpoints(station).control}/report.xlsx`} className="inline-flex items-center gap-1.5 rounded-md border border-line px-2.5 py-1 hover:border-muted">
            <FileSpreadsheet size={15} className="text-ok" /> Export to Excel
          </a>
        )}
      </div>
      <div className="mb-3 grid grid-cols-3 gap-2">
        <Stat label="cycles (24 h)" value={a ? String(a.cycles) : '—'} />
        <Stat label="first-time-right" value={ftr == null ? '—' : `${ftr.toFixed(0)}%`} />
        <Stat label="anomalies" value={a ? String(a.anomalies) : '—'} />
      </div>
      <div className="grid flex-1 grid-cols-2 gap-3">
        <div className="min-h-[140px]">
          <div className="mb-1 text-[12px] text-muted">Most-violated steps</div>
          <ResponsiveContainer width="100%" height="88%">
            <BarChart data={a?.most_violated ?? []} layout="vertical" margin={{ left: 8, right: 8 }}>
              <XAxis type="number" allowDecimals={false} {...axis} />
              <YAxis type="category" dataKey={(d) => `${d.step} · ${d.violation_type}`} width={150} {...axis} />
              <Tooltip contentStyle={{ background: 'var(--surface-2)', border: '1px solid var(--line)' }} />
              <Bar dataKey="count" fill="var(--state-fault)" radius={[0, 4, 4, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>
        <div className="min-h-[140px]">
          <div className="mb-1 text-[12px] text-muted">First-time-right by hour</div>
          <ResponsiveContainer width="100%" height="88%">
            <LineChart data={hours} margin={{ left: -12, right: 8 }}>
              <CartesianGrid stroke="var(--line)" strokeDasharray="3 3" />
              <XAxis dataKey="hour" {...axis} />
              <YAxis domain={[0, 100]} unit="%" {...axis} />
              <Tooltip contentStyle={{ background: 'var(--surface-2)', border: '1px solid var(--line)' }} />
              <Line dataKey="pct" stroke="var(--state-ok)" strokeWidth={2} dot={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </div>
      {a?.synthetic && <div className="mt-1 text-[11px] text-active">includes seeded synthetic history</div>}
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-md border border-line bg-surface-2 px-3 py-2">
      <div className="mono text-xl font-semibold">{value}</div>
      <div className="text-[12px] text-muted">{label}</div>
    </div>
  );
}
