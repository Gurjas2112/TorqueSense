// torquesense — one station's verification engine (spec Section 14).
//
//   torquesense --version                                   build, ONNX Runtime, the providers that load
//   torquesense --conformance samples [--json out.json]     every sample trace through this engine (I16)
//   torquesense --sample <dir> --replay-trace <trace.yaml>  one trace, printed
//   torquesense --config config\station_w14.yaml [--record web/public/replay/session.jsonl] [--seconds N]
//                                                           run the station
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "Conformance.hpp"
#include "IoSide.hpp"
#include "MediaVerify.hpp"
#include "Pipeline.hpp"

#ifdef SUNVERIFY_WITH_CUDA
#include <onnxruntime_cxx_api.h>
#endif

namespace {

// "Loads" means appending the provider succeeds — the check Section 30 trusts. GetAvailableProviders()
// only lists what was compiled in, so it passes on exactly the misconfiguration this must catch.
void print_providers() {
#ifdef SUNVERIFY_WITH_CUDA
  std::printf("onnxruntime %s\n", Ort::GetVersionString().c_str());
  const OrtApi& api = Ort::GetApi();
  {
    Ort::SessionOptions so;
    OrtTensorRTProviderOptionsV2* trt = nullptr;
    try {
      Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&trt));
      so.AppendExecutionProvider_TensorRT_V2(*trt);
      std::printf("provider tensorrt: loads\n");
    } catch (const Ort::Exception& e) {
      std::printf("provider tensorrt: unavailable — %s\n", e.what());
    }
    if (trt) api.ReleaseTensorRTProviderOptions(trt);
  }
  {
    Ort::SessionOptions so;
    OrtCUDAProviderOptionsV2* cu = nullptr;
    try {
      Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cu));
      so.AppendExecutionProvider_CUDA_V2(*cu);
      std::printf("provider cuda: loads\n");
    } catch (const Ort::Exception& e) {
      std::printf("provider cuda: unavailable — %s\n", e.what());
    }
    if (cu) api.ReleaseCUDAProviderOptions(cu);
  }
#else
  std::printf("onnxruntime: not built in (SUNVERIFY_WITH_CUDA=OFF) — perception runs as ReplayDetector\n");
  std::printf("provider replay: loads\n");
#endif
}

const char* arg_after(int argc, char** argv, const char* flag) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  return nullptr;
}

bool has(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return true;
  return false;
}

std::atomic<bool> g_stop{false};
extern "C" void on_signal(int) { g_stop = true; }

int run_station(const char* config, const char* record, const char* seconds) {
  using namespace sunverify::app;
  StationConfig cfg = load_station_config(config);
  Pipeline p{cfg};
  IoSide io{p};
  if (record) io.record_to(record);
  std::printf("torquesense %s: station %s, profile %s (%s), %zu scenarios, provider %s\n", TORQUESENSE_VERSION,
              cfg.station_id.c_str(), p.profile().name().c_str(), p.profile().version().c_str(),
              p.detector().scenarios().size(), p.detector().provider());
  std::printf("  ws :%d  mjpeg :%d  control :%d  plc %s (%s)\n", cfg.ws_port, cfg.mjpeg_port, cfg.control_port,
              cfg.opcua_endpoint.c_str(), cfg.alert_backend.c_str());
  std::fflush(stdout);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  p.start();
  io.start();
  const auto t0 = std::chrono::steady_clock::now();
  const double limit = seconds ? std::stod(seconds) : 0.0;
  while (!g_stop && !io.shutdown_requested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (limit > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= limit) break;
  }
  std::printf("stopping: %s\n", io.health_json().c_str());
  p.stop();
  io.stop();
  return 0;
}

int usage() {
  std::fprintf(stderr,
               "usage: torquesense --version\n"
               "       torquesense --conformance <samples-dir> [--template] [--json <out.json>]\n"
               "       torquesense --sample <dir> --replay-trace <trace.yaml>\n"
               "       torquesense --sample <dir> --verify-media all|<trace> [--frames-csv <out.csv>]\n"
               "       torquesense --sample <dir> --classify-image <frame.png>\n"
               "       torquesense --config <station.yaml> [--record <session.jsonl>] [--seconds N]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (has(argc, argv, "--version")) {
      std::printf("torquesense %s (GCC %d.%d.%d, __cplusplus %ld)\n", TORQUESENSE_VERSION, __GNUC__, __GNUC_MINOR__,
                  __GNUC_PATCHLEVEL__, static_cast<long>(__cplusplus));
      print_providers();
      return 0;
    }
    if (const char* dir = arg_after(argc, argv, "--conformance")) {
      const char* json = arg_after(argc, argv, "--json");
      return sunverify::app::run_conformance(dir, json ? json : "", has(argc, argv, "--template"));
    }
    if (const char* config = arg_after(argc, argv, "--config"))
      return run_station(config, arg_after(argc, argv, "--record"), arg_after(argc, argv, "--seconds"));
    if (const char* sample = arg_after(argc, argv, "--sample")) {
      if (const char* trace = arg_after(argc, argv, "--replay-trace")) return sunverify::app::run_one_trace(sample, trace);
      if (const char* which = arg_after(argc, argv, "--verify-media")) {
        const char* csv = arg_after(argc, argv, "--frames-csv");
        return sunverify::app::run_verify_media(sample, which, csv ? csv : "");
      }
      if (const char* img = arg_after(argc, argv, "--classify-image")) return sunverify::app::run_classify_image(sample, img);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "torquesense: %s\n", e.what());
    return 1;
  }
  return usage();
}
