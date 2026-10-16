// The GPU detector's arithmetic, tested without a GPU: letterbox geometry and per-class NMS.
#include <gtest/gtest.h>

#include <array>

#include "sunverify/perception/OnnxDetector.hpp"

using namespace sunverify::perception;

TEST(Letterbox, FullHdFillsTheWidthAndPadsTopAndBottom) {
  const Letterbox lb = letterbox_for(1920, 1080);
  EXPECT_FLOAT_EQ(lb.scale, 640.0f / 1920.0f);
  EXPECT_FLOAT_EQ(lb.pad_x, 0.0f);
  EXPECT_FLOAT_EQ(lb.pad_y, 140.0f);   // 640 - 360, split evenly
}

TEST(Letterbox, PortraitPadsTheSides) {
  const Letterbox lb = letterbox_for(480, 640);
  EXPECT_FLOAT_EQ(lb.scale, 1.0f);
  EXPECT_FLOAT_EQ(lb.pad_x, 80.0f);
  EXPECT_FLOAT_EQ(lb.pad_y, 0.0f);
}

TEST(Nms, KeepsTheBestOfOverlappingBoxesOfOneClass) {
  std::array<Box, 3> in{{{0, 0, 10, 10, 0.6f, 0}, {1, 1, 11, 11, 0.9f, 0}, {50, 50, 60, 60, 0.5f, 0}}};
  std::array<Box, 3> keep{};
  const int k = nms(in.data(), 3, 0.5f, keep.data(), 3);
  ASSERT_EQ(k, 2);
  EXPECT_FLOAT_EQ(keep[0].score, 0.9f);   // the overlapping 0.6 box is suppressed
  EXPECT_FLOAT_EQ(keep[1].score, 0.5f);
}

TEST(Nms, NeverSuppressesAcrossClasses) {
  // a hand holding the tool: the two boxes overlap, and both must count
  std::array<Box, 2> in{{{0, 0, 10, 10, 0.8f, 0}, {0, 0, 10, 10, 0.7f, 1}}};
  std::array<Box, 2> keep{};
  EXPECT_EQ(nms(in.data(), 2, 0.5f, keep.data(), 2), 2);
}

TEST(Nms, StopsAtTheFixedLimit) {
  std::array<Box, 4> in{{{0, 0, 1, 1, 0.9f, 0}, {5, 5, 6, 6, 0.8f, 0}, {9, 9, 10, 10, 0.7f, 0}, {20, 20, 21, 21, 0.6f, 0}}};
  std::array<Box, 2> keep{};
  EXPECT_EQ(nms(in.data(), 4, 0.5f, keep.data(), 2), 2);
}
