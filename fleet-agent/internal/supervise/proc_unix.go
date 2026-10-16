//go:build !windows

package supervise

import (
	"os/exec"
	"syscall"
)

// On Linux each child leads its own process group and is killed if the supervisor dies.
type group struct{}

func newGroup() (*group, error)         { return &group{}, nil }
func (g *group) add(*exec.Cmd) error    { return nil }
func (g *group) close() error           { return nil }
func osPath() []string                  { return []string{"/usr/local/bin", "/usr/bin", "/bin"} }
func sysProcAttr() *syscall.SysProcAttr { return newSysProcAttr() }

func killTree(cmd *exec.Cmd) {
	syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL) // the whole group: uvicorn workers included
}
