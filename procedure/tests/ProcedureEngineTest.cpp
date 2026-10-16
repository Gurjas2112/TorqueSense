// Protects: I6 (written before ProcedureEngine), I16 (the same verdicts as reference/tsref), I15.
// Table-driven over every violation kind, on the samples' compiled profiles — no test names an
// assembly in engine code, because there is none: every behaviour comes from the profile's data.
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sunverify/procedure/ProcedureEngine.hpp"

using namespace sunverify::procedure;

namespace {

Profile sample(const std::string& name) {
  return Profile::load(std::string(TS_SOURCE_DIR) + "/samples/" + name + "/procedure.json");
}

struct Ev {
  char kind;           // 's' state · 'g' plant signal · 't' tick only
  std::int64_t t;
  std::string a;       // inspection point, or tag
  std::string b;       // state ('s'); value "true"/"false" ('g')
};

struct Result {
  std::optional<bool> ok;
  std::vector<Violation> v;
  std::vector<std::string> kinds;
};

Result run(const Profile& p, const std::vector<Ev>& events, std::optional<std::int64_t> release_at = std::nullopt) {
  ProcedureEngine e{p};
  e.start(0);
  for (const auto& ev : events) {
    if (ev.kind == 's') {
      const int ip = p.ip_index(ev.a);
      e.on_state(ev.t, ip, p.rank(ip, ev.b));
    } else if (ev.kind == 'g') {
      e.on_tag(ev.t, p.tag_index(ev.a), TagValue::of_bool(ev.b == "true"));
    }
    e.tick(ev.t);
  }
  Result r;
  if (release_at) r.ok = e.release(*release_at);
  for (const auto& v : e.violations()) {
    r.v.push_back(v);
    r.kinds.push_back(to_string(v.kind));
  }
  return r;
}

const std::string kNut = "Station.W14.Nutrunner.ResultOK";

std::vector<Ev> wheel(const std::vector<int>& order, bool nut = true, std::int64_t t0 = 1000) {
  std::vector<Ev> ev;
  for (int b = 1; b <= 5; ++b) ev.push_back({'s', t0 + 100 * (b - 1), "bolt-" + std::to_string(b), "seated"});
  for (std::size_t i = 0; i < order.size(); ++i) {
    const std::int64_t t = t0 + 2000 + 800 * static_cast<std::int64_t>(i);
    if (nut) ev.push_back({'g', t - 100, kNut, "true"});
    ev.push_back({'s', t, "bolt-" + std::to_string(order[i]), "fastened"});
  }
  return ev;
}

std::string ip_id(const Profile& p, int ip) { return ip < 0 ? "" : p.ips()[ip].id; }
std::string step_id(const Profile& p, int s) { return p.steps()[s].id; }

}  // namespace

// --- the six violation kinds, one row each, plus the clean cycle ---------------------------
struct KindCase {
  const char* name;
  const char* sample;
  std::vector<Ev> events;
  std::int64_t release_at;
  bool release_ok;
  const char* first_kind;     // nullptr: no violation at all
  const char* first_step;
};

class EveryViolationKind : public ::testing::TestWithParam<KindCase> {};

TEST_P(EveryViolationKind, MatchesTheReference) {
  const auto& c = GetParam();
  const Profile p = sample(c.sample);
  const Result r = run(p, c.events, c.release_at);
  EXPECT_EQ(*r.ok, c.release_ok) << c.name;
  if (!c.first_kind) {
    EXPECT_TRUE(r.v.empty()) << c.name;
    return;
  }
  ASSERT_FALSE(r.v.empty()) << c.name;
  EXPECT_STREQ(to_string(r.v[0].kind), c.first_kind) << c.name;
  EXPECT_EQ(step_id(p, r.v[0].step), c.first_step) << c.name;
}

