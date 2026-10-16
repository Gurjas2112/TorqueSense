#include "sunverify/procedure/Profile.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace sunverify::procedure {
namespace {

std::int32_t intern(std::vector<std::string>& pool, const std::string& s) {
  const auto it = std::find(pool.begin(), pool.end(), s);
  if (it != pool.end()) return static_cast<std::int32_t>(it - pool.begin());
  pool.push_back(s);
  return static_cast<std::int32_t>(pool.size() - 1);
}

}  // namespace

TagValue tag_value_from_json(const Profile& p, const nlohmann::json& v) {
  if (v.is_boolean()) return TagValue::of_bool(v.get<bool>());
  if (v.is_number()) return TagValue::of(v.get<double>());
  if (v.is_string()) return TagValue::of_string(p.string_id(v.get<std::string>()));
  return {};
}

Profile Profile::load(const std::filesystem::path& path, const LoadOptions& opt) {
  std::ifstream f(path);
  if (!f) throw ProfileError("cannot read " + path.string());
  try {
    return from_json(nlohmann::json::parse(f), opt);
  } catch (const nlohmann::json::exception& e) {
    throw ProfileError(path.string() + ": " + e.what());
  }
}

Profile Profile::from_json(const nlohmann::json& j, const LoadOptions& opt) {
  Profile p;
  p.name_ = j.at("profile").get<std::string>();
  p.title_ = j.value("title", "");
  p.version_ = j.value("profile_version", "");
  p.cad_revision_ = j.value("cad_revision", "");

  for (const auto& ip : j.at("inspection_points")) {
    InspectionPoint x;
    x.id = ip.at("id").get<std::string>();
    x.family = ip.value("family", "");
    x.states = ip.at("states").get<std::vector<std::string>>();
    if (ip.contains("position_mm")) {
      const auto pos = ip["position_mm"];
      for (std::size_t k = 0; k < 3 && k < pos.size(); ++k) x.position_mm[k] = pos[k].get<double>();
    }
    if (ip.contains("normal")) {
      const auto n = ip["normal"];
      for (std::size_t k = 0; k < 3 && k < n.size(); ++k) x.normal[k] = n[k].get<double>();
    }
    x.feature_mm = ip.value("feature_mm", 0.0);
    x.camera = ip.value("camera", "");
    x.feature_px = ip.value("feature_px", 0.0);
    if (x.states.empty() || x.states.size() > 127) throw ProfileError(x.id + ": a family needs 1 to 127 states");
    p.ips_.push_back(std::move(x));
  }
  if (p.ips_.size() > kMaxIps) throw ProfileError("more than 256 inspection points");

  const auto& steps = j.at("steps");
  if (steps.size() > kMaxSteps) throw ProfileError("more than 64 steps");
  for (const auto& s : steps) {
    Step st;
    st.id = s.at("id").get<std::string>();
    const std::string kind = s.at("kind").get<std::string>();
    if (kind != "complete" && kind != "sequence") throw ProfileError(st.id + ": kind must be complete or sequence");
    st.kind = kind == "sequence" ? StepKind::kSequence : StepKind::kComplete;
    st.state = s.at("state").get<std::string>();
    for (const auto& t : s.at("targets")) {
      const int ip = p.ip_index(t.get<std::string>());
      if (ip < 0) throw ProfileError(st.id + ": no inspection point " + t.get<std::string>());
      const int r = p.rank(ip, st.state);
      // a state outside the point's family can never be reached: fail at load, as the reference does
      if (r < 0) throw ProfileError(st.id + ": state '" + st.state + "' not in " + p.ips_[ip].id + "'s family");
      st.targets.push_back(static_cast<std::int16_t>(ip));
      st.need.push_back(static_cast<std::int8_t>(r));
    }
    if (st.targets.empty() || st.targets.size() > kMaxTargets) throw ProfileError(st.id + ": 1 to 64 targets");
    st.all_targets_mask = st.targets.size() == 64 ? ~0ULL : ((1ULL << st.targets.size()) - 1);
    if (s.contains("max_duration_s")) st.max_duration_ms = std::llround(s["max_duration_s"].get<double>() * 1000.0);
    if (opt.require_deadlines && st.max_duration_ms <= 0)
      throw ProfileError(st.id + ": every step needs max_duration_s — a skipped step is otherwise never caught (I15)");
    st.pattern = s.value("pattern", "");
    p.steps_.push_back(std::move(st));
  }

  // requires and evidence need every step id and the tag/string pools
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const auto& s = steps[i];
    Step& st = p.steps_[i];
    for (const auto& r : s.value("requires", nlohmann::json::array())) {
      const int k = p.step_index(r.get<std::string>());
      if (k < 0) throw ProfileError(st.id + ": requires unknown step " + r.get<std::string>());
      st.requires_.push_back(static_cast<std::int16_t>(k));
      st.requires_mask |= 1ULL << k;
    }
    if (s.contains("evidence") && !s["evidence"].is_null()) {
      const auto& e = s["evidence"];
      st.has_evidence = true;
      st.evidence.tag = intern(p.tags_, e.at("tag").get<std::string>());
      const auto& eq = e.at("equals");
      if (eq.is_string()) st.evidence.equals = TagValue::of_string(intern(p.strings_, eq.get<std::string>()));
      else if (eq.is_boolean()) st.evidence.equals = TagValue::of_bool(eq.get<bool>());
      else st.evidence.equals = TagValue::of(eq.get<double>());
      st.evidence.within_ms = e.at("within_ms").get<std::int64_t>();
    }
  }

  // ip -> the steps that target it, sorted by the rank each asks for (Python's stable sort, keyed on
  // the step's first target's rank — the same family for every target of a step)
  std::vector<std::vector<TargetRef>> lists(p.ips_.size());
  for (std::size_t s = 0; s < p.steps_.size(); ++s) {
    const Step& st = p.steps_[s];
    for (std::size_t k = 0; k < st.targets.size(); ++k)
      lists[st.targets[k]].push_back({static_cast<std::int16_t>(s), static_cast<std::int8_t>(k), st.need[k]});
  }
  p.by_target_offset_.push_back(0);
  for (auto& l : lists) {
    std::stable_sort(l.begin(), l.end(), [&](const TargetRef& a, const TargetRef& b) {
      return p.steps_[a.step].need[0] < p.steps_[b.step].need[0];
    });
    p.by_target_.insert(p.by_target_.end(), l.begin(), l.end());
    p.by_target_offset_.push_back(p.by_target_.size());
  }
  return p;
}

int Profile::ip_index(std::string_view id) const noexcept {
  for (std::size_t i = 0; i < ips_.size(); ++i)
    if (ips_[i].id == id) return static_cast<int>(i);
  return -1;
}

int Profile::step_index(std::string_view id) const noexcept {
  for (std::size_t i = 0; i < steps_.size(); ++i)
    if (steps_[i].id == id) return static_cast<int>(i);
  return -1;
}

int Profile::tag_index(std::string_view name) const noexcept {
  for (std::size_t i = 0; i < tags_.size(); ++i)
    if (tags_[i] == name) return static_cast<int>(i);
  return -1;
}

int Profile::rank(int ip, std::string_view state) const noexcept {
  const auto& s = ips_[ip].states;
  for (std::size_t i = 0; i < s.size(); ++i)
    if (s[i] == state) return static_cast<int>(i);
  return -1;
}

std::int32_t Profile::string_id(std::string_view s) const noexcept {
  for (std::size_t i = 0; i < strings_.size(); ++i)
    if (strings_[i] == s) return static_cast<std::int32_t>(i);
  return -1;
}

}  // namespace sunverify::procedure
