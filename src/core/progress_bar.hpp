#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "timers.hpp"

class ProgressBar
{
public:
  // width: Visual width of the bar graphic
  explicit ProgressBar(const std::string& name, int width = 50) :
      m_name(name), m_width(width)
  {
  }

  void update(uint32_t current, uint32_t total);

  void finish();

private:
  std::string m_name;
  int m_width;
  core::PerformanceTimer m_timer;

  static std::string formatTime(double s);
};