INSTANTIATE_TEST_SUITE_P(
    Table, EveryViolationKind,
    ::testing::Values(
        KindCase{"clean_star", "wheel-hub-5lug", wheel({1, 3, 5, 2, 4}), 12000, true, nullptr, nullptr},
        KindCase{"sequence", "wheel-hub-5lug", wheel({1, 2, 3, 4, 5}), 12000, false, "SEQUENCE", "torque"},
        KindCase{"precedence", "door-trim-harness",
                 {{'s', 100, "clip-1", "closed"}, {'s', 200, "clip-2", "closed"}, {'s', 300, "clip-3", "closed"},
                  {'s', 400, "clip-5", "closed"}, {'s', 500, "clip-6", "closed"},
                  {'g', 900, "Station.D07.ConnectorLock", "true"}, {'s', 1000, "connector-1", "mated"},
                  {'s', 5000, "panel-1", "installed"}},
                 6000, false, "PRECEDENCE", "trim_panel"},
        KindCase{"evidence_missing", "wheel-hub-5lug",
                 [] { auto e = wheel({1, 3, 5, 2, 4}, false); e.push_back({'t', 9000, "", ""}); return e; }(), 12000,
                 false, "EVIDENCE_MISSING", "torque"},
        KindCase{"regression", "wheel-hub-5lug",
                 [] { auto e = wheel({1, 3, 5, 2, 4}); e.push_back({'s', 7000, "bolt-3", "seated"}); return e; }(),
                 12000, false, "REGRESSION", "torque"},
        KindCase{"step_timeout", "wheel-hub-5lug",
                 [] {
                   std::vector<Ev> e;
                   for (const auto& x : wheel({1, 3, 5, 2, 4}))
                     if (x.a != "bolt-4") e.push_back(x);
                   e.push_back({'t', 30000, "", ""});
                   return e;
                 }(),
                 31000, false, "PRECEDENCE", "torque"},   // torquing starts early; the timeout follows (below)
        KindCase{"incomplete_at_release", "electronics-enclosure",
                 [] {
                   std::vector<Ev> e{{'s', 100, "connector-1", "mated"}};
                   const int order[] = {2, 5, 3, 4, 6, 1};
                   for (int i = 0; i < 6; ++i) e.push_back({'s', 1000 + 300 * i, "screw-" + std::to_string(order[i]), "fastened"});
                   return e;
                 }(),
                 5000, false, "INCOMPLETE_AT_RELEASE", "label"}),
    [](const auto& info) { return std::string(info.param.name); });

// --- the reference suite, case for case (reference/tests/test_procedure.py) -----------------
TEST(ProcedureEngine, WheelAdjacentOrderNamesObservedAndExpected) {
  const Profile p = sample("wheel-hub-5lug");
  const Result r = run(p, wheel({1, 2, 3, 4, 5}), 12000);
  ASSERT_FALSE(r.v.empty());
  EXPECT_EQ(r.kinds[0], "SEQUENCE");
  EXPECT_EQ(ip_id(p, r.v[0].ip), "bolt-2");
  EXPECT_EQ(ip_id(p, r.v[0].expected), "bolt-3");
}

TEST(ProcedureEngine, SpecSequenceTable) {   // spec Section 31: star, adjacent, resume-from-two, double torque
  const Profile p = sample("wheel-hub-5lug");
  struct Row { std::vector<int> order; const char* observed; const char* expected; };
  for (const Row& row : {Row{{1, 3, 5, 2, 4}, nullptr, nullptr}, Row{{1, 2, 3, 4, 5}, "bolt-2", "bolt-3"},
                         Row{{1, 3, 2}, "bolt-2", "bolt-5"}, Row{{1, 1, 3, 5, 2, 4}, nullptr, nullptr}}) {
    const Result r = run(p, wheel(row.order));
    std::optional<Violation> seq;
    for (const auto& v : r.v)
      if (v.kind == ViolationKind::kSequence && !seq) seq = v;
    if (!row.observed) {
      EXPECT_FALSE(seq.has_value());
    } else {
      ASSERT_TRUE(seq.has_value());
      EXPECT_EQ(ip_id(p, seq->ip), row.observed);
      EXPECT_EQ(ip_id(p, seq->expected), row.expected);
    }
  }
}

