// Linked only into the allocation test binary (spec Section 31). Replaces the global operator new so
// a test can count heap allocations made by one thread while it is counting.
#include <atomic>
#include <cstdlib>
#include <new>

std::atomic<std::size_t> g_allocs{0};
thread_local bool g_counting = false;

void* operator new(std::size_t n) {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t n) {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
