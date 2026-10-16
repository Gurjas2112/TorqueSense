//go:build !linux && !windows

package supervise

import "syscall"

func newSysProcAttr() *syscall.SysProcAttr { return &syscall.SysProcAttr{Setpgid: true} }
