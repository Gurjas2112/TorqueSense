package supervise

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// State of one process, as /status reports it.
type State string

const (
	Waiting  State = "waiting"  // its dependencies are not ready yet
	Starting State = "starting" // running, readiness not yet confirmed
	Ready    State = "ready"
	Backoff  State = "backoff" // exited; waiting to restart
	Stopped  State = "stopped"
)

type Status struct {
	Name         string    `json:"name"`
	State        State     `json:"state"`
	PID          int       `json:"pid,omitempty"`
	Starts       int       `json:"starts"`
	LastExit     string    `json:"last_exit,omitempty"`
	Since        time.Time `json:"since"`
	CrashLooping bool      `json:"crash_looping"`
}

type proc struct {
	spec   Spec
	mu     sync.Mutex
	st     Status
	starts []time.Time
	ready  chan struct{} // closed the first time the process becomes ready
	once   sync.Once
	cmd    *exec.Cmd
	exited chan struct{}
}

func (p *proc) set(f func(*Status)) {
	p.mu.Lock()
	f(&p.st)
	p.mu.Unlock()
}

func (p *proc) status() Status {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.st
}

// Supervisor owns every process in a Config.
type Supervisor struct {
	cfg   *Config
	order []Spec
	group *group // kills every child if the supervisor itself dies (job object / Pdeathsig)
	log   *slog.Logger

	mu          sync.Mutex
	procs       map[string]*proc // the current session's processes
	maintenance bool
	toggle      chan bool // true: enter maintenance, false: leave it
}

func New(cfg *Config, log *slog.Logger) (*Supervisor, error) {
	order, err := cfg.Order()
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(cfg.LogDir, 0o755); err != nil {
		return nil, err
	}
	g, err := newGroup()
	if err != nil {
		return nil, fmt.Errorf("process group: %w", err)
	}
	s := &Supervisor{cfg: cfg, order: order, group: g, log: log, toggle: make(chan bool)}
	s.procs = s.freshProcs(Waiting)
	return s, nil
}

func (s *Supervisor) freshProcs(st State) map[string]*proc {
	m := map[string]*proc{}
	for _, sp := range s.order {
		m[sp.Name] = &proc{spec: sp, ready: make(chan struct{}),
			st: Status{Name: sp.Name, State: st, Since: time.Now()}}
	}
	return m
}

// Run starts everything and blocks until ctx is cancelled, then stops everything in reverse order.
// Maintenance mode stops every process the same way and holds them stopped until it is left, so a
// release gate can have the GPU to itself without anyone touching Task Scheduler.
func (s *Supervisor) Run(ctx context.Context) error {
	for {
		sessCtx, cancel := context.WithCancel(ctx)
		s.mu.Lock()
		procs := s.procs
		s.mu.Unlock()
		done := s.session(sessCtx, procs)
		enter := false
		for !enter {
			select {
			case <-ctx.Done():
				cancel()
				s.stopAll(procs)
				<-done
				return s.group.close()
			case on := <-s.toggle:
				enter = on
			}
		}
		cancel()
		s.stopAll(procs)
		<-done
		s.log.Warn("maintenance: every process stopped")
		s.mu.Lock()
		s.procs = s.freshProcs(Stopped)
		s.maintenance = true
		s.mu.Unlock()
		for leave := false; !leave; {
			select {
			case <-ctx.Done():
				return s.group.close()
			case on := <-s.toggle:
				leave = !on
			}
		}
		s.mu.Lock()
		s.procs = s.freshProcs(Waiting)
		s.maintenance = false
		s.mu.Unlock()
		s.log.Info("maintenance over: starting again")
	}
}

func (s *Supervisor) session(ctx context.Context, procs map[string]*proc) <-chan struct{} {
	var wg sync.WaitGroup
	for _, sp := range s.order {
		p := procs[sp.Name]
		wg.Add(1)
		go func() { defer wg.Done(); s.keep(ctx, p, procs) }()
	}
	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	return done
}

func (s *Supervisor) stopAll(procs map[string]*proc) {
	for i := len(s.order) - 1; i >= 0; i-- { // dependents first
		s.stop(procs[s.order[i].Name])
	}
}

// SetMaintenance enters (true) or leaves (false) maintenance mode.
func (s *Supervisor) SetMaintenance(ctx context.Context, on bool) {
	select {
	case s.toggle <- on:
	case <-ctx.Done():
	}
}

