// latency_test — the I4 gate tool (spec Sections 13, 31).
//
//   latency_test --config config/station_w14.yaml --scenario data/scenarios/fault_interrupt_resume.mp4
//                --runs 100 --budget-evidence-p99-ms 150 --histogram docs\latency.hgrm --summary-json out.json
//
// Runs the station engine — the same Pipeline and threads as torquesense.exe, writing to the PLC in
// the config — on one scenario, cycle after cycle, and measures both clocks for every alert:
//   engine latency    commit frame              -> OPC-UA write acknowledged
//   evidence latency  first frame showing it    -> OPC-UA write acknowledged
// It prints p50/p99/p99.9, the provider and the GPU clocks observed, and exits non-zero if evidence
// p99 exceeds the budget. I4 is defined for the GPU detector on locked clocks; a run on the replay
// provider says so in its output and in the summary, and is never quoted as I4.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "IoSide.hpp"
#include "Pipeline.hpp"

namespace fs = std::filesystem;
using namespace sunverify::app;

namespace {

const char* arg(int argc, char** argv, const char* flag, const char* dflt = nullptr) {
  for (int i = 1; i + 1 < argc; ++i)
    if (!std::strcmp(argv[i], flag)) return argv[i + 1];
  return dflt;
}

std::string gpu_conditions() {
  std::string out;
  if (std::FILE* p = popen("nvidia-smi --query-gpu=name,clocks.sm,clocks.max.sm,temperature.gpu,driver_version --format=csv,noheader", "r")) {
    char buf[256];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out.empty() ? "nvidia-smi unavailable" : out;
}

}  // namespace

int main(int argc, char** argv) {
  const fs::path config = arg(argc, argv, "--config", "config/station_w14.yaml");
  const std::string scenario = fs::path(arg(argc, argv, "--scenario", "fault_interrupt_resume")).stem().string();
  const int runs = std::atoi(arg(argc, argv, "--runs", "100"));
  const double budget = std::atof(arg(argc, argv, "--budget-evidence-p99-ms", "150"));
  const char* hist = arg(argc, argv, "--histogram");
  const char* summary = arg(argc, argv, "--summary-json");
  const std::string provider_asked = arg(argc, argv, "--provider", "replay");

  StationConfig cfg = load_station_config(config);
  cfg.playlist.assign(1, scenario);                     // one scenario, cycle after cycle
  cfg.line_simulator = cfg.alert_backend != "none";
  cfg.sidecar_enabled = false;                          // adjudication is off the alert path; keep it out of the run
  if (provider_asked != cfg.provider) {
    std::fprintf(stderr, "latency_test: provider %s requested, but this build runs %s\n", provider_asked.c_str(), cfg.provider.c_str());
    return 2;
  }
  const std::string before = gpu_conditions();
  Pipeline p{cfg};
  IoSide io{p};
  const int idx = p.detector().find(scenario);
  if (idx < 0) {
    std::fprintf(stderr, "latency_test: unknown scenario %s\n", scenario.c_str());
    return 2;
  }
  const auto& s = p.detector().scenarios()[static_cast<std::size_t>(idx)];
  std::printf("latency_test: %d runs of %s (%.1f s each), provider %s, PLC %s (%s)\n", runs, scenario.c_str(),
              s.n_frames / s.fps, p.detector().provider(), cfg.opcua_endpoint.c_str(), cfg.alert_backend.c_str());
  std::printf("conditions before: %s\n", before.c_str());
  std::fflush(stdout);

  p.start();
  io.start();
  // cycles count from 1; wait until `runs` complete cycles have passed
  while (p.counters().cycles.load() <= static_cast<std::uint64_t>(runs))
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const std::string after = gpu_conditions();
  const auto lat = nlohmann::json::parse(io.latency_summary());
  const auto health = nlohmann::json::parse(io.health_json());
  p.stop();
  io.stop();

  const auto& ev = lat["evidence"];
  const auto& en = lat["engine"];
  std::printf("engine   latency (ms): n=%lld p50=%.2f p99=%.2f p99.9=%.2f max=%.2f\n", en["n"].get<long long>(),
              en["p50_ms"].get<double>(), en["p99_ms"].get<double>(), en["p999_ms"].get<double>(), en["max_ms"].get<double>());
  std::printf("evidence latency (ms): n=%lld p50=%.2f p99=%.2f p99.9=%.2f max=%.2f\n", ev["n"].get<long long>(),
              ev["p50_ms"].get<double>(), ev["p99_ms"].get<double>(), ev["p999_ms"].get<double>(), ev["max_ms"].get<double>());
  std::printf("frames verified %llu, dropped %llu, fps %.2f\n", health["frames_verified"].get<unsigned long long>(),
              health["frames_dropped"].get<unsigned long long>(), health["fps"].get<double>());
  std::printf("conditions after: %s\n", after.c_str());
  const bool replay = std::string(p.detector().provider()) == "replay";
  if (replay)
    std::printf("NOTE: replay provider — no detector inference in the loop. This measures the engine, rings and the "
                "OPC-UA write, not I4 (which needs the GPU detector on locked clocks).\n");

  if (hist) io.write_histogram(hist);
  const bool pass = ev["n"].get<long long>() > 0 && ev["p99_ms"].get<double>() <= budget;
  if (summary) {
    std::ofstream out(summary);
    out << nlohmann::json{{"scenario", scenario}, {"runs", runs}, {"provider", p.detector().provider()},
                          {"budget_evidence_p99_ms", budget}, {"pass", pass}, {"engine", en}, {"evidence", ev},
                          {"fps", health["fps"]}, {"frames_dropped", health["frames_dropped"]},
                          {"conditions_before", before}, {"conditions_after", after},
                          {"counts_as_I4", !replay}}
               .dump(2)
        << '\n';
  }
  std::printf("%s: evidence p99 %.2f ms against a %.0f ms budget\n", pass ? "PASS" : "FAIL", ev["p99_ms"].get<double>(), budget);
  return pass ? 0 : 1;
}
