// One station's live state: the engine's WebSocket (spec Section 21), or — in replay mode, for the
// public Vercel page — a recorded session played back on its own timestamps, labelled as a replay.
import { useEffect, useRef, useState } from 'react';
import { REPLAY, endpoints } from './config';
import type { LiveState } from './types';

export interface StationFeed {
  state: LiveState | null;
  connected: boolean;
  lastMessageMs: number;      // performance.now() of the last document
  silentMs: number;           // how long the feed has been quiet
  replay: boolean;
}

/** The HMI does not wait to be told: a feed silent for 1 s is INSPECTION OFFLINE (spec Section 21). */
export const OFFLINE_AFTER_MS = 1000;

export function useStation(id: string): StationFeed {
  const [state, setState] = useState<LiveState | null>(null);
  const [connected, setConnected] = useState(false);
  const last = useRef(0);
  const [, tick] = useState(0);

  useEffect(() => {
    const t = setInterval(() => tick((n) => n + 1), 250);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    let stop = false;
    if (REPLAY) {
      const timers: ReturnType<typeof setTimeout>[] = [];
      fetch(`${import.meta.env.BASE_URL}replay/session.jsonl`)
        .then((r) => (r.ok ? r.text() : ''))
        .then((text) => {
          const rows = text.split('\n').filter(Boolean).map((l) => JSON.parse(l) as { t: number; state: LiveState });
          if (!rows.length || stop) return;
          setConnected(true);
          const play = () => {
            const span = rows[rows.length - 1].t + 1500;
            for (const r of rows)
              timers.push(setTimeout(() => {
                if (stop) return;
                last.current = performance.now();
                setState({ ...r.state, station_id: id });
              }, r.t));
            timers.push(setTimeout(() => !stop && play(), span));
          };
          play();
        })
        .catch(() => setConnected(false));
      return () => {
        stop = true;
        timers.forEach(clearTimeout);
      };
    }

    let ws: WebSocket | null = null;
    let retry: ReturnType<typeof setTimeout> | undefined;
    const open = () => {
      ws = new WebSocket(endpoints(id).ws);
      ws.onopen = () => setConnected(true);
      ws.onmessage = (e) => {
        last.current = performance.now();
        try {
          setState(JSON.parse(e.data as string) as LiveState);
        } catch {
          /* a torn message: the next one is 66 ms away */
        }
      };
      ws.onclose = () => {
        setConnected(false);
        if (!stop) retry = setTimeout(open, 1000);
      };
      ws.onerror = () => ws?.close();
    };
    open();
    return () => {
      stop = true;
      clearTimeout(retry);
      ws?.close();
    };
  }, [id]);

  const silentMs = last.current ? performance.now() - last.current : Infinity;
  return { state, connected, lastMessageMs: last.current, silentMs, replay: REPLAY };
}
