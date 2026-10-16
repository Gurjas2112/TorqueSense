// The engine's Windows-specific calls (Affinity.cpp, ipc/) and C++20 features it relies on.
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>
#include <stop_token>
struct alignas(std::hardware_destructive_interference_size) Slot { std::atomic<unsigned> seq{0}; };
int main() {
  DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 8);   // logical CPU 8
  timeBeginPeriod(1);
  PROCESS_POWER_THROTTLING_STATE p{};                                              // opt out of EcoQoS
  p.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  p.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED; p.StateMask = 0;
  BOOL eco = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &p, sizeof p);
  HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"Local\\torquesense_state_W14");
  void* view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
  Slot s; std::jthread t([&](std::stop_token) { s.seq.store(1, std::memory_order_release); s.seq.notify_one(); });
  s.seq.wait(0, std::memory_order_acquire);
  std::printf("affinity_prev=%llu eco=%d map=%p seq=%u align=%zu\n", (unsigned long long)prev, eco, view, s.seq.load(), alignof(Slot));
  UnmapViewOfFile(view); CloseHandle(h); timeEndPeriod(1);
}
