// The engine's state document is the generated contract (contracts/state.schema.json); these are the
// few fields the engine adds beside it, and the API's read models.
import type { StationState } from '../contracts';

export type Andon = 'GREEN' | 'RED' | 'AMBER';

export interface LiveState extends StationState {
  scenario?: string | null;
  hands?: number;
  plc_connected?: boolean;
  frozen?: boolean;
}

export interface AnomalyRow {
  event_id: string;
  ts: string;
  station_id: string;
  cycle_id: string;
  profile: string;
  step_id: string;
  violation_type: string;
  observed_ip?: string | null;
  expected_ip?: string | null;
  detail?: Record<string, unknown>;
  engine_ms: number;
  evidence_ms: number;
  clip_key?: string | null;
  clip_local?: string | null;
  narrative: string;
  adjudicated?: boolean;
  false_positive?: boolean | null;
  reviewed_at?: string | null;
  synthetic?: boolean;
}

export interface Analytics {
  cycles: number;
  passed: number;
  anomalies: number;
  ftr_hourly: { bucket: string; first_time_right: number; cycles: number }[];
  most_violated: { step: string; violation_type: string; count: number }[];
  synthetic?: boolean;
}

export interface SignalChart {
  state: 'baseline' | 'in_control' | 'drift';
  direction: 'up' | 'down';
  center: number | null;
  limit: number | null;
  series: { t: string; v: number }[];
}

export interface FleetStation {
  station_id: string;
  engine_up: boolean | null;
  model_version: string | null;
  cad_revision: string | null;
  signals: Record<string, SignalChart>;
  drifting: string[];
  diagnosis: { signals: string[]; likely_cause: string; action: string } | null;
  events: { ts: string; kind: string; metric: string | null }[];
  canary: boolean;
  synthetic: boolean;
}
