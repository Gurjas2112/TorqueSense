// History, analytics and the fleet: from the local API (same origin), or — on the public Vercel page —
// read-only from Supabase with the publishable key, which RLS limits to the showcase (spec Section 16).
import { createClient, type SupabaseClient } from '@supabase/supabase-js';
import { API, REPLAY, SUPABASE_KEY, SUPABASE_URL } from './config';
import type { Analytics, AnomalyRow, FleetStation } from './types';

let sb: SupabaseClient | null = null;
export function supabase(): SupabaseClient | null {
  if (!SUPABASE_URL || !SUPABASE_KEY) return null;
  sb ??= createClient(SUPABASE_URL, SUPABASE_KEY, { auth: { persistSession: false } });
  return sb;
}

async function json<T>(path: string): Promise<T> {
  const r = await fetch(`${API}${path}`);
  if (!r.ok) throw new Error(`${path}: ${r.status}`);
  return (await r.json()) as T;
}

async function replayFile<T>(name: string): Promise<T | null> {
  const r = await fetch(`${import.meta.env.BASE_URL}replay/${name}`).catch(() => null);
  return r && r.ok ? ((await r.json()) as T) : null;
}

export async function anomalies(limit = 40): Promise<AnomalyRow[]> {
  if (REPLAY) {
    const s = supabase();
    if (s) {
      const { data } = await s.from('anomalies').select('*').order('ts', { ascending: false }).limit(limit);
      if (data?.length) return data as AnomalyRow[];
    }
    return (await replayFile<AnomalyRow[]>('anomalies.json')) ?? [];
  }
  return json<AnomalyRow[]>(`/v1/anomalies?limit=${limit}`);
}

export async function analytics(hours = 24): Promise<Analytics> {
  if (REPLAY) return (await replayFile<Analytics>('analytics.json')) ?? { cycles: 0, passed: 0, anomalies: 0, ftr_hourly: [], most_violated: [] };
  return json<Analytics>(`/v1/analytics?hours=${hours}`);
}

export async function fleet(hours = 24): Promise<FleetStation[]> {
  if (REPLAY) return (await replayFile<FleetStation[]>('fleet.json')) ?? [];
  return json<FleetStation[]>(`/v1/fleet?hours=${hours}`);
}

export async function review(eventId: string, falsePositive: boolean): Promise<void> {
  if (REPLAY) return;   // the public page is read-only
  await fetch(`${API}/v1/anomalies/${eventId}/review`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ false_positive: falsePositive }),
  });
}

export function clipUrl(a: AnomalyRow): string | null {
  if (a.clip_key && SUPABASE_URL) return `${SUPABASE_URL}/storage/v1/object/public/clips/${a.clip_key}`;
  if (a.clip_local && !REPLAY) return `${API}/v1/clips/${a.event_id}.mp4`;
  return null;
}

/** Live: new anomalies as the outbox delivers them (Supabase Realtime). Returns an unsubscribe. */
export function onNewAnomaly(cb: (a: AnomalyRow) => void): () => void {
  const s = supabase();
  if (!s || !REPLAY) return () => {};
  const ch = s
    .channel('anomalies')
    .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'anomalies' }, (p) => cb(p.new as AnomalyRow))
    .subscribe();
  return () => void s.removeChannel(ch);
}
