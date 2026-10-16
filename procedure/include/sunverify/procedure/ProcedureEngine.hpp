#pragma once
// The generic procedure interpreter (spec Section 6). The C++ copy of reference/tsref/procedure.py,
// which is the specification: the two must produce identical violation lists on every trace (I16).
//
// A Profile is compiled into flat tables once, at load (ProfileLoader). After that the engine
// allocates nothing (I1) and locks nothing (I2): a committed transition touches only the steps
// that target that one inspection point, through a precomputed index. Limits that follow from
// the fixed tables: 256 inspection points, 64 steps, 64 targets per step.
//
// The engine knows no assembly, part or family by name. Everything specific is data.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sunverify/procedure/Profile.hpp"

namespace sunverify::procedure {

inline constexpr std::size_t kMaxPending = 256;      // transitions awaiting their plant signal
inline constexpr std::size_t kTagHistory = 128;      // recent plant signals, for evidence windows
inline constexpr std::size_t kMaxViolations = 256;   // per cycle

enum class ViolationKind : std::uint8_t {
  kSequence,             // a target reached the state out of the listed order
  kPrecedence,           // a step made progress before a step it requires was complete
  kEvidenceMissing,      // a transition was not corroborated by its plant signal in time
  kRegression,           // a completed target fell back to a lower state
  kStepTimeout,          // an active step exceeded its deadline
  kIncompleteAtRelease,  // the release gate found a step unfinished
};
const char* to_string(ViolationKind k) noexcept;

struct Violation {
  std::int64_t t_ms = 0;
  ViolationKind kind = ViolationKind::kSequence;
  std::int16_t step = -1;
  std::int16_t ip = -1;            // the inspection point (SEQUENCE: the observed one)
  std::int16_t expected = -1;      // SEQUENCE: the inspection point that was due
  std::int8_t now_rank = -1;       // REGRESSION: the state it fell back to
  std::uint64_t missing = 0;       // STEP_TIMEOUT, INCOMPLETE_AT_RELEASE: bit i = step target i
  std::uint64_t before = 0;        // PRECEDENCE: bit s = required step s, not yet complete
};

enum class StepStatus : std::uint8_t { kWaiting, kActive, kComplete };

class ProcedureEngine {
 public:
  explicit ProcedureEngine(const Profile& profile);

  // Starts a cycle: clears every step and activates those with no prerequisites.
  void start(std::int64_t t_ms) noexcept;

  // A committed state change of one inspection point (rank in its ordered family).
  void on_state(std::int64_t t_ms, int ip, int rank) noexcept;
  // A plant signal. tag is the profile's tag index, or -1 for a tag no step uses.
  void on_tag(std::int64_t t_ms, int tag, const TagValue& value) noexcept;
  // Deadlines: evidence windows and step durations.
  void tick(std::int64_t t_ms) noexcept;
  // The release gate. True when every step is complete and the cycle raised no SEQUENCE,
  // PRECEDENCE or EVIDENCE_MISSING.
  bool release(std::int64_t t_ms) noexcept;

  std::span<const Violation> violations() const noexcept { return {violations_.data(), n_violations_}; }
  std::size_t dropped_violations() const noexcept { return dropped_violations_; }

  // Read-only views for the io thread's state snapshot and the narrator.
  const Profile& profile() const noexcept { return *profile_; }
  StepStatus step_status(int step) const noexcept;
  int step_done_count(int step) const noexcept { return runtime_[step].done_n; }
  int step_done_at(int step, int i) const noexcept { return runtime_[step].done_order[i]; }  // target position
  std::int64_t step_activated_ms(int step) const noexcept { return runtime_[step].activated_ms; }
  bool all_complete() const noexcept;
  // The next target a sequence step expects, or -1 (an inspection point index).
  int expected_next(int step) const noexcept;

 private:
  struct StepRuntime {
    std::array<std::int16_t, kMaxTargets> done_order{};   // target positions, in the order reached
    std::uint8_t done_n = 0;
    std::uint64_t done_mask = 0;
    std::int64_t activated_ms = -1;
    std::int64_t completed_ms = -1;
    bool timed_out = false;
  };
  struct Pending {
    std::int64_t deadline_ms;
    std::int16_t step;
    std::int16_t ip;
  };
  struct TagEvent {
    std::int64_t t_ms;
    std::int32_t tag;
    TagValue value;
  };

  bool complete(int step) const noexcept { return runtime_[step].completed_ms >= 0; }
  void maybe_activate(std::int64_t t_ms) noexcept;
  void advance(std::int64_t t_ms, int step, int pos, int ip) noexcept;
  void flag(const Violation& v) noexcept;
  std::uint64_t missing_mask(int step) const noexcept;

  const Profile* profile_;
  std::array<StepRuntime, kMaxSteps> runtime_{};
  std::array<Pending, kMaxPending> pending_{};
  std::size_t n_pending_ = 0;
  std::array<TagEvent, kTagHistory> tags_{};
  std::size_t n_tags_ = 0;                                 // total seen this cycle; ring index = n % size
  std::array<Violation, kMaxViolations> violations_{};
  std::size_t n_violations_ = 0;
  std::size_t dropped_violations_ = 0;
};

}  // namespace sunverify::procedure
