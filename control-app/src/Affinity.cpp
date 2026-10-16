// One of the files allowed platform #ifdefs (coding instructions, "Toolchain").
#include "Affinity.hpp"

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace sunverify::app {

std::int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool pin_current_thread(int cpu, std::string& why) {
  if (cpu < 0) return true;
#ifdef _WIN32
  if (cpu >= 64) {
    why = "logical CPU beyond 63";
    return false;
  }
  if (SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << cpu) == 0) {   // 0 means it failed
    why = "SetThreadAffinityMask failed, error " + std::to_string(GetLastError());
    return false;
  }
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  return true;
#else
  if (static_cast<unsigned>(cpu) >= std::thread::hardware_concurrency()) {
    why = "logical CPU " + std::to_string(cpu) + " does not exist here";
    return false;
  }
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  if (rc != 0) why = "pthread_setaffinity_np failed: " + std::to_string(rc);
  return rc == 0;
#endif
}

void name_current_thread(const char* name) {
#ifdef _WIN32
  wchar_t w[64];
  int i = 0;
  for (; name[i] && i < 63; ++i) w[i] = static_cast<wchar_t>(name[i]);
  w[i] = 0;
  SetThreadDescription(GetCurrentThread(), w);
#else
  pthread_setname_np(pthread_self(), name);
#endif
}

void prepare_process_for_realtime_work() {
#ifdef _WIN32
  PROCESS_POWER_THROTTLING_STATE s{};
  s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  s.StateMask = 0;   // 0 = throttling OFF for the controlled policy
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);   // never REALTIME_PRIORITY_CLASS
#endif
}

FrameTimer::FrameTimer() {
#ifdef _WIN32
  handle_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
}

FrameTimer::~FrameTimer() {
#ifdef _WIN32
  if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#endif
}

void FrameTimer::sleep_until_ns(std::int64_t deadline_ns) {
  const std::int64_t wait = deadline_ns - now_ns();
  if (wait <= 0) return;
#ifdef _WIN32
  if (handle_) {
    LARGE_INTEGER due;
    due.QuadPart = -(wait / 100);   // relative, in 100 ns units
    if (SetWaitableTimerEx(static_cast<HANDLE>(handle_), &due, 0, nullptr, nullptr, nullptr, 0)) {
      WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);
      return;
    }
  }
  std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
#else
  timespec ts{static_cast<time_t>(deadline_ns / 1'000'000'000), static_cast<long>(deadline_ns % 1'000'000'000)};
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
#endif
}

}  // namespace sunverify::app
