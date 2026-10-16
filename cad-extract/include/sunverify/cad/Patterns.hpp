#pragma once
// Tightening-order solvers (spec Section 7). Each takes fastener positions in the part's own plane
// (mm) and returns 0-based indices in fastening order. The compiler picks the solver from the
// layout's geometry, so the order is derived, never typed.
//
// The C++ copy of reference/tsref/patterns.py, including its tie-break keys: the reference is the
// specification, and cad-extract/tests checks every case against its output.
#include <string>
#include <utility>
#include <vector>

namespace sunverify::cad {

struct Point2 {
  double x = 0;
  double y = 0;
};

enum class Layout { kCircle, kLine, kField };
const char* to_string(Layout l) noexcept;

Point2 centroid(const std::vector<Point2>& pts);

// circle: every point equidistant from the centroid (bolt circle, flange);
// line: collinear (rail, seam); field: anything else (cover, cylinder head, panel).
Layout classify(const std::vector<Point2>& pts, double tol = 0.05);

// Bolt circle, starting at member 1 and running in the numbering's direction. Odd N: stride
// floor(N/2), one cycle. Even N: diametric pairs, pair starts strided by N/4, falling back to the
// first unvisited start when the stride cycles early (N = 8 would otherwise loop 0,2,0,2).
std::vector<int> star(const std::vector<Point2>& pts);

// Line or field: start nearest the centroid and work outward; among points equally far out, the
// one most nearly opposite the last.
std::vector<int> center_out(const std::vector<Point2>& pts);

std::pair<Layout, std::vector<int>> solve(const std::vector<Point2>& pts);

}  // namespace sunverify::cad
