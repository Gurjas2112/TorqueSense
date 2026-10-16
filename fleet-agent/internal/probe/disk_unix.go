//go:build !windows

package probe

import "syscall"

// DiskFree returns bytes available to unprivileged users on the filesystem holding path.
func DiskFree(path string) (uint64, error) {
	var s syscall.Statfs_t
	if err := syscall.Statfs(path, &s); err != nil {
		return 0, err
	}
	return s.Bavail * uint64(s.Bsize), nil
}
