#pragma once
// Scheduling (spec Section 14): pin the pipeline threads to their own physical cores, raise the
// process to HIGH priority (never realtime), opt out of Windows' EcoQoS, and pace frames with a
// high-resolution timer instead of the 15.6 ms system tick.
#include <cstdint>
#include <memory>
#include <string>

namespace sunverify::app {

// False (and a reason) if the pin failed: a silently failed pin makes every latency figure meaningless.
bool pin_current_thread(int logical_cpu, std::string& why);
void name_current_thread(const char* name);
// HIGH_PRIORITY_CLASS and the EcoQoS opt-out on Windows; a no-op elsewhere.
void prepare_process_for_realtime_work();

// Sleeps until an absolute monotonic deadline, with sub-millisecond resolution.
class FrameTimer {
 public:
  FrameTimer();
  ~FrameTimer();
  void sleep_until_ns(std::int64_t deadline_ns);

 private:
  void* handle_ = nullptr;
};

std::int64_t now_ns();   // steady clock, ns: the one clock every latency is measured on

}  // namespace sunverify::app
