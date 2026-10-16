// Package supervise runs a station's processes: in dependency order, each started only after
// the ones it needs are ready, restarted with backoff when it exits, and all stopped — children
// included — when the supervisor stops or dies. Standard library only, like the rest of the agent.
package supervise

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Duration is a time.Duration that reads "2s" or "500ms" from JSON.
type Duration struct{ time.Duration }

func (d *Duration) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return fmt.Errorf("duration must be a string such as \"2s\": %w", err)
	}
	v, err := time.ParseDuration(s)
	if err != nil {
		return err
	}
	d.Duration = v
	return nil
}

func (d Duration) MarshalJSON() ([]byte, error) { return json.Marshal(d.String()) }

// Spec is one supervised process.
type Spec struct {
	Name         string            `json:"name"`
	Exe          string            `json:"exe"`
	Args         []string          `json:"args"`
	Dir          string            `json:"dir"`
	Env          map[string]string `json:"env"`
	EnvFile      string            `json:"env_file"`      // KEY=VALUE lines, e.g. {config}\.env; Env wins on a clash
	Ready        string            `json:"ready"`         // "tcp://127.0.0.1:4840", "http://…/health", or "" (ready once started)
	ReadyTimeout Duration          `json:"ready_timeout"` // default 60s; the TensorRT engine load needs it
	After        []string          `json:"after"`         // started only once these are ready
	StopURL      string            `json:"stop_url"`      // optional: POSTed first, for a clean shutdown
	StopGrace    Duration          `json:"stop_grace"`    // default 5s, then the process is killed
}

// Config is the whole station: supervisor.json.
type Config struct {
	Vars        map[string]string `json:"vars"`    // {release}, {data}, {config}, … substituted everywhere
	Path        []string          `json:"path"`    // the children's PATH, in order, before the OS directories
	LogDir      string            `json:"log_dir"` // one rotating log per process
	LogMaxBytes int64             `json:"log_max_bytes"`
	LogKeep     int               `json:"log_keep"`
	Listen      string            `json:"listen"` // status endpoint, e.g. 127.0.0.1:8099
	Backoff     struct {
		Min         Duration `json:"min"`          // first restart delay
		Max         Duration `json:"max"`          // ceiling
		StableAfter Duration `json:"stable_after"` // a run this long resets the delay
		LoopStarts  int      `json:"loop_starts"`  // this many starts…
		LoopWindow  Duration `json:"loop_window"`  // …inside this window = crash-looping
	} `json:"backoff"`
	Processes []Spec `json:"processes"`
}

// Load reads, substitutes and validates a configuration. Every error names the process and field.
func Load(path string) (*Config, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return Parse(b)
}

func Parse(b []byte) (*Config, error) {
	var c Config
	dec := json.NewDecoder(strings.NewReader(string(b)))
	dec.DisallowUnknownFields() // a misspelt key is an error, not a silently ignored setting
	if err := dec.Decode(&c); err != nil {
		return nil, fmt.Errorf("supervisor.json: %w", err)
	}
	c.defaults()
	sub := func(s string) string {
		for k, v := range c.Vars {
			s = strings.ReplaceAll(s, "{"+k+"}", v)
		}
		return s
	}
	c.LogDir = sub(c.LogDir)
	for i := range c.Path {
		c.Path[i] = sub(c.Path[i])
	}
	for i := range c.Processes {
		p := &c.Processes[i]
		p.Exe, p.Dir, p.Ready, p.StopURL, p.EnvFile = sub(p.Exe), sub(p.Dir), sub(p.Ready), sub(p.StopURL), sub(p.EnvFile)
		for j := range p.Args {
			p.Args[j] = sub(p.Args[j])
		}
		for k, v := range p.Env {
			p.Env[k] = sub(v)
		}
	}
	if err := c.validate(); err != nil {
		return nil, err
	}
	return &c, nil
}

func (c *Config) defaults() {
	if c.LogMaxBytes == 0 {
		c.LogMaxBytes = 10 << 20
	}
	if c.LogKeep == 0 {
		c.LogKeep = 5
	}
	if c.Listen == "" {
		c.Listen = "127.0.0.1:8099"
	}
	b := &c.Backoff
	if b.Min.Duration == 0 {
		b.Min.Duration = time.Second
	}
	if b.Max.Duration == 0 {
		b.Max.Duration = 30 * time.Second
	}
	if b.StableAfter.Duration == 0 {
		b.StableAfter.Duration = time.Minute
	}
	if b.LoopStarts == 0 {
		b.LoopStarts = 5
	}
	if b.LoopWindow.Duration == 0 {
		b.LoopWindow.Duration = 5 * time.Minute
	}
	for i := range c.Processes {
		p := &c.Processes[i]
		if p.ReadyTimeout.Duration == 0 {
			p.ReadyTimeout.Duration = time.Minute
		}
		if p.StopGrace.Duration == 0 {
			p.StopGrace.Duration = 5 * time.Second
		}
	}
}

