#include "timers.hpp"

// PerformanceTimer platform headers
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <realtimeapiset.h>
#elif defined(__unix__)
#  include <time.h>
#else
#  include <chrono>
#endif

namespace common
{

//-------------------------------------------------------------------------------------------------
// PerformanceTimer

PerformanceTimer::TimeValue PerformanceTimer::now() const
{
#if defined(_WIN32)  // Windows implementation

  // On Windows, we use QueryUnbiasedInterruptTimePrecise, which
  // has good accuracy and ignores suspensions.
  // This is inspired by Calder White's article,
  // https://www.rippling.com/blog/rust-suspend-time .
  ULONGLONG uptime = 0;
  QueryUnbiasedInterruptTimePrecise(&uptime);
  // QueryUnbiasedInterruptTimePrecise returns values in 100ns intervals,
  // so we can return the value directly.
  return {.ticks_100ns = static_cast<int64_t>(uptime)};

#elif defined(__unix__)

  // On most Unix systems, we query CLOCK_MONOTONIC. We could do
  // CLOCK_MONOTONIC_RAW, but falling out-of-sync with real-world time is
  // probably worse than occasionally jumping backwards if the system's
  // oscillator is flawed.
  // On Linux, CLOCK_MONOTONIC does not include suspend time; see
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=810fb07a9b504ac22b95899cf8b39d25a5f3e5c5
  // . On Apple platforms, CLOCK_MONOTONIC includes suspend time (according to
  // https://www.manpagez.com/man/3/clock_gettime/), so we use CLOCK_UPTIME_RAW instead.
#  ifdef __APPLE__
  constexpr clockid_t clockID = CLOCK_UPTIME_RAW;
#  else
  constexpr clockid_t clockID = CLOCK_MONOTONIC;
#  endif
  static_assert(sizeof(time_t) >= 4);  // Make sure we aren't setting up for a Year 2038 bug
  timespec tv{};
  clock_gettime(clockID, &tv);
  return {.seconds = static_cast<int64_t>(tv.tv_sec),
          .nanoseconds = static_cast<int64_t>(tv.tv_nsec)};

#else  // Fallback implementation

  int64_t PerformanceTimer::now() const
  {
    const std::chrono::steady_clock::duration time =
        std::chrono::steady_clock::now().time_since_epoch();
    using ns100 = std::chrono::duration<int64_t, std::ratio<1i64, 10'000'000i64>>;
    return {.ticks_100ns = std::chrono::duration_cast<ns100>(time).count()};
  }

#endif
}
}  // namespace common
