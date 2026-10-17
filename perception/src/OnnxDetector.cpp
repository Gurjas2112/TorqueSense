#include "sunverify/perception/OnnxDetector.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace sunverify::perception {
namespace {

constexpr int kSide = 640;                          // the detector's input
constexpr int kCrop = 96;                           // the classifier's input
constexpr int kMaxCandidates = 512, kMaxKeep = 32;
constexpr float kPad = 114.0f / 255.0f;            // the letterbox grey YOLO was trained with
constexpr std::array<float, 3> kMean{0.485f, 0.456f, 0.406f}, kStd{0.229f, 0.224f, 0.225f};   // ImageNet, RGB

double ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// A bilinear sample position: two source indices and the weight of the second.
struct Tap {
  int i0, i1;
  float w;
};
Tap tap(float src, int n) {
  src = std::clamp(src, 0.0f, static_cast<float>(n - 1));
  const int i0 = static_cast<int>(src);
  const int i1 = std::min(i0 + 1, n - 1);
  return {i0, i1, src - static_cast<float>(i0)};
}

}  // namespace

struct OnnxDetector::Impl {
  OnnxConfig cfg;
  std::vector<CropPoint> pts;
  int fw = 0, fh = 0;
  Letterbox lb;
  std::string provider = "cuda";
  bool degraded = false;
  double warm_ms = 0, det_ms = 0, cls_ms = 0;

  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "torquesense"};
  Ort::Session det{nullptr}, cls{nullptr};
  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  // Tensors wrap the buffers below and are passed to Run() each frame. Not IoBinding: a bound CPU input
  // is copied to the device once, at bind time, so every later frame would be inferred on that copy.
  Ort::Value det_in_v{nullptr}, det_out_v{nullptr}, cls_in_v{nullptr}, cls_out_v{nullptr};
  std::string det_in_name, det_out_name, cls_in_name, cls_out_name;
  const char *det_in_c = nullptr, *det_out_c = nullptr, *cls_in_c = nullptr, *cls_out_c = nullptr;
  Ort::RunOptions run;

  void run_det() { det.Run(run, &det_in_c, &det_in_v, 1, &det_out_c, &det_out_v, 1); }
  void run_cls() { cls.Run(run, &cls_in_c, &cls_in_v, 1, &cls_out_c, &cls_out_v, 1); }

  // sized once in create(); infer() only writes into them
  std::vector<float> det_in, det_out, cls_in, cls_out;
  std::vector<Tap> lx, ly;                          // letterbox: per output column/row inside the image
  int lb_x0 = 0, lb_x1 = 0, lb_y0 = 0, lb_y1 = 0;
  int n_anchor = 0, n_class = 0, n_out_states = 0;
  std::vector<Box> cand, keep;

  void append_providers(Ort::SessionOptions& so, bool detector);
  void letterbox(const std::uint8_t* bgr, int stride) noexcept;
  void crops(const std::uint8_t* bgr, int stride) noexcept;
  void decode(StateVector& sv) noexcept;
};

void OnnxDetector::Impl::append_providers(Ort::SessionOptions& so, bool detector) {
  const OrtApi& api = Ort::GetApi();
  // TensorRT for the detector only: its input is fixed at 640², which is what TensorRT optimises best. The
  // classifier's batch varies with the points in view; it stays on CUDA (Section 30).
  if (detector && cfg.execution_provider == "tensorrt") {
    try {
      if (!cfg.allow_trt_build && !std::filesystem::exists(cfg.trt_cache_dir))
        throw std::runtime_error("no TensorRT engine cache at " + cfg.trt_cache_dir.string() +
                                 " — run `torquesense --build-trt-cache` first; an engine is never built during a shift");
      std::filesystem::create_directories(cfg.trt_cache_dir);
      OrtTensorRTProviderOptionsV2* raw = nullptr;
      Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&raw));
      std::unique_ptr<OrtTensorRTProviderOptionsV2, decltype(api.ReleaseTensorRTProviderOptions)> opts{raw, api.ReleaseTensorRTProviderOptions};
      const std::string cache = cfg.trt_cache_dir.string();
      const std::string ws = std::to_string(static_cast<std::size_t>(cfg.trt_workspace_mb) << 20);
      const char* keys[] = {"device_id", "trt_fp16_enable", "trt_max_workspace_size", "trt_engine_cache_enable",
                            "trt_engine_cache_path", "trt_timing_cache_enable", "trt_timing_cache_path"};
      const char* vals[] = {"0", cfg.trt_fp16 ? "1" : "0", ws.c_str(), "1", cache.c_str(), "1", cache.c_str()};
      Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(opts.get(), keys, vals, std::size(keys)));
      so.AppendExecutionProvider_TensorRT_V2(*opts);
      provider = "tensorrt";
    } catch (const std::exception& e) {
      // a validated fallback, never a silent one: reported as DEGRADED on /health (Section 33)
      std::fprintf(stderr, "[engine] TensorRT provider failed to load: %s — running on CUDA, DEGRADED\n", e.what());
      provider = "cuda";
      degraded = true;
    }
  }
  OrtCUDAProviderOptions cuda{};
  cuda.device_id = 0;
  cuda.gpu_mem_limit = static_cast<std::size_t>(cfg.gpu_mem_limit_mb) << 20;
  cuda.arena_extend_strategy = 1;                  // kSameAsRequested: no greedy doubling
  cuda.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
  cuda.do_copy_in_default_stream = 1;
  so.AppendExecutionProvider_CUDA(cuda);            // throws: no CUDA provider means no start
}