// keep runs one process for as long as ctx lives.
func (s *Supervisor) keep(ctx context.Context, p *proc, procs map[string]*proc) {
	for _, dep := range p.spec.After { // first start only: a dependency's later restart does not cascade
		select {
		case <-procs[dep].ready:
		case <-ctx.Done():
			p.set(func(st *Status) { st.State, st.Since = Stopped, time.Now() })
			return
		}
	}
	b := s.cfg.Backoff
	delay := b.Min.Duration
	logf, err := OpenRotating(filepath.Join(s.cfg.LogDir, p.spec.Name+".log"), s.cfg.LogMaxBytes, s.cfg.LogKeep)
	if err != nil {
		s.log.Error("cannot open log", "process", p.spec.Name, "err", err)
		return
	}
	defer logf.Close()
	for ctx.Err() == nil {
		started := time.Now()
		cmd, err := s.start(p, logf)
		if err != nil {
			s.log.Error("start failed", "process", p.spec.Name, "err", err)
			p.set(func(st *Status) { st.State, st.LastExit, st.Since = Backoff, err.Error(), time.Now() })
		} else {
			readyCtx, cancelReady := context.WithCancel(ctx)
			go s.awaitReady(readyCtx, p, cmd)
			werr := cmd.Wait()
			cancelReady()
			close(p.exited)
			exit := "exit 0"
			if werr != nil {
				exit = werr.Error()
			}
			if ctx.Err() != nil {
				p.set(func(st *Status) { st.State, st.PID, st.LastExit, st.Since = Stopped, 0, exit, time.Now() })
				return
			}
			s.log.Warn("process exited", "process", p.spec.Name, "exit", exit, "ran", time.Since(started).Round(time.Millisecond))
			p.set(func(st *Status) { st.State, st.PID, st.LastExit, st.Since = Backoff, 0, exit, time.Now() })
		}
		if time.Since(started) >= b.StableAfter.Duration {
			delay = b.Min.Duration // it ran long enough to count as healthy
		}
		select {
		case <-time.After(delay):
		case <-ctx.Done():
			p.set(func(st *Status) { st.State, st.Since = Stopped, time.Now() })
			return
		}
		if delay *= 2; delay > b.Max.Duration {
			delay = b.Max.Duration
		}
	}
}

func (s *Supervisor) start(p *proc, logf *RotatingFile) (*exec.Cmd, error) {
	cmd := exec.Command(p.spec.Exe, p.spec.Args...)
	cmd.Dir = p.spec.Dir
	env, err := s.env(p.spec)
	if err != nil {
		return nil, err
	}
	cmd.Env = env
	cmd.Stdout, cmd.Stderr = logf, logf
	cmd.SysProcAttr = sysProcAttr()
	fmt.Fprintf(logf, "\n=== %s start: %s %s\n", time.Now().UTC().Format(time.RFC3339), p.spec.Exe, strings.Join(p.spec.Args, " "))
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	if err := s.group.add(cmd); err != nil { // without it an orphan could keep the camera, GPU or port
		cmd.Process.Kill()
		cmd.Wait()
		return nil, fmt.Errorf("adding %s to the process group: %w", p.spec.Name, err)
	}
	now := time.Now()
	p.mu.Lock()
	p.cmd, p.exited = cmd, make(chan struct{})
	p.starts = append(p.starts, now)
	cut := now.Add(-s.cfg.Backoff.LoopWindow.Duration)
	for len(p.starts) > 0 && p.starts[0].Before(cut) {
		p.starts = p.starts[1:]
	}
	p.st.State, p.st.PID, p.st.Since = Starting, cmd.Process.Pid, now
	p.st.Starts++
	p.st.CrashLooping = len(p.starts) >= s.cfg.Backoff.LoopStarts
	p.mu.Unlock()
	return cmd, nil
}

