/* Generated from contracts/host_health.schema.json by scripts/gen-contracts.sh. Do not edit. */

/**
 * Fleet agent -> API (POST /v1/health/host), every 10 s. The record fleet-agent/main.go sends; the API flattens it into public.host_health.
 */
export interface HostHealth {
  ts: string;
  station_id: string;
  node: string;
  engine: {
    status: "up" | "down";
    reason?: string;
    fps?: number;
    evidence_p99_ms?: number;
    model_version?: string;
    cad_revision?: string;
    uptime_s?: number;
  };
  gpu?: {
    temp_c: number;
    sm_clock_mhz: number;
    sm_clock_max_mhz: number;
    mem_used_mib: number;
    util_pct: number;
    sm_clock_ratio: number;
  };
  disk_free_bytes: number;
}
