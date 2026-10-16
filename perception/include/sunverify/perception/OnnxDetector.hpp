#pragma once
// The GPU detector (spec Sections 9, 13, 30): two ONNX models on ONNX Runtime's CUDA execution
// provider, or its TensorRT provider with CUDA behind it.
//
//   scene  — YOLO11n, hands and tools, on the frame letterboxed to 640 x 640;
//   points — the family classifier (MobileNetV3-Small) on one 96 x 96 crop per inspection point, cut at
//            the point's CAD projection, all points in one batch. Its outputs are the family's states in
//            rank order plus one more, `occluded`: a glove or tool over the face is the classifier's call,
//            per point, because a hand's bounding box is too coarse to say which bolt it covers.
//
// Everything is sized at create(): the letterbox map, the crop buffers, the bound input and output
// tensors. infer() fills them in place and runs both sessions through I/O bindings — no allocation of
// ours on the inference thread (I1). ONNX Runtime types stay behind the pimpl, so nothing else in the
// engine needs ORT's headers. Built only with SUNVERIFY_WITH_CUDA.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "sunverify/perception/StateVector.hpp"

namespace sunverify::perception {

struct OnnxConfig {
  std::string execution_provider = "cuda";       // cuda | tensorrt (TensorRT first, CUDA behind it)
  std::filesystem::path detector_model;           // YOLO11n, [1,3,640,640] -> [1,4+classes,anchors]
  std::filesystem::path classifier_model;         // [N,3,96,96] -> [N,states+1] probabilities
  int gpu_mem_limit_mb = 1024;                    // the CUDA provider's arena cap (Section 13)
  int warmup_runs = 20;                           // cuDNN's exhaustive search lands here, never in a histogram
  double max_median_ms = 40.0;                    // slower than this after warm-up: nodes on the CPU
  std::filesystem::path trt_cache_dir;            // cache/trt/<TensorRT>/<model sha8>/ (I12)
  bool trt_fp16 = true;
  int trt_workspace_mb = 256;
  bool allow_trt_build = false;                   // only `torquesense --build-trt-cache` sets it
  float det_conf = 0.35f, det_iou = 0.5f;
};

// One inspection point in the processed frame: where to cut, and how many states its family has.
struct CropPoint {
  float u = 0, v = 0;        // CAD projection, processed-frame pixels
  float side = 48;           // crop side in pixels: 3 x the face's projected size
  std::uint8_t n_states = 2;
};

class OnnxDetector {
 public:
  ~OnnxDetector();
  OnnxDetector(const OnnxDetector&) = delete;
  OnnxDetector& operator=(const OnnxDetector&) = delete;

  // Loads both models on the configured provider, warms up and times the detector. On failure returns
  // null and says why in `error` — the engine then refuses to start (never a silent drop to the CPU).
  static std::unique_ptr<OnnxDetector> create(const OnnxConfig& cfg, std::vector<CropPoint> points,
                                              int frame_w, int frame_h, std::string& error);

  // One BGR frame (frame_w x frame_h, row stride in bytes) -> sv.ips[0..n), sv.hands, sv.tools.
  void infer(const std::uint8_t* bgr, int stride, StateVector& sv) noexcept;

  const char* provider() const noexcept;          // "tensorrt" | "cuda"
  bool degraded() const noexcept;                 // TensorRT asked for, CUDA running
  double warm_median_ms() const noexcept;         // the detector's median after warm-up
  double last_detector_ms() const noexcept;
  double last_classifier_ms() const noexcept;

 private:
  struct Impl;
  explicit OnnxDetector(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// The letterbox the detector sees, and where a 640-space box lands in the frame — exposed for tests.
struct Letterbox {
  float scale = 1, pad_x = 0, pad_y = 0;
};
Letterbox letterbox_for(int frame_w, int frame_h, int side = 640) noexcept;

// Greedy non-maximum suppression over boxes already above the confidence floor, per class, into fixed
// arrays. Returns how many survive. Exposed for tests.
struct Box {
  float x0, y0, x1, y1, score;
  int cls;
};
int nms(Box* boxes, int n, float iou, Box* keep, int max_keep) noexcept;

}  // namespace sunverify::perception
