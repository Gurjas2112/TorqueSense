// The pattern solvers, against properties and against the reference (spec Section 7).
// Expected orders come from reference/tsref through tests/fixtures/tsref_cases.json — never typed.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <numeric>

#include <nlohmann/json.hpp>

#include "sunverify/cad/Patterns.hpp"

using namespace sunverify::cad;

namespace {

std::vector<Point2> circle(int n, double r = 57.15) {
  std::vector<Point2> p;
  for (int i = 0; i < n; ++i) {
    const double a = (90.0 - 360.0 / n * i) * std::numbers::pi / 180.0;
    p.push_back({r * std::cos(a), r * std::sin(a)});
  }
  return p;
}

std::vector<int> one_based(std::vector<int> v) {
  for (int& x : v) ++x;
  return v;
}

nlohmann::json fixture() {
  std::ifstream f(std::string(TS_FIXTURES) + "/tsref_cases.json");
  return nlohmann::json::parse(f);
}

}  // namespace

TEST(Patterns, WheelStarMatchesTheDocumentedSequence) {
  const auto [kind, order] = solve(circle(5));
  EXPECT_EQ(kind, Layout::kCircle);
  EXPECT_EQ(one_based(order), (std::vector<int>{1, 3, 5, 2, 4}));
}

TEST(Patterns, SevenPointLineIsCentreOutAlternating) {
  std::vector<Point2> pts;
  for (int x = 0; x < 700; x += 100) pts.push_back({double(x), 0.0});
  const auto [kind, order] = solve(pts);
  EXPECT_EQ(kind, Layout::kLine);
  EXPECT_EQ(one_based(order), (std::vector<int>{4, 5, 3, 6, 2, 7, 1}));
}

TEST(Patterns, TwoByThreeFieldPairsAcrossTheDiagonal) {
  std::vector<Point2> pts;
  for (double y : {50.0, -50.0})
    for (double x : {-100.0, 0.0, 100.0}) pts.push_back({x, y});
  const auto [kind, order] = solve(pts);
  EXPECT_EQ(kind, Layout::kField);
  EXPECT_EQ(one_based(order), (std::vector<int>{2, 5, 3, 4, 6, 1}));
}

TEST(Patterns, CircleTableFromTheSpec) {
  EXPECT_EQ(one_based(star(circle(4))), (std::vector<int>{1, 3, 2, 4}));
  EXPECT_EQ(one_based(star(circle(6))), (std::vector<int>{1, 4, 2, 5, 3, 6}));
  EXPECT_EQ(one_based(star(circle(8))), (std::vector<int>{1, 5, 3, 7, 2, 6, 4, 8}));
}

TEST(Patterns, StarPropertiesForMany) {
  for (int n = 4; n <= 12; ++n) {
    const auto order = star(circle(n));
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> all(n);
    std::iota(all.begin(), all.end(), 0);
    ASSERT_EQ(sorted, all) << "every bolt exactly once, n=" << n;
    if (n % 2 == 0) {
      for (int i = 0; i + 1 < n; i += 2) EXPECT_EQ(((order[i] - order[i + 1]) % n + n) % n, n / 2) << "diametric pair, n=" << n;
    } else {
      for (int i = 0; i + 1 < n; ++i) EXPECT_EQ(((order[i + 1] - order[i]) % n + n) % n, n / 2) << "stride, n=" << n;
    }
  }
}

TEST(Patterns, LineNeverMovesBackInwardAndAlternatesSides) {
  std::vector<Point2> pts;
  for (int x = 0; x < 700; x += 100) pts.push_back({double(x), 0.0});
  const auto order = center_out(pts);
  for (std::size_t i = 1; i < order.size(); ++i)
    EXPECT_LE(std::abs(pts[order[i - 1]].x - 300), std::abs(pts[order[i]].x - 300));
  for (std::size_t i = 2; i < order.size(); ++i) EXPECT_NE(pts[order[i - 1]].x > 300, pts[order[i]].x > 300);
}

TEST(Patterns, AgreesWithTheReferenceOnEveryLayout) {
  const auto fx = fixture();
  int checked = 0;
  for (const auto& lay : fx["layouts"]) {
    std::vector<Point2> pts;
    for (const auto& p : lay["points"]) pts.push_back({p[0].get<double>(), p[1].get<double>()});
    const auto [kind, order] = solve(pts);
    EXPECT_EQ(to_string(kind), lay["kind"].get<std::string>()) << lay["name"];
    EXPECT_EQ(order, lay["order"].get<std::vector<int>>()) << lay["name"];
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> all(pts.size());
    std::iota(all.begin(), all.end(), 0);
    EXPECT_EQ(sorted, all) << "a permutation: " << lay["name"];
    ++checked;
  }
  EXPECT_GE(checked, 200);
}
