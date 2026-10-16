// I1 — zero heap allocation in the steady-state hot path. The counting allocator counts every
// operator new made by this thread while g_counting is set; the hot-path calls must make none.
//
// Excluded from the ASan preset on purpose: ASan replaces operator new itself and the two
// replacements conflict (see tests/CMakeLists.txt). Do not "fix" that by deleting the exclusion.
#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

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
