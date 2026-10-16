#include "sunverify/cad/Compiler.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <picosha2.h>
#include <yaml-cpp/yaml.h>

#include "sunverify/cad/Patterns.hpp"
#include "sunverify/cad/StepReader.hpp"

namespace sunverify::cad {
namespace {

Vec3 vec3(const YAML::Node& n, const char* what) {
  if (!n.IsSequence() || n.size() < 2 || n.size() > 3) throw CompileError(std::string(what) + " must be [x, y, z]");
  return {n[0].as<double>(), n[1].as<double>(), n.size() == 3 ? n[2].as<double>() : 0.0};
}

// A YAML scalar as the JSON value Python's yaml.safe_load would produce: bool, int, float or string.
nlohmann::json scalar(const YAML::Node& n) {
  const std::string& s = n.Scalar();
  if (n.Tag() == "!") return s;                                      // explicitly quoted: a string
  if (s == "true" || s == "True" || s == "TRUE") return true;
  if (s == "false" || s == "False" || s == "FALSE") return false;
  try {
    std::size_t used = 0;
    const long long i = std::stoll(s, &used);
    if (used == s.size()) return i;
  } catch (...) {
  }
  try {
    std::size_t used = 0;
    const double d = std::stod(s, &used);
    if (used == s.size()) return d;
  } catch (...) {
  }
  return s;
}

nlohmann::json to_json(const YAML::Node& n) {
  if (!n) return nullptr;
  if (n.IsScalar()) return scalar(n);
  if (n.IsSequence()) {
    auto a = nlohmann::json::array();
    for (const auto& x : n) a.push_back(to_json(x));
    return a;
  }
  if (n.IsMap()) {
    auto o = nlohmann::json::object();
    for (const auto& kv : n) o[kv.first.as<std::string>()] = to_json(kv.second);
    return o;
  }
  return nullptr;
}

double round1(double x) { return std::nearbyint(x * 10.0) / 10.0; }

nlohmann::json vec_json(const Vec3& v) { return nlohmann::json::array({v[0], v[1], v[2]}); }

std::string read_file(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw CompileError("cannot read " + p.string());
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

Manifest parse_manifest(const std::string& yaml_text) {
  YAML::Node y;
  try {
    y = YAML::Load(yaml_text);
  } catch (const YAML::Exception& e) {
    throw CompileError(std::string("profile.yaml: ") + e.what());
  }
  Manifest m;
  m.profile = y["profile"].as<std::string>("");
  if (m.profile.empty()) throw CompileError("profile.yaml: 'profile' is required");
  m.title = y["title"].as<std::string>("");
  m.geometry = y["geometry"].as<std::string>("measured");
  m.cad_revision = y["cad_revision"].as<std::string>("");
  m.registration = to_json(y["registration"]);
  m.sources = to_json(y["sources"]);

  for (const auto& c : y["cameras"]) {
    Camera cam;
    cam.id = c["id"].as<std::string>();
    cam.width_px = c["width_px"].as<int>();
    cam.hfov_deg = c["hfov_deg"].as<double>();
    cam.position_mm = vec3(c["position_mm"], "camera position_mm");
    cam.look_at_mm = vec3(c["look_at_mm"], "camera look_at_mm");
    if (c["aspect"]) cam.aspect = c["aspect"].as<double>();
    m.cameras.push_back(std::move(cam));
  }
  if (m.cameras.empty()) throw CompileError("profile.yaml: at least one camera is required");

  for (const auto& g : y["groups"]) {
    Group grp;
    grp.id = g["id"].as<std::string>();
    grp.family = g["family"].as<std::string>("");
    for (const auto& s : g["states"]) grp.states.push_back(s.as<std::string>());
    grp.feature_mm = g["feature_mm"].as<double>();
    if (g["normal"]) grp.normal = vec3(g["normal"], "group normal");
    grp.part_number = g["part_number"].as<std::string>("");
    for (const auto& mem : g["members"]) {
      Member mm;
      mm.position_mm = vec3(mem["position_mm"], "member position_mm");
      if (mem["normal"]) mm.normal = vec3(mem["normal"], "member normal");
      grp.members.push_back(mm);
    }
    m.groups.push_back(std::move(grp));
  }

  for (const auto& s : y["steps"]) {
    StepSpec st;
    st.id = s["id"].as<std::string>();
    st.group = s["group"].as<std::string>();
    st.kind = s["kind"].as<std::string>();
    st.state = s["state"].as<std::string>();
    if (st.kind != "complete" && st.kind != "sequence")
      throw CompileError("step " + st.id + ": kind must be complete or sequence, not " + st.kind);
    if (const auto o = s["order"]) {
      if (o.IsSequence()) {
        st.order_derive = false;
        for (const auto& x : o) st.order.push_back(x.as<int>());
      } else if (o.as<std::string>() != "derive") {
        throw CompileError("step " + st.id + ": order must be 'derive' or a list of member numbers");
      }
    }
    for (const auto& r : s["requires"]) st.requires_.push_back(r.as<std::string>());
    if (s["max_duration_s"]) st.max_duration_s = s["max_duration_s"].as<double>();
    if (s["evidence"]) st.evidence = to_json(s["evidence"]);
    m.steps.push_back(std::move(st));
  }
  return m;
}

Manifest load_manifest(const std::filesystem::path& profile_yaml) { return parse_manifest(read_file(profile_yaml)); }

Compiled compile(const Manifest& m, double min_feature_px, const std::string& profile_version) {
  Compiled out;
  auto ips = nlohmann::json::array();
  for (const auto& g : m.groups) {
    for (std::size_t i = 0; i < g.members.size(); ++i) {
      const Member& mem = g.members[i];
      const std::string id = g.id + "-" + std::to_string(i + 1);
      const Vec3 normal = mem.normal ? *mem.normal : (g.normal ? *g.normal : Vec3{0, 0, 1});
      Footprint fp = best(m.cameras, mem.position_mm, normal, g.feature_mm);
      const bool resolved = fp.feature_px >= min_feature_px;
      ips.push_back({{"id", id}, {"family", g.family}, {"states", g.states}, {"position_mm", vec_json(mem.position_mm)},
                     {"normal", vec_json(normal)}, {"feature_mm", g.feature_mm}, {"camera", fp.camera},
                     {"feature_px", round1(fp.feature_px)}});
      if (!resolved) out.unresolved.push_back(id);
      out.rows.push_back({id, std::move(fp), resolved, "not_run"});
    }
  }

  auto steps = nlohmann::json::array();
  for (const auto& st : m.steps) {
    const auto g = std::find_if(m.groups.begin(), m.groups.end(), [&](const Group& x) { return x.id == st.group; });
    if (g == m.groups.end()) throw CompileError("step " + st.id + ": no group " + st.group);
    std::vector<std::string> ids;
    for (std::size_t i = 1; i <= g->members.size(); ++i) ids.push_back(g->id + "-" + std::to_string(i));

    nlohmann::json step = {{"id", st.id}, {"kind", st.kind}, {"state", st.state}};
    if (!st.requires_.empty()) step["requires"] = st.requires_;
    if (st.max_duration_s) step["max_duration_s"] = *st.max_duration_s;
    if (st.evidence) step["evidence"] = *st.evidence;
    if (st.kind == "sequence" && st.order_derive) {
      std::vector<Point2> pts;
      for (const auto& mem : g->members) pts.push_back({mem.position_mm[0], mem.position_mm[1]});
      const auto [layout, order] = solve(pts);
      auto targets = nlohmann::json::array();
      for (int i : order) targets.push_back(ids[i]);
      step["targets"] = targets;
      step["pattern"] = to_string(layout);
      step["order_source"] = "derived";
    } else if (st.kind == "sequence") {
      auto targets = nlohmann::json::array();
      for (int k : st.order) {
        if (k < 1 || k > static_cast<int>(ids.size()))
          throw CompileError("step " + st.id + ": order names member " + std::to_string(k) + " of " +
                             std::to_string(ids.size()));
        targets.push_back(ids[k - 1]);
      }
      if (targets.size() != ids.size()) throw CompileError("step " + st.id + ": an explicit order must list every member once");
      step["targets"] = targets;
      step["order_source"] = "specified";
    } else {
      step["targets"] = ids;
    }
    steps.push_back(std::move(step));
  }

  auto cams = nlohmann::json::array();
  for (const auto& c : m.cameras)
    cams.push_back({{"id", c.id}, {"width_px", c.width_px}, {"hfov_deg", c.hfov_deg},
                    {"position_mm", vec_json(c.position_mm)}, {"look_at_mm", vec_json(c.look_at_mm)},
                    {"aspect", c.aspect}, {"f_px", c.f_px()}});

  out.procedure = {{"schema", "torquesense.procedure/1"},
                   {"profile", m.profile},
                   {"title", m.title},
                   {"profile_version", profile_version},
                   {"cad_revision", m.cad_revision.empty() ? m.geometry : m.cad_revision},
                   {"geometry", m.geometry},
                   {"cameras", cams},
                   {"inspection_points", ips},
                   {"steps", steps}};

  auto rows = nlohmann::json::array();
  for (const auto& r : out.rows)
    rows.push_back({{"ip", r.ip}, {"camera", r.footprint.camera}, {"feature_px", round1(r.footprint.feature_px)},
                    {"distance_mm", round1(r.footprint.distance_mm)}, {"obliquity_deg", round1(r.footprint.obliquity_deg)},
                    {"in_frame_and_facing", r.footprint.visible}, {"resolved", r.resolved}, {"occlusion", r.occlusion}});
  out.coverage = {{"profile", m.profile}, {"profile_version", profile_version}, {"min_feature_px", min_feature_px},
                  {"points", rows}, {"unresolved", out.unresolved}, {"ok", out.ok()}};
  return out;
}

std::string profile_hash(const std::filesystem::path& dir) {
  picosha2::hash256_one_by_one h;
  h.init();
  for (const char* name : {"profile.yaml", "assembly.step", "cameras.yaml"}) {
    const auto p = dir / name;
    if (!std::filesystem::exists(p)) continue;
    const std::string bytes = read_file(p);
    const std::string tag = std::string(name) + "\n";
    h.process(tag.begin(), tag.end());
    h.process(bytes.begin(), bytes.end());
  }
  h.finish();
  std::string hex;
  picosha2::get_hash_hex_string(h, hex);
  return hex.substr(0, 12);
}

Compiled compile_folder(const std::filesystem::path& dir, double min_feature_px) {
  Manifest m = load_manifest(dir / "profile.yaml");
  const auto step = dir / "assembly.step";
  std::vector<Occurrence> occurrences;
  const bool use_step = m.geometry == "cad" && std::filesystem::exists(step);
  if (use_step) {
    occurrences = read_step_occurrences(step);
    join_by_part_number(m, occurrences);              // positions and normals now come from CAD
  }
  Compiled c = compile(m, min_feature_px, profile_hash(dir));
  if (use_step) apply_occlusion(m, occurrences, c);
  return c;
}

}  // namespace sunverify::cad
