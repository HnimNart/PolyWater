#include <chrono>
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
// Exact precision and dependency depends on the platform; Windows, for instance,
// will attempt to correct for innacuracies
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
    const double delta = 1e-9 * static_cast<double>(t.nanoseconds - m_start.nanoseconds)  //
                         + static_cast<double>(t.seconds - m_start.seconds);
    return delta >= 0 ? delta : 0.;
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

class ScopedTimer
{
public:
  explicit ScopedTimer(const std::string& name) : m_name(name)
  {
    // Track nesting but DON'T print yet
    s_nesting++;
  }

  ~ScopedTimer()
  {

    const double time = m_timer.getMicroseconds();
    // Decrement nesting level before printing our line
    s_nesting--;

    // Construct the output string
    // If s_openNewline is false, it means a child timer just printed
    // on its own line, so we should prefix with a pipe to show the "Total".
    std::string prefix = indent();
    if (!s_openNewline)
    {
      prefix += "| ";
    }

    // Print everything at once: Name + Duration
    printf("%s%s -> %.3f us\n", prefix.c_str(), m_name.c_str(), time);

    // Inform any parent timer that we've taken up a line
    s_openNewline = false;
  }

private:
  std::string m_name;
  PerformanceTimer m_timer{};

  // Static state to track hierarchy
  static inline int s_nesting = 0;
  static inline bool s_openNewline = true;

  std::string indent() const { return std::string(s_nesting * 2, ' '); }
};

}  // namespace common
#define SCOPED_TIMER(name) auto scopedTimer##__LINE__ = nvutils::ScopedTimer(name)
