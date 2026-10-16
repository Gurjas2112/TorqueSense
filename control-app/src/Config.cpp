#include "Config.hpp"

#include <cstdlib>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace sunverify::app {

StationConfig load_station_config(const std::filesystem::path& path) {
  YAML::Node y;
  try {
    y = YAML::LoadFile(path.string());
  } catch (const YAML::Exception& e) {
    throw std::runtime_error(path.string() + ": " + e.what());
  }
  StationConfig c;
  auto get = [](const YAML::Node& n, const char* k, auto dflt) { return n && n[k] ? n[k].as<decltype(dflt)>() : dflt; };

  const auto st = y["station"];
  c.station_id = get(st, "id", c.station_id);
  c.station_name = get(st, "name", std::string{});
  c.takt_s = get(st, "takt_seconds", c.takt_s);

  const auto src = y["source"];
  c.source_kind = get(src, "kind", c.source_kind);
  c.scenarios_dir = get(src, "scenarios_dir", c.scenarios_dir.string());
  if (src && src["playlist"])
    for (const auto& s : src["playlist"]) c.playlist.push_back(s.as<std::string>());
  c.fps = get(src, "fps", c.fps);
  c.process_width = get(src, "process_width", c.process_width);
  c.process_height = get(src, "process_height", c.process_height);

  const auto th = y["threads"];
  c.cpu_capture = get(th, "capture", c.cpu_capture);
  c.cpu_inference = get(th, "inference", c.cpu_inference);
  c.cpu_verify = get(th, "verify", c.cpu_verify);
  c.cpu_alert = get(th, "alert", c.cpu_alert);
  c.pin_threads = get(th, "pin", c.pin_threads);
  if (get(th, "priority_class", std::string("high")) == "realtime")
    throw std::runtime_error("threads.priority_class: realtime is forbidden (spec Section 14)");

  const auto pe = y["perception"];
  c.provider = get(pe, "provider", c.provider);
  c.fixtures = get(pe, "fixtures", c.fixtures.string());
  c.model_manifest = get(pe, "model_manifest", c.model_manifest.string());
  c.model_version = get(pe, "model_version", c.model_version);
  c.conf_commit = get(pe, "conf_commit", c.conf_commit);
  c.conf_floor = get(pe, "conf_floor", c.conf_floor);
  c.debounce_frames = get(pe, "debounce_frames", c.debounce_frames);
  c.crop_pool_size = get(pe, "crop_pool_size", c.crop_pool_size);
  c.detector_model = get(pe, "detector_model", c.detector_model.string());
  c.classifier_model = get(pe, "classifier_model", c.classifier_model.string());
  c.gpu_mem_limit_mb = get(pe, "gpu_mem_limit_mb", c.gpu_mem_limit_mb);
  c.warmup_runs = get(pe, "warmup_runs", c.warmup_runs);
  c.trt_cache_root = get(pe, "trt_cache", c.trt_cache_root.string());
  c.trt_version = get(pe, "trt_version", c.trt_version);

  const auto pr = y["profile"];
  c.profile_dir = get(pr, "dir", c.profile_dir.string());
  c.advisory_only = get(pr, "advisory_only", c.advisory_only);

  const auto al = y["alert"];
  c.alert_backend = get(al, "backend", c.alert_backend);
  c.opcua_endpoint = get(al, "endpoint", c.opcua_endpoint);
  c.tag_prefix = get(al, "prefix", c.tag_prefix);
  c.line_simulator = get(y["line_simulator"], "enabled", c.line_simulator);

  const auto cr = y["clip_ring"];
  c.clip_ring_name = get(cr, "name", c.clip_ring_name);
  c.clip_slots = get(cr, "slots", c.clip_slots);
  c.clip_slot_bytes = get(cr, "slot_bytes", c.clip_slot_bytes);
  c.jpeg_quality = get(cr, "jpeg_quality", c.jpeg_quality);

  const auto sc = y["sidecar"];
  c.sidecar_enabled = get(sc, "enabled", c.sidecar_enabled);
  c.sidecar_url = get(sc, "url", c.sidecar_url);
  c.sidecar_timeout_ms = get(sc, "timeout_ms", c.sidecar_timeout_ms);
  c.sidecar_failures = get(sc, "failure_threshold", c.sidecar_failures);
  c.sidecar_open_s = get(sc, "circuit_open_seconds", c.sidecar_open_s);
  c.sidecar_queue = get(sc, "queue_depth", c.sidecar_queue);

  const auto io = y["io"];
  c.ws_port = get(io, "websocket_port", c.ws_port);
  c.mjpeg_port = get(io, "mjpeg_port", c.mjpeg_port);
  c.control_port = get(io, "control_port", c.control_port);
  c.api_url = get(io, "api_url", c.api_url);
  c.event_spool = get(io, "event_spool", c.event_spool.string());

  const auto he = y["health"];
  c.bucket_s = get(he, "bucket_s", c.bucket_s);
  c.heartbeat_ms = get(he, "heartbeat_ms", c.heartbeat_ms);

  if (const char* k = std::getenv("API_KEY")) c.api_key = k;
  if (const char* u = std::getenv("TS_API_URL")) c.api_url = u;   // the launcher's choice of API port
#ifdef SUNVERIFY_WITH_CUDA
  const bool gpu_built = true;
#else
  const bool gpu_built = false;
#endif
  const bool gpu = c.provider == "cuda" || c.provider == "tensorrt";
  if (!gpu && c.provider != "replay" && c.provider != "schematic")
    throw std::runtime_error("perception.provider '" + c.provider + "': expected replay, schematic, cuda or tensorrt");
  if (gpu && !gpu_built)
    throw std::runtime_error("perception.provider '" + c.provider +
                             "' needs the ONNX Runtime build (windows-release preset, SUNVERIFY_WITH_CUDA) — "
                             "this build runs the replay and schematic providers only");
  return c;
}

}  // namespace sunverify::app
