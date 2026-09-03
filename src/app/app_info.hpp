#pragma once

#include <cstdint>
#include <string>

#include "core/types.hpp"

namespace app
{

//------------------------------------------------------------
// ApplicationCreateInfo
//------------------------------------------------------------
struct ApplicationCreateInfo
{
  // General
  std::string name{"Application"};
  std::string sceneFile{"default_scene.json"};

  // Window / runtime
  WindowSize windowSize{0, 0};  // Window size or Viewport size (headless)
  bool vSync{true};             // Enable V-Sync by default

  // Headless
  bool headless = false;
  uint32_t headlessFrameCount = 100;

  // UI
  bool useMenu{true};                 // Include a menubar
  bool hasUndockableViewport{false};  // Allow floating windows
};

}  // namespace app
