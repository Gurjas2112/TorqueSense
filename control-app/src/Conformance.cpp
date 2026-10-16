#include "Conformance.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "sunverify/cad/Compiler.hpp"
#include "sunverify/procedure/Describe.hpp"

namespace fs = std::filesystem;
using namespace sunverify::procedure;

namespace sunverify::app {
namespace {

const char* kKinds[] = {"SEQUENCE", "PRECEDENCE", "EVIDENCE_MISSING", "REGRESSION", "STEP_TIMEOUT",
                        "INCOMPLETE_AT_RELEASE"};

// A YAML scalar as Python's yaml.safe_load sees it: true/false, a number, or a string.
nlohmann::json yaml_scalar(const YAML::Node& n) {
  if (!n) return nullptr;
  if (n.IsSequence()) {
    auto a = nlohmann::json::array();
    for (const auto& x : n) a.push_back(yaml_scalar(x));
    return a;
  }
  if (n.IsMap()) {
    auto o = nlohmann::json::object();
    for (const auto& kv : n) o[kv.first.as<std::string>()] = yaml_scalar(kv.second);
    return o;
  }
  const std::string& s = n.Scalar();
  if (n.Tag() == "!") return s;
  if (s == "true" || s == "True") return true;
  if (s == "false" || s == "False") return false;
  try {
    std::size_t used = 0;
    const long long i = std::stoll(s, &used);
    if (used == s.size()) return i;
    const double d = std::stod(s, &used);
    if (used == s.size()) return d;
  } catch (...) {
  }
  return s;
}

// run_samples.matches(): same count, and every key the expectation names has the same value.
std::string matches(const nlohmann::json& expected, const nlohmann::json& got) {
  if (expected.size() != got.size())
    return "expected " + std::to_string(expected.size()) + " violation(s), got " + std::to_string(got.size()) + ": " + got.dump();
  for (std::size_t i = 0; i < expected.size(); ++i)
    for (const auto& [k, v] : expected[i].items())
      if (!got[i].contains(k) || got[i][k] != v)
        return "violation " + std::to_string(i + 1) + ": expected " + k + "=" + v.dump() + ", got " + got[i].dump();
  return {};
}

struct SampleReport {
  std::string name, profile, derived;
  std::vector<std::string> problems;
  int traces = 0, passed = 0;
  std::size_t n_ips = 0;
  double min_px = 0;
  std::set<std::string> kinds;
  nlohmann::json runs = nlohmann::json::array();
};

SampleReport check(const fs::path& dir) {
  SampleReport r;
  r.name = dir.filename().string();
  cad::Compiled c;
  try {
    c = cad::compile_folder(dir);
  } catch (const std::exception& e) {
    r.problems.push_back(std::string("C1 profile does not compile: ") + e.what());
    return r;
  }
  r.profile = c.procedure["profile"].get<std::string>();
  r.n_ips = c.rows.size();
  r.min_px = 1e9;
  for (const auto& row : c.rows) r.min_px = std::min(r.min_px, row.footprint.feature_px);
  for (const auto& s : c.procedure["steps"])
    if (s.contains("pattern")) {
      std::string order;
      for (const auto& t : s["targets"]) {
        const std::string id = t.get<std::string>();
        order += (order.empty() ? "" : ",") + id.substr(id.rfind('-') + 1);
      }
      r.derived += (r.derived.empty() ? "" : ", ") + s["id"].get<std::string>() + ": " + s["pattern"].get<std::string>() + " " + order;
    }
  if (!c.ok()) {
    std::string u;
    for (const auto& row : c.rows)
      if (!row.resolved) u += (u.empty() ? "" : ", ") + row.ip;
    r.problems.push_back("C1 unresolved inspection points: " + u);
  }
  std::string no_deadline;
  for (const auto& s : c.procedure["steps"])
    if (!s.contains("max_duration_s")) no_deadline += (no_deadline.empty() ? "" : ", ") + s["id"].get<std::string>();
  if (!no_deadline.empty()) r.problems.push_back("C2 steps without max_duration_s: " + no_deadline);

  Profile p;
  try {
    p = Profile::from_json(c.procedure, LoadOptions{.require_deadlines = false});   // C2 reported above
  } catch (const std::exception& e) {
    r.problems.push_back(std::string("C1 ") + e.what());
    return r;
  }

  std::vector<fs::path> traces;
  if (fs::exists(dir / "traces"))
    for (const auto& e : fs::directory_iterator(dir / "traces"))
      if (e.path().extension() == ".yaml") traces.push_back(e.path());
  std::sort(traces.begin(), traces.end());
  const bool has_correct = std::any_of(traces.begin(), traces.end(), [](const fs::path& t) { return t.stem() == "correct"; });
  if (!has_correct) r.problems.push_back("C3 traces/correct.yaml is missing");
  if (traces.size() - (has_correct ? 1 : 0) == 0) r.problems.push_back("C4 no fault trace");

  for (const auto& t : traces) {
    const YAML::Node y = YAML::LoadFile(t.string());
    const nlohmann::json exp = yaml_scalar(y["expect"]);
    const TraceRun run = replay_trace(p, t);
    const bool exp_ok = exp.value("release_ok", false);
    const nlohmann::json exp_v = exp.contains("violations") && !exp["violations"].is_null() ? exp["violations"] : nlohmann::json::array();
    std::string problem;
    if (t.stem() == "correct" && (!exp_v.empty() || !exp_ok)) problem = "C3 the correct trace must expect a clean release";
    else if (t.stem() != "correct" && exp_ok) problem = "C4 a fault trace must expect the release gate to refuse";
    else if (run.release_ok != exp_ok) problem = std::string("C5 release gate returned ") + (run.release_ok ? "True" : "False");
    else if (auto m = matches(exp_v, run.violations); !m.empty()) problem = "C5 " + m;
    ++r.traces;
    if (problem.empty()) ++r.passed;
    else r.problems.push_back(t.stem().string() + ": " + problem);
    for (const auto& v : run.violations) r.kinds.insert(v["kind"].get<std::string>());
    r.runs.push_back({{"trace", t.stem().string()}, {"release_ok", run.release_ok}, {"violations", run.violations}});
  }
  return r;
}

}  // namespace

TraceRun replay_trace(const Profile& p, const fs::path& trace_yaml) {
  const YAML::Node y = YAML::LoadFile(trace_yaml.string());
  ProcedureEngine e{p};
  e.start(0);
  for (const auto& ev : y["events"]) {
    const std::int64_t t = ev["t"].as<std::int64_t>();
    if (ev["ip"]) {
      const int ip = p.ip_index(ev["ip"].as<std::string>());
      if (ip < 0) throw std::runtime_error(trace_yaml.string() + ": unknown inspection point " + ev["ip"].as<std::string>());
      const int r = p.rank(ip, ev["state"].as<std::string>());
      if (r < 0) throw std::runtime_error(trace_yaml.string() + ": " + ev["state"].as<std::string>() + " is not a state of " + p.ips()[ip].id);
      e.on_state(t, ip, r);
    } else if (ev["tag"]) {
      e.on_tag(t, p.tag_index(ev["tag"].as<std::string>()), tag_value_from_json(p, yaml_scalar(ev["value"])));
    }
    e.tick(t);
  }
  TraceRun out;
  out.release_ok = e.release(y["release_at"].as<std::int64_t>());
  for (const auto& v : e.violations()) out.violations.push_back(describe(p, v));
  return out;
}

int run_conformance(const fs::path& samples_dir, const fs::path& json_out, bool include_template) {
  std::vector<fs::path> dirs;
  for (const auto& e : fs::directory_iterator(samples_dir))
    if (e.is_directory() && fs::exists(e.path() / "profile.yaml") &&
        (include_template || e.path().filename().string()[0] != '_'))
      dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());