void OnnxDetector::Impl::letterbox(const std::uint8_t* bgr, int stride) noexcept {
  float* r = det_in.data();
  float* g = r + kSide * kSide;
  float* b = g + kSide * kSide;
  for (int y = 0; y < kSide; ++y) {
    const bool row_in = y >= lb_y0 && y < lb_y1;
    for (int x = 0; x < kSide; ++x) {
      const int o = y * kSide + x;
      if (!row_in || x < lb_x0 || x >= lb_x1) {
        r[o] = g[o] = b[o] = kPad;
        continue;
      }
      const Tap& tx = lx[static_cast<std::size_t>(x - lb_x0)];
      const Tap& ty = ly[static_cast<std::size_t>(y - lb_y0)];
      const std::uint8_t* p00 = bgr + ty.i0 * stride + tx.i0 * 3;
      const std::uint8_t* p01 = bgr + ty.i0 * stride + tx.i1 * 3;
      const std::uint8_t* p10 = bgr + ty.i1 * stride + tx.i0 * 3;
      const std::uint8_t* p11 = bgr + ty.i1 * stride + tx.i1 * 3;
      float c[3];
      for (int k = 0; k < 3; ++k) {
        const float top = p00[k] + (p01[k] - p00[k]) * tx.w;
        const float bot = p10[k] + (p11[k] - p10[k]) * tx.w;
        c[k] = (top + (bot - top) * ty.w) / 255.0f;
      }
      b[o] = c[0];
      g[o] = c[1];
      r[o] = c[2];
    }
  }
}

void OnnxDetector::Impl::crops(const std::uint8_t* bgr, int stride) noexcept {
  constexpr int plane = kCrop * kCrop;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const CropPoint& p = pts[i];
    float* out = cls_in.data() + i * 3 * plane;
    const float x0 = p.u - p.side / 2, y0 = p.v - p.side / 2, step = p.side / kCrop;
    for (int y = 0; y < kCrop; ++y) {
      const Tap ty = tap(y0 + (static_cast<float>(y) + 0.5f) * step - 0.5f, fh);
      for (int x = 0; x < kCrop; ++x) {
        const Tap tx = tap(x0 + (static_cast<float>(x) + 0.5f) * step - 0.5f, fw);
        const std::uint8_t* p00 = bgr + ty.i0 * stride + tx.i0 * 3;
        const std::uint8_t* p01 = bgr + ty.i0 * stride + tx.i1 * 3;
        const std::uint8_t* p10 = bgr + ty.i1 * stride + tx.i0 * 3;
        const std::uint8_t* p11 = bgr + ty.i1 * stride + tx.i1 * 3;
        for (int k = 0; k < 3; ++k) {                 // BGR in, RGB planes out, ImageNet-normalised
          const float top = p00[2 - k] + (p01[2 - k] - p00[2 - k]) * tx.w;
          const float bot = p10[2 - k] + (p11[2 - k] - p10[2 - k]) * tx.w;
          out[k * plane + y * kCrop + x] = ((top + (bot - top) * ty.w) / 255.0f - kMean[static_cast<std::size_t>(k)]) /
                                           kStd[static_cast<std::size_t>(k)];
        }
      }
    }
  }
}

