#pragma once
// The CAD compiler (spec Section 7): manifest + geometry -> procedure.json + coverage.json.
// Geometry comes from the STEP file when the profile has one (StepReader), otherwise from the
// manifest's measured positions. Intent always comes from the manifest. The compiler refuses as
// well as produces: a profile with an inspection point no camera can resolve is never deployable
// (I17). The C++ copy of reference/tsref/compiler.py.
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sunverify/cad/Optics.hpp"

namespace sunverify::cad {

struct CompileError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Member {
  Vec3 position_mm{};
  std::optional<Vec3> normal;
};

struct Group {
  std::string id;
  std::string family;
  std::vector<std::string> states;
  double feature_mm = 0;
  std::optional<Vec3> normal;
  std::string part_number;           // joins the group to its STEP occurrences, when there is a STEP file
  std::vector<Member> members;
};

struct StepSpec {
  std::string id;
  std::string group;
  std::string kind;                   // complete | sequence
  std::string state;
  bool order_derive = true;           // order: derive (the default), or an explicit list
  std::vector<int> order;             // 1-based member numbers when explicit
  std::vector<std::string> requires_;
  std::optional<double> max_duration_s;
  std::optional<nlohmann::json> evidence;   // {tag, equals, within_ms}
};

struct Manifest {
  std::string profile;
  std::string title;
  std::string geometry;               // cad | measured
  std::string cad_revision;
  std::vector<Camera> cameras;
  std::vector<Group> groups;
  std::vector<StepSpec> steps;
  nlohmann::json registration;
  nlohmann::json sources;
};

Manifest parse_manifest(const std::string& yaml_text);
Manifest load_manifest(const std::filesystem::path& profile_yaml);

struct CoverageRow {
  std::string ip;
  Footprint footprint;
  bool resolved = false;
  std::string occlusion = "not_run";  // not_run | clear | occluded:<part>
};

struct Compiled {
  nlohmann::json procedure;           // what the engine loads
  nlohmann::json coverage;            // every point, its best camera, size, framing and occlusion
  std::vector<CoverageRow> rows;
  std::vector<std::string> unresolved;
  bool ok() const { return unresolved.empty(); }
};

// Compile a manifest. profile_version is the profile folder's content hash (profile_hash()).
Compiled compile(const Manifest& m, double min_feature_px = 16.0, const std::string& profile_version = "");

// SHA-256 over the files that define a profile (profile.yaml, assembly.step, cameras.yaml), first
// 12 hex characters: the profile_version stamped on every anomaly row.
std::string profile_hash(const std::filesystem::path& profile_dir);

// Compile a profile folder: reads profile.yaml, joins assembly.step through StepReader when the
// profile says geometry: cad and the file exists.
Compiled compile_folder(const std::filesystem::path& profile_dir, double min_feature_px = 16.0);

}  // namespace sunverify::cad
