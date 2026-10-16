#pragma once
// Perception behind one interface (spec Section 9): the inference thread asks a detector to fill one
// frame's StateVector. YoloOnnxDetector (SUNVERIFY_WITH_CUDA) runs the models; ReplayDetector replays
// recorded detector output, for the sanitizer builds, CI, and every run before a model is trained.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "sunverify/perception/StateVector.hpp"

namespace sunverify::perception {

class IDetector {
 public:
  virtual ~IDetector() = default;
  // Fill sv.ips[0..n) for frame `frame_in_cycle` of the current scenario. No allocation (I1).
  virtual void infer(std::uint64_t frame_in_cycle, StateVector& sv) noexcept = 0;
  virtual const char* provider() const noexcept = 0;   // tensorrt | cuda | replay
};

// One recorded scenario: per-frame detector output as keyframes (tests/fixtures/detections/*.json,
// written by synth/scenarios.py). An observation holds until the point's next keyframe.
struct ReplayScenario {
  std::string name, title;
  double fps = 30;
  std::uint64_t n_frames = 0;
  std::int64_t release_at_ms = 0;
  float anchor_residual_px = 0;
  struct Key {
    std::uint64_t frame;
    std::uint16_t ip;     // profile inspection-point index
    std::int8_t rank;
    std::uint8_t flags;
    float conf;
  };
  std::vector<Key> keys;                      // sorted by frame
  struct Hands {
    std::uint64_t from, to;
    std::uint16_t count;
  };
  std::vector<Hands> hands;
  struct Tag {
    std::int64_t t_ms;
    std::string tag;
    bool value;
  };
  std::vector<Tag> tags;                      // plant signals the line produces during the scenario
  bool must_alert = false;
};

class ReplayDetector final : public IDetector {
 public:
  // ip_ids: the profile's inspection points, in profile order; a fixture's points are mapped onto them.
  ReplayDetector(std::vector<std::string> ip_ids, std::vector<float> u, std::vector<float> v);

  // Loads <dir>/<name>.json (and its .expected.json for must_alert) for every scenario in the folder.
  std::size_t load_dir(const std::filesystem::path& dir);
  const std::vector<ReplayScenario>& scenarios() const noexcept { return scenarios_; }
  int find(const std::string& name) const noexcept;

  // Selects the scenario the next frames come from (the cycle boundary). Index change only.
  void select(int scenario) noexcept;
  int selected() const noexcept { return current_; }

  void infer(std::uint64_t frame_in_cycle, StateVector& sv) noexcept override;
  const char* provider() const noexcept override { return "replay"; }

 private:
  std::vector<std::string> ids_;
  std::vector<float> u_, v_;
  std::vector<ReplayScenario> scenarios_;
  std::vector<IpObservation> held_;           // per point: the observation in force
  int current_ = -1;
  std::size_t cursor_ = 0;
  std::uint64_t last_frame_ = 0;
};

}  // namespace sunverify::perception
