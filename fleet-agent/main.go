// fleet-agent: per-station host and liveness reporter for TorqueSense.
//
// Why a separate process: the engine cannot report its own death. The agent runs beside
// it, probes its health endpoint, samples GPU and disk, and pushes one record per
// interval to the API. If the API is down it spools to a local JSONL file and replays later.
// Standard library only: one static binary, nothing third-party to patch on an edge PC.
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Gurjas2112/torquesense/fleet-agent/internal/probe"
)

type Record struct {
	TS        time.Time         `json:"ts"`
	Station   string            `json:"station_id"`
	Node      string            `json:"node"`
	Engine    probe.EngineState `json:"engine"`
	GPU       *probe.GPU        `json:"gpu,omitempty"`
	DiskFreeB uint64            `json:"disk_free_bytes"`
}

func main() {
	station := flag.String("station", "W-14", "station id")
	engineURL := flag.String("engine", "http://127.0.0.1:8095/health", "engine health URL")
	apiURL := flag.String("api", "http://127.0.0.1:8000/v1/health/host", "API ingest URL")
	diskPath := flag.String("disk", ".", "path whose volume is checked for free space")
	spool := flag.String("spool", "data/spool/fleet-agent.jsonl", "spool file when API is down")
	every := flag.Duration("every", 10*time.Second, "sampling interval")
	gpuSource := flag.String("gpu-source", "nvidia-smi", "nvidia-smi (laptop) | dcgm (GPU Operator node)")
	dcgmURL := flag.String("dcgm-url", "http://dcgm-local.gpu-operator.svc:9400/metrics", "this node's dcgm-exporter")
	smMax := flag.Float64("sm-clock-max", 0, "the GPU model's maximum SM clock in MHz, for dcgm")
	flag.Parse()
	apiKey := os.Getenv("API_KEY")
	node, _ := os.Hostname()
	if n := os.Getenv("NODE_NAME"); n != "" { // Kubernetes downward API
		node = n
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	client := &http.Client{Timeout: 2 * time.Second}
	sp := probe.NewSpool(*spool, 5<<20) // cap 5 MiB: a dead API must not fill the disk

	tick := time.NewTicker(*every)
	defer tick.Stop()
	for {
		rec := Record{TS: time.Now().UTC(), Station: *station, Node: node}
		rec.Engine = probe.Engine(ctx, client, *engineURL)
		var g probe.GPU
		var err error
		if *gpuSource == "dcgm" {
			g, err = probe.DCGM(ctx, client, *dcgmURL, *smMax)
		} else {
			g, err = probe.NvidiaSMI(ctx)
		}
		if err == nil {
			rec.GPU = &g
		} else {
			slog.Debug("gpu probe", "err", err)
		}
		rec.DiskFreeB, _ = probe.DiskFree(*diskPath)

		// One path for every record: append, then flush oldest-first. A record sent
		// directly while older ones sit in the spool would reach the API out of order,
		// and the drift job processes buckets in time order.
		line, _ := json.Marshal(rec)
		if err := sp.Append(line); err != nil {
			slog.Error("spool append failed; record lost", "err", err) // full or unwritable
		}
		sent, pending, err := sp.Flush(func(b []byte) error { return post(ctx, client, *apiURL, apiKey, b) })
		if pending > 0 {
			slog.Warn("api unreachable; records held in spool", "pending", pending, "sent", sent)
		}
		if err != nil {
			slog.Error("spool rewrite failed", "err", err)
		}

		select {
		case <-ctx.Done():
			slog.Info("shutting down")
			return
		case <-tick.C:
		}
	}
}

func post(ctx context.Context, c *http.Client, url, key string, body []byte) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-API-Key", key)
	resp, err := c.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 300 {
		return &statusError{resp.StatusCode}
	}
	return nil
}

type statusError struct{ code int }

func (e *statusError) Error() string { return http.StatusText(e.code) }