void OnnxDetector::Impl::decode(StateVector& sv) noexcept {
  // detector: [1, 4 + classes, anchors], centre-size boxes in letterbox pixels, class scores already sigmoid
  int n = 0;
  const float* o = det_out.data();
  for (int a = 0; a < n_anchor && n < kMaxCandidates; ++a) {
    int best = -1;
    float s = cfg.det_conf;
    for (int c = 0; c < n_class; ++c) {
      const float v = o[(4 + c) * n_anchor + a];
      if (v > s) {
        s = v;
        best = c;
      }
    }
    if (best < 0) continue;
    const float cx = o[a], cy = o[n_anchor + a], w = o[2 * n_anchor + a], h = o[3 * n_anchor + a];
    cand[static_cast<std::size_t>(n++)] = {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, s, best};
  }
  const int k = nms(cand.data(), n, cfg.det_iou, keep.data(), kMaxKeep);
  std::uint16_t hands = 0, tools = 0;
  for (int i = 0; i < k; ++i) (keep[static_cast<std::size_t>(i)].cls == 0 ? hands : tools)++;
  sv.hands = hands;
  sv.tools = tools;

  // classifier: [N, states + 1] probabilities; the last column is `occluded`
  sv.n_ips = static_cast<std::uint16_t>(pts.size());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const float* pr = cls_out.data() + i * static_cast<std::size_t>(n_out_states);
    const int ns = std::min<int>(pts[i].n_states, n_out_states - 1);
    int arg = 0;
    for (int s = 1; s < ns; ++s)
      if (pr[s] > pr[arg]) arg = s;
    IpObservation& ob = sv.ips[i];
    ob.u = pts[i].u;
    ob.v = pts[i].v;
    if (pr[n_out_states - 1] > pr[arg]) {             // a glove or tool over the face: no answer
      ob.rank = -1;
      ob.flags = kOccluded;
      ob.conf = 0;
    } else {
      ob.rank = static_cast<std::int8_t>(arg);
      ob.flags = kVisible;
      ob.conf = pr[arg];
    }
  }
}

OnnxDetector::OnnxDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxDetector::~OnnxDetector() = default;

