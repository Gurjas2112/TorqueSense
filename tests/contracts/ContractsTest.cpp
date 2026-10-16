// The generated contract types compile cleanly (I5) and round-trip the spec's own examples.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "contracts.hpp"

namespace c = sunverify::contracts;

TEST(Contracts, StateRoundTripsTheSpecExample) {
  const auto j = nlohmann::json::parse(R"({
    "station_id": "W-14", "frame_seq": 18233, "cycle_id": "3f9c", "step": "S4",
    "profile": "wheel-fitment-rl",
    "ips": [ { "id": "bolt-1", "family": "threaded_fastener", "state": "fastened", "conf": 0.97, "u": 824, "v": 576 } ],
    "next": ["bolt-3"],
    "latency": { "engine_ms_p50": 24.9, "engine_ms_p99": 34.6, "evidence_ms_p99": 69.3 },
    "verdict": null
  })");
  const c::StationState s = j.get<c::StationState>();
  EXPECT_EQ(s.station_id, "W-14");
  ASSERT_EQ(s.ips.size(), 1u);
  EXPECT_EQ(s.ips[0].state, "fastened");
  EXPECT_FALSE(s.verdict.has_value());
  const nlohmann::json back = s;
  EXPECT_EQ(back.at("frame_seq"), 18233);
  EXPECT_EQ(back.at("next")[0], "bolt-3");
}

TEST(Contracts, AnomalyCarriesTheViolationKind) {
  c::Anomaly a;
  a.event_id = "00000000-0000-0000-0000-000000000001";
  a.violation_type = c::ViolationType::SEQUENCE;
  a.narrative = "Bolt 2 torqued before bolt 3.";
  const nlohmann::json j = a;
  EXPECT_EQ(j.at("violation_type"), "SEQUENCE");
  EXPECT_EQ(j.at("narrative"), "Bolt 2 torqued before bolt 3.");
}
