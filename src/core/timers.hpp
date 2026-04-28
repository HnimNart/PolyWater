#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <string>

namespace core
{

// Generic utility class for measuring CPU time.
//
// Usage:
// ```
// PerformanceTimer timer;
// // ... do something...
// printf("Operation 1 took %f seconds\n", timer.getSeconds());
//
// timer.reset();
// // ... do something else...
// printf("Operation 2 took %f seconds\n", timer.getSeconds();
// ```
//
// On Windows and Unix systems, this timer should have precision within 100
// nanoseconds and ignore time when the computer is suspended (e.g. asleep or
// hibernating).
//
// On other systems, this falls back to std::chrono::steady_clock.
//
// Exact precision and dependency depends on the platform; Windows, for
// instance, will attempt to correct for innacuracies
// (https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps#low-level-hardware-clock-characteristics),
// and on Unix we choose a method that's synced to Network Time Protocol (at
// the expense of a higher chance of non-monotonicity).
class PerformanceTimer
{
public:
  PerformanceTimer()
  {
    reset();
  }

  // Starts or re-starts counting from the current time.
  void reset()
  {
    m_start = now();
  }

  // Returns the number of seconds since the clock was initialized.
  // Always non-negative even if the underlying timer is non-monotonic.
  double getSeconds() const;
  // Convenience functions returning total time in different units
  double getMilliseconds() const
  {
    return getSeconds() * 1e3;
  }
  double getMicroseconds() const
  {
    return getSeconds() * 1e6;
  }

private:
  struct TimeValue
  {
#if defined(__unix__) || defined(__APPLE__)
    // On Unix platforms, store the full 128-bit time struct; this gets us
    // nanosecond precision and still avoids overflow issues.
    int64_t seconds{};
    int64_t nanoseconds{};
#else
    // Store the start time in ticks as a 64-bit signed integer, in units of
    // 100 nanoseconds (as this is what Windows uses).
    // Since on Windows we measure time since boot, rollover is implausible.
    // On other platforms, this will only roll over about 29226 years after the
    // platform's epoch.
    int64_t ticks_100ns{};
#endif
  };

  TimeValue m_start{};
  // Returns the current TimeValue.
  TimeValue now() const;
};

struct TimerResult
{
  int depth;
  std::string name;
  double timeMs;
};

class ScopedTimer
{
public:
  ScopedTimer(const std::string& str);
  ScopedTimer(const char* fmt, ...);
  void init_(const std::string& str);
  ~ScopedTimer();
  static std::string indent();

private:
  PerformanceTimer m_timer;
  bool m_manualIndent = false;
  static inline thread_local int s_nesting = 0;
  static inline thread_local bool s_openNewline = false;
};

}  // namespace core

#if defined(_MSC_VER)
#  define FUNC_SIG __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#  define FUNC_SIG __PRETTY_FUNCTION__
#else
#  define FUNC_SIG __func__
#endif

#if defined(NDEBUG) || defined(ENABLE_PROFILING)
   // --- Helpers for __LINE__ expansion ---
#  define TIMER_CONCAT_INNER(a, b) a##b
#  define TIMER_CONCAT(a, b) TIMER_CONCAT_INNER(a, b)

// --- Debug / Profiling Mode: Enable Timers ---
#  define SCOPED_TIMER_SIG()                                                   \
    core::ScopedTimer TIMER_CONCAT(_timer_, __LINE__)(FUNC_SIG)
#  define SCOPED_TIMER(name)                                                   \
    core::ScopedTimer TIMER_CONCAT(_timer_, __LINE__)(name)
#  define SCOPED_TIMER_FUNC()                                                  \
    core::ScopedTimer TIMER_CONCAT(_timer_, __LINE__)(__FUNCTION__)

#else
   // --- Release Mode: Compile to Nothing ---
#  define SCOPED_TIMER_SIG()                                                   \
    do                                                                         \
    {                                                                          \
    } while (0)
#  define SCOPED_TIMER(name)                                                   \
    do                                                                         \
    {                                                                          \
    } while (0)
#  define SCOPED_TIMER_FUNC()                                                  \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif
