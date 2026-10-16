// Where everything is. Each station's engine listens on 8080 (WebSocket), 8090 (MJPEG) and 8095
// (control); every further station adds 100 (spec Section 29). The API serves this app, so /v1 is
// same-origin in a release and proxied by Vite in development.

export const REPLAY = import.meta.env.MODE === 'replay' || import.meta.env.VITE_REPLAY === '1';
export const SUPABASE_URL = import.meta.env.VITE_SUPABASE_URL as string | undefined;
export const SUPABASE_KEY = import.meta.env.VITE_SUPABASE_PUBLISHABLE_KEY as string | undefined;

export function stationIds(): string[] {
  const q = new URLSearchParams(globalThis.location?.search ?? '').get('stations');
  const env = import.meta.env.VITE_STATIONS as string | undefined;
  return (q ?? env ?? 'W-14').split(',').map((s) => s.trim()).filter(Boolean);
}

export function stationIndex(id: string): number {
  const i = stationIds().indexOf(id);
  return i < 0 ? 0 : i;
}

export function endpoints(id: string) {
  const host = '127.0.0.1';
  const o = 100 * stationIndex(id);
  return {
    ws: `ws://${host}:${8080 + o}`,
    mjpeg: `http://${host}:${8090 + o}/stream`,
    frame: `http://${host}:${8090 + o}/frame.jpg`,
    control: `http://${host}:${8095 + o}`,
  };
}

export const API = (import.meta.env.VITE_API as string | undefined) ?? '';
