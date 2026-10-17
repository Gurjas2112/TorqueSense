#pragma once
// A station must not doze: idle sleep on a laptop stops the capture clock mid-cycle, and on resume the
// timer races to catch up and drops frames. While an engine runs, its main thread asks Windows to keep
// the system (not the display) awake. A no-op elsewhere.
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall SetThreadExecutionState(unsigned long flags);
#endif

namespace sunverify::app {

inline void keep_system_awake() noexcept {
#ifdef _WIN32
  constexpr unsigned long kContinuous = 0x80000000UL, kSystemRequired = 0x00000001UL;
  SetThreadExecutionState(kContinuous | kSystemRequired);
#endif
}

}  // namespace sunverify::app
