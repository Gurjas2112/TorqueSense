#pragma once
// The plant interface the alert thread drives (spec Sections 14, 18). The OPC-UA write is blocking
// I/O, which is why it lives on its own pinned thread and never on the verify thread.
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sunverify::procedure {

// What the station's lamps and conveyor request should say. Written whole; an implementation may
// skip tags that did not change.
struct AndonState {
  bool red = false;
  bool green = false;
  bool hold_request = false;          // Conveyor.HoldReq — a request, never a command (Section 18)
  std::int32_t cycle_state = 0;       // 0 idle · 1 in cycle · 2 released OK · 3 violation
  std::array<char, 160> last_violation{};
};

// A plant tag the engine reads: the release request and every evidence tag a profile names.
struct PlantRead {
  std::int32_t tag = -1;              // profile tag index; -1 for the release request
  bool is_release = false;
  double number = 0;                  // bools as 0/1
  bool valid = false;
};

class IAlerter {
 public:
  virtual ~IAlerter() = default;
  virtual bool connect() noexcept = 0;
  virtual bool connected() const noexcept = 0;
  virtual bool apply(const AndonState& s) noexcept = 0;
  virtual bool heartbeat(std::uint32_t n) noexcept = 0;
  // Reads the release request and the evidence tags into out (one entry each, in order).
  virtual bool poll(std::span<PlantRead> out) noexcept = 0;
};

}  // namespace sunverify::procedure
