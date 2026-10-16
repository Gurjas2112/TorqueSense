#pragma once
// The line around the station, simulated: what the nutrunner controller and the line PLC would
// write — each scenario's plant signals at their times, and the release request when the part is
// due to leave. It writes them INTO the PLC, so the engine reads them back over OPC-UA exactly as it
// would at a real station: evidence always arrives through the PLC (spec Section 10). It runs on its
// own unpinned thread, never in the engine's hot path, and is switched off at a real station.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sunverify::app {

struct LineEvent {
  std::int64_t t_ms;
  std::string tag;      // full tag name
  bool value;
};

class LineSimulator {
 public:
  LineSimulator(std::string endpoint, std::string release_tag);
  ~LineSimulator();
  void start();
  void stop();
  // A new cycle began at t0_ns (steady clock); schedule its events (the release request included).
  void begin_cycle(std::int64_t t0_ns, std::vector<LineEvent> events);

 private:
  void run();
  std::string endpoint_, release_tag_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex m_;                       // off the hot path: a lock is allowed here (I2 covers perception/, procedure/)
  std::condition_variable cv_;
  std::vector<LineEvent> schedule_;
  std::int64_t t0_ns_ = 0;
  bool fresh_ = false;
};

}  // namespace sunverify::app
