#pragma once

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

#include <cstdint>
#include <functional>
#include <string>

#include "core/Types.hpp"

namespace app {

//------------------------------------------------------------
// ApplicationCreateInfo
//------------------------------------------------------------
struct ApplicationCreateInfo {
  // General
  std::string name{"Application"};

  // Window / runtime
  WindowSize windowSize{0, 0}; // Window size or Viewport size (headless)
  bool vSync{true};            // Enable V-Sync by default

  // Headless
  bool headless = false;
  uint32_t headlessFrameCount = 10;

  // UI
  bool useMenu{true};                     // Include a menubar
  bool hasUndockableViewport{false};      // Allow floating windows
  std::function<void(ImGuiID)> dockSetup; // Dock layout setup
  ImGuiConfigFlags imguiConfigFlags{ImGuiConfigFlags_NavEnableKeyboard |
                                    ImGuiConfigFlags_DockingEnable};
};

} // namespace app
