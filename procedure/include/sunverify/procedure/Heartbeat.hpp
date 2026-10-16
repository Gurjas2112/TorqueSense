#pragma once
// The heartbeat rule (spec Section 14; I13): every period the alert thread may advance the PLC's
// heartbeat counter — but only if the verify thread's frame counter has moved since the last beat.
// A frozen camera, a hung inference thread and a dead process all look the same to the PLC: the
// counter stops. Liveness is tied to work, never to a thread merely running.
#include <cstdint>

namespace sunverify::procedure {

class HeartbeatGate {
 public:
  explicit HeartbeatGate(std::int64_t period_ms = 500) noexcept : period_ms_(period_ms) {}

  // Returns true when a beat is due; `beat()` is then the value to write.
  bool due(std::int64_t now_ms, std::uint64_t frames_verified) noexcept {
    if (now_ms < next_ms_) return false;
    next_ms_ = now_ms + period_ms_;
    if (frames_verified == last_verified_) return false;   // no work done: let the PLC see the silence
    last_verified_ = frames_verified;
    ++beat_;
    return true;
  }
  std::uint32_t beat() const noexcept { return beat_; }

 private:
  std::int64_t period_ms_;
  std::int64_t next_ms_ = 0;
  std::uint64_t last_verified_ = 0;
  std::uint32_t beat_ = 0;
};

}  // namespace sunverify::procedure