  std::printf("%-24s %4s %7s  %7s  %s\n", "sample", "IPs", "min px", "traces", "derived order (C++ engine)");
  bool all_ok = !dirs.empty();
  std::set<std::string> kinds;
  nlohmann::json dump = nlohmann::json::array();
  for (const auto& d : dirs) {
    SampleReport r = check(d);
    const bool ok = r.problems.empty() && r.passed == r.traces;
    all_ok = all_ok && ok;
    std::printf("%-24s %4zu %7.1f  %3d/%-3d  %s  %s\n", r.name.c_str(), r.n_ips, r.min_px, r.passed, r.traces,
                r.derived.empty() ? "—" : r.derived.c_str(), ok ? "PASS" : "FAIL");
    for (const auto& pr : r.problems) std::printf("    %s\n", pr.c_str());
    kinds.insert(r.kinds.begin(), r.kinds.end());
    dump.push_back({{"sample", r.name}, {"profile", r.profile}, {"traces", r.runs}});
  }
  std::string missing;
  for (const char* k : kKinds)
    if (!kinds.count(k)) missing += (missing.empty() ? "" : ", ") + std::string(k);
  std::printf("\nviolation kinds exercised: %zu/6%s\n", kinds.size(), missing.empty() ? "" : (" — not exercised: " + missing).c_str());
  if (!json_out.empty()) {
    std::ofstream f(json_out, std::ios::binary);
    f << dump.dump(1) << '\n';
  }
  return all_ok ? 0 : 1;
}

int run_one_trace(const fs::path& sample_dir, const fs::path& trace) {
  const cad::Compiled c = cad::compile_folder(sample_dir);
  const Profile p = Profile::from_json(c.procedure);
  const fs::path t = trace.is_absolute() || fs::exists(trace) ? trace : sample_dir / trace;
  const TraceRun r = replay_trace(p, t);
  std::printf("%s / %s: release %s\n", p.name().c_str(), t.stem().string().c_str(), r.release_ok ? "OK" : "REFUSED");
  for (const auto& v : r.violations) std::printf("  %s\n", v.dump().c_str());
  return 0;
}

}  // namespace sunverify::app

namespace sunverify::app {

TraceSpec load_trace(const Profile& p, const fs::path& trace_yaml) {
  const YAML::Node y = YAML::LoadFile(trace_yaml.string());
  TraceSpec t;
  for (const auto& ev : y["events"])
    if (ev["tag"])
      t.signals.push_back({ev["t"].as<std::int64_t>(), p.tag_index(ev["tag"].as<std::string>()),
                           tag_value_from_json(p, yaml_scalar(ev["value"]))});
  t.release_at_ms = y["release_at"].as<std::int64_t>();
  const nlohmann::json exp = yaml_scalar(y["expect"]);
  t.expect_release_ok = exp.value("release_ok", false);
  if (exp.contains("violations") && !exp["violations"].is_null()) t.expect_violations = exp["violations"];
  return t;
}

std::string expect_matches(const nlohmann::json& expected, const nlohmann::json& got) { return matches(expected, got); }

}  // namespace sunverify::app
