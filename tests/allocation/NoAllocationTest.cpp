// I1 — zero heap allocation in the steady-state hot path. The counting allocator counts every
// operator new made by this thread while g_counting is set; the hot-path calls must make none.
//
// Excluded from the ASan preset on purpose: ASan replaces operator new itself and the two
// replacements conflict (see tests/CMakeLists.txt). Do not "fix" that by deleting the exclusion.
#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

#include "VerifyCore.hpp"
#include "sunverify/perception/Detector.hpp"
#include "sunverify/procedure/ProcedureEngine.hpp"

extern std::atomic<std::size_t> g_allocs;
extern thread_local bool g_counting;

using namespace sunverify::procedure;

namespace {

struct Event {
  std::int64_t t;
  int ip, rank, tag;
};

}  // namespace

TEST(NoAllocation, ProcedureEngineOverTenThousandEvents) {
  const Profile p = Profile::load(std::string(TS_SOURCE_DIR) + "/samples/wheel-hub-5lug/procedure.json");
  ProcedureEngine e{p};
  const int nut = p.tag_index("Station.W14.Nutrunner.ResultOK");
  // one cycle of a wheel — seat all five, torque in the derived star, with the nutrunner's OK — plus
  // a faulty one (adjacent order, a loosened bolt) so every code path that flags runs too
  std::vector<Event> good, bad;
  for (int b = 0; b < 5; ++b) good.push_back({1000 + 100 * b, b, 1, -1});
  const int star[] = {0, 2, 4, 1, 3};
  for (int i = 0; i < 5; ++i) {
    good.push_back({2900 + 800 * i, -1, -1, nut});
    good.push_back({3000 + 800 * i, star[i], 2, -1});
  }
  for (int b = 0; b < 5; ++b) bad.push_back({1000 + 100 * b, b, 1, -1});
  for (int b = 0; b < 5; ++b) bad.push_back({3000 + 800 * b, b, 2, -1});   // no nutrunner, adjacent order
  bad.push_back({8000, 2, 1, -1});                                          // loosened

  // warm-up: the first cycles touch every table once
  for (int c = 0; c < 4; ++c) {
    e.start(0);
    for (const auto& ev : (c % 2 ? bad : good)) {
      if (ev.tag >= 0) e.on_tag(ev.t, ev.tag, TagValue::of_bool(true));
      else e.on_state(ev.t, ev.ip, ev.rank);
      e.tick(ev.t);
    }
    e.release(30000);
  }

  std::size_t events = 0, violations = 0;
  g_allocs = 0;
  g_counting = true;
  for (int c = 0; events < 10'000; ++c) {
    e.start(0);
    for (const auto& ev : (c % 2 ? bad : good)) {
      if (ev.tag >= 0) e.on_tag(ev.t, ev.tag, TagValue::of_bool(true));
      else e.on_state(ev.t, ev.ip, ev.rank);
      e.tick(ev.t);
      ++events;
    }
    e.release(30000);
    violations += e.violations().size();
  }
  g_counting = false;
  EXPECT_EQ(g_allocs.load(), 0u) << "allocations on the verify path over " << events << " events";
  EXPECT_GT(violations, 0u) << "the faulty cycles must have flagged";
}

// Pipeline::tick(): one frame through inference (ReplayDetector) and verify (committer, interpreter,
// deadlines) — the spec's NoAllocationInSteadyState, over 10,000 frames that cycle through every
// recorded scenario, faults included.
TEST(NoAllocation, PipelineTickOverTenThousandFrames) {
  const Profile p = Profile::load(std::string(TS_SOURCE_DIR) + "/samples/wheel-hub-5lug/procedure.json");
  std::vector<std::string> ids;
  for (const auto& ip : p.ips()) ids.push_back(ip.id);
  sunverify::perception::ReplayDetector det{ids, std::vector<float>(ids.size()), std::vector<float>(ids.size())};
  det.load_dir(std::string(TS_SOURCE_DIR) + "/tests/fixtures/detections");
  sunverify::app::VerifyCore core{p, {}};
  auto sv = std::make_unique<sunverify::perception::StateVector>();
  const int n_scn = static_cast<int>(det.scenarios().size());

  std::uint64_t frame = 0, f_in_cycle = 0, cycle = 0;
  int scn = 0;
  auto tick = [&] {
    const auto& s = det.scenarios()[static_cast<std::size_t>(scn)];
    if (f_in_cycle == 0) det.select(scn);
    det.infer(f_in_cycle, *sv);
    sv->frame_seq = frame;
    sv->t_capture_ns = static_cast<std::int64_t>(frame) * 33'333'333;
    sv->cycle_no = cycle;
    sv->frame_in_cycle = f_in_cycle;
    sv->new_cycle = f_in_cycle == 0;
    core.on_frame(*sv);
    core.mark_reported();
    ++frame;
    if (++f_in_cycle >= s.n_frames) {
      core.on_release(sv->t_capture_ns);
      f_in_cycle = 0;
      ++cycle;
      scn = (scn + 1) % n_scn;
    }
  };
  for (int i = 0; i < 100; ++i) tick();          // warm-up: pools, first cycle
  g_allocs = 0;
  g_counting = true;
  for (int i = 0; i < 10'000; ++i) tick();
  g_counting = false;
  EXPECT_EQ(g_allocs.load(), 0u) << "allocations in Pipeline::tick over 10,000 frames";
  EXPECT_GT(cycle, 5u);
}
