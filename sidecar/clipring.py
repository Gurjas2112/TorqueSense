"""The engine's SharedClipRing, read from Python (spec Section 30). One layout, two languages:

    0     4    magic 'TSCR'           64   8          write_seq (producer-owned cache line)
    4     4    version = 1            128  slots x 32  slot table: seq u64 · ts_ns u64 · frame_seq u64 · len u32 · pad u32
    8     4    slots                  8192 slots x slot_bytes payloads
    12    4    slot_bytes

Each slot is a seqlock: odd while the engine writes it, 2n+2 when frame n is complete. A copy whose
sequence changed under it is discarded. Correct on x86-64, where loads are not reordered with other
loads; on an ARM edge device this reader moves to C++ with acquire loads (docs/architecture note in
spec Section 30).
"""
from __future__ import annotations
import mmap, os, struct, sys

HEADER, TABLE, PAYLOAD = 64, 128, 8192
MAGIC = b"TSCR"


class ClipRing:
    def __init__(self, station: str):
        if sys.platform == "win32":
            m = mmap.mmap(-1, PAYLOAD, tagname=f"Local\\torquesense_{station}")
            if m[0:4] != MAGIC:
                m.close()
                raise FileNotFoundError(f"no clip ring for {station}: engine not running, or layout mismatch")
            self.slots, self.slot_bytes = struct.unpack_from("<II", m, 8)
            m.close()
            self.m = mmap.mmap(-1, PAYLOAD + self.slots * self.slot_bytes, tagname=f"Local\\torquesense_{station}")
        else:
            fd = os.open(f"/dev/shm/torquesense_{station}", os.O_RDONLY)
            self.m = mmap.mmap(fd, 0, prot=mmap.PROT_READ)
            os.close(fd)
            self.slots, self.slot_bytes = struct.unpack_from("<II", self.m, 8)
        if self.m[0:4] != MAGIC:
            raise FileNotFoundError("engine not running, or layout mismatch")

    def write_seq(self) -> int:
        return struct.unpack_from("<Q", self.m, HEADER)[0]

    def read(self, n: int) -> tuple[bytes, int, int] | None:
        """Frame n as (jpeg, ts_ns, frame_seq), or None if it is not (or no longer) in the ring."""
        base = TABLE + (n % self.slots) * 32
        for _ in range(3):                                         # seqlock retry
            s1, ts, fseq, ln = struct.unpack_from("<QQQI", self.m, base)
            if s1 & 1:                                             # writer mid-copy
                continue
            if s1 != 2 * n + 2:                                    # not yet written, or overwritten
                return None
            off = PAYLOAD + (n % self.slots) * self.slot_bytes
            data = bytes(self.m[off: off + ln])
            if struct.unpack_from("<Q", self.m, base)[0] == s1:
                return data, ts, fseq
        return None

    def close(self) -> None:
        self.m.close()
