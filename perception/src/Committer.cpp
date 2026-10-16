#include "sunverify/perception/Committer.hpp"

namespace sunverify::perception {

void Committer::reset(std::uint16_t n_ips) noexcept {
  n_ = n_ips;
  for (auto& p : points_) p = Point{};
}

void Committer::observe(const StateVector& sv, std::span<CommitEvent> events, std::size_t& n_events,
                        std::span<AmbiguousRequest> asks, std::size_t& n_asks) noexcept {
  n_events = n_asks = 0;
  const std::uint16_t n = sv.n_ips < n_ ? sv.n_ips : n_;
  for (std::uint16_t i = 0; i < n; ++i) {
    const IpObservation& o = sv.ips[i];
    Point& p = points_[i];
    if (o.rank < 0 || (o.flags & kOccluded) || o.conf < cfg_.conf_floor) {
      p.candidate = -1;                                  // no answer: hold, and start the debounce again
      p.count = 0;
      p.asked = false;
      continue;
    }
    ++observations_;
    conf_sum_ += o.conf;
    p.conf = o.conf;
    if (o.rank == p.committed) {                         // no change
      p.candidate = -1;
      p.count = 0;
      p.asked = false;
      continue;
    }
    if (o.rank == p.candidate) {
      if (p.count < 255) ++p.count;
    } else {
      p.candidate = o.rank;
      p.count = 1;
      p.asked = false;
      p.t_first_ns = sv.t_capture_ns;
    }
    if (p.count < cfg_.debounce_frames) continue;        // a single frame of flicker never commits
    if (o.conf >= cfg_.conf_commit) {
      if (n_events < events.size())
        events[n_events++] = {i, o.rank, false, p.t_first_ns, sv.t_capture_ns, sv.frame_seq};
      p.committed = o.rank;
      p.candidate = -1;
      p.count = 0;
      p.asked = false;
    } else {
      ++ambiguous_;
      if (!p.asked && n_asks < asks.size()) {            // hold the previous state; ask once
        asks[n_asks++] = {i, o.rank, o.conf, sv.frame_seq, sv.ring_seq, o.u, o.v};
        p.asked = true;
      }
    }
  }
}

bool Committer::resolve(std::uint16_t ip, std::int8_t candidate, bool confirmed, std::int64_t t_ns, CommitEvent& out) noexcept {
  Point& p = points_[ip];
  if (!p.asked || p.candidate != candidate) return false;   // the detector moved on: the answer is stale
  p.asked = false;
  if (!confirmed) {
    p.candidate = -1;
    p.count = 0;
    return false;
  }
  out = {ip, candidate, true, p.t_first_ns, t_ns, 0};
  p.committed = candidate;
  p.candidate = -1;
  p.count = 0;
  return true;
}

}  // namespace sunverify::perception
