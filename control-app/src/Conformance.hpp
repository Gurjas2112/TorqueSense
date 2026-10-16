#pragma once
// torquesense --conformance <samples-dir>: every trace of every sample through the C++ engine,
// checked against the trace's own expect: block — the same contract samples/run_samples.py checks
// with the reference interpreter (spec Section 12). Together they are the differential test (I16).
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sunverify/procedure/ProcedureEngine.hpp"

namespace sunverify::app {

struct TraceRun {
  bool release_ok = false;
  nlohmann::json violations = nlohmann::json::array();   // describe() of each, in order
};

// Replays one trace file through a fresh engine, exactly as run_samples.replay() does.
TraceRun replay_trace(const procedure::Profile& p, const std::filesystem::path& trace_yaml);

// Runs the whole folder; prints a table like run_samples.py. Returns the process exit code.
// json_out, when set, receives every trace's full violation list for the differential test.
int run_conformance(const std::filesystem::path& samples_dir, const std::filesystem::path& json_out, bool include_template);

// torquesense --sample <dir> --replay-trace <file>: one trace, printed.
int run_one_trace(const std::filesystem::path& sample_dir, const std::filesystem::path& trace);

// A trace file, parsed: its plant signals (a camera cannot see them), its release time and its expect: block.
struct TraceSpec {
  struct Signal {
    std::int64_t t_ms;
    int tag;
    procedure::TagValue value;
  };
  std::vector<Signal> signals;
  std::int64_t release_at_ms = 0;
  bool expect_release_ok = false;
  nlohmann::json expect_violations = nlohmann::json::array();
};
TraceSpec load_trace(const procedure::Profile& p, const std::filesystem::path& trace_yaml);

// run_samples.matches(): the same count, and every key an expectation names has the same value ("" = match).
std::string expect_matches(const nlohmann::json& expected, const nlohmann::json& got);

}  // namespace sunverify::app
