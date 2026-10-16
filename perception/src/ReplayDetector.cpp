#include "sunverify/perception/Detector.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace sunverify::perception {

ReplayDetector::ReplayDetector(std::vector<std::string> ip_ids, std::vector<float> u, std::vector<float> v)
    : ids_(std::move(ip_ids)), u_(std::move(u)), v_(std::move(v)), held_(ids_.size()) {
  if (ids_.size() > kMaxIps) throw std::invalid_argument("ReplayDetector: more than 256 inspection points");
}

std::size_t ReplayDetector::load_dir(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator(dir)) {
    const auto name = e.path().filename().string();
    if (e.path().extension() == ".json" && name.find(".expected.") == std::string::npos) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    std::ifstream in(f);
    const auto j = nlohmann::json::parse(in);
    ReplayScenario s;
    s.name = j.at("scenario").get<std::string>();
    s.title = j.value("title", s.name);
    s.fps = j.value("fps", 30.0);
    s.n_frames = j.at("n_frames").get<std::uint64_t>();
    s.release_at_ms = j.at("release_at_ms").get<std::int64_t>();
    s.anchor_residual_px = j.value("anchor_residual_px", 1.0f);
    const auto fixture_ips = j.at("ips").get<std::vector<std::string>>();
    std::vector<int> map(fixture_ips.size(), -1);
    for (std::size_t i = 0; i < fixture_ips.size(); ++i) {
      const auto it = std::find(ids_.begin(), ids_.end(), fixture_ips[i]);
      if (it == ids_.end()) throw std::runtime_error(f.string() + ": " + fixture_ips[i] + " is not in the profile");
      map[i] = static_cast<int>(it - ids_.begin());
    }
    for (const auto& k : j.at("keyframes"))
      s.keys.push_back({k.at("frame").get<std::uint64_t>(), static_cast<std::uint16_t>(map[k.at("ip").get<int>()]),
                        static_cast<std::int8_t>(k.at("rank").get<int>()), static_cast<std::uint8_t>(k.at("flags").get<int>()),
                        k.at("conf").get<float>()});
    std::stable_sort(s.keys.begin(), s.keys.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
    for (const auto& h : j.value("hands", nlohmann::json::array()))
      s.hands.push_back({h.at("from_frame").get<std::uint64_t>(), h.at("to_frame").get<std::uint64_t>(),
                         static_cast<std::uint16_t>(h.at("count").get<int>())});
    for (const auto& t : j.value("tags", nlohmann::json::array()))
      s.tags.push_back({t.at("t_ms").get<std::int64_t>(), t.at("tag").get<std::string>(), t.at("value").get<bool>()});
    auto exp = f;
    exp.replace_filename(s.name + ".expected.json");
    if (std::filesystem::exists(exp)) {
      std::ifstream ein(exp);
      s.must_alert = nlohmann::json::parse(ein).value("must_alert", false);
    }
    scenarios_.push_back(std::move(s));
  }
  return scenarios_.size();
}

int ReplayDetector::find(const std::string& name) const noexcept {
  for (std::size_t i = 0; i < scenarios_.size(); ++i)
    if (scenarios_[i].name == name) return static_cast<int>(i);
  return -1;
}

void ReplayDetector::select(int scenario) noexcept {
  current_ = scenario;
  cursor_ = 0;
  last_frame_ = 0;
  for (auto& h : held_) h = IpObservation{};
}

void ReplayDetector::infer(std::uint64_t frame, StateVector& sv) noexcept {
  sv.n_ips = static_cast<std::uint16_t>(ids_.size());
  sv.hands = sv.tools = 0;
  if (current_ < 0) {
    for (std::size_t i = 0; i < ids_.size(); ++i) sv.ips[i] = IpObservation{};
    return;
  }
  const ReplayScenario& s = scenarios_[static_cast<std::size_t>(current_)];
  if (frame < last_frame_) {                       // a rewind: start the keyframes again
    cursor_ = 0;
    for (auto& h : held_) h = IpObservation{};
  }
  last_frame_ = frame;
  while (cursor_ < s.keys.size() && s.keys[cursor_].frame <= frame) {
    const auto& k = s.keys[cursor_++];
    held_[k.ip].rank = k.rank;
    held_[k.ip].flags = k.flags;
    held_[k.ip].conf = k.conf;
  }
  for (std::size_t i = 0; i < ids_.size(); ++i) {
    sv.ips[i] = held_[i];
    sv.ips[i].u = u_[i];
    sv.ips[i].v = v_[i];
  }
  for (const auto& h : s.hands)
    if (frame >= h.from && frame < h.to) sv.hands = static_cast<std::uint16_t>(sv.hands > h.count ? sv.hands : h.count);
  sv.tools = sv.hands ? 1 : 0;
  sv.anchor_residual_px = s.anchor_residual_px;
}

}  // namespace sunverify::perception