TEST(ProcedureEngine, MissingBoltCaughtByDeadlineAndRelease) {
  const Profile p = sample("wheel-hub-5lug");
  std::vector<Ev> ev;
  for (const auto& x : wheel({1, 3, 5, 2, 4}))
    if (x.a != "bolt-4") ev.push_back(x);
  ev.push_back({'t', 30000, "", ""});
  const Result r = run(p, ev, 31000);
  EXPECT_FALSE(*r.ok);
  EXPECT_NE(std::find(r.kinds.begin(), r.kinds.end(), "STEP_TIMEOUT"), r.kinds.end());
  EXPECT_NE(std::find(r.kinds.begin(), r.kinds.end(), "INCOMPLETE_AT_RELEASE"), r.kinds.end());
}

TEST(ProcedureEngine, LooksTorquedButNoNutrunnerResultRaisesFive) {
  const Profile p = sample("wheel-hub-5lug");
  auto ev = wheel({1, 3, 5, 2, 4}, false);
  ev.push_back({'t', 9000, "", ""});
  const Result r = run(p, ev, 12000);
  EXPECT_FALSE(*r.ok);
  EXPECT_EQ(std::count(r.kinds.begin(), r.kinds.end(), "EVIDENCE_MISSING"), 5);
}

TEST(ProcedureEngine, LoosenedBoltResetsThePattern) {
  const Profile p = sample("wheel-hub-5lug");
  auto ev = wheel({1, 3, 5, 2, 4});
  ev.push_back({'s', 7000, "bolt-3", "seated"});
  const Result r = run(p, ev, 12000);
  EXPECT_FALSE(*r.ok);
  EXPECT_EQ(r.kinds, (std::vector<std::string>{"REGRESSION", "INCOMPLETE_AT_RELEASE"}));
}

TEST(ProcedureEngine, DoorPanelHidesAnOpenClip) {
  const Profile p = sample("door-trim-harness");
  const Result r = run(p,
                       {{'s', 100, "clip-1", "closed"}, {'s', 200, "clip-2", "closed"}, {'s', 300, "clip-3", "closed"},
                        {'s', 400, "clip-5", "closed"}, {'s', 500, "clip-6", "closed"},
                        {'g', 900, "Station.D07.ConnectorLock", "true"}, {'s', 1000, "connector-1", "mated"},
                        {'s', 5000, "panel-1", "installed"}},
                       6000);
  ASSERT_FALSE(r.v.empty());
  EXPECT_EQ(r.kinds[0], "PRECEDENCE");
  EXPECT_EQ(ip_id(p, r.v[0].ip), "panel-1");
  EXPECT_EQ(r.v[0].before, 1ULL << p.step_index("clips"));
}

TEST(ProcedureEngine, ConnectorLooksMatedButLockSwitchNeverClosed) {
  const Profile p = sample("door-trim-harness");
  std::vector<Ev> ev;
  for (int c = 1; c <= 6; ++c) ev.push_back({'s', 100 * c, "clip-" + std::to_string(c), "closed"});
  ev.push_back({'s', 1000, "connector-1", "mated"});
  ev.push_back({'s', 5000, "panel-1", "installed"});
  const Result r = run(p, ev, 6000);
  EXPECT_FALSE(*r.ok);
  bool found = false;
  for (const auto& v : r.v) found |= v.kind == ViolationKind::kEvidenceMissing && step_id(p, v.step) == "connector";
  EXPECT_TRUE(found);
}

TEST(ProcedureEngine, WindshieldGlassBeforeGasketFullySeated) {
  const Profile p = sample("windshield-gasket");
  std::vector<Ev> ev;
  int i = 1;
  for (int g : {1, 2, 3, 4, 5, 7, 8}) ev.push_back({'s', 100 * i++, "gasket-" + std::to_string(g), "seated"});
  ev.push_back({'s', 2000, "glass-1", "placed"});
  const Result r = run(p, ev, 3000);
  ASSERT_FALSE(r.v.empty());
  EXPECT_EQ(r.kinds[0], "PRECEDENCE");
  EXPECT_EQ(r.v[0].before, 1ULL << p.step_index("gasket"));
}

