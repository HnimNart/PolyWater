#include <chrono>
#include <format>
#include <iostream>
#include <string>

namespace common
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
  PerformanceTimer() { reset(); }

  // Starts or re-starts counting from the current time.
  void reset() { m_start = now(); }

  // Returns the number of seconds since the clock was initialized.
  // Always non-negative even if the underlying timer is non-monotonic.
  double getSeconds() const
  {
#ifdef __unix__
    const TimeValue t = now();

    // 1. Calculate integer differences
    int64_t diff_s = t.seconds - m_start.seconds;
    int64_t diff_ns = t.nanoseconds - m_start.nanoseconds;

    // 2. Handle the "borrow" if nanoseconds wrapped around
    if (diff_ns < 0)
    {
      diff_s -= 1;
      diff_ns += 1000000000LL;
    }

    // 3. Convert to double only at the very last step
    return static_cast<double>(diff_s) + (static_cast<double>(diff_ns) * 1e-9);
#else
    const int64_t delta = now().ticks_100ns - m_start.ticks_100ns;
    return delta >= 0 ? static_cast<double>(delta) * 1e-7 : 0.;
#endif
  }
  // Convenience functions returning total time in different units
  double getMilliseconds() const { return getSeconds() * 1e3; }
  double getMicroseconds() const { return getSeconds() * 1e6; }

private:
  struct TimeValue
  {
#ifdef __unix__
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
  static std::string indent()
  {
    std::string result(static_cast<size_t>(s_nesting * 2), ' ');
    for (int i = 0; i < s_nesting * 2; i += 2)
      result[i] = '|';
    return result;
  }

private:
  PerformanceTimer m_timer;
  bool m_manualIndent = false;
  static inline thread_local int s_nesting = 0;
  static inline thread_local bool s_openNewline = false;
};

}  // namespace common

#if defined(_MSC_VER)
#  define FUNC_SIG __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#  define FUNC_SIG __PRETTY_FUNCTION__
#else
#  define FUNC_SIG __func__
#endif

#define SCOPED_TIMER_SIG() common::ScopedTimer _timer_##__LINE__(FUNC_SIG)
#define SCOPED_TIMER(name) common::ScopedTimer _timer_##__LINE__(name)
#define SCOPED_TIMER_FUNC() common::ScopedTimer _timer_##__LINE__(__FUNCTION__)
