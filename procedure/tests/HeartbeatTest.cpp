// I13 on the engine's side: the heartbeat advances only while frames are being verified.
#include <gtest/gtest.h>

#include "sunverify/procedure/Heartbeat.hpp"

using sunverify::procedure::HeartbeatGate;

TEST(Heartbeat, BeatsEveryPeriodWhileFramesAreVerified) {
  HeartbeatGate g{500};
  std::uint64_t frames = 0;
  int beats = 0;
  for (std::int64_t t = 0; t <= 3000; t += 33) {
    ++frames;                                      // the verify thread is working
    if (g.due(t, frames)) ++beats;
  }
  // next = now + period, as spec Section 14's AlertThread does: one beat per 500 ms, give or take a frame
  EXPECT_GE(beats, 5);
  EXPECT_LE(beats, 7);
  EXPECT_EQ(g.beat(), static_cast<std::uint32_t>(beats));
}

TEST(Heartbeat, StopsWhenTheFrameCounterStops) {   // a frozen camera or a hung thread
  HeartbeatGate g{500};
  std::uint64_t frames = 0;
  for (std::int64_t t = 0; t < 2000; t += 33) g.due(t, ++frames);
  // frames verified since the last beat still earn one more; after that, silence
  int late = 0;
  for (std::int64_t t = 2000; t < 10000; t += 33) late += g.due(t, frames);
  EXPECT_LE(late, 1);
  const auto before = g.beat();
  for (std::int64_t t = 10000; t < 20000; t += 33) EXPECT_FALSE(g.due(t, frames)) << "no work, no beat";
  EXPECT_EQ(g.beat(), before);
}

TEST(Heartbeat, ResumesWhenWorkResumes) {
  HeartbeatGate g{500};
  EXPECT_FALSE(g.due(0, 0)) << "never beat before the first verified frame";
  EXPECT_TRUE(g.due(500, 1));
  EXPECT_FALSE(g.due(1000, 1));
  EXPECT_TRUE(g.due(1500, 2));
}
