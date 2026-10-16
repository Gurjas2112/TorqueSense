/* Generated from contracts/state.schema.json by scripts/gen-contracts.sh. Do not edit. */

/**
 * Engine -> dashboard and HMI, about 15 Hz over the WebSocket on :8080. Built on the io thread from the hot path's fixed-size StateVector; JSON never touches a pinned thread (spec Section 30).
 */
export interface StationState {
  /**
   * e.g. W-14
   */
  station_id: string;
  frame_seq: number;
  /**
   * engine wall clock, ms since the Unix epoch
   */
  ts_ms?: number;
  cycle_id: string;
  cycle_elapsed_ms?: number;
  takt_s?: number;
  /**
   * id of the first incomplete step, or an empty string when every step is complete
   */
  step: string;
  profile: string;
  profile_version?: string;
  cad_revision?: string;
  model_version?: string;
  /**
   * execution provider in use: tensorrt | cuda | replay
   */
  provider?: string;
  /**
   * why the engine runs on a fallback provider, or null
   */
  degraded?: string | null;
  advisory_only?: boolean;
  steps?: StepState[];
  ips: InspectionPointState[];
  /**
   * the inspection points the operator should act on next
   */
  next: string[];
  latency: Latency;
  verdict: null | Verdict;
  andon?: "GREEN" | "RED" | "AMBER";
}
export interface StepState {
  id: string;
  kind: "complete" | "sequence";
  state: string;
  targets?: string[];
  done: number;
  total: number;
  status: "waiting" | "active" | "complete" | "violated";
  elapsed_ms?: number;
  allotted_ms?: number;
}
export interface InspectionPointState {
  id: string;
  family: string;
  state: string;
  conf: number;
  ambiguous?: boolean;
  /**
   * image column of the CAD-projected point, full-resolution pixels
   */
  u: number;
  v: number;
  /**
   * position in the assembly frame, for the part map
   */
  x_mm?: number;
  y_mm?: number;
}
export interface Latency {
  engine_ms_p50: number;
  engine_ms_p99: number;
  evidence_ms_p99: number;
  last_engine_ms?: number;
  last_evidence_ms?: number;
}
export interface Verdict {
  kind:
    | "SEQUENCE"
    | "PRECEDENCE"
    | "EVIDENCE_MISSING"
    | "REGRESSION"
    | "STEP_TIMEOUT"
    | "INCOMPLETE_AT_RELEASE"
    | "RELEASED";
  step: string;
  observed_ip?: string | null;
  expected_ip?: string | null;
  narrative?: string;
  /**
   * the order still to do, for the corrective arc
   */
  corrective?: string[];
}
