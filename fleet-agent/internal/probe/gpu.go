package probe

import (
	"context"
	"fmt"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

type GPU struct {
	TempC        float64 `json:"temp_c"`
	SMClockMHz   float64 `json:"sm_clock_mhz"`
	SMClockMax   float64 `json:"sm_clock_max_mhz"`
	MemUsedMiB   float64 `json:"mem_used_mib"`
	UtilPct      float64 `json:"util_pct"`
	SMClockRatio float64 `json:"sm_clock_ratio"` // < 1 under load means throttling
}

const gpuQuery = "temperature.gpu,clocks.sm,clocks.max.sm,memory.used,utilization.gpu"

// NvidiaSMI samples the first GPU. nvidia-smi ships with the driver on Windows and Linux.
func NvidiaSMI(ctx context.Context) (GPU, error) {
	ctx, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, "nvidia-smi",
		"--query-gpu="+gpuQuery, "--format=csv,noheader,nounits", "--id=0").Output()
	if err != nil {
		return GPU{}, err
	}
	return ParseGPU(string(out))
}

// ParseGPU parses one CSV line, e.g. "61, 1200, 2100, 2894, 47".
func ParseGPU(line string) (GPU, error) {
	f := strings.Split(strings.TrimSpace(line), ",")
	if len(f) != 5 {
		return GPU{}, fmt.Errorf("want 5 fields, got %d: %q", len(f), line)
	}
	v := make([]float64, 5)
	for i, s := range f {
		x, err := strconv.ParseFloat(strings.TrimSpace(s), 64)
		if err != nil {
			return GPU{}, fmt.Errorf("field %d %q: %w", i, s, err) // "[N/A]" on some laptops
		}
		v[i] = x
	}
	g := GPU{TempC: v[0], SMClockMHz: v[1], SMClockMax: v[2], MemUsedMiB: v[3], UtilPct: v[4]}
	if g.SMClockMax > 0 {
		g.SMClockRatio = g.SMClockMHz / g.SMClockMax
	}
	return g, nil
}
