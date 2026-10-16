package supervise

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// TestHelperProcess is not a test: the supervisor's children are this test binary in a mode.
func TestHelperProcess(t *testing.T) {
	switch os.Getenv("TS_HELPER") {
	case "":
		return
	case "crash":
		fmt.Println("crashing")
		os.Exit(3)
	case "listen": // ready once it listens; exits cleanly on POST /stop
		ln, err := net.Listen("tcp", os.Getenv("TS_ADDR"))
		if err != nil {
			os.Exit(4)
		}
		mux := http.NewServeMux()
		mux.HandleFunc("/health", func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte("ok")) })
		mux.HandleFunc("/stop", func(w http.ResponseWriter, _ *http.Request) {
			os.WriteFile(os.Getenv("TS_MARK"), []byte("clean"), 0o644)
			go func() { time.Sleep(50 * time.Millisecond); os.Exit(0) }()
		})
		http.Serve(ln, mux)
	case "record": // writes when it started, the PATH it was given and API_KEY, then sleeps
		os.WriteFile(os.Getenv("TS_MARK"), []byte(time.Now().Format(time.RFC3339Nano)+"\n"+os.Getenv("PATH")+"\n"+os.Getenv("API_KEY")), 0o644)
		time.Sleep(time.Hour)
	case "sleep":
		time.Sleep(time.Hour)
	}
	os.Exit(0)
}

func freeAddr(t *testing.T) string {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().String()
}

func helper(name, mode string, env map[string]string) Spec {
	e := map[string]string{"TS_HELPER": mode}
	for k, v := range env {
		e[k] = v
	}
	exe, _ := os.Executable()
	return Spec{Name: name, Exe: exe, Args: []string{"-test.run=TestHelperProcess"}, Env: e}
}

func cfgFor(t *testing.T, procs ...Spec) *Config {
	t.Helper()
	b, _ := json.Marshal(map[string]any{"log_dir": t.TempDir(), "processes": procs,
		"backoff": map[string]string{"min": "20ms", "max": "80ms", "stable_after": "10s", "loop_window": "10s"}})
	c, err := Parse(b)
	if err != nil {
		t.Fatal(err)
	}
	return c
}

func run(t *testing.T, c *Config) (*Supervisor, context.CancelFunc, chan error) {
	s, err := New(c, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- s.Run(ctx) }()
	return s, cancel, done
}

func waitFor(t *testing.T, what string, f func() bool) {
	t.Helper()
	for end := time.Now().Add(10 * time.Second); time.Now().Before(end); time.Sleep(20 * time.Millisecond) {
		if f() {
			return
		}
	}
	t.Fatalf("timed out waiting for %s", what)
}

func TestCrashRestartsWithBackoffAndFlagsLoop(t *testing.T) {
	s, cancel, done := run(t, cfgFor(t, helper("plc", "crash", nil)))
	waitFor(t, "five starts", func() bool { return s.Statuses()[0].Starts >= 5 })
	st := s.Statuses()[0]
	if !st.CrashLooping {
		t.Fatalf("5 starts inside the window should flag a crash loop: %+v", st)
	}
	if !strings.Contains(st.LastExit, "exit status 3") {
		t.Fatalf("last exit not recorded: %q", st.LastExit)
	}
	cancel()
	<-done
	b, _ := os.ReadFile(filepath.Join(s.cfg.LogDir, "plc.log"))
	if strings.Count(string(b), "crashing") < 4 {
		t.Fatalf("child output not captured in its log:\n%s", b)
	}
}

func TestDependentStartsOnlyAfterReady(t *testing.T) {
	addr, mark := freeAddr(t), filepath.Join(t.TempDir(), "engine.started")
	plc := helper("plc", "listen", map[string]string{"TS_ADDR": addr, "TS_MARK": filepath.Join(t.TempDir(), "x")})
	plc.Ready = "tcp://" + addr
	eng := helper("engine", "record", map[string]string{"TS_MARK": mark})
	eng.After = []string{"plc"}
	s, cancel, done := run(t, cfgFor(t, eng, plc)) // defined in the wrong order on purpose
	waitFor(t, "engine started", func() bool { _, err := os.Stat(mark); return err == nil })
	sts := s.Statuses()
	if sts[0].Name != "plc" || sts[0].State != Ready {
		t.Fatalf("plc should be first and ready: %+v", sts)
	}
	b, _ := os.ReadFile(mark)
	started, _ := time.Parse(time.RFC3339Nano, strings.SplitN(string(b), "\n", 2)[0])
	if started.Before(sts[0].Since) {
		t.Fatalf("engine started at %v, before plc was ready at %v", started, sts[0].Since)
	}
	cancel()
	<-done
}

