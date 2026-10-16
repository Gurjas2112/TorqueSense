// The compiler on every sample, against the reference's output, and its refusals (I17).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "sunverify/cad/Compiler.hpp"

using namespace sunverify::cad;
namespace fs = std::filesystem;

namespace {

nlohmann::json fixture() {
  std::ifstream f(std::string(TS_FIXTURES) + "/tsref_cases.json");
  return nlohmann::json::parse(f);
}

const char* kSmall = R"(
profile: tiny
cameras:
  - {id: cam, width_px: 1920, hfov_deg: 69, position_mm: [0, 0, 350], look_at_mm: [0, 0, 0]}
groups:
  - id: screw
    family: threaded_fastener
    states: [absent, seated, fastened]
    feature_mm: 7
    members:
      - {position_mm: [-40, 40, 0]}
      - {position_mm: [40, 40, 0]}
      - {position_mm: [40, -40, 0]}
      - {position_mm: [-40, -40, 0]}
steps:
  - {id: screws, group: screw, kind: sequence, order: derive, state: fastened, max_duration_s: 60}
)";

}  // namespace

TEST(Compiler, EverySampleMatchesTheReference) {
  const auto fx = fixture();
  int samples = 0;
  for (const auto& s : fx["samples"]) {
    const fs::path dir = fs::path(TS_SOURCE_DIR) / "samples" / s["sample"].get<std::string>();
    const Compiled c = compile_folder(dir);
    EXPECT_EQ(c.procedure["profile"], s["profile"]) << dir;
    const auto& ips = c.procedure["inspection_points"];
    ASSERT_EQ(ips.size(), s["ips"].size()) << dir;
    for (std::size_t i = 0; i < ips.size(); ++i) {
      EXPECT_EQ(ips[i]["id"], s["ips"][i]["id"]);
      EXPECT_EQ(ips[i]["camera"], s["ips"][i]["camera"]) << ips[i]["id"];
      EXPECT_NEAR(ips[i]["feature_px"].get<double>(), s["ips"][i]["feature_px"].get<double>(), 0.051) << ips[i]["id"];
    }
    const auto& steps = c.procedure["steps"];
    ASSERT_EQ(steps.size(), s["steps"].size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
      EXPECT_EQ(steps[i]["targets"], s["steps"][i]["targets"]) << steps[i]["id"];
      if (!s["steps"][i]["pattern"].is_null()) {
        EXPECT_EQ(steps[i]["pattern"], s["steps"][i]["pattern"]);
      }
    }
    EXPECT_EQ(c.unresolved, s["unresolved"].get<std::vector<std::string>>()) << dir;
    ++samples;
  }
  EXPECT_GE(samples, 7);
}

TEST(Compiler, WheelCoverageIsClean) {
  const Compiled c = compile_folder(fs::path(TS_SOURCE_DIR) / "samples" / "wheel-hub-5lug");
  EXPECT_TRUE(c.ok());
  EXPECT_TRUE(c.coverage["ok"].get<bool>());
  EXPECT_EQ(c.coverage["points"].size(), 5u);
  for (const auto& p : c.coverage["points"]) EXPECT_GE(p["feature_px"].get<double>(), 16.0);
}

TEST(Compiler, DerivesTheDiagonalOrderForASquareLid) {
  const Compiled c = compile(parse_manifest(kSmall));
  EXPECT_EQ(c.procedure["steps"][0]["targets"], (std::vector<std::string>{"screw-1", "screw-3", "screw-2", "screw-4"}));
  EXPECT_EQ(c.procedure["steps"][0]["pattern"], "circle");
  EXPECT_EQ(c.procedure["steps"][0]["order_source"], "derived");
}

TEST(Compiler, AnExplicitOrderOverridesDerivation) {
  std::string y = kSmall;
  y.replace(y.find("order: derive"), 13, "order: [1, 2, 3, 4]");
  const Compiled c = compile(parse_manifest(y));
  EXPECT_EQ(c.procedure["steps"][0]["targets"], (std::vector<std::string>{"screw-1", "screw-2", "screw-3", "screw-4"}));
  EXPECT_EQ(c.procedure["steps"][0]["order_source"], "specified");
}

TEST(Compiler, RefusesATwoMillimetreScrewHead) {   // C1 / I17
  std::string y = kSmall;
  y.replace(y.find("feature_mm: 7"), 13, "feature_mm: 2");
  const Compiled c = compile(parse_manifest(y));
  EXPECT_FALSE(c.ok());
  EXPECT_EQ(c.unresolved.size(), 4u);
  EXPECT_FALSE(c.coverage["ok"].get<bool>());
}

TEST(Compiler, RefusesACameraMovedBackTooFar) {   // C1 / I17
  std::string y = kSmall;
  y.replace(y.find("[0, 0, 350]"), 11, "[0, 0, 900]");
  EXPECT_FALSE(compile(parse_manifest(y)).ok());
}

TEST(Compiler, RejectsAStepOnAMissingGroup) {
  std::string y = kSmall;
  y.replace(y.find("group: screw"), 12, "group: bolts");
  EXPECT_THROW(compile(parse_manifest(y)), CompileError);
}
