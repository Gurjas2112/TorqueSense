// cad-extract — compile a profile folder into procedure.json and coverage.json (spec Section 7).
//
//   cad-extract samples\wheel-hub-5lug                 write both files beside profile.yaml
//   cad-extract samples\wheel-hub-5lug --check         compile and compare with the committed files
//   cad-extract <dir> --out <dir> --min-px 16
//
// Exit 0: compiled, every point resolved. Exit 3: refused — coverage.json names the unresolved
// points and procedure.json is not written (I17). Exit 4 (--check): the committed files are stale.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "sunverify/cad/Compiler.hpp"
#include "sunverify/cad/StepReader.hpp"

namespace fs = std::filesystem;
using sunverify::cad::Compiled;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string s = ss.str();
  std::erase(s, '\r');            // compare text, not the checkout's line endings
  return s;
}

void write(const fs::path& p, const nlohmann::json& j) {
  std::ofstream f(p, std::ios::binary);
  f << j.dump(2) << '\n';
}

int usage() {
  std::fprintf(stderr, "usage: cad-extract <profile-dir> [--out <dir>] [--min-px 16] [--check]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  fs::path dir = argv[1], out;
  double min_px = 16.0;
  bool check = false;
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!std::strcmp(argv[i], "--min-px") && i + 1 < argc) min_px = std::stod(argv[++i]);
    else if (!std::strcmp(argv[i], "--check")) check = true;
    else return usage();
  }
  if (out.empty()) out = dir;

  Compiled c;
  try {
    c = sunverify::cad::compile_folder(dir, min_px);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "cad-extract: %s: %s\n", dir.string().c_str(), e.what());
    return 1;
  }

  const std::string proc = c.procedure.dump(2) + "\n", cov = c.coverage.dump(2) + "\n";
  if (check) {
    const bool same = slurp(dir / "procedure.json") == proc && slurp(dir / "coverage.json") == cov;
    std::printf("%s %s\n", dir.filename().string().c_str(), same ? "up to date" : "STALE: re-run cad-extract");
    return same ? 0 : 4;
  }

  fs::create_directories(out);
  write(out / "coverage.json", c.coverage);
  std::printf("%s: %zu inspection points, OCCT %s\n", c.procedure["profile"].get<std::string>().c_str(),
              c.rows.size(), sunverify::cad::step_support_built() ? "built in" : "not built in");
  for (const auto& s : c.procedure["steps"])
    if (s.contains("pattern")) {
      std::string order;
      for (const auto& t : s["targets"]) order += (order.empty() ? "" : ",") + t.get<std::string>();
      std::printf("  %s: %s order %s\n", s["id"].get<std::string>().c_str(), s["pattern"].get<std::string>().c_str(),
                  order.c_str());
    }
  if (!c.ok()) {
    std::fprintf(stderr, "REFUSED (I17): unresolved inspection points:");
    for (const auto& r : c.rows)
      if (!r.resolved)
        std::fprintf(stderr, " %s %.1f px on %s (%s);", r.ip.c_str(), r.footprint.feature_px, r.footprint.camera.c_str(),
                     r.occlusion.c_str());
    std::fprintf(stderr, "\nprocedure.json not written; see coverage.json\n");
    return 3;
  }
  write(out / "procedure.json", c.procedure);
  std::printf("  coverage clean: every point >= %.0f px on some camera\n", min_px);
  return 0;
}
