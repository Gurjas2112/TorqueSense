/* Generated from contracts/health.schema.json by scripts/gen-contracts.sh. Do not edit. */

/**
 * Engine io thread -> API, one bucket per station per bucket_s (300 s, 5 in demo mode). Columns of public.station_health (spec Sections 16 and 19).
 */
export interface StationHealth {
  station_id: string;
  bucket_start: string;
  bucket_s: number;
  fps: number;
  dropped_frame_rate: number;
  ip_conf_mean?: number | null;
  ambiguous_rate: number;
  anchor_residual_px?: number | null;
  evidence_p99_ms?: number | null;
  /**
   * NULL for file, webcam and phone sources
   */
  camera_temp_c?: number | null;
  /**
   * industrial GigE only (Appendix D.1)
   */
  packets_resent?: number | null;
  adjudications?: number;
  model_version: string;
  cad_revision: string;
}
