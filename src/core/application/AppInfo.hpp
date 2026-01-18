#pragma once

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

#include <cstdint>
#include <functional>
#include <string>

#include <glm/vec2.hpp>
#include <nvapp/frame_pacer.hpp>
#include <nvgui/settings_handler.hpp>

namespace core
{

//------------------------------------------------------------
// ApplicationCreateInfo
//------------------------------------------------------------
struct ApplicationCreateInfo
{
  // General
  std::string name{"Application"};

  // Window / runtime
  glm::uvec2 windowSize{0, 0};  // Window size (width, height) or Viewport size (headless)
  bool vSync{true};             // Enable V-Sync by default

  // Headless
  bool headless = false;
  uint32_t headlessFrameCount = 1;

  // UI
  bool useMenu{true};                      // Include a menubar
  bool hasUndockableViewport{false};       // Allow floating windows
  std::function<void(ImGuiID)> dockSetup;  // Dock layout setup
  ImGuiConfigFlags imguiConfigFlags{ImGuiConfigFlags_NavEnableKeyboard |
                                    ImGuiConfigFlags_DockingEnable};
};

}  // namespace core
