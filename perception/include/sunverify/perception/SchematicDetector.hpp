#pragma once
// Reads inspection-point states from the pixels of a schematic frame (synth/schematic.py): at each
// point's projected position, the colour encodes its rank in its ordered family — a ring (rank 0), a
// grey disc (a middle rank), a yellow disc (the last rank) — and a glove over the point means no answer.
//
// It stands where the family classifiers stand (spec Section 9) so the sample media in samples/*/media
// can be verified frame by frame through the real debounce, committer and interpreter, before any
// model is trained. It is valid only on schematic frames; it is never pointed at a camera, and no
// accuracy figure is ever taken from it. Fixed arrays, no allocation (I1).
#include <array>
#include <cstdint>

#include "sunverify/perception/StateVector.hpp"

namespace sunverify::perception {

class SchematicDetector {
 public:
  struct Point {
    std::int16_t x = 0, y = 0;   // pixel in the 960 x 540 schematic frame
    std::uint8_t n_states = 2;
  };

  void set_points(const Point* pts, std::uint16_t n, int radius) noexcept;

  // Classifies one BGR frame (row stride in bytes) into sv.ips[0..n).
  void classify(const std::uint8_t* bgr, int width, int height, int stride, StateVector& sv) const noexcept;

  enum class Read : std::uint8_t { kRing, kMiddle, kLast, kGlove, kUnknown };
  // The colour class of a mean BGR patch — exposed for tests.
  static Read read(double b, double g, double r) noexcept;

 private:
  std::array<Point, kMaxIps> pts_{};
  std::uint16_t n_ = 0;
  int half_ = 2;   // half-size of the sampled patch
};

}  // namespace sunverify::perception
