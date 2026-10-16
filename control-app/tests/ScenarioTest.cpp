// The scenario matrix (spec Section 31): every recorded scenario through perception replay, the
// debounce/ambiguity committer and the interpreter must raise exactly its expected violations, each
// inside its frame window — and the two edge cases (hand occlusion with flicker and an ambiguous
// frame; a second person) must raise nothing. Expected verdicts come from the reference interpreter
// (synth/scenarios.py), never from this test.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "VerifyCore.hpp"
#include "sunverify/perception/Detector.hpp"
#include "sunverify/procedure/Describe.hpp"

using namespace sunverify;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot = TS_SOURCE_DIR;

struct ScenarioRun {
  bool release_ok = false;
  nlohmann::json violations = nlohmann::json::array();
};

ScenarioRun play(const procedure::Profile& p, perception::ReplayDetector& det, int scenario) {
  const auto& s = det.scenarios()[static_cast<std::size_t>(scenario)];
  app::VerifyCore core{p, {}};
  det.select(scenario);
  const double frame_ms = 1000.0 / s.fps;
  std::size_t next_tag = 0;
  bool released = false;
  auto sv = std::make_unique<perception::StateVector>();
  ScenarioRun out;
  for (std::uint64_t f = 0; f < s.n_frames; ++f) {
    const auto t_ns = static_cast<std::int64_t>(f * frame_ms * 1e6);
    // plant signals that arrived before this frame
    while (next_tag < s.tags.size() && s.tags[next_tag].t_ms * 1'000'000 <= t_ns) {
      const auto& g = s.tags[next_tag++];
      core.on_signal(g.t_ms * 1'000'000, p.tag_index(g.tag), procedure::TagValue::of_bool(g.value));
    }
    if (!released && f * frame_ms >= static_cast<double>(s.release_at_ms)) {
      out.release_ok = core.on_release(s.release_at_ms * 1'000'000);
      released = true;
    }
    det.infer(f, *sv);
    sv->frame_seq = f;
    sv->t_capture_ns = t_ns;
    sv->cycle_no = 1;
    sv->frame_in_cycle = f;
    sv->new_cycle = f == 0;
    core.on_frame(*sv);
  }
  for (std::size_t i = 0; i < core.violations_total(); ++i) out.violations.push_back(procedure::describe(p, core.violation(i)));
  return out;
}

}  // namespace

TEST(Scenario, EveryRecordedScenarioRaisesExactlyItsExpectedVerdict) {
  const procedure::Profile p = procedure::Profile::load(kRoot / "samples/wheel-hub-5lug/procedure.json");
  std::vector<std::string> ids;
  std::vector<float> u, v;
  for (const auto& ip : p.ips()) {
    ids.push_back(ip.id);
    u.push_back(0);
    v.push_back(0);
  }
  perception::ReplayDetector det{ids, u, v};
  ASSERT_EQ(det.load_dir(kRoot / "tests/fixtures/detections"), 10u);

  for (int i = 0; i < static_cast<int>(det.scenarios().size()); ++i) {
    const auto& s = det.scenarios()[static_cast<std::size_t>(i)];
    std::ifstream in(kRoot / "tests/fixtures/detections" / (s.name + ".expected.json"));
    const auto exp = nlohmann::json::parse(in);
    const ScenarioRun got = play(p, det, i);
    EXPECT_EQ(got.release_ok, exp["release_ok"].get<bool>()) << s.name;
    ASSERT_EQ(got.violations.size(), exp["violations"].size()) << s.name << ": " << got.violations.dump();
    for (std::size_t k = 0; k < got.violations.size(); ++k) {
      const auto& e = exp["violations"][k];
      const auto& g = got.violations[k];
      for (const auto& [key, val] : e.items()) {
        if (key == "window_ms" || key == "t_ms") continue;
        EXPECT_EQ(g.value(key, nlohmann::json()), val) << s.name << " violation " << k << " field " << key;
      }
      const double t = g["t_ms"].get<double>();
      EXPECT_GE(t, e["window_ms"][0].get<double>()) << s.name << " violation " << k << " fired early";
      EXPECT_LE(t, e["window_ms"][1].get<double>()) << s.name << " violation " << k << " fired late";
    }
    if (s.name.rfind("edge_", 0) == 0) {
      EXPECT_TRUE(got.violations.empty()) << s.name << " must not alert";
    }
  }
}
