#include "sunverify/perception/SchematicDetector.hpp"

#include <algorithm>

namespace sunverify::perception {

// The palette, BGR — keep in step with synth/schematic.py:
//   tile (44, 35, 31) · ring (110, 100, 95) · metal (170, 166, 160) · mark (36, 190, 245) · glove (60, 90, 150)
// Thresholds are wide on purpose: MPEG-4 compression moves every colour a little.
SchematicDetector::Read SchematicDetector::read(double b, double g, double r) noexcept {
  const double mx = std::max({b, g, r}), mn = std::min({b, g, r});
  if (r > 170 && g > 130 && b < 130 && r - b > 80) return Read::kLast;            // yellow
  if (r > 100 && r < 210 && g > 55 && g < 135 && b < 110 && r - g > 30) return Read::kGlove;
  if (mn > 120 && mx - mn < 50) return Read::kMiddle;                               // grey metal
  if (mx < 95) return Read::kRing;                                                  // the empty inside of a ring
  return Read::kUnknown;
}

void SchematicDetector::set_points(const Point* pts, std::uint16_t n, int radius) noexcept {
  n_ = n < kMaxIps ? n : static_cast<std::uint16_t>(kMaxIps);
  for (std::uint16_t i = 0; i < n_; ++i) pts_[i] = pts[i];
  half_ = std::max(1, radius / 3);                                                  // well inside the disc
}

void SchematicDetector::classify(const std::uint8_t* bgr, int width, int height, int stride, StateVector& sv) const noexcept {
  sv.n_ips = n_;
  for (std::uint16_t i = 0; i < n_; ++i) {
    const Point& p = pts_[i];
    double b = 0, g = 0, r = 0;
    int n = 0;
    for (int dy = -half_; dy <= half_; ++dy)
      for (int dx = -half_; dx <= half_; ++dx) {
        const int x = p.x + dx, y = p.y + dy;
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        const std::uint8_t* px = bgr + static_cast<std::ptrdiff_t>(y) * stride + x * 3;
        b += px[0];
        g += px[1];
        r += px[2];
        ++n;
      }
    IpObservation& o = sv.ips[i];
    o = IpObservation{};
    if (!n) continue;
    switch (read(b / n, g / n, r / n)) {
      case Read::kRing: o.rank = 0; o.conf = 0.95f; o.flags = kVisible; break;
      case Read::kMiddle: o.rank = static_cast<std::int8_t>(p.n_states > 2 ? 1 : 0); o.conf = 0.95f; o.flags = kVisible; break;
      case Read::kLast: o.rank = static_cast<std::int8_t>(p.n_states - 1); o.conf = 0.95f; o.flags = kVisible; break;
      case Read::kGlove: o.rank = -1; o.conf = 0.0f; o.flags = kOccluded; break;
      case Read::kUnknown: o.rank = -1; o.conf = 0.0f; break;
    }
  }
}

}  // namespace sunverify::perception
