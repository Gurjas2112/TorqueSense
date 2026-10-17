// The GPU detector must see each frame it is given. A detector that answers every frame from the first
// one (inputs copied to the device once, then rewritten in place) passed a latency gate on timers alone,
// so this checks the answers move with the pixels. CUDA build only; skips when the models are absent.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "sunverify/perception/OnnxDetector.hpp"

namespace sp = sunverify::perception;
namespace fs = std::filesystem;

namespace {

fs::path repo_file(const char* rel) {
  for (fs::path d = fs::current_path(); !d.empty(); d = d.parent_path()) {
    if (fs::exists(d / rel)) return d / rel;
    if (d == d.parent_path()) break;
  }
  return {};
}

}  // namespace

TEST(OnnxDetectorGpu, AnswersFollowThePixels) {
  const fs::path det = repo_file("models/yolo11n_wheel.onnx"), cls = repo_file("models/threaded_fastener.onnx");
  if (det.empty() || cls.empty()) GTEST_SKIP() << "trained models not present (dvc pull)";
  constexpr int kW = 320, kH = 180;
  sp::OnnxConfig cfg;
  cfg.detector_model = det;
  cfg.classifier_model = cls;
  cfg.warmup_runs = 2;
  cfg.max_median_ms = 1000;
  std::vector<sp::CropPoint> pts(2);
  pts[0] = {80, 90, 60, 3};
  pts[1] = {240, 90, 60, 3};
  std::string why;
  auto d = sp::OnnxDetector::create(cfg, pts, kW, kH, why);
  ASSERT_TRUE(d) << why;

  // left half black, right half white; then the reverse. Each point's answer must change with its pixels.
  std::vector<std::uint8_t> a(static_cast<std::size_t>(kW) * kH * 3), b(a.size());
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x)
      for (int k = 0; k < 3; ++k) {
        const auto i = static_cast<std::size_t>((y * kW + x) * 3 + k);
        a[i] = x < kW / 2 ? 0 : 255;
        b[i] = x < kW / 2 ? 255 : 0;
      }
  auto sa = std::make_unique<sp::StateVector>(), sb = std::make_unique<sp::StateVector>();
  d->infer(a.data(), kW * 3, *sa);
  d->infer(b.data(), kW * 3, *sb);
  ASSERT_EQ(sa->n_ips, 2);
  // the same crop content gives the same answer: point 0 on frame a == point 1 on frame b, and vice versa
  EXPECT_EQ(sa->ips[0].rank, sb->ips[1].rank);
  EXPECT_NEAR(sa->ips[0].conf, sb->ips[1].conf, 1e-3);
  EXPECT_EQ(sa->ips[1].rank, sb->ips[0].rank);
  EXPECT_NEAR(sa->ips[1].conf, sb->ips[0].conf, 1e-3);
  // and black and white crops are told apart at all
  EXPECT_TRUE(sa->ips[0].rank != sa->ips[1].rank || std::abs(sa->ips[0].conf - sa->ips[1].conf) > 1e-3);
}
