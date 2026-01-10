#pragma once

#include <cstdint>
struct WindowSize
{
  uint32_t width;
  uint32_t height;
  // Generates == and != automatically
  bool operator==(const WindowSize&) const = default;
};