// env gives the child the parent's environment, with PATH replaced by the configured directories
// followed by the OS's own, and the spec's variables on top. Nothing depends on the user's PATH.
func (s *Supervisor) env(sp Spec) ([]string, error) {
	path := strings.Join(append(append([]string{}, s.cfg.Path...), osPath()...), string(os.PathListSeparator))
	vars := map[string]string{}
	if sp.EnvFile != "" { // read at every start, so an edited .env applies on the next restart
		f, err := ReadEnvFile(sp.EnvFile)
		if err != nil {
			return nil, fmt.Errorf("env_file: %w", err)
		}
		vars = f
	}
	for k, v := range sp.Env {
		vars[k] = v
	}
	out := []string{}
	for _, kv := range os.Environ() {
		k, _, _ := strings.Cut(kv, "=")
		if strings.EqualFold(k, "PATH") {
			continue
		}
		if _, overridden := vars[k]; overridden {
			continue
		}
		out = append(out, kv)
	}
	out = append(out, "PATH="+path)
	for k, v := range vars {
		out = append(out, k+"="+v)
	}
	return out, nil
}

func (s *Supervisor) awaitReady(ctx context.Context, p *proc, cmd *exec.Cmd) {
	mark := func() {
		p.set(func(st *Status) { st.State, st.Since = Ready, time.Now() })
		p.once.Do(func() { close(p.ready) })
		s.log.Info("ready", "process", p.spec.Name, "pid", cmd.Process.Pid)
	}
	if p.spec.Ready == "" {
		mark()
		return
	}
	deadline := time.Now().Add(p.spec.ReadyTimeout.Duration)
	for {
		if probeReady(ctx, p.spec.Ready) {
			mark()
			return
		}
		if time.Now().After(deadline) {
			s.log.Error("not ready in time; killing", "process", p.spec.Name, "timeout", p.spec.ReadyTimeout.Duration)
			cmd.Process.Kill()
			return
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(250 * time.Millisecond):
		}
	}
}

func probeReady(ctx context.Context, target string) bool {
	if addr, ok := strings.CutPrefix(target, "tcp://"); ok {
		c, err := (&net.Dialer{Timeout: time.Second}).DialContext(ctx, "tcp", addr)
		if err != nil {
			return false
		}
		c.Close()
		return true
	}
	rctx, cancel := context.WithTimeout(ctx, 2*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(rctx, http.MethodGet, target, nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return false
	}
	resp.Body.Close()
	return resp.StatusCode == http.StatusOK
}

// stop asks politely when the process offers a way, then kills it after its grace period.
func (s *Supervisor) stop(p *proc) {
	p.mu.Lock()
	cmd, exited := p.cmd, p.exited
	p.mu.Unlock()
	if cmd == nil || exited == nil {
		return
	}
	select {
	case <-exited:
		return
	default:
	}
	if p.spec.StopURL != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		req, _ := http.NewRequestWithContext(ctx, http.MethodPost, p.spec.StopURL, nil)
		if resp, err := http.DefaultClient.Do(req); err == nil {
			resp.Body.Close()
		}
		cancel()
		select {
		case <-exited:
			return
		case <-time.After(p.spec.StopGrace.Duration):
		}
	}
	killTree(cmd)
	<-exited
}

// Statuses returns every process's status in start order.
func (s *Supervisor) Statuses() []Status {
	s.mu.Lock()
	procs := s.procs
	s.mu.Unlock()
	out := make([]Status, 0, len(s.order))
	for _, sp := range s.order {
		out = append(out, procs[sp.Name].status())
	}
	return out
}

// Handler serves GET /status (JSON), GET /health (200 only when every process is ready) and
// POST /shutdown, which stops everything in order — each process asked first through its stop_url —
// and POST /maintenance?on=1|0.
// It listens on loopback only; the installer uses /shutdown before it switches releases.
func (s *Supervisor) Handler(shutdown func()) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /status", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(s.Statuses())
	})
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		s.mu.Lock()
		m := s.maintenance
		s.mu.Unlock()
		if m {
			http.Error(w, "maintenance", http.StatusServiceUnavailable)
			return
		}
		for _, st := range s.Statuses() {
			if st.State != Ready {
				http.Error(w, st.Name+" is "+string(st.State), http.StatusServiceUnavailable)
				return
			}
		}
		w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("POST /maintenance", func(w http.ResponseWriter, r *http.Request) {
		on := r.URL.Query().Get("on")
		if on != "1" && on != "0" {
			http.Error(w, "use ?on=1 or ?on=0", http.StatusBadRequest)
			return
		}
		s.SetMaintenance(r.Context(), on == "1")
		w.WriteHeader(http.StatusAccepted)
	})
	mux.HandleFunc("POST /shutdown", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusAccepted)
		go shutdown()
	})
	return mux
}
