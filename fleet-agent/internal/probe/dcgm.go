package probe

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
)

// On a production node the GPU belongs to the NVIDIA GPU Operator, and an unprivileged pod
// that does not request a GPU gets no nvidia-smi. The operator's dcgm-exporter already
// publishes the same figures; read them from the exporter on this node.
var dcgmFields = map[string]func(*GPU, float64){
	"DCGM_FI_DEV_GPU_TEMP": func(g *GPU, v float64) { g.TempC = v },
	"DCGM_FI_DEV_SM_CLOCK": func(g *GPU, v float64) { g.SMClockMHz = v },
	"DCGM_FI_DEV_FB_USED":  func(g *GPU, v float64) { g.MemUsedMiB = v },
	"DCGM_FI_DEV_GPU_UTIL": func(g *GPU, v float64) { g.UtilPct = v },
}

// ParseDCGM reads Prometheus text exposition for GPU 0. dcgm-exporter does not publish a
// maximum SM clock, so it is configured per GPU model at commissioning (smClockMax).
func ParseDCGM(r io.Reader, smClockMax float64) (GPU, error) {
	var g GPU
	seen := 0
	sc := bufio.NewScanner(r)
	for sc.Scan() {
		line := sc.Text()
		if line == "" || line[0] == '#' {
			continue
		}
		name, rest, ok := strings.Cut(line, "{")
		if !ok {
			continue
		}
		set, want := dcgmFields[name]
		if !want || !strings.Contains(rest, `gpu="0"`) {
			continue
		}
		f := strings.Fields(rest[strings.LastIndex(rest, "}")+1:])
		if len(f) == 0 {
			continue
		}
		v, err := strconv.ParseFloat(f[0], 64)
		if err != nil {
			return GPU{}, fmt.Errorf("%s: %w", name, err)
		}
		set(&g, v)
		seen++
	}
	if seen == 0 {
		return GPU{}, fmt.Errorf("no DCGM GPU 0 fields found")
	}
	g.SMClockMax = smClockMax
	if smClockMax > 0 {
		g.SMClockRatio = g.SMClockMHz / smClockMax
	}
	return g, sc.Err()
}

func DCGM(ctx context.Context, c *http.Client, url string, smClockMax float64) (GPU, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return GPU{}, err
	}
	resp, err := c.Do(req)
	if err != nil {
		return GPU{}, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return GPU{}, fmt.Errorf("dcgm-exporter: %s", resp.Status)
	}
	return ParseDCGM(resp.Body, smClockMax)
}
