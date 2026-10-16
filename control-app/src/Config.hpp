#pragma once
// A station's configuration (spec Section 29), read once at start-up. Settings are configuration,
// not defaults (Section 8): what the station must do is written down, and read back.
#include <filesystem>
#include <string>
#include <vector>

namespace sunverify::app {

struct StationConfig {
  std::string station_id = "W-14";
  std::string station_name;
  double takt_s = 58;

  std::string source_kind = "file";
  std::filesystem::path scenarios_dir = "data/scenarios";
  std::vector<std::string> playlist;
  double fps = 30;
  int process_width = 960, process_height = 540;

  int cpu_capture = 8, cpu_inference = 10, cpu_verify = 12, cpu_alert = 14;
  bool pin_threads = true;

  std::string provider = "replay";
  std::filesystem::path fixtures = "tests/fixtures/detections";
  std::filesystem::path model_manifest = "models/manifest.json";
  std::string model_version = "replay-fixtures-v1";
  float conf_commit = 0.65f, conf_floor = 0.35f;
  int debounce_frames = 2;
  int crop_pool_size = 4;
  // provider cuda | tensorrt: the GPU detector (perception/src/OnnxDetector.cpp), models checked against
  // the manifest's SHA-256 before load (I12)
  std::filesystem::path detector_model = "models/yolo11n_wheel.onnx";
  std::filesystem::path classifier_model = "models/threaded_fastener.onnx";
  int gpu_mem_limit_mb = 1024, warmup_runs = 20;
  std::filesystem::path trt_cache_root = "cache/trt";
  std::string trt_version = "10.13.3";
  bool allow_trt_build = false;          // `torquesense --build-trt-cache` only, never in the station config

  std::filesystem::path profile_dir = "samples/wheel-hub-5lug";
  bool advisory_only = false;

  std::string alert_backend = "opcua";
  std::string opcua_endpoint = "opc.tcp://127.0.0.1:4840";
  std::string tag_prefix = "Station.W14";
  bool line_simulator = true;

  std::string clip_ring_name = "W-14";
  int clip_slots = 180, clip_slot_bytes = 262144, jpeg_quality = 80;

  bool sidecar_enabled = true;
  std::string sidecar_url = "http://127.0.0.1:8001";
  int sidecar_timeout_ms = 1500, sidecar_failures = 3, sidecar_open_s = 60, sidecar_queue = 16;

  int ws_port = 8080, mjpeg_port = 8090, control_port = 8095;
  std::string api_url = "http://127.0.0.1:8000";
  std::filesystem::path event_spool = "var/spool/W-14.jsonl";

  int bucket_s = 300, heartbeat_ms = 500;
  std::string api_key;   // from the environment (API_KEY), never from a committed file
};

StationConfig load_station_config(const std::filesystem::path& yaml);

}  // namespace sunverify::app
