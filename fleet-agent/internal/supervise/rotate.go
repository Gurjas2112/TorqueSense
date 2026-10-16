package supervise

import (
	"fmt"
	"os"
	"sync"
)

// RotatingFile is an io.Writer that keeps name.log under max bytes, shifting older content to
// name.1.log … name.<keep>.log. A process that logs in a loop cannot fill the disk.
type RotatingFile struct {
	mu   sync.Mutex
	path string
	max  int64
	keep int
	f    *os.File
	size int64
}

func OpenRotating(path string, max int64, keep int) (*RotatingFile, error) {
	r := &RotatingFile{path: path, max: max, keep: keep}
	if err := r.open(); err != nil {
		return nil, err
	}
	return r, nil
}

func (r *RotatingFile) open() error {
	f, err := os.OpenFile(r.path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return err
	}
	r.f, r.size = f, st.Size()
	return nil
}

func (r *RotatingFile) Write(p []byte) (int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.size > 0 && r.size+int64(len(p)) > r.max {
		if err := r.rotate(); err != nil {
			return 0, err
		}
	}
	n, err := r.f.Write(p)
	r.size += int64(n)
	return n, err
}

func (r *RotatingFile) rotate() error {
	r.f.Close()
	os.Remove(r.name(r.keep))
	for i := r.keep - 1; i >= 1; i-- {
		os.Rename(r.name(i), r.name(i+1)) // missing generations are fine
	}
	if err := os.Rename(r.path, r.name(1)); err != nil {
		return err
	}
	return r.open()
}

func (r *RotatingFile) name(i int) string {
	ext := ".log"
	base := r.path
	if len(base) > len(ext) && base[len(base)-len(ext):] == ext {
		base = base[:len(base)-len(ext)]
	}
	return fmt.Sprintf("%s.%d%s", base, i, ext)
}

func (r *RotatingFile) Close() error {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.f.Close()
}
