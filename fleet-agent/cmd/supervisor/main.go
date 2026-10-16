// supervisor: starts a TorqueSense station's processes in dependency order, restarts them with
// backoff, and stops all of them — children included — when it stops or dies.
//
//	supervisor -config C:\TorqueSense\config\supervisor.json
//	supervisor -config … -check        validate the file and print the start order, then exit
//
// Task Scheduler starts it at sign-in and restarts it if it exits (spec Section 33).
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Gurjas2112/torquesense/fleet-agent/internal/supervise"
)

func main() {
	cfgPath := flag.String("config", "config/supervisor.json", "supervisor configuration")
	check := flag.Bool("check", false, "validate the configuration and print the start order")
	flag.Parse()
	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))

	cfg, err := supervise.Load(*cfgPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	if *check {
		order, _ := cfg.Order()
		for i, p := range order {
			fmt.Printf("%d. %-10s %s  ready=%s\n", i+1, p.Name, p.Exe, p.Ready)
		}
		return
	}
	s, err := supervise.New(cfg, log)
	if err != nil {
		log.Error("cannot start", "err", err)
		os.Exit(1)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	srv := &http.Server{Addr: cfg.Listen, Handler: s.Handler(stop), ReadHeaderTimeout: 5 * time.Second}
	go func() {
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Error("status endpoint", "err", err) // a port clash must be visible, not fatal
		}
	}()
	log.Info("supervising", "processes", len(cfg.Processes), "status", "http://"+cfg.Listen+"/status")
	err = s.Run(ctx)
	shut, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	srv.Shutdown(shut)
	cancel()
	if err != nil {
		log.Error("shutdown", "err", err)
		os.Exit(1)
	}
}
