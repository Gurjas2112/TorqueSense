#pragma once
// IAlerter over OPC-UA (IEC 62541), the open62541 1.4.21 client — the protocol the line PLC already
// speaks. Point it at the station PLC instead of the soft PLC and the same binary drives the tower.
// Tag names: ns=<urn:torquesense:station>;s=<prefix>.Andon.Red … (spec Section 18).
#include <memory>
#include <string>
#include <vector>

#include "sunverify/procedure/IAlerter.hpp"

namespace sunverify::procedure {

struct OpcUaConfig {
  std::string endpoint = "opc.tcp://127.0.0.1:4840";
  std::string prefix = "Station.W14";
  std::string namespace_uri = "urn:torquesense:station";
  int timeout_ms = 500;
  std::vector<std::string> evidence_tags;   // full names, in the profile's tag order
};

class OpcUaAlerter final : public IAlerter {
 public:
  explicit OpcUaAlerter(OpcUaConfig cfg);
  ~OpcUaAlerter() override;
  bool connect() noexcept override;
  bool connected() const noexcept override;
  bool apply(const AndonState& s) noexcept override;
  bool heartbeat(std::uint32_t n) noexcept override;
  bool poll(std::span<PlantRead> out) noexcept override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sunverify::procedure
