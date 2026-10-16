/* Generated from contracts/anomaly.schema.json by scripts/gen-contracts.sh. Do not edit. */

/**
 * Engine -> API, once per violation (spec Section 22). The API stores it in the outbox and forwards it to public.anomalies; the columns here are that table's.
 */
export interface Anomaly {
  /**
   * minted at the source: replays are idempotent
   */
  event_id: string;
  ts: string;
  station_id: string;
  cycle_id: string;
  profile: string;
  profile_version: string;
  step_id: string;
  violation_type:
    "SEQUENCE" | "PRECEDENCE" | "EVIDENCE_MISSING" | "REGRESSION" | "STEP_TIMEOUT" | "INCOMPLETE_AT_RELEASE";
  observed_ip?: string | null;
  expected_ip?: string | null;
  /**
   * violation-specific facts from the interpreter (missing, before, signal …)
   */
  detail?: {};
  engine_ms: number;
  evidence_ms: number;
  /**
   * clip ring sequence at the violation; the sidecar cuts the clip around it
   */
  ring_end_seq?: number;
  clip_key?: string | null;
  narrative: string;
  adjudicated?: boolean;
  cad_revision: string;
  model_version: string;
}
