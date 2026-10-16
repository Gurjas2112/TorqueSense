#pragma once
// What the hot path carries between threads (spec Section 30): fixed-size, trivially copyable, no
// pointers into anything that can move. The io thread turns it into the JSON contract
// (contracts/state.schema.json); JSON never touches a pinned thread.
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sunverify::perception {

inline constexpr std::size_t kMaxIps = 256;   // per profile (spec Section 6)

enum ObservationFlags : std::uint8_t {
  kVisible = 1u << 0,     // the point was in view and classified this frame
  kOccluded = 1u << 1,    // a hand or tool covered it: no state information this frame
};

// One inspection point in one frame: the classifier's answer, before debounce.
struct IpObservation {
  std::int8_t rank = -1;      // index in the point's ordered state family; -1 = no answer
  std::uint8_t flags = 0;
  std::uint16_t reserved = 0;
  float conf = 0.0f;          // classifier confidence in `rank`
  float u = 0.0f;             // CAD-projected image position, full-resolution pixels
  float v = 0.0f;
};

// One frame's perception result: inference thread -> verify thread.
struct StateVector {
  std::uint64_t frame_seq = 0;
  std::int64_t t_capture_ns = 0;     // monotonic clock when the grab completed
  std::int64_t t_inferred_ns = 0;    // when inference finished
  std::uint64_t ring_seq = 0;        // this frame's slot in the SharedClipRing
  std::uint16_t n_ips = 0;
  std::uint16_t hands = 0;           // detector: hands in view
  std::uint16_t tools = 0;           // detector: tools in view
  std::uint16_t reserved = 0;
  float anchor_residual_px = 0.0f;   // mean distance, detection to CAD projection (fleet health)
  std::array<IpObservation, kMaxIps> ips{};
};

static_assert(std::is_trivially_copyable_v<StateVector>, "the hot path copies StateVectors through rings");

// A plant signal, as the alert thread reads it from the PLC: alert thread -> verify thread.
struct PlantSignal {
  std::int64_t t_ns = 0;      // monotonic clock on receipt
  std::int32_t tag = -1;      // the profile's tag index
  std::uint8_t kind = 0;      // 0 bool/number, 1 string id
  std::uint8_t reserved[3] = {};
  double number = 0.0;
  std::int32_t string_id = -1;
};
static_assert(std::is_trivially_copyable_v<PlantSignal>);

}  // namespace sunverify::perception
