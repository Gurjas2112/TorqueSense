#pragma once
// The verify thread's work, without the thread (spec Section 14): commit perception's observations,
// feed the interpreter, run its deadlines and the release gate, and say what changed. The Pipeline
// wraps it in a pinned thread and rings; the scenario and allocation tests drive it directly.
// No allocation after construction (I1), no lock (I2), no I/O.
#include <array>
#include <cstdint>
#include <span>

#include "Messages.hpp"
#include "sunverify/perception/Committer.hpp"
#include "sunverify/procedure/ProcedureEngine.hpp"

namespace sunverify::app {

class VerifyCore {
 public:
  VerifyCore(const procedure::Profile& p, perception::CommitConfig cfg);

  // A frame. New cycle: the procedure restarts at this frame's time.
  void on_frame(const perception::StateVector& sv) noexcept;
  // A plant signal (evidence tag), already mapped to the profile's tag index.
  void on_signal(std::int64_t t_ns, int tag, const procedure::TagValue& v) noexcept;
  // Florence-2's answer for an ambiguous point.
  void on_adjudication(const AdjResult& r) noexcept;
  // The release gate. Returns whether the part may leave.
  bool on_release(std::int64_t t_ns) noexcept;

  // What this call produced. Valid until the next call.
  std::size_t new_violations_from() const noexcept { return reported_; }
  std::size_t violations_total() const noexcept { return engine_.violations().size(); }
  const procedure::Violation& violation(std::size_t i) const noexcept { return engine_.violations()[i]; }
  // timing of the transition behind violation i (commit and first-seen), or the frame's for deadlines
  std::int64_t violation_first_ns(std::size_t i) const noexcept { return first_ns_[i]; }
  std::int64_t violation_commit_ns(std::size_t i) const noexcept { return commit_ns_[i]; }
  void mark_reported() noexcept { reported_ = engine_.violations().size(); }

  std::span<const perception::AmbiguousRequest> asks() const noexcept { return {asks_.data(), n_asks_}; }
  std::span<const perception::CommitEvent> commits() const noexcept { return {events_.data(), n_events_}; }

  const procedure::ProcedureEngine& engine() const noexcept { return engine_; }
  const perception::Committer& committer() const noexcept { return committer_; }
  std::int64_t cycle_ms(std::int64_t t_ns) const noexcept { return (t_ns - cycle_t0_ns_) / 1'000'000; }
  std::uint64_t cycle_no() const noexcept { return cycle_no_; }
  bool released() const noexcept { return released_; }
  bool release_ok() const noexcept { return release_ok_; }

 private:
  void stamp_new(std::int64_t first_ns, std::int64_t commit_ns) noexcept;

  const procedure::Profile& profile_;
  procedure::ProcedureEngine engine_;
  perception::Committer committer_;
  std::array<perception::CommitEvent, 64> events_{};
  std::size_t n_events_ = 0;
  std::array<perception::AmbiguousRequest, 16> asks_{};
  std::size_t n_asks_ = 0;
  std::array<std::int64_t, procedure::kMaxViolations> first_ns_{}, commit_ns_{};
  std::size_t stamped_ = 0, reported_ = 0;
  std::int64_t cycle_t0_ns_ = 0;
  std::uint64_t cycle_no_ = 0;
  bool started_ = false, released_ = false, release_ok_ = false;
};

}  // namespace sunverify::app
