#pragma once

#include <cstdint>

#include <glm/vec2.hpp>
struct WindowSize
{
  uint32_t width;
  uint32_t height;
  // Generates == and != automatically
  bool operator==(const WindowSize&) const = default;

  operator glm::uvec2() const { return glm::uvec2(width, height); }
};
