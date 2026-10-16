#include "SchematicLayout.hpp"

#include <array>
#include <cmath>
#include <map>
#include <string>

namespace sunverify::app {
namespace {
constexpr int kW = 960, kH = 540;
}
using nlohmann::json;

// synth/schematic.py's layout(): each camera a tile across the frame, every point at its CAD position
// projected through its own camera, scaled into that tile.
SchematicLayout schematic_layout(const json& proc) {
  const auto& cams = proc["cameras"];
  const double k = static_cast<double>(cams.size());
  const double tile_w = kW / k;
  std::map<std::string, std::array<double, 3>> origin;   // x0, y0, scale
  std::map<std::string, const json*> cam_of;
  for (std::size_t i = 0; i < cams.size(); ++i) {
    const auto& c = cams[i];
    const double s = tile_w / c["width_px"].get<double>();
    const double th = c["width_px"].get<double>() / c["aspect"].get<double>() * s;
    origin[c["id"]] = {static_cast<double>(i) * tile_w, (kH - th) / 2, s};
    cam_of[c["id"]] = &c;
  }
  SchematicLayout out;
  out.radius = std::max(5, static_cast<int>(9 / std::sqrt(k)));
  for (const auto& ip : proc["inspection_points"]) {
    const json& c = *cam_of.at(ip["camera"]);
    double P[3], L[3], X[3];
    for (int i = 0; i < 3; ++i) {
      P[i] = c["position_mm"][i].get<double>();
      L[i] = c["look_at_mm"][i].get<double>();
      X[i] = ip["position_mm"][i].get<double>();
    }
    double f[3] = {L[0] - P[0], L[1] - P[1], L[2] - P[2]};
    const double fn = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (double& v : f) v /= fn;
    const double up[3] = {0, std::abs(f[2]) < 0.99 ? 0.0 : 1.0, std::abs(f[2]) < 0.99 ? 1.0 : 0.0};
    double r[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    const double rn = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (double& v : r) v /= rn;
    const double u3[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const double d[3] = {X[0] - P[0], X[1] - P[1], X[2] - P[2]};
    const double z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    const double W = c["width_px"].get<double>(), H = W / c["aspect"].get<double>(), fp = c["f_px"].get<double>();
    const double u = W / 2 + fp * (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / z;
    const double v = H / 2 - fp * (d[0] * u3[0] + d[1] * u3[1] + d[2] * u3[2]) / z;
    const auto& o = origin.at(ip["camera"]);
    perception::SchematicDetector::Point pt;
    const int x = static_cast<int>(std::floor(o[0] + u * o[2] + 0.5));
    int y = static_cast<int>(std::floor(o[1] + v * o[2] + 0.5));
    const int clear = 2 * out.radius + 4;
    for (bool moved = true; moved;) {   // a point at another's position is nudged down, as the renderer does
      moved = false;
      for (const auto& q : out.pts)
        if ((x - q.x) * (x - q.x) + (y - q.y) * (y - q.y) < clear * clear) {
          y += 2 * out.radius + 6;
          moved = true;
          break;
        }
    }
    pt.x = static_cast<std::int16_t>(x);
    pt.y = static_cast<std::int16_t>(y);
    pt.n_states = static_cast<std::uint8_t>(ip["states"].size());
    out.pts.push_back(pt);
  }
  return out;
}

}  // namespace sunverify::app
