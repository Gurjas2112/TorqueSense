#include "sunverify/cad/Optics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sunverify::cad {
namespace {

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 unit(const Vec3& a) {
  const double n = std::sqrt(dot(a, a));
  return {a[0] / n, a[1] / n, a[2] / n};
}
double radians(double deg) { return deg * std::numbers::pi / 180.0; }

}  // namespace

double Camera::f_px() const { return (width_px / 2.0) / std::tan(radians(hfov_deg) / 2.0); }

bool in_frame(const Camera& cam, const Vec3& p) {
  const Vec3 f = unit(sub(cam.look_at_mm, cam.position_mm));
  const Vec3 up_hint = std::abs(f[2]) < 0.99 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  const Vec3 r = unit(cross(f, up_hint));
  const Vec3 u = cross(r, f);
  const Vec3 d = sub(p, cam.position_mm);
  const double z = dot(d, f);
  if (z <= 0) return false;
  const double th = std::tan(radians(cam.hfov_deg) / 2.0);
  return std::abs(dot(d, r) / z) <= th && std::abs(dot(d, u) / z) <= th / cam.aspect;
}

Footprint footprint(const Camera& cam, const Vec3& ip_pos, const Vec3& ip_normal, double feature_mm,
                    double max_obliquity_deg) {
  const Vec3 v = sub(cam.position_mm, ip_pos);
  const double d = std::sqrt(dot(v, v));
  const double n = std::sqrt(dot(ip_normal, ip_normal));
  const double cos_t = dot(v, ip_normal) / (d * n);
  const double obliq = std::acos(std::clamp(cos_t, -1.0, 1.0)) * 180.0 / std::numbers::pi;
  const bool facing = obliq < max_obliquity_deg && in_frame(cam, ip_pos);
  const double px = facing ? cam.f_px() * feature_mm / d * std::max(0.0, cos_t) : 0.0;
  return {cam.id, d, obliq, px, facing};
}

Footprint best(const std::vector<Camera>& cams, const Vec3& ip_pos, const Vec3& ip_normal, double feature_mm) {
  Footprint out;
  bool first = true;
  for (const auto& c : cams) {
    Footprint f = footprint(c, ip_pos, ip_normal, feature_mm);
    if (first || f.feature_px > out.feature_px) {
      out = std::move(f);
      first = false;
    }
  }
  return out;
}

}  // namespace sunverify::cad
