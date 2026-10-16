#pragma once
// Single-producer, single-consumer ring: the only way pipeline threads talk (spec Section 14).
// Lock-free and wait-free: one atomic load and one atomic store per operation, acquire/release
// ordering, and the producer's and consumer's indices on separate cache lines so the two threads
// never false-share. Each side also keeps a cached copy of the other side's index and refreshes it
// only when the ring looks full (or empty), so the common case touches no shared line at all.
// No allocation after construction (I1), no mutex (I2).
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace sunverify::perception {

inline constexpr std::size_t kCacheLine = 64;   // x86-64 and the ARM cores a station would use

template <typename T, std::size_t N>
class SpscRingBuffer {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");
  static_assert(std::is_nothrow_copy_assignable_v<T> || std::is_nothrow_move_assignable_v<T>,
                "elements are assigned on the hot path and must not throw");

 public:
  static constexpr std::size_t capacity() noexcept { return N; }

  // Producer thread only. Returns false when full: the caller decides whether to drop and count.
  bool try_push(const T& v) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_cache_ == N) {
      tail_cache_ = tail_.load(std::memory_order_acquire);
      if (head - tail_cache_ == N) return false;
    }
    slots_[head & (N - 1)] = v;
    head_.store(head + 1, std::memory_order_release);   // publishes the slot
    return true;
  }

  // Consumer thread only. Returns false when empty.
  bool try_pop(T& out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_cache_) {
      head_cache_ = head_.load(std::memory_order_acquire);
      if (tail == head_cache_) return false;
    }
    out = slots_[tail & (N - 1)];
    tail_.store(tail + 1, std::memory_order_release);   // frees the slot for the producer
    return true;
  }

  // Either thread; a snapshot, exact only when the other side is idle.
  std::size_t size_approx() const noexcept {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }
  bool empty_approx() const noexcept { return size_approx() == 0; }

 private:
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};   // written by the producer
  alignas(kCacheLine) std::size_t tail_cache_{0};          // producer's view of tail_
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};   // written by the consumer
  alignas(kCacheLine) std::size_t head_cache_{0};          // consumer's view of head_
  alignas(kCacheLine) std::array<T, N> slots_{};
};

}  // namespace sunverify::perception
