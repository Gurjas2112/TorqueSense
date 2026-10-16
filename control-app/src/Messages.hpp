#pragma once
// What the pipeline threads pass each other (spec Section 14). Every message is a fixed-size POD
// carried by an SpscRingBuffer; nothing here owns memory.
#include <array>
#include <cstdint>
#include <type_traits>

#include "sunverify/perception/StateVector.hpp"
#include "sunverify/procedure/ProcedureEngine.hpp"

namespace sunverify::app {

using perception::kMaxIps;

// capture -> inference
struct FrameMsg {
  std::uint64_t frame_seq = 0;
  std::int64_t t_capture_ns = 0;
  std::uint64_t ring_seq = 0;
  std::uint64_t cycle_no = 0;
  std::uint64_t frame_in_cycle = 0;
  std::int16_t scenario = -1;
  bool new_cycle = false;
  std::uint16_t pool_index = 0;
  std::uint16_t full_index = 0xffff;   // full-resolution crop pool frame, if leased
};

// verify -> alert
enum class AlertKind : std::uint8_t { kCycleStart, kViolation, kReleaseOk, kReleaseHeld };
struct AlertCmd {
  AlertKind kind = AlertKind::kCycleStart;
  std::uint32_t seq = 0;                 // violation number within the engine's lifetime
  std::int64_t t_first_ns = 0;
  std::int64_t t_commit_ns = 0;
  std::array<char, 160> text{};          // Station.<id>.LastViolation
};

// alert -> io: when the PLC acknowledged the write
struct AckMsg {
  AlertKind kind = AlertKind::kCycleStart;
  std::uint32_t seq = 0;
  bool ok = false;
  double engine_ms = 0;                  // commit frame -> OPC-UA write acknowledged
  double evidence_ms = 0;                // first frame showing it -> OPC-UA write acknowledged
};

// verify -> io: what the dashboard, HMI and records need
enum class IoKind : std::uint8_t { kFrame, kViolation, kCycleStart, kRelease, kAsk };
struct IoMsg {
  IoKind kind = IoKind::kFrame;
  std::uint64_t frame_seq = 0;
  std::uint64_t cycle_no = 0;
  std::int64_t t_ns = 0;
  std::int64_t cycle_ms = 0;
  std::int16_t scenario = -1;
  bool release_ok = false;
  std::uint32_t seq = 0;                 // violation number (kViolation)
  procedure::Violation violation{};
  std::uint64_t ring_seq = 0;
  std::uint16_t hands = 0;
  float anchor_residual_px = 0;
  // kFrame: the committed picture
  std::uint16_t n_ips = 0;
  std::array<std::int8_t, kMaxIps> committed{};
  std::array<float, kMaxIps> conf{};
  std::array<std::uint8_t, kMaxIps> ambiguous{};
  std::array<std::uint8_t, procedure::kMaxSteps> step_done{};
  std::array<std::uint8_t, procedure::kMaxSteps> step_status{};
  std::array<std::int16_t, 4> next{-1, -1, -1, -1};
  // kAsk: an ambiguous point for Florence-2
  perception::IpObservation ask_obs{};
  std::uint16_t ask_ip = 0;
  // health
  std::uint64_t observations = 0, ambiguous_obs = 0;
  double conf_sum = 0;
};

// io (reasoning) -> verify
struct AdjResult {
  std::uint16_t ip = 0;
  std::int8_t candidate = 0;
  bool confirmed = false;
  std::int64_t t_ns = 0;
};

static_assert(std::is_trivially_copyable_v<FrameMsg> && std::is_trivially_copyable_v<AlertCmd> &&
              std::is_trivially_copyable_v<AckMsg> && std::is_trivially_copyable_v<IoMsg> &&
              std::is_trivially_copyable_v<AdjResult>);

}  // namespace sunverify::app
