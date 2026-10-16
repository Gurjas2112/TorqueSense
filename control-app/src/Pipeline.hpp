#pragma once
// The station engine at run time (spec Section 14): four pinned threads joined by SPSC rings, plus
// the unpinned io side. Everything slow — JSON, HTTP, logging, the narrator — lives on the io side;
// the verify thread never touches the network, and the OPC-UA write is the alert thread's alone.
//
//   capture --32--> inference --32--> verify --16--> alert --(OPC-UA)--> PLC
//                                       |  \--64--> io <--16-- alert (acks, latency)
//                                       ^-- plant signals (alert) · adjudications (io)
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Config.hpp"
#include "Messages.hpp"
#include "VerifyCore.hpp"
#include "sunverify/ipc/SharedClipRing.hpp"
#include "sunverify/perception/Detector.hpp"
#include "sunverify/perception/FramePool.hpp"
#include "sunverify/perception/SpscRingBuffer.hpp"
#include "sunverify/procedure/IAlerter.hpp"

namespace sunverify::app {

class IoSide;
class IFrameSource;

struct PipelineCounters {
  std::atomic<std::uint64_t> frames_captured{0};
  std::atomic<std::uint64_t> frames_dropped{0};
  std::atomic<std::uint64_t> frames_verified{0};
  std::atomic<std::uint64_t> alerts_written{0};
  std::atomic<std::uint64_t> beats{0};
  std::atomic<bool> plc_connected{false};
  std::atomic<std::uint64_t> cycles{0};
};

class Pipeline {
 public:
  explicit Pipeline(StationConfig cfg);
  ~Pipeline();

  void start();
  void stop();                           // orderly: io drains, threads join
  bool running() const noexcept { return running_; }

  // The fault-injection harness: which scenario feeds the next cycle (spec Section 21, /harness).
  // It changes the input, never the logic.
  bool request_scenario(const std::string& name);
  // Stop feeding frames (a frozen camera, for the fail-visible demo) and resume.
  void freeze(bool on) noexcept { frozen_ = on; }
  bool frozen() const noexcept { return frozen_; }

  const StationConfig& config() const noexcept { return cfg_; }
  const procedure::Profile& profile() const noexcept { return profile_; }
  const perception::ReplayDetector& detector() const noexcept { return *detector_; }
  PipelineCounters& counters() noexcept { return counters_; }
  ipc::SharedClipRing* clip_ring() noexcept { return ring_.get(); }
  std::string source_description() const;
  std::int64_t started_ns() const noexcept { return started_ns_; }

  // Rings the io side consumes and feeds (single consumer / single producer each).
  perception::SpscRingBuffer<IoMsg, 64>& io_ring() noexcept { return *to_io_; }
  perception::SpscRingBuffer<AckMsg, 16>& ack_ring() noexcept { return *acks_; }
  perception::SpscRingBuffer<AdjResult, 16>& adj_ring() noexcept { return *adj_; }

 private:
  void capture_loop();
  void inference_loop();
  void verify_loop();
  void alert_loop();
  void emit_violations(VerifyCore& core, std::int64_t t_ns);

  StationConfig cfg_;
  procedure::Profile profile_;
  std::unique_ptr<perception::ReplayDetector> detector_;
  std::unique_ptr<IFrameSource> source_;
  std::unique_ptr<perception::FramePool> pool_;
  std::unique_ptr<ipc::SharedClipRing> ring_;
  std::unique_ptr<procedure::IAlerter> alerter_;
  std::vector<std::string> evidence_tags_;

  std::unique_ptr<perception::SpscRingBuffer<FrameMsg, 32>> to_inference_;
  std::unique_ptr<perception::SpscRingBuffer<perception::StateVector, 32>> to_verify_;
  std::unique_ptr<perception::SpscRingBuffer<AlertCmd, 16>> to_alert_;
  std::unique_ptr<perception::SpscRingBuffer<IoMsg, 64>> to_io_;
  std::unique_ptr<perception::SpscRingBuffer<AckMsg, 16>> acks_;
  std::unique_ptr<perception::SpscRingBuffer<perception::PlantSignal, 64>> plant_;
  std::unique_ptr<perception::SpscRingBuffer<AdjResult, 16>> adj_;

  std::atomic<bool> running_{false};
  std::atomic<bool> frozen_{false};
  std::atomic<int> requested_{-1};
  std::int64_t started_ns_ = 0;
  PipelineCounters counters_;
  std::uint32_t violation_seq_ = 0;     // verify thread only
  std::vector<std::thread> threads_;
};

}  // namespace sunverify::app
