#include "sunverify/procedure/Describe.hpp"

#include <cctype>
#include <cmath>

namespace sunverify::procedure {
namespace {

std::vector<std::string> mask_targets(const Profile& p, int step, std::uint64_t mask) {
  std::vector<std::string> out;
  const auto& t = p.steps()[step].targets;
  for (std::size_t k = 0; k < t.size(); ++k)
    if (mask & (1ULL << k)) out.push_back(p.ips()[t[k]].id);
  return out;
}

std::string join_names(const std::vector<std::string>& ids, const char* sep) {
  std::string s;
  for (std::size_t i = 0; i < ids.size(); ++i) s += (i ? sep : "") + display_name(ids[i], false);
  return s;
}

// "bolt-1 bolt-3 bolt-5" -> "1 → 3 → 5" when every id shares its group, else the names.
std::string order_line(const std::vector<std::string>& ids) {
  std::string group, out;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto dash = ids[i].rfind('-');
    const std::string g = dash == std::string::npos ? ids[i] : ids[i].substr(0, dash);
    if (i == 0) group = g;
    if (g != group || dash == std::string::npos) return join_names(ids, " → ");
    out += (i ? " → " : "") + ids[i].substr(dash + 1);
  }
  return out;
}

}  // namespace

std::string display_name(const std::string& id, bool capital) {
  std::string s = id;
  const auto dash = s.rfind('-');
  if (dash != std::string::npos && dash + 1 < s.size() &&
      std::isdigit(static_cast<unsigned char>(s[dash + 1])))
    s = s.substr(0, dash) + " " + s.substr(dash + 1);
  for (auto& c : s)
    if (c == '_' || c == '-') c = ' ';
  if (capital && !s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

nlohmann::json describe(const Profile& p, const Violation& v) {
  nlohmann::json j = {{"kind", to_string(v.kind)}, {"step", p.steps()[v.step].id}, {"t_ms", v.t_ms}};
  const Step& st = p.steps()[v.step];
  switch (v.kind) {
    case ViolationKind::kRegression:
      j["ip"] = p.ips()[v.ip].id;
      j["now"] = p.ips()[v.ip].states[v.now_rank];
      break;
    case ViolationKind::kPrecedence: {
      j["ip"] = p.ips()[v.ip].id;
      auto before = nlohmann::json::array();
      for (const auto r : st.requires_)
        if (v.before & (1ULL << r)) before.push_back(p.steps()[r].id);
      j["before"] = before;
      break;
    }
    case ViolationKind::kSequence:
      j["observed"] = p.ips()[v.ip].id;
      j["expected"] = p.ips()[v.expected].id;
      break;
    case ViolationKind::kEvidenceMissing:
      j["ip"] = p.ips()[v.ip].id;
      j["signal"] = p.tags()[st.evidence.tag];
      break;
    case ViolationKind::kStepTimeout:
    case ViolationKind::kIncompleteAtRelease:
      j["missing"] = mask_targets(p, v.step, v.missing);
      break;
  }
  return j;
}

std::string narrate(const Profile& p, const Violation& v) {
  const Step& st = p.steps()[v.step];
  std::vector<std::string> order;
  for (const auto t : st.targets) order.push_back(p.ips()[t].id);
  const std::string step = display_name(st.id, false);
  switch (v.kind) {
    case ViolationKind::kSequence:
      return display_name(p.ips()[v.ip].id) + " " + st.state + " before " + display_name(p.ips()[v.expected].id, false) +
             ". Expected " + order_line(order) + ".";
    case ViolationKind::kPrecedence: {
      std::string before;
      for (const auto r : st.requires_)
        if (v.before & (1ULL << r)) before += (before.empty() ? "" : ", ") + display_name(p.steps()[r].id, false);
      return display_name(p.ips()[v.ip].id) + " " + st.state + " before step '" + before +
             "' was complete. That step must be finished first.";
    }
    case ViolationKind::kEvidenceMissing:
      return display_name(p.ips()[v.ip].id) + " looks " + st.state + ", but " + p.tags()[st.evidence.tag] +
             " did not confirm it within " + std::to_string(st.evidence.within_ms) + " ms.";
    case ViolationKind::kRegression:
      return display_name(p.ips()[v.ip].id) + " fell back to " + p.ips()[v.ip].states[v.now_rank] + " after reaching " +
             st.state + ". Step '" + step + "' must be redone" +
             (st.kind == StepKind::kSequence ? ", in order: " + order_line(order) + "." : ".");
    case ViolationKind::kStepTimeout:
      return "Step '" + step + "' ran past its " +
             std::to_string(static_cast<long long>(std::llround(st.max_duration_ms / 1000.0))) +
             " s deadline. Still missing: " + join_names(mask_targets(p, v.step, v.missing), ", ") + ".";
    case ViolationKind::kIncompleteAtRelease:
      return "Release requested with step '" + step + "' unfinished. Missing: " +
             join_names(mask_targets(p, v.step, v.missing), ", ") + ". The part is held.";
  }
  return to_string(v.kind);
}

std::vector<std::string> corrective_order(const Profile& p, const ProcedureEngine& e, int step) {
  std::vector<std::string> out;
  const Step& st = p.steps()[step];
  std::uint64_t done = 0;
  for (int i = 0; i < e.step_done_count(step); ++i) done |= 1ULL << e.step_done_at(step, i);
  for (std::size_t k = 0; k < st.targets.size(); ++k)
    if (!(done & (1ULL << k))) out.push_back(p.ips()[st.targets[k]].id);
  return out;
}

}  // namespace sunverify::procedure
