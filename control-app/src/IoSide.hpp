#pragma once
// The unpinned side of the engine (spec Sections 14, 21, 30): everything slow. It turns the verify
// thread's fixed-size messages into the contracts (WebSocket state, anomaly rows, health buckets),
// serves MJPEG from the clip ring and the control port, records latency histograms, forwards
// records to the API through an on-disk spool, asks Florence-2 about ambiguous points behind a
// circuit breaker, and drives the line simulator. Nothing here can delay an alert: the alert path is
// capture -> inference -> verify -> alert -> PLC, and this side only reads its results.
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Pipeline.hpp"

namespace sunverify::app {

class IoSide {
 public:
  explicit IoSide(Pipeline& p);
  ~IoSide();
  void start();
  void stop();
  bool shutdown_requested() const noexcept;
  // The engine's /health document — what the fleet agent and the supervisor read.
  std::string health_json() const;
  // Every state document, timestamped, to a JSONL file: the replay the Vercel page plays (Section 32).
  void record_to(const std::filesystem::path& p);
  // engine and evidence latency so far: n, p50, p99, p99.9, max (ms), as JSON
  std::string latency_summary() const;
  // HdrHistogram's percentile distribution (.hgrm), both latencies
  bool write_histogram(const std::filesystem::path& out) const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace sunverify::app
