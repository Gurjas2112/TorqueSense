#pragma once
// Can this camera see this inspection point well enough? Pinhole model (spec Section 7). The C++
// copy of reference/tsref/optics.py. The compiler runs it for every point and refuses a profile
// with a point that no camera resolves (I17).
#include <array>
#include <string>
#include <vector>

namespace sunverify::cad {

using Vec3 = std::array<double, 3>;

struct Camera {
  std::string id;
  int width_px = 1920;      // the width the crop is taken from, after any downscale
  double hfov_deg = 70;     // horizontal field of view at that width; replaced by calibration
  Vec3 position_mm{};
  Vec3 look_at_mm{};
  double aspect = 16.0 / 9.0;

  double f_px() const;
};

struct Footprint {
  std::string camera;
  double distance_mm = 0;
  double obliquity_deg = 0;
  double feature_px = 0;
  bool visible = false;
};

// Inside the image rectangle, from the camera's pose, horizontal FOV and aspect ratio.
bool in_frame(const Camera& cam, const Vec3& p);

// Projected size of the distinguishing feature, in pixels, with foreshortening. Occlusion by other
// parts is a separate test: a ray-cast against the CAD (StepReader).
Footprint footprint(const Camera& cam, const Vec3& ip_pos, const Vec3& ip_normal, double feature_mm,
                    double max_obliquity_deg = 70.0);

// The camera that resolves the point best (the first, on a tie).
Footprint best(const std::vector<Camera>& cams, const Vec3& ip_pos, const Vec3& ip_normal, double feature_mm);

}  // namespace sunverify::cad
