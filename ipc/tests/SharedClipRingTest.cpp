// SharedClipRing: the layout the Python reader parses, wrap-around, and a writer racing a reader —
// the seqlock must never hand back a torn frame (spec Section 31, "Concurrency").
#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "sunverify/ipc/SharedClipRing.hpp"

using namespace sunverify::ipc;

namespace {
std::string unique_name(const char* tag) { return std::string("test_") + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()); }
}  // namespace

TEST(SharedClipRing, HeaderMatchesTheDocumentedLayout) {
  auto r = SharedClipRing::create(unique_name("layout"), 180, 262144);
  ASSERT_TRUE(r);
  EXPECT_EQ(SharedClipRing::total_bytes(180, 262144), 47194112u);   // spec Section 30: ~45 MiB
  auto reader = SharedClipRing::open(unique_name("layout"));
  ASSERT_TRUE(reader);
  EXPECT_EQ(reader->slots(), 180u);
  EXPECT_EQ(reader->slot_bytes(), 262144u);
}

TEST(SharedClipRing, PublishThenReadAcrossMappings) {
  auto w = SharedClipRing::create(unique_name("rw"), 8, 1024);
  auto rd = SharedClipRing::open(unique_name("rw"));
  ASSERT_TRUE(w && rd);
  std::vector<std::byte> jpeg(100, std::byte{0x5A});
  ASSERT_TRUE(w->publish(jpeg, 1234, 77));
  std::vector<std::byte> out(1024);
  std::uint64_t ts = 0, fs = 0;
  EXPECT_EQ(rd->read(0, out, &ts, &fs), 100u);
  EXPECT_EQ(ts, 1234u);
  EXPECT_EQ(fs, 77u);
  EXPECT_EQ(rd->write_seq(), 1u);
  EXPECT_EQ(rd->read(1, out), 0u) << "not written yet";
}

TEST(SharedClipRing, RefusesAnOversizeFrameAndForgetsOverwrittenOnes) {
  auto w = SharedClipRing::create(unique_name("wrap"), 4, 64);
  ASSERT_TRUE(w);
  std::vector<std::byte> big(65), small(10);
  EXPECT_FALSE(w->publish(big, 0, 0)) << "never truncate";
  for (int i = 0; i < 6; ++i) ASSERT_TRUE(w->publish(small, i, i));
  std::vector<std::byte> out(64);
  EXPECT_EQ(w->read(0, out), 0u) << "frame 0 was overwritten by frame 4";
  EXPECT_EQ(w->read(5, out), 10u);
}

TEST(SharedClipRing, AReaderNeverSeesATornFrame) {
  auto w = SharedClipRing::create(unique_name("race"), 4, 4096);
  auto rd = SharedClipRing::open(unique_name("race"));
  ASSERT_TRUE(w && rd);
  std::atomic<bool> stop{false}, torn{false};
  std::thread reader([&] {
    std::vector<std::byte> out(4096);
    while (!stop) {
      const std::uint64_t n = rd->write_seq();
      if (n == 0) continue;
      const std::size_t len = rd->read(n - 1, out);
      if (len == 0) continue;
      const auto first = out[0];
      for (std::size_t i = 1; i < len; ++i)
        if (out[i] != first) torn = true;
    }
  });
  std::vector<std::byte> frame(4096);
  for (int i = 0; i < 200'000; ++i) {
    std::memset(frame.data(), i & 0xff, frame.size());
    w->publish(frame, i, i);
  }
  stop = true;
  reader.join();
  EXPECT_FALSE(torn.load());
}
