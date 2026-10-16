#include "sunverify/ipc/SharedClipRing.hpp"

#include <cstring>
#include <new>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sunverify::ipc {

bool SharedClipRing::map(const std::string& name, bool create, std::size_t bytes) {
#ifdef _WIN32
  const std::string full = "Local\\torquesense_" + name;
  std::wstring w(full.begin(), full.end());
  HANDLE h = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         static_cast<DWORD>(static_cast<std::uint64_t>(bytes) >> 32),
                                         static_cast<DWORD>(bytes & 0xffffffffu), w.c_str())
                    : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, w.c_str());
  if (!h) return false;
  void* v = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (!v) {
    CloseHandle(h);
    return false;
  }
  handle_ = h;
  base_ = static_cast<std::byte*>(v);
#else
  shm_name_ = "/torquesense_" + name;
  const int fd = create ? shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0600) : shm_open(shm_name_.c_str(), O_RDWR, 0);
  if (fd < 0) return false;
  if (create && ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
    close(fd);
    return false;
  }
  if (!create) {
    // a reader learns the size from the header
    void* head = mmap(nullptr, kTableOffset, PROT_READ, MAP_SHARED, fd, 0);
    if (head == MAP_FAILED) {
      close(fd);
      return false;
    }
    std::uint32_t slots = 0, slot_bytes = 0;
    std::memcpy(&slots, static_cast<char*>(head) + 8, 4);
    std::memcpy(&slot_bytes, static_cast<char*>(head) + 12, 4);
    munmap(head, kTableOffset);
    bytes = total_bytes(slots, slot_bytes);
  }
  void* v = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (v == MAP_FAILED) {
    close(fd);
    return false;
  }
  fd_ = fd;
  base_ = static_cast<std::byte*>(v);
#endif
  bytes_ = bytes;
  owner_ = create;
  return true;
}

std::unique_ptr<SharedClipRing> SharedClipRing::create(const std::string& name, std::uint32_t slots, std::uint32_t slot_bytes) {
  std::unique_ptr<SharedClipRing> r(new SharedClipRing());
  if (!r->map(name, true, total_bytes(slots, slot_bytes))) return nullptr;
  r->slots_ = slots;
  r->slot_bytes_ = slot_bytes;
  std::memset(r->base_, 0, kPayloadOffset);
  new (r->base_ + 64) std::atomic<std::uint64_t>(0);
  for (std::uint32_t i = 0; i < slots; ++i) new (&r->meta(i).seq) std::atomic<std::uint64_t>(0);
  std::memcpy(r->base_ + 4, &kVersion, 4);
  std::memcpy(r->base_ + 8, &slots, 4);
  std::memcpy(r->base_ + 12, &slot_bytes, 4);
  std::atomic_thread_fence(std::memory_order_release);
  std::memcpy(r->base_, &kMagic, 4);   // last: a reader that sees the magic sees a complete header
  return r;
}

std::unique_ptr<SharedClipRing> SharedClipRing::open(const std::string& name) {
  std::unique_ptr<SharedClipRing> r(new SharedClipRing());
#ifdef _WIN32
  // the reader maps the header first to learn the size, then the whole ring
  if (!r->map(name, false, kPayloadOffset)) return nullptr;
  std::uint32_t magic = 0, slots = 0, slot_bytes = 0;
  std::memcpy(&magic, r->base_, 4);
  std::memcpy(&slots, r->base_ + 8, 4);
  std::memcpy(&slot_bytes, r->base_ + 12, 4);
  UnmapViewOfFile(r->base_);
  CloseHandle(static_cast<HANDLE>(r->handle_));
  r->base_ = nullptr;
  if (magic != kMagic || !r->map(name, false, total_bytes(slots, slot_bytes))) return nullptr;
  r->slots_ = slots;
  r->slot_bytes_ = slot_bytes;
#else
  if (!r->map(name, false, 0)) return nullptr;
  std::uint32_t magic = 0;
  std::memcpy(&magic, r->base_, 4);
  if (magic != kMagic) return nullptr;
  std::memcpy(&r->slots_, r->base_ + 8, 4);
  std::memcpy(&r->slot_bytes_, r->base_ + 12, 4);
#endif
  r->owner_ = false;
  return r;
}

SharedClipRing::~SharedClipRing() {
#ifdef _WIN32
  if (base_) UnmapViewOfFile(base_);
  if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#else
  if (base_) munmap(base_, bytes_);
  if (fd_ >= 0) close(fd_);
  if (owner_ && !shm_name_.empty()) shm_unlink(shm_name_.c_str());
#endif
}

bool SharedClipRing::publish(std::span<const std::byte> jpeg, std::uint64_t ts_ns, std::uint64_t frame_seq) noexcept {
  if (jpeg.size() > slot_bytes_) return false;                    // never truncate, never block
  auto& ws = write_seq_ref();
  const std::uint64_t n = ws.load(std::memory_order_relaxed);
  SlotMeta& m = meta(n % slots_);
  m.seq.store(2 * n + 1, std::memory_order_relaxed);              // odd: write in progress
  std::atomic_thread_fence(std::memory_order_release);            // odd seq visible before payload
  std::memcpy(payload(n % slots_), jpeg.data(), jpeg.size());
  m.ts_ns = ts_ns;
  m.frame_seq = frame_seq;
  m.len = static_cast<std::uint32_t>(jpeg.size());
  m.seq.store(2 * n + 2, std::memory_order_release);              // even: payload complete
  ws.store(n + 1, std::memory_order_release);                     // publish
  return true;
}

std::size_t SharedClipRing::read(std::uint64_t n, std::span<std::byte> out, std::uint64_t* ts_ns,
                                 std::uint64_t* frame_seq) const noexcept {
  SlotMeta& m = meta(n % slots_);
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::uint64_t s1 = m.seq.load(std::memory_order_acquire);
    if (s1 & 1) continue;                                         // writer mid-copy: retry
    if (s1 != 2 * n + 2) return 0;                                // not yet written, or overwritten
    const std::uint32_t len = m.len;
    if (len > out.size()) return 0;
    const std::uint64_t ts = m.ts_ns, fs = m.frame_seq;
    std::memcpy(out.data(), payload(n % slots_), len);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (m.seq.load(std::memory_order_relaxed) == s1) {
      if (ts_ns) *ts_ns = ts;
      if (frame_seq) *frame_seq = fs;
      return len;
    }
  }
  return 0;
}

std::uint64_t SharedClipRing::write_seq() const noexcept { return write_seq_ref().load(std::memory_order_acquire); }

}  // namespace sunverify::ipc
