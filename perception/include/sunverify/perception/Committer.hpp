#pragma once
// From per-frame observations to committed state changes (spec Figure 2b). A change is committed only
// after it is seen in `debounce` consecutive frames with confidence at or above the commit floor
// (0.65). Between 0.35 and 0.65 it is ambiguous: the previous state is held and Florence-2 is asked,
// once — so ambiguity can delay a commit but never cause one. Below 0.35, or occluded, is no answer.
// Fixed arrays only: this runs on the verify thread (I1, I2).
#include <array>
#include <cstdint>
#include <span>

#include "sunverify/perception/StateVector.hpp"

namespace sunverify::perception {

struct CommitConfig {
  int debounce_frames = 2;
  float conf_commit = 0.65f;
  float conf_floor = 0.35f;
};

struct CommitEvent {
  std::uint16_t ip = 0;
  std::int8_t rank = 0;
  bool adjudicated = false;
  std::int64_t t_first_ns = 0;     // the first frame showing the new state: evidence latency starts here
  std::int64_t t_commit_ns = 0;    // the frame that committed it: engine latency starts here
  std::uint64_t frame_seq = 0;
};

struct AmbiguousRequest {
  std::uint16_t ip = 0;
  std::int8_t candidate = 0;
  float conf = 0;
  std::uint64_t frame_seq = 0;
  std::uint64_t ring_seq = 0;
  float u = 0, v = 0;
};

class Committer {
 public:
  explicit Committer(CommitConfig cfg = {}) noexcept : cfg_(cfg) {}

  void reset(std::uint16_t n_ips) noexcept;

  // One frame. Writes committed transitions to `events` and new adjudication requests to `asks`;
  // returns how many of each were written (bounded by the spans' sizes).
  void observe(const StateVector& sv, std::span<CommitEvent> events, std::size_t& n_events,
               std::span<AmbiguousRequest> asks, std::size_t& n_asks) noexcept;

  // Florence-2's answer for an ambiguous point. Commits only if that candidate is still pending.
  bool resolve(std::uint16_t ip, std::int8_t candidate, bool confirmed, std::int64_t t_ns, CommitEvent& out) noexcept;

  std::int8_t committed(std::uint16_t ip) const noexcept { return points_[ip].committed; }
  float last_conf(std::uint16_t ip) const noexcept { return points_[ip].conf; }
  bool ambiguous(std::uint16_t ip) const noexcept { return points_[ip].asked; }

  // Health counters since the last read (the io thread's buckets).
  std::uint64_t observations() const noexcept { return observations_; }
  std::uint64_t ambiguous_observations() const noexcept { return ambiguous_; }
  double conf_sum() const noexcept { return conf_sum_; }

 private:
  struct Point {
    std::int8_t committed = 0;
    std::int8_t candidate = -1;
    std::uint8_t count = 0;
    bool asked = false;
    float conf = 0;
    std::int64_t t_first_ns = 0;
  };
  CommitConfig cfg_;
  std::uint16_t n_ = 0;
  std::array<Point, kMaxIps> points_{};
  std::uint64_t observations_ = 0, ambiguous_ = 0;
  double conf_sum_ = 0;
};

}  // namespace sunverify::perception
