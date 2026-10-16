// A build without ONNX Runtime (SUNVERIFY_WITH_CUDA off): the GPU detector exists as a type, so the
// engine links, and create() says why it cannot run. The replay and schematic providers are unaffected.
#include "sunverify/perception/OnnxDetector.hpp"

namespace sunverify::perception {

struct OnnxDetector::Impl {};

OnnxDetector::OnnxDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxDetector::~OnnxDetector() = default;

std::unique_ptr<OnnxDetector> OnnxDetector::create(const OnnxConfig&, std::vector<CropPoint>, int, int, std::string& error) {
  error = "this build has no ONNX Runtime: configure with the windows-release preset (SUNVERIFY_WITH_CUDA=ON, ORT_ROOT set)";
  return nullptr;
}

void OnnxDetector::infer(const std::uint8_t*, int, StateVector&) noexcept {}
const char* OnnxDetector::provider() const noexcept { return "none"; }
bool OnnxDetector::degraded() const noexcept { return false; }
double OnnxDetector::warm_median_ms() const noexcept { return 0; }
double OnnxDetector::last_detector_ms() const noexcept { return 0; }
double OnnxDetector::last_classifier_ms() const noexcept { return 0; }

}  // namespace sunverify::perception
