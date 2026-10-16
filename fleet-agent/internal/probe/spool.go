package probe

import (
	"bufio"
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"sync"
)

// Spool is an append-only JSONL buffer used while the API is unreachable.
type Spool struct {
	mu   sync.Mutex
	path string
	max  int64
}

func NewSpool(path string, maxBytes int64) *Spool { return &Spool{path: path, max: maxBytes} }

var ErrSpoolFull = errors.New("spool full; dropping newest record")

func (s *Spool) Append(line []byte) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	if fi, err := os.Stat(s.path); err == nil && fi.Size()+int64(len(line))+1 > s.max {
		return ErrSpoolFull
	}
	f, err := os.OpenFile(s.path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(append(line, '\n'))
	return err
}

// Flush sends records oldest-first and stops at the first failure, so order is never broken.
// It returns how many were sent and how many remain spooled.
func (s *Spool) Flush(send func([]byte) error) (sent, pending int, err error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	data, err := os.ReadFile(s.path)
	if errors.Is(err, os.ErrNotExist) || len(data) == 0 {
		return 0, 0, nil
	}
	if err != nil {
		return 0, 0, err
	}
	var rest bytes.Buffer
	sc := bufio.NewScanner(bytes.NewReader(data))
	sc.Buffer(make([]byte, 64<<10), 1<<20)
	failed := false
	for sc.Scan() {
		line := append([]byte(nil), sc.Bytes()...)
		if failed || send(line) != nil {
			failed = true
			rest.Write(line)
			rest.WriteByte('\n')
			pending++
			continue
		}
		sent++
	}
	if sent == 0 {
		return 0, pending, nil // nothing delivered: leave the file untouched
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, rest.Bytes(), 0o644); err != nil {
		return sent, pending, err
	}
	return sent, pending, os.Rename(tmp, s.path) // atomic replace on Windows and Linux
}