TEST(ProcedureEngine, EnclosureLabelForgottenIsCaughtAtRelease) {
  const Profile p = sample("electronics-enclosure");
  std::vector<Ev> ev{{'s', 100, "connector-1", "mated"}};
  const int order[] = {2, 5, 3, 4, 6, 1};
  for (int k = 0; k < 6; ++k) ev.push_back({'s', 1000 + 300 * k, "screw-" + std::to_string(order[k]), "fastened"});
  const Result r = run(p, ev, 5000);
  EXPECT_FALSE(*r.ok);
  ASSERT_EQ(r.v.size(), 1u);
  EXPECT_EQ(r.kinds[0], "INCOMPLETE_AT_RELEASE");
  EXPECT_EQ(r.v[0].missing, 1ULL);   // label-1, the step's only target
}

TEST(ProcedureEngine, JumpingPastAStateSatisfiesTheEarlierStepFirst) {
  const auto j = nlohmann::json::parse(R"({"profile": "mini",
    "inspection_points": [{"id": "s-1", "states": ["absent", "seated", "fastened"]}],
    "steps": [{"id": "seat", "kind": "complete", "targets": ["s-1"], "state": "seated", "max_duration_s": 10},
              {"id": "fasten", "kind": "complete", "targets": ["s-1"], "state": "fastened", "requires": ["seat"],
               "max_duration_s": 10}]})");
  const Profile p = Profile::from_json(j);
  const Result r = run(p, {{'s', 100, "s-1", "fastened"}}, 200);
  EXPECT_TRUE(*r.ok);
  EXPECT_TRUE(r.v.empty());
}

TEST(ProcedureEngine, ADoubleTorqueIsNotAViolation) {
  const Profile p = sample("wheel-hub-5lug");
  const Result r = run(p, wheel({1, 1, 3, 5, 2, 4}), 12000);
  EXPECT_TRUE(*r.ok);
  EXPECT_TRUE(r.v.empty());
}

TEST(ProfileLoader, AStateOutsideItsFamilyIsRejectedAtLoad) {
  const auto j = nlohmann::json::parse(R"({"profile": "bad",
    "inspection_points": [{"id": "bolt-1", "states": ["absent", "seated", "fastened"]}],
    "steps": [{"id": "s", "kind": "complete", "targets": ["bolt-1"], "state": "closed", "max_duration_s": 5}]})");
  EXPECT_THROW(Profile::from_json(j), ProfileError);
}

TEST(ProfileLoader, AStepWithoutADeadlineIsRejected) {   // I15
  const auto j = nlohmann::json::parse(R"({"profile": "bad",
    "inspection_points": [{"id": "c-1", "states": ["open", "closed"]}],
    "steps": [{"id": "s", "kind": "complete", "targets": ["c-1"], "state": "closed"}]})");
  EXPECT_THROW(Profile::from_json(j), ProfileError);
  EXPECT_NO_THROW(Profile::from_json(j, LoadOptions{.require_deadlines = false}));
}

TEST(ProcedureEngine, ResetsCompletelyForTheNextCycle) {
  const Profile p = sample("wheel-hub-5lug");
  ProcedureEngine e{p};
  e.start(0);
  for (const auto& ev : wheel({1, 2, 3, 4, 5})) {
    if (ev.kind == 's') e.on_state(ev.t, p.ip_index(ev.a), p.rank(p.ip_index(ev.a), ev.b));
    else e.on_tag(ev.t, p.tag_index(ev.a), TagValue::of_bool(true));
    e.tick(ev.t);
  }
  EXPECT_FALSE(e.release(12000));
  e.start(20000);
  EXPECT_TRUE(e.violations().empty());
  for (int s = 0; s < static_cast<int>(p.steps().size()); ++s) EXPECT_EQ(e.step_done_count(s), 0);
}
