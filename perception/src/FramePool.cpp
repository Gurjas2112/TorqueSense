#include "sunverify/perception/FramePool.hpp"

#include <stdexcept>

namespace sunverify::perception {

FramePool::FramePool(int width, int height, int channels, std::size_t count)
    : width_(width),
      height_(height),
      channels_(channels),
      frame_bytes_(static_cast<std::size_t>(width) * height * channels),
      count_(count),
      storage_(new std::uint8_t[static_cast<std::size_t>(width) * height * channels * count]()) {
  if (count == 0 || count > kMaxPoolFrames) throw std::invalid_argument("FramePool: 1 to 32 frames");
  // Filled before any pipeline thread starts; thread creation orders these writes before their reads.
  for (std::size_t i = 0; i < count; ++i) free_.try_push(static_cast<std::uint16_t>(i));
}

std::optional<std::uint16_t> FramePool::lease() noexcept {
  std::uint16_t i;
  if (!free_.try_pop(i)) return std::nullopt;
  return i;
}

void FramePool::release(std::uint16_t index) noexcept {
  // Capacity is 32 and at most count <= 32 indices exist, so this cannot fail.
  free_.try_push(index);
}

}  // namespace sunverify::perception
