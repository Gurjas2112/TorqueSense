#pragma once
// The shared clip ring (spec Section 30): the engine writes every frame as a JPEG; the sidecar reads
// them for adjudication crops and clip assembly, so no image ever crosses HTTP. One layout, two
// languages — sidecar/clipring.py parses it with struct offsets, so it is fixed:
//
//   Offset   Size          Field
//   0        4             magic 'TSCR'
//   4        4             version = 1
//   8        4             slots
//   12       4             slot_bytes
//   64       8             write_seq          (own cache line; producer-owned)
//   128      slots x 32    slot table: seq u64 · ts_ns u64 · frame_seq u64 · len u32 · pad u32
//   8192     slots x bytes payloads
//
// Each slot is a seqlock: odd while being written, even (2n+2) when frame n is complete. A reader
// that sees the sequence change, or odd, discards its copy. The producer never blocks.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace sunverify::ipc {

inline constexpr std::uint32_t kMagic = 0x52435354;   // "TSCR" little-endian
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kTableOffset = 128;
inline constexpr std::size_t kPayloadOffset = 8192;

struct SlotMeta {
  std::atomic<std::uint64_t> seq;
  std::uint64_t ts_ns;
  std::uint64_t frame_seq;
  std::uint32_t len;
  std::uint32_t pad;
};
static_assert(sizeof(SlotMeta) == 32);

class SharedClipRing {
 public:
  // Creates (producer) or opens (reader) the named mapping. Win32: Local\torquesense_<station>;
  // POSIX: /torquesense_<station> in shm.
  static std::unique_ptr<SharedClipRing> create(const std::string& name, std::uint32_t slots = 180,
                                                std::uint32_t slot_bytes = 262144);
  static std::unique_ptr<SharedClipRing> open(const std::string& name);
  ~SharedClipRing();

  static std::size_t total_bytes(std::uint32_t slots, std::uint32_t slot_bytes) {
    return kPayloadOffset + std::size_t{slots} * slot_bytes;
  }

  // Producer (capture thread). False if the JPEG does not fit: never truncate, never block.
  bool publish(std::span<const std::byte> jpeg, std::uint64_t ts_ns, std::uint64_t frame_seq) noexcept;

  // Reader. Copies frame n into out; returns its length, or 0 if n is not (or no longer) there.
  std::size_t read(std::uint64_t n, std::span<std::byte> out, std::uint64_t* ts_ns = nullptr,
                   std::uint64_t* frame_seq = nullptr) const noexcept;

  std::uint64_t write_seq() const noexcept;
  std::uint32_t slots() const noexcept { return slots_; }
  std::uint32_t slot_bytes() const noexcept { return slot_bytes_; }

 private:
  SharedClipRing() = default;
  bool map(const std::string& name, bool create, std::size_t bytes);
  std::byte* payload(std::uint64_t slot) const noexcept { return base_ + kPayloadOffset + slot * slot_bytes_; }
  SlotMeta& meta(std::uint64_t slot) const noexcept {
    return *reinterpret_cast<SlotMeta*>(base_ + kTableOffset + slot * sizeof(SlotMeta));
  }
  std::atomic<std::uint64_t>& write_seq_ref() const noexcept {
    return *reinterpret_cast<std::atomic<std::uint64_t>*>(base_ + 64);
  }

  std::byte* base_ = nullptr;
  std::size_t bytes_ = 0;
  std::uint32_t slots_ = 0, slot_bytes_ = 0;
  void* handle_ = nullptr;   // Win32 mapping handle
  int fd_ = -1;              // POSIX shm descriptor
  std::string shm_name_;
  bool owner_ = false;
};

}  // namespace sunverify::ipc
