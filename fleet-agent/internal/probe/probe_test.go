package probe

import (
	"errors"
	"path/filepath"
	"strings"
	"testing"
)

func TestParseGPU(t *testing.T) {
	g, err := ParseGPU("61, 1200, 2100, 2894, 47\n")
	if err != nil {
		t.Fatal(err)
	}
	if g.TempC != 61 || g.SMClockRatio < 0.571 || g.SMClockRatio > 0.572 {
		t.Fatalf("unexpected %+v", g)
	}
}

func TestParseGPURejectsNA(t *testing.T) {
	if _, err := ParseGPU("61, [N/A], 2100, 2894, 47"); err == nil {
		t.Fatal("want error for [N/A]")
	}
}

func TestSpoolFlushKeepsOrderAndStopsAtFailure(t *testing.T) {
	s := NewSpool(filepath.Join(t.TempDir(), "s.jsonl"), 1<<20)
	for _, l := range []string{"a", "b", "c"} {
		if err := s.Append([]byte(l)); err != nil {
			t.Fatal(err)
		}
	}
	var got []string
	n, pending, _ := s.Flush(func(b []byte) error {
		if string(b) == "b" {
			return errors.New("api down again")
		}
		got = append(got, string(b))
		return nil
	})
	if n != 1 || pending != 2 || len(got) != 1 || got[0] != "a" {
		t.Fatalf("first flush: sent=%d pending=%d got=%v", n, pending, got)
	}
	got = nil
	n, pending, _ = s.Flush(func(b []byte) error { got = append(got, string(b)); return nil })
	if n != 2 || pending != 0 || got[0] != "b" || got[1] != "c" {
		t.Fatalf("second flush: sent=%d got=%v (order must be preserved)", n, got)
	}
}

func TestSpoolCap(t *testing.T) {
	s := NewSpool(filepath.Join(t.TempDir(), "s.jsonl"), 8)
	_ = s.Append([]byte("12345"))
	if err := s.Append([]byte("67890")); !errors.Is(err, ErrSpoolFull) {
		t.Fatalf("want ErrSpoolFull, got %v", err)
	}
}

func TestParseDCGM(t *testing.T) {
	text := `# HELP DCGM_FI_DEV_SM_CLOCK SM clock frequency (in MHz).
# TYPE DCGM_FI_DEV_SM_CLOCK gauge
DCGM_FI_DEV_SM_CLOCK{gpu="0",UUID="GPU-1",device="nvidia0",modelName="NVIDIA RTX 2000 Ada Generation",Hostname="edge-w14"} 1620
DCGM_FI_DEV_GPU_TEMP{gpu="0",UUID="GPU-1",device="nvidia0",Hostname="edge-w14"} 63
DCGM_FI_DEV_FB_USED{gpu="0",UUID="GPU-1",device="nvidia0",Hostname="edge-w14"} 3012
DCGM_FI_DEV_GPU_UTIL{gpu="0",UUID="GPU-1",device="nvidia0",Hostname="edge-w14"} 41
DCGM_FI_DEV_SM_CLOCK{gpu="1",UUID="GPU-2",device="nvidia1",Hostname="edge-w14"} 300
`
	g, err := ParseDCGM(strings.NewReader(text), 2025)
	if err != nil {
		t.Fatal(err)
	}
	if g.SMClockMHz != 1620 || g.TempC != 63 || g.MemUsedMiB != 3012 || g.UtilPct != 41 {
		t.Fatalf("unexpected %+v (GPU 1 must be ignored)", g)
	}
	if g.SMClockRatio < 0.79 || g.SMClockRatio > 0.81 {
		t.Fatalf("ratio %v", g.SMClockRatio)
	}
}

func TestParseDCGMEmpty(t *testing.T) {
	if _, err := ParseDCGM(strings.NewReader("# nothing\n"), 2025); err == nil {
		t.Fatal("want error when no GPU 0 fields")
	}
}
