// SpscRingBuffer and FramePool. The torture test is the one the linux-tsan preset runs (spec
// Section 31, "Concurrency"): two threads, millions of items, every item arrives once, in order.
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <set>
#include <thread>

#include "sunverify/perception/FramePool.hpp"
#include "sunverify/perception/SpscRingBuffer.hpp"
#include "sunverify/perception/StateVector.hpp"

using namespace sunverify::perception;

TEST(SpscRingBuffer, PushPopInOrderAndReportsFullAndEmpty) {
  SpscRingBuffer<int, 4> r;
  int x = 0;
  EXPECT_FALSE(r.try_pop(x));
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(r.try_push(i));
  EXPECT_FALSE(r.try_push(99)) << "full";
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(r.try_pop(x));
    EXPECT_EQ(x, i);
  }
  EXPECT_FALSE(r.try_pop(x)) << "empty";
}

TEST(SpscRingBuffer, WrapsAroundManyTimes) {
  SpscRingBuffer<std::uint64_t, 8> r;
  std::uint64_t next = 0, got = 0;
  for (std::uint64_t i = 0; i < 1000; ++i) {
    ASSERT_TRUE(r.try_push(i));
    if (i % 3 == 2) {
      while (r.try_pop(got)) EXPECT_EQ(got, next++);
    }
  }
}

TEST(SpscRingBuffer, IndicesLiveOnSeparateCacheLines) {
  // producer and consumer state must not false-share (spec Section 14)
  EXPECT_GE(alignof(SpscRingBuffer<int, 2>), kCacheLine);
  EXPECT_GE(sizeof(SpscRingBuffer<int, 2>), 4 * kCacheLine);
}

TEST(SpscRingBuffer, TortureTwoThreadsEveryItemOnceInOrder) {
  constexpr std::uint64_t kItems = 2'000'000;
  auto r = std::make_unique<SpscRingBuffer<std::uint64_t, 32>>();
  std::atomic<bool> bad{false};
  std::thread consumer([&] {
    std::uint64_t expect = 0, v = 0, sum = 0;
    while (expect < kItems) {
      if (r->try_pop(v)) {
        if (v != expect) bad = true;
        sum += v;
        ++expect;
      }
    }
    if (sum != kItems * (kItems - 1) / 2) bad = true;
  });
  for (std::uint64_t i = 0; i < kItems;)
    if (r->try_push(i)) ++i;
  consumer.join();
  EXPECT_FALSE(bad.load());
}

TEST(SpscRingBuffer, CarriesWholeStateVectors) {
  auto r = std::make_unique<SpscRingBuffer<StateVector, 32>>();
  std::thread producer([&] {
    for (std::uint64_t i = 0; i < 20'000;) {
      StateVector sv;
      sv.frame_seq = i;
      sv.n_ips = static_cast<std::uint16_t>(i % kMaxIps);
      sv.ips[sv.n_ips].conf = static_cast<float>(i);
      if (r->try_push(sv)) ++i;
    }
  });
  StateVector got;
  for (std::uint64_t i = 0; i < 20'000;) {
    if (!r->try_pop(got)) continue;
    ASSERT_EQ(got.frame_seq, i);
    ASSERT_EQ(got.ips[got.n_ips].conf, static_cast<float>(i)) << "a torn copy";
    ++i;
  }
  producer.join();
}

TEST(FramePool, LeasesEveryFrameOnceThenRunsDry) {
  FramePool pool(960, 540, 3, 12);
  EXPECT_EQ(pool.bytes(), 960u * 540 * 3 * 12);   // 18.7 MB, as spec Section 9 budgets
  std::set<std::uint16_t> seen;
  for (int i = 0; i < 12; ++i) {
    auto f = pool.lease();
    ASSERT_TRUE(f.has_value());
    EXPECT_TRUE(seen.insert(*f).second);
  }
  EXPECT_FALSE(pool.lease().has_value()) << "every frame in flight: the capture thread drops";
  pool.release(3);
  auto again = pool.lease();
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(*again, 3);
}

TEST(FramePool, LeaseOnOneThreadReturnOnAnother) {
  FramePool pool(64, 32, 3, 4);
  SpscRingBuffer<std::uint16_t, 8> in_flight;
  std::atomic<bool> done{false};
  std::thread inference([&] {
    std::uint16_t i;
    while (!done || !in_flight.empty_approx())
      if (in_flight.try_pop(i)) {
        pool.data(i)[0] = 1;
        pool.release(i);
      }
  });
  int leased = 0;
  while (leased < 100'000) {
    if (auto f = pool.lease()) {
      pool.data(*f)[0] = 0;
      while (!in_flight.try_push(*f)) {
      }
      ++leased;
    }
  }
  done = true;
  inference.join();
  int back = 0;
  while (pool.lease()) ++back;
  EXPECT_EQ(back, 4) << "every frame came home";
}