func TestChildPATHIsTheConfiguredOne(t *testing.T) {
	mark := filepath.Join(t.TempDir(), "p")
	c := cfgFor(t, helper("engine", "record", map[string]string{"TS_MARK": mark}))
	c.Path = []string{"/opt/torquesense/runtime"}
	t.Setenv("PATH", "/home/user/whatever:"+os.Getenv("PATH"))
	_, cancel, done := run(t, c)
	waitFor(t, "engine started", func() bool { _, err := os.Stat(mark); return err == nil })
	b, _ := os.ReadFile(mark)
	path := strings.Split(string(b), "\n")[1]
	if !strings.HasPrefix(path, "/opt/torquesense/runtime") || strings.Contains(path, "/home/user/whatever") {
		t.Fatalf("child PATH = %q", path)
	}
	cancel()
	<-done
}

func TestStopAsksFirstThenKills(t *testing.T) {
	addr, clean := freeAddr(t), filepath.Join(t.TempDir(), "clean")
	polite := helper("api", "listen", map[string]string{"TS_ADDR": addr, "TS_MARK": clean})
	polite.Ready, polite.StopURL = "http://"+addr+"/health", "http://"+addr+"/stop"
	stubborn := helper("sidecar", "sleep", nil)
	stubborn.StopGrace.Duration = 100 * time.Millisecond
	s, cancel, done := run(t, cfgFor(t, polite, stubborn))
	waitFor(t, "both running", func() bool {
		st := s.Statuses()
		return st[0].State == Ready && st[1].State == Ready
	})
	pid := s.Statuses()[1].PID
	cancel()
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatal("supervisor did not stop")
	}
	if b, _ := os.ReadFile(clean); string(b) != "clean" {
		t.Fatal("the API was not asked to stop cleanly")
	}
	if alive(pid) {
		t.Fatalf("sidecar pid %d still alive", pid)
	}
	for _, st := range s.Statuses() {
		if st.State != Stopped {
			t.Fatalf("%s is %s after stop", st.Name, st.State)
		}
	}
}

func TestReadyTimeoutKillsAndRestarts(t *testing.T) {
	p := helper("engine", "sleep", nil)
	p.Ready, p.ReadyTimeout.Duration = "tcp://"+freeAddr(t), 200*time.Millisecond
	s, cancel, done := run(t, cfgFor(t, p))
	waitFor(t, "a restart", func() bool { return s.Statuses()[0].Starts >= 2 })
	cancel()
	<-done
}

func TestHealthEndpoint(t *testing.T) {
	addr := freeAddr(t)
	p := helper("plc", "listen", map[string]string{"TS_ADDR": addr, "TS_MARK": filepath.Join(t.TempDir(), "m")})
	p.Ready = "tcp://" + addr
	s, cancel, done := run(t, cfgFor(t, p))
	srv := httptest.NewServer(s.Handler(cancel))
	defer srv.Close()
	waitFor(t, "healthy", func() bool {
		r, err := http.Get(srv.URL + "/health")
		if err != nil {
			return false
		}
		r.Body.Close()
		return r.StatusCode == 200
	})
	r, err := http.Post(srv.URL+"/shutdown", "", nil)
	if err != nil || r.StatusCode != http.StatusAccepted {
		t.Fatalf("shutdown: %v %v", r, err)
	}
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatal("POST /shutdown did not stop the supervisor")
	}
}

