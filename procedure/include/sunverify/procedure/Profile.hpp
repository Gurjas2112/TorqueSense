#pragma once
// A compiled profile (procedure.json) as flat, immutable tables. Built once at load, with
// nlohmann/json, off the hot path. Everything the interpreter touches per event is an index into
// these tables; nothing is looked up by name after load.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace sunverify::procedure {

inline constexpr std::size_t kMaxIps = 256;
inline constexpr std::size_t kMaxSteps = 64;
inline constexpr std::size_t kMaxTargets = 64;

struct ProfileError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// A plant signal's value. Bools and numbers compare as numbers (true == 1), as the reference does.
struct TagValue {
  enum class Kind : std::uint8_t { kNone, kNumber, kString };
  Kind kind = Kind::kNone;
  double number = 0.0;
  std::int32_t string_id = -1;    // Profile::string_id(); -1 = a string the profile never mentions

  static TagValue of(double v) noexcept { return {Kind::kNumber, v, -1}; }
  static TagValue of_bool(bool b) noexcept { return {Kind::kNumber, b ? 1.0 : 0.0, -1}; }
  static TagValue of_string(std::int32_t id) noexcept { return {Kind::kString, 0.0, id}; }
  friend bool operator==(const TagValue& a, const TagValue& b) noexcept {
    if (a.kind != b.kind) return false;
    if (a.kind == Kind::kNumber) return a.number == b.number;
    if (a.kind == Kind::kString) return a.string_id >= 0 && a.string_id == b.string_id;
    return true;
  }
};

struct InspectionPoint {
  std::string id;
  std::string family;
  std::vector<std::string> states;    // ordered: reaching a later state implies the earlier ones
  std::array<double, 3> position_mm{};
  std::array<double, 3> normal{0, 0, 1};
  double feature_mm = 0;
  std::string camera;
  double feature_px = 0;
};

enum class StepKind : std::uint8_t { kComplete, kSequence };

struct Evidence {
  std::int32_t tag = -1;
  TagValue equals;
  std::int64_t within_ms = 0;
};

struct Step {
  std::string id;
  StepKind kind = StepKind::kComplete;
  std::string state;
  std::vector<std::int16_t> targets;     // inspection point indices, in the step's order
  std::vector<std::int8_t> need;         // per target: the rank of `state` in that point's family
  std::vector<std::int16_t> requires_;   // step indices
  std::uint64_t requires_mask = 0;
  std::int64_t max_duration_ms = 0;      // 0: no deadline of its own (the release gate still applies)
  bool has_evidence = false;
  Evidence evidence;
  std::string pattern;                   // circle | line | field, when the order was derived
  std::uint64_t all_targets_mask = 0;
};

// One step that targets an inspection point, and where that point sits in the step.
struct TargetRef {
  std::int16_t step;
  std::int8_t pos;      // index into Step::targets
  std::int8_t need;     // rank the step asks for
};

struct LoadOptions {
  bool require_deadlines = true;   // I15: the loader rejects a step without max_duration_s
};

class Profile {
 public:
  static Profile from_json(const nlohmann::json& procedure, const LoadOptions& opt = {});
  static Profile load(const std::filesystem::path& procedure_json, const LoadOptions& opt = {});

  const std::string& name() const noexcept { return name_; }
  const std::string& title() const noexcept { return title_; }
  const std::string& version() const noexcept { return version_; }
  const std::string& cad_revision() const noexcept { return cad_revision_; }

  const std::vector<InspectionPoint>& ips() const noexcept { return ips_; }
  const std::vector<Step>& steps() const noexcept { return steps_; }
  const std::vector<std::string>& tags() const noexcept { return tags_; }

  int ip_index(std::string_view id) const noexcept;
  int step_index(std::string_view id) const noexcept;
  int tag_index(std::string_view name) const noexcept;
  int rank(int ip, std::string_view state) const noexcept;      // -1 if not in the family
  std::int32_t string_id(std::string_view s) const noexcept;    // -1 if the profile never mentions it

  // Steps targeting ip, ordered by the rank they ask for (stable over profile order).
  const TargetRef* targets_of(int ip, std::size_t& n) const noexcept {
    n = by_target_offset_[ip + 1] - by_target_offset_[ip];
    return by_target_.data() + by_target_offset_[ip];
  }

 private:
  std::string name_, title_, version_, cad_revision_;
  std::vector<InspectionPoint> ips_;
  std::vector<Step> steps_;
  std::vector<std::string> tags_;
  std::vector<std::string> strings_;
  std::vector<TargetRef> by_target_;
  std::vector<std::size_t> by_target_offset_;
};

// A tag value from JSON as the reference compares it.
TagValue tag_value_from_json(const Profile& p, const nlohmann::json& v);

}  // namespace sunverify::procedure
