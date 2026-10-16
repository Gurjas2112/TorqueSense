#pragma once
// Pre-allocated frame pools (spec Section 9, "Memory: a full-resolution pool for crops"). Two pools
// are made at start-up and never grow: 12 frames at the detector's 960x540 and 4 at full resolution
// for inspection-point crops. A frame is leased by the capture thread and returned by the inference
// thread when its crops are taken. The free list is an SpscRingBuffer — the returning thread is its
// only producer and the capture thread its only consumer — so leasing and returning are lock-free
// and allocate nothing (I1, I2).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "sunverify/perception/SpscRingBuffer.hpp"

namespace sunverify::perception {

inline constexpr std::size_t kMaxPoolFrames = 32;

class FramePool {
 public:
  // Allocates count frames of width x height x channels bytes, once.
  FramePool(int width, int height, int channels, std::size_t count);

  FramePool(const FramePool&) = delete;
  FramePool& operator=(const FramePool&) = delete;

  // Capture thread. Empty when every frame is in flight: the caller drops the frame and counts it.
  std::optional<std::uint16_t> lease() noexcept;
  // The thread that finished with the frame (inference). Never blocks.
  void release(std::uint16_t index) noexcept;

  std::uint8_t* data(std::uint16_t index) noexcept { return storage_.get() + std::size_t{index} * frame_bytes_; }
  const std::uint8_t* data(std::uint16_t index) const noexcept {
    return storage_.get() + std::size_t{index} * frame_bytes_;
  }

  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }
  int channels() const noexcept { return channels_; }
  std::size_t frame_bytes() const noexcept { return frame_bytes_; }
  std::size_t count() const noexcept { return count_; }
  std::size_t bytes() const noexcept { return frame_bytes_ * count_; }
  std::size_t available_approx() const noexcept { return free_.size_approx(); }

 private:
  int width_, height_, channels_;
  std::size_t frame_bytes_, count_;
  std::unique_ptr<std::uint8_t[]> storage_;
  SpscRingBuffer<std::uint16_t, kMaxPoolFrames> free_;
};

}  // namespace sunverify::perception
