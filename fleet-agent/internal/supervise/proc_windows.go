//go:build windows

package supervise

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"unsafe"
)

// On Windows every child goes into one job object created with KILL_ON_JOB_CLOSE. The job's only
// handle is the supervisor's, so when the supervisor exits — cleanly, crashed or killed — Windows
// closes the handle and terminates every process in the job, grandchildren included. A restarted
// supervisor never finds an orphan still holding the camera, the GPU or a port.
var (
	kernel32               = syscall.NewLazyDLL("kernel32.dll")
	procCreateJobObjectW   = kernel32.NewProc("CreateJobObjectW")
	procSetInformationJob  = kernel32.NewProc("SetInformationJobObject")
	procAssignProcessToJob = kernel32.NewProc("AssignProcessToJobObject")
	procTerminateJobObject = kernel32.NewProc("TerminateJobObject")
)

const (
	jobObjectExtendedLimitInformation = 9
	jobObjectLimitKillOnJobClose      = 0x2000
	processSetQuota                   = 0x0100
	processTerminate                  = 0x0001
	createNoWindow                    = 0x08000000
)

type ioCounters struct{ R, W, O, RB, WB, OB uint64 }

type basicLimit struct {
	PerProcessUserTimeLimit int64
	PerJobUserTimeLimit     int64
	LimitFlags              uint32
	MinimumWorkingSetSize   uintptr
	MaximumWorkingSetSize   uintptr
	ActiveProcessLimit      uint32
	Affinity                uintptr
	PriorityClass           uint32
	SchedulingClass         uint32
}

type extendedLimit struct {
	Basic                 basicLimit
	IO                    ioCounters
	ProcessMemoryLimit    uintptr
	JobMemoryLimit        uintptr
	PeakProcessMemoryUsed uintptr
	PeakJobMemoryUsed     uintptr
}

type group struct{ job syscall.Handle }

func newGroup() (*group, error) {
	h, _, err := procCreateJobObjectW.Call(0, 0)
	if h == 0 {
		return nil, fmt.Errorf("CreateJobObjectW: %w", err)
	}
	var info extendedLimit
	info.Basic.LimitFlags = jobObjectLimitKillOnJobClose
	r, _, err := procSetInformationJob.Call(h, jobObjectExtendedLimitInformation,
		uintptr(unsafe.Pointer(&info)), unsafe.Sizeof(info))
	if r == 0 {
		syscall.CloseHandle(syscall.Handle(h))
		return nil, fmt.Errorf("SetInformationJobObject: %w", err)
	}
	return &group{job: syscall.Handle(h)}, nil
}

func (g *group) add(cmd *exec.Cmd) error {
	ph, err := syscall.OpenProcess(processSetQuota|processTerminate, false, uint32(cmd.Process.Pid))
	if err != nil {
		return fmt.Errorf("OpenProcess: %w", err)
	}
	defer syscall.CloseHandle(ph)
	if r, _, err := procAssignProcessToJob.Call(uintptr(g.job), uintptr(ph)); r == 0 {
		return fmt.Errorf("AssignProcessToJobObject: %w", err)
	}
	return nil
}

func (g *group) close() error {
	procTerminateJobObject.Call(uintptr(g.job), 1)
	return syscall.CloseHandle(g.job)
}

func osPath() []string {
	root := os.Getenv("SystemRoot")
	if root == "" {
		root = `C:\Windows`
	}
	return []string{filepath.Join(root, "System32"), root, filepath.Join(root, "System32", "Wbem")}
}

// No console window per child: the demo screen shows the HMI, not eight terminals.
func sysProcAttr() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{HideWindow: true, CreationFlags: createNoWindow}
}

func killTree(cmd *exec.Cmd) { cmd.Process.Kill() }
