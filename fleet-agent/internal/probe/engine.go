package probe

import (
	"context"
	"encoding/json"
	"net/http"
)

// EngineState is "up" with the engine's own figures, or "down" with the reason.
type EngineState struct {
	Status       string  `json:"status"` // up | down
	Reason       string  `json:"reason,omitempty"`
	FPS          float64 `json:"fps,omitempty"`
	EvidenceP99  float64 `json:"evidence_p99_ms,omitempty"`
	ModelVersion string  `json:"model_version,omitempty"`
	CADRevision  string  `json:"cad_revision,omitempty"`
	UptimeS      float64 `json:"uptime_s,omitempty"`
}

func Engine(ctx context.Context, c *http.Client, url string) EngineState {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return EngineState{Status: "down", Reason: err.Error()}
	}
	resp, err := c.Do(req)
	if err != nil {
		return EngineState{Status: "down", Reason: "unreachable"}
	}
	defer resp.Body.Close()
	var s EngineState
	if resp.StatusCode != http.StatusOK || json.NewDecoder(resp.Body).Decode(&s) != nil {
		return EngineState{Status: "down", Reason: "bad health response"}
	}
	s.Status = "up"
	return s
}