std::unique_ptr<OnnxDetector> OnnxDetector::create(const OnnxConfig& cfg, std::vector<CropPoint> points, int frame_w,
                                                   int frame_h, std::string& error) {
  auto im = std::make_unique<Impl>();
  im->cfg = cfg;
  im->pts = std::move(points);
  im->fw = frame_w;
  im->fh = frame_h;
  if (im->pts.empty() || im->pts.size() > kMaxIps) {
    error = "the profile has no inspection points, or more than the engine's limit";
    return nullptr;
  }
  try {
    Ort::SessionOptions det_so, cls_so;
    for (Ort::SessionOptions* so : {&det_so, &cls_so}) {
      so->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      so->SetIntraOpNumThreads(1);                    // the inference thread owns these sessions
    }
    im->append_providers(det_so, true);
    im->append_providers(cls_so, false);
    im->det = Ort::Session(im->env, cfg.detector_model.c_str(), det_so);
    im->cls = Ort::Session(im->env, cfg.classifier_model.c_str(), cls_so);
  } catch (const Ort::Exception& e) {
    // Typical here: the CUDA 12 ORT asset, cudnn64_9.dll missing from PATH, or the System32 copy of
    // onnxruntime.dll loaded ahead of ours (the build copies ours beside torquesense.exe).
    error = std::string("ONNX Runtime ") + im->provider + " provider failed: " + e.what() +
            " — run scripts\\gpu_preflight.py and check `where cudnn64_9.dll`";
    return nullptr;
  }

  // shapes, from the models themselves
  const auto dshape = im->det.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
  if (dshape.size() != 3 || dshape[1] < 5) {
    error = "detector output is not [1, 4 + classes, anchors]";
    return nullptr;
  }
  im->n_class = static_cast<int>(dshape[1] - 4);
  im->n_anchor = static_cast<int>(dshape[2]);
  const auto cshape = im->cls.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
  im->n_out_states = static_cast<int>(cshape.back());
  const auto n = static_cast<std::int64_t>(im->pts.size());

  im->det_in.assign(3 * kSide * kSide, kPad);
  im->det_out.assign(static_cast<std::size_t>(dshape[1] * dshape[2]), 0.0f);
  im->cls_in.assign(static_cast<std::size_t>(n) * 3 * kCrop * kCrop, 0.0f);
  im->cls_out.assign(static_cast<std::size_t>(n * im->n_out_states), 0.0f);
  im->cand.resize(kMaxCandidates);
  im->keep.resize(kMaxKeep);

  im->lb = letterbox_for(frame_w, frame_h, kSide);
  im->lb_x0 = static_cast<int>(std::lround(im->lb.pad_x));
  im->lb_y0 = static_cast<int>(std::lround(im->lb.pad_y));
  im->lb_x1 = kSide - im->lb_x0;
  im->lb_y1 = kSide - im->lb_y0;
  for (int x = im->lb_x0; x < im->lb_x1; ++x) im->lx.push_back(tap((static_cast<float>(x - im->lb_x0) + 0.5f) / im->lb.scale - 0.5f, frame_w));
  for (int y = im->lb_y0; y < im->lb_y1; ++y) im->ly.push_back(tap((static_cast<float>(y - im->lb_y0) + 0.5f) / im->lb.scale - 0.5f, frame_h));

  try {
    Ort::AllocatorWithDefaultOptions alloc;
    const std::array<std::int64_t, 4> din{1, 3, kSide, kSide};
    const std::array<std::int64_t, 4> cin{n, 3, kCrop, kCrop};
    const std::array<std::int64_t, 2> cout{n, im->n_out_states};
    im->det_in_name = im->det.GetInputNameAllocated(0, alloc).get();
    im->det_out_name = im->det.GetOutputNameAllocated(0, alloc).get();
    im->cls_in_name = im->cls.GetInputNameAllocated(0, alloc).get();
    im->cls_out_name = im->cls.GetOutputNameAllocated(0, alloc).get();
    im->det_in_c = im->det_in_name.c_str();
    im->det_out_c = im->det_out_name.c_str();
    im->cls_in_c = im->cls_in_name.c_str();
    im->cls_out_c = im->cls_out_name.c_str();
    im->det_in_v = Ort::Value::CreateTensor<float>(im->mem, im->det_in.data(), im->det_in.size(), din.data(), din.size());
    im->det_out_v = Ort::Value::CreateTensor<float>(im->mem, im->det_out.data(), im->det_out.size(), dshape.data(), dshape.size());
    im->cls_in_v = Ort::Value::CreateTensor<float>(im->mem, im->cls_in.data(), im->cls_in.size(), cin.data(), cin.size());
    im->cls_out_v = Ort::Value::CreateTensor<float>(im->mem, im->cls_out.data(), im->cls_out.size(), cout.data(), cout.size());

    // Warm-up: the first runs pay for cuDNN's exhaustive algorithm search (and, under TensorRT, the
    // engine load). They must never land in a latency histogram.
    for (int i = 0; i < std::max(1, cfg.warmup_runs); ++i) {
      im->run_det();
      im->run_cls();
    }
    std::array<double, 10> t{};
    for (double& v : t) {
      const auto t0 = std::chrono::steady_clock::now();
      im->run_det();
      v = ms_since(t0);
    }
    std::sort(t.begin(), t.end());
    im->warm_ms = (t[4] + t[5]) / 2;
  } catch (const Ort::Exception& e) {
    error = std::string("warm-up failed: ") + e.what();
    return nullptr;
  }
  if (im->warm_ms > cfg.max_median_ms) {
    error = "median detector inference " + std::to_string(im->warm_ms) + " ms after warm-up — nodes are probably " +
            "running on the CPU, or the GPU is throttled";
    return nullptr;
  }
  return std::unique_ptr<OnnxDetector>(new OnnxDetector(std::move(im)));
}

void OnnxDetector::infer(const std::uint8_t* bgr, int stride, StateVector& sv) noexcept {
  Impl& m = *impl_;
  auto t0 = std::chrono::steady_clock::now();
  m.letterbox(bgr, stride);
  try {
    m.run_det();
  } catch (const Ort::Exception&) {
    sv.hands = sv.tools = 0;
  }
  m.det_ms = ms_since(t0);
  t0 = std::chrono::steady_clock::now();
  m.crops(bgr, stride);
  try {
    m.run_cls();
  } catch (const Ort::Exception&) {
    for (std::size_t i = 0; i < m.pts.size(); ++i) sv.ips[i] = IpObservation{};   // no answer this frame
    sv.n_ips = static_cast<std::uint16_t>(m.pts.size());
    return;
  }
  m.cls_ms = ms_since(t0);
  m.decode(sv);
  sv.anchor_residual_px = 0;   // fixed registration: no fiducials on this station (Section 9)
}

const char* OnnxDetector::provider() const noexcept { return impl_->provider.c_str(); }
bool OnnxDetector::degraded() const noexcept { return impl_->degraded; }
double OnnxDetector::warm_median_ms() const noexcept { return impl_->warm_ms; }
double OnnxDetector::last_detector_ms() const noexcept { return impl_->det_ms; }
double OnnxDetector::last_classifier_ms() const noexcept { return impl_->cls_ms; }

}  // namespace sunverify::perception