func (c *Config) validate() error {
	if len(c.Processes) == 0 {
		return fmt.Errorf("supervisor.json: no processes")
	}
	if c.LogDir == "" {
		return fmt.Errorf("supervisor.json: log_dir is required")
	}
	for _, d := range append([]string{c.LogDir}, c.Path...) {
		if strings.Contains(d, "{") {
			return fmt.Errorf("supervisor.json: unresolved variable in %q", d)
		}
	}
	for k, v := range c.Vars {
		if v == "" { // register-station.ps1 fills cuda_bin and cudnn_bin from where the DLLs really are
			return fmt.Errorf("supervisor.json: variable %q is empty — run register-station.ps1", k)
		}
	}
	seen := map[string]bool{}
	for _, p := range c.Processes {
		switch {
		case p.Name == "":
			return fmt.Errorf("a process has no name")
		case seen[p.Name]:
			return fmt.Errorf("process %q is defined twice", p.Name)
		case p.Exe == "":
			return fmt.Errorf("process %q: exe is required", p.Name)
		case strings.Contains(p.Exe+" "+strings.Join(p.Args, " ")+" "+p.Dir+" "+p.EnvFile, "{"):
			return fmt.Errorf("process %q: unresolved variable in %q", p.Name, p.Exe+" "+strings.Join(p.Args, " "))
		case p.Ready != "" && !strings.HasPrefix(p.Ready, "tcp://") && !strings.HasPrefix(p.Ready, "http://"):
			return fmt.Errorf("process %q: ready must start with tcp:// or http://", p.Name)
		}
		if !filepath.IsAbs(p.Exe) && !strings.ContainsAny(p.Exe, `/\`) {
			// a bare name would be looked up on the children's PATH; refuse, so the release decides
			return fmt.Errorf("process %q: exe must be a path, not a bare name", p.Name)
		}
		seen[p.Name] = true
	}
	for _, p := range c.Processes {
		for _, a := range p.After {
			if !seen[a] {
				return fmt.Errorf("process %q: after %q, which is not defined", p.Name, a)
			}
		}
	}
	if _, err := c.Order(); err != nil {
		return err
	}
	return nil
}

// Order returns the processes so that each comes after everything it needs.
func (c *Config) Order() ([]Spec, error) {
	byName := map[string]Spec{}
	for _, p := range c.Processes {
		byName[p.Name] = p
	}
	const (
		white = iota
		grey
		black
	)
	color := map[string]int{}
	var out []Spec
	var visit func(n string, chain []string) error
	visit = func(n string, chain []string) error {
		switch color[n] {
		case grey:
			return fmt.Errorf("dependency cycle: %s", strings.Join(append(chain, n), " → "))
		case black:
			return nil
		}
		color[n] = grey
		for _, a := range byName[n].After {
			if err := visit(a, append(chain, n)); err != nil {
				return err
			}
		}
		color[n] = black
		out = append(out, byName[n])
		return nil
	}
	for _, p := range c.Processes { // definition order breaks ties, so the order is stable
		if err := visit(p.Name, nil); err != nil {
			return nil, err
		}
	}
	return out, nil
}

// ReadEnvFile parses KEY=VALUE lines: blank lines and # comments skipped, one level of matching
// quotes removed. Enough for the project's .env; no interpolation, on purpose.
func ReadEnvFile(path string) (map[string]string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	out := map[string]string{}
	for i, line := range strings.Split(strings.ReplaceAll(string(b), "\r\n", "\n"), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok || strings.TrimSpace(k) == "" {
			return nil, fmt.Errorf("%s:%d: not KEY=VALUE", path, i+1)
		}
		v = strings.TrimSpace(v)
		if len(v) >= 2 && (v[0] == '"' || v[0] == '\'') && v[len(v)-1] == v[0] {
			v = v[1 : len(v)-1]
		}
		out[strings.TrimSpace(k)] = v
	}
	return out, nil
}
