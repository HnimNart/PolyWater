#include "timers.hpp"

#include <cassert>
#include <cstdarg>

#include <core/logger.hpp>

// PerformanceTimer platform headers
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <realtimeapiset.h>
#elif defined(__unix__)
#include <time.h>
#else
#include <chrono>
#endif

namespace core
{

//-------------------------------------------------------------------------------------------------
// PerformanceTimer

/**********************************************************/
PerformanceTimer::TimeValue PerformanceTimer::now() const
/**********************************************************/
{
#if defined(_WIN32)
  // Windows implementation
  ULONGLONG uptime = 0;
  // Note: Requires linking against Realtime.lib or using GetTickCount64 for
  // older Win10
  QueryUnbiasedInterruptTimePrecise(&uptime);
  return {.ticks_100ns = static_cast<int64_t>(uptime)};

#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
// POSIX / Apple implementation

// Select the best clock ID for the platform
#if defined(__APPLE__)
  // On Apple, CLOCK_MONOTONIC includes sleep. CLOCK_UPTIME_RAW is the precise
  // uptime.
  const clockid_t clockID = CLOCK_UPTIME_RAW;
#elif defined(CLOCK_MONOTONIC_RAW)
  // Linux: MONOTONIC_RAW is preferred to avoid NTP frequency adjustments
  const clockid_t clockID = CLOCK_MONOTONIC_RAW;
#else
  const clockid_t clockID = CLOCK_MONOTONIC;
#endif

  struct timespec tv
  {
  };
  clock_gettime(clockID, &tv);
  return {.seconds = static_cast<int64_t>(tv.tv_sec),
          .nanoseconds = static_cast<int64_t>(tv.tv_nsec)};

#else
  // Fallback implementation using Standard C++
  auto now = std::chrono::steady_clock::now().time_since_epoch();

  // Standard-compliant 100ns ratio (1 tick = 1/10,000,000 of a second)
  using ns100 = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
  return {.ticks_100ns = std::chrono::duration_cast<ns100>(now).count()};

#endif
}

/**********************************************************/
double PerformanceTimer::getSeconds() const
/**********************************************************/
{
#if defined(__unix__) || defined(__APPLE__)
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

//-------------------------------------------------------------------------------------------------
// ScopedTimer

/**********************************************************/
ScopedTimer::ScopedTimer(const char* fmt, ...)
/**********************************************************/
{
  std::string str(256, '\0');  // initial guess. ideally the first try fits
  va_list args1, args2;
  va_start(args1, fmt);
  va_copy(args2, args1);  // make a backup as vsnprintf may consume args1
  int rc = vsnprintf(str.data(), str.size(), fmt, args1);
  if (rc >= 0 && static_cast<size_t>(rc + 1) > str.size())
  {
    str.resize(rc + 1);  // include storage for '\0'
    rc = vsnprintf(str.data(), str.size(), fmt, args2);
  }
  va_end(args1);
  assert(rc >= 0 && "vsnprintf error");
  str.resize(rc >= 0 ? static_cast<size_t>(rc) : 0);
  init_(str);
}

/**********************************************************/
ScopedTimer::ScopedTimer(const std::string& str)
/**********************************************************/
{
  init_(str);
}

/**********************************************************/
void ScopedTimer::init_(const std::string& str)
/**********************************************************/
{
  // If nesting timers, break the newline of the previous one
  if (s_openNewline)
  {
    assert(s_nesting > 0);
    LOGSTATS("\n");
  }

  m_manualIndent =
      !str.empty() && (str[0] == ' ' || str[0] == '-' || str[0] == '|');

  // Add indentation automatically if not already in str.
  if (s_nesting > 0 && !m_manualIndent)
  {
    LOGSTATS("%s", indent().c_str());
  }

  LOGSTATS("%s", str.c_str());
  s_openNewline = str.empty() || str[str.size() - 1] != '\n';
  ++s_nesting;
}

/**********************************************************/
ScopedTimer::~ScopedTimer()
/**********************************************************/
{
  --s_nesting;
  // If nesting timers and this is the second destructor in a row, indent and
  // print "Total" as it won't be on the same line.
  if (!s_openNewline && !m_manualIndent)
  {
    LOGSTATS("%s|", indent().c_str());
  }
  else
  {
    LOGSTATS(" ");
  }
  LOGSTATS("-> %.3f ms\n", m_timer.getMilliseconds());
  s_openNewline = false;
}

/**********************************************************/
std::string ScopedTimer::indent()
/**********************************************************/
{
  std::string result(static_cast<size_t>(s_nesting * 2), ' ');
  for (int i = 0; i < s_nesting * 2; i += 2)
    result[i] = '|';
  return result;
}

}  // namespace core
