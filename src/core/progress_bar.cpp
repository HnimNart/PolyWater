#include "progress_bar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

void ProgressBar::update(uint32_t current, uint32_t total)
{
  float progress = 0.0f;
  if (total > 0)
    progress = std::clamp(
        static_cast<float>(current) / static_cast<float>(total), 0.0f, 1.0f);

  // Calculate timings
  double elapsed = m_timer.getSeconds();
  double eta = 0.0;

  if (current > 0 && elapsed > 0.001)
  {
    double rate = static_cast<double>(current) / elapsed;
    double remainingItems = static_cast<double>(total - current);
    eta = remainingItems / rate;
  }

  int pos = static_cast<int>(m_width * progress);

  // Print: Name [===>   ] ...
  // \r resets the line, then we print the name first
  std::printf("\r%s [", m_name.c_str());

  for (int i = 0; i < m_width; ++i)
  {
    if (i < pos)
      std::printf("=");
    else if (i == pos)
      std::printf(">");
    else
      std::printf(" ");
  }

  std::string sElapsed = formatTime(elapsed);
  std::string sEta = formatTime(eta);

  std::printf("] %3d%% (%d/%d) [%s < %s]   ",
              static_cast<int>(progress * 100.0f), current, total,
              sElapsed.c_str(), sEta.c_str());

  std::fflush(stdout);
}

void ProgressBar::finish()
{
  double totalTime = m_timer.getSeconds();

  // Ensure the final line also has the name so it looks consistent
  std::printf("\r%s [", m_name.c_str());
  for (int i = 0; i < m_width; ++i)
    std::printf("=");

  std::printf("] 100%% Done! Total time: %s      \n",
              formatTime(totalTime).c_str());
  std::fflush(stdout);
}

std::string ProgressBar::formatTime(double s)
{
  int totalSeconds = static_cast<int>(s);
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  return std::string(buffer);
}
