#include "sunverify/cad/Patterns.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <tuple>

namespace sunverify::cad {
namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

// Python's `atan2(...) % (2 * pi)`: the angle in [0, 2π).
double angle(const Point2& p, const Point2& c) {
  double a = std::atan2(p.y - c.y, p.x - c.x);
  if (a < 0) a += kTwoPi;
  return a;
}

double dist(const Point2& a, const Point2& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Python's round(x, 6) for sort keys: ties between points that are equal in exact arithmetic but
// differ in the last bits of floating point must sort as equal, in both implementations.
double round6(double x) { return std::nearbyint(x * 1e6) / 1e6; }

}  // namespace

const char* to_string(Layout l) noexcept {
  switch (l) {
    case Layout::kCircle: return "circle";
    case Layout::kLine: return "line";
    case Layout::kField: return "field";
  }
  return "field";
}

Point2 centroid(const std::vector<Point2>& pts) {
  Point2 c;
  for (const auto& p : pts) {
    c.x += p.x;
    c.y += p.y;
  }
  const double n = static_cast<double>(pts.size());
  return {c.x / n, c.y / n};
}

Layout classify(const std::vector<Point2>& pts, double tol) {
  if (pts.size() < 3) return Layout::kField;
  const Point2 c = centroid(pts);
  double rmax = 0, rmin = std::numeric_limits<double>::infinity();
  for (const auto& p : pts) {
    const double r = dist(p, c);
    rmax = std::max(rmax, r);
    rmin = std::min(rmin, r);
  }
  if (rmax > 0 && (rmax - rmin) / rmax < tol) return Layout::kCircle;

  // Python's max() returns the first maximum.
  const Point2 p0 = pts[0];
  Point2 p1 = pts[0];
  double best = -1;
  for (const auto& p : pts) {
    const double d = dist(p, p0);
    if (d > best) {
      best = d;
      p1 = p;
    }
  }
  const double len = dist(p0, p1);
  if (len > 0) {
    bool collinear = true;
    for (const auto& p : pts) {
      const double cross = std::abs((p1.x - p0.x) * (p0.y - p.y) - (p0.x - p.x) * (p1.y - p0.y)) / len;
      if (!(cross < tol * len)) {
        collinear = false;
        break;
      }
    }
    if (collinear) return Layout::kLine;
  }
  return Layout::kField;
}

std::vector<int> star(const std::vector<Point2>& pts) {
  const int n = static_cast<int>(pts.size());
  const Point2 c = centroid(pts);
  std::vector<int> by_angle(n);
  std::iota(by_angle.begin(), by_angle.end(), 0);
  std::stable_sort(by_angle.begin(), by_angle.end(),
                   [&](int a, int b) { return angle(pts[a], c) < angle(pts[b], c); });
  // start at the first member…
  const auto k = std::find(by_angle.begin(), by_angle.end(), 0);
  std::rotate(by_angle.begin(), k, by_angle.end());
  // …and run in the numbering's direction
  if (n > 2 && by_angle[n - 1] < by_angle[1]) std::reverse(by_angle.begin() + 1, by_angle.end());

  std::vector<int> out;
  out.reserve(n);
  if (n % 2 == 1) {
    for (int i = 0; i < n; ++i) out.push_back(by_angle[(i * (n / 2)) % n]);
    return out;
  }
  const int half = n / 2;
  const int step = std::max(1, n / 4);
  std::vector<bool> seen(half, false);
  int s = 0;
  for (int placed = 0; placed < half; ++placed) {
    if (seen[s]) s = static_cast<int>(std::find(seen.begin(), seen.end(), false) - seen.begin());
    seen[s] = true;
    out.push_back(by_angle[s]);
    out.push_back(by_angle[s + half]);
    s = (s + step) % half;
  }
  return out;
}

std::vector<int> center_out(const std::vector<Point2>& pts) {
  const Point2 c = centroid(pts);
  std::vector<int> rest(pts.size());
  std::iota(rest.begin(), rest.end(), 0);
  std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
    return std::make_tuple(round6(dist(pts[a], c)), angle(pts[a], c)) <
           std::make_tuple(round6(dist(pts[b], c)), angle(pts[b], c));
  });
  std::vector<int> out{rest.front()};
  rest.erase(rest.begin());
  while (!rest.empty()) {
    const Point2 last = pts[out.back()];
    auto cos_to_last = [&](const Point2& p) {
      const double ax = p.x - c.x, ay = p.y - c.y, bx = last.x - c.x, by = last.y - c.y;
      const double na = std::hypot(ax, ay), nb = std::hypot(bx, by);
      return (na == 0 || nb == 0) ? 0.0 : (ax * bx + ay * by) / (na * nb);
    };
    auto key = [&](int i) {
      const Point2& p = pts[i];
      return std::make_tuple(round6(dist(p, c)), round6(cos_to_last(p)), angle(p, c));
    };
    // Python's min() returns the first minimum.
    auto it = rest.begin();
    auto best = key(*it);
    for (auto j = rest.begin() + 1; j != rest.end(); ++j) {
      const auto kj = key(*j);
      if (kj < best) {
        best = kj;
        it = j;
      }
    }
    out.push_back(*it);
    rest.erase(it);
  }
  return out;
}

std::pair<Layout, std::vector<int>> solve(const std::vector<Point2>& pts) {
  const Layout kind = classify(pts);
  return {kind, kind == Layout::kCircle ? star(pts) : center_out(pts)};
}

}  // namespace sunverify::cad
