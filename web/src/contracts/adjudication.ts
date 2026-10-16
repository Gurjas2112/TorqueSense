/* Generated from contracts/adjudication.schema.json by scripts/gen-contracts.sh. Do not edit. */

/**
 * Engine io thread <-> sidecar (:8001), POST /v1/adjudicate. The engine names a frame in the SharedClipRing and an inspection point's crop; the sidecar grounds a phrase in it with Florence-2. Never on the alert path (I3, I11): an answer can only resolve an ambiguous state commit.
 */
export interface Adjudication {
  request: AdjudicationRequest;
  response?: AdjudicationResponse;
}
export interface AdjudicationRequest {
  request_id: string;
  station_id: string;
  ip: string;
  family: string;
  /**
   * the state the detector thinks it sees, with confidence in the ambiguous band
   */
  candidate_state: string;
  conf?: number;
  /**
   * what to ground, from the family library; defaults to the family's phrase for candidate_state
   */
  phrase?: string;
  /**
   * SharedClipRing sequence of the frame
   */
  ring_seq: number;
  crop: {
    x: number;
    y: number;
    w: number;
    h: number;
  };
}
export interface AdjudicationResponse {
  request_id: string;
  /**
   * true if the grounded phrase sits on the inspection point
   */
  confirmed: boolean;
  boxes?: [number, number, number, number][];
  model?: string;
  revision?: string;
  latency_ms: number;
  cached?: boolean;
  error?: string | null;
}