func TestConfigRejects(t *testing.T) {
	cases := map[string]string{
		"cycle":   `{"log_dir":"l","processes":[{"name":"a","exe":"/x","after":["b"]},{"name":"b","exe":"/x","after":["a"]}]}`,
		"unknown": `{"log_dir":"l","processes":[{"name":"a","exe":"/x","after":["plc"]}]}`,
		"typo":    `{"log_dir":"l","procesess":[]}`,
		"bare":    `{"log_dir":"l","processes":[{"name":"a","exe":"python"}]}`,
		"unset":   `{"log_dir":"l","processes":[{"name":"a","exe":"{release}/bin/a"}]}`,
		"ready":   `{"log_dir":"l","processes":[{"name":"a","exe":"/x","ready":"udp://1"}]}`,
		"dup":     `{"log_dir":"l","processes":[{"name":"a","exe":"/x"},{"name":"a","exe":"/y"}]}`,
		"empty":   `{"vars":{"cuda_bin":""},"log_dir":"l","path":["{cuda_bin}"],"processes":[{"name":"a","exe":"/x"}]}`,
	}
	for name, js := range cases {
		if _, err := Parse([]byte(js)); err == nil {
			t.Errorf("%s: accepted", name)
		}
	}
	c, err := Parse([]byte(`{"vars":{"release":"/r"},"log_dir":"{release}/logs","processes":[{"name":"a","exe":"{release}/bin/a","ready_timeout":"90s"}]}`))
	if err != nil || c.Processes[0].Exe != "/r/bin/a" || c.LogDir != "/r/logs" || c.Processes[0].ReadyTimeout.Duration != 90*time.Second {
		t.Fatalf("substitution: %+v %v", c, err)
	}
}

func TestRotatingFile(t *testing.T) {
	p := filepath.Join(t.TempDir(), "engine.log")
	r, err := OpenRotating(p, 100, 2)
	if err != nil {
		t.Fatal(err)
	}
	line := strings.Repeat("x", 39) + "\n"
	for i := 0; i < 20; i++ {
		r.Write([]byte(line))
	}
	r.Close()
	for _, f := range []string{"engine.log", "engine.1.log", "engine.2.log"} {
		st, err := os.Stat(filepath.Join(filepath.Dir(p), f))
		if err != nil || st.Size() > 100 {
			t.Fatalf("%s: %v %v", f, st, err)
		}
	}
	if _, err := os.Stat(filepath.Join(filepath.Dir(p), "engine.3.log")); err == nil {
		t.Fatal("kept more generations than asked")
	}
}

func TestEnvFile(t *testing.T) {
	dir := t.TempDir()
	envf, mark := filepath.Join(dir, ".env"), filepath.Join(dir, "m")
	os.WriteFile(envf, []byte("# secrets\r\nAPI_KEY=\"from-file\"\r\n\r\nOTHER=1\r\n"), 0o600)
	p := helper("engine", "record", map[string]string{"TS_MARK": mark})
	p.EnvFile = envf
	_, cancel, done := run(t, cfgFor(t, p))
	waitFor(t, "engine started", func() bool { b, _ := os.ReadFile(mark); return len(b) > 0 })
	b, _ := os.ReadFile(mark)
	if got := strings.Split(string(b), "\n")[2]; got != "from-file" {
		t.Fatalf("API_KEY = %q", got)
	}
	cancel()
	<-done
	if _, err := ReadEnvFile(writeTemp(t, "NOEQUALS\n")); err == nil {
		t.Fatal("a line without = was accepted")
	}
}

func writeTemp(t *testing.T, s string) string {
	p := filepath.Join(t.TempDir(), "f")
	os.WriteFile(p, []byte(s), 0o600)
	return p
}

func TestMaintenanceStopsAndResumes(t *testing.T) {
	addr := freeAddr(t)
	p := helper("plc", "listen", map[string]string{"TS_ADDR": addr, "TS_MARK": filepath.Join(t.TempDir(), "m")})
	p.Ready = "tcp://" + addr
	s, cancel, done := run(t, cfgFor(t, p))
	srv := httptest.NewServer(s.Handler(cancel))
	defer srv.Close()
	waitFor(t, "ready", func() bool { return s.Statuses()[0].State == Ready })
	first := s.Statuses()[0].PID
	if r, _ := http.Post(srv.URL+"/maintenance?on=1", "", nil); r.StatusCode != http.StatusAccepted {
		t.Fatalf("maintenance on: %d", r.StatusCode)
	}
	waitFor(t, "stopped", func() bool { return s.Statuses()[0].State == Stopped && !alive(first) })
	if r, _ := http.Get(srv.URL + "/health"); r.StatusCode != http.StatusServiceUnavailable {
		t.Fatal("health should be 503 in maintenance")
	}
	if c, err := net.DialTimeout("tcp", addr, 200*time.Millisecond); err == nil {
		c.Close()
		t.Fatal("the port is still served during maintenance")
	}
	http.Post(srv.URL+"/maintenance?on=0", "", nil)
	waitFor(t, "ready again", func() bool { st := s.Statuses()[0]; return st.State == Ready && st.PID != first })
	cancel()
	<-done
}
