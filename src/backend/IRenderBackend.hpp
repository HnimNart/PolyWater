#pragma once

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <imgui.h>
#include <vulkan/vulkan_core.h>

#include <filesystem>
#include <functional>
#include <vector>

#include "core/application/AppInfo.hpp"
#include "core/application/IAppElement.hpp"
#include "core/application/types.h"

//------------------------------------------------------------
// IRenderBackend
//------------------------------------------------------------
// Abstract interface for a render backend. Concrete implementations
// include Vulkan, D3D12, Metal, OpenGL, or headless backends.
//
// Responsibilities:
// - Manage frame lifecycle
// - Provide a per-frame RenderContext
// - Allow scene renderers to draw via the context
// - Support vsync, resizing, and screenshots
class IRenderBackend
{
public:
  virtual ~IRenderBackend() = default;

  //----------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------
  virtual void init(const core::ApplicationCreateInfo& appInfo) = 0;
  virtual void deinit() = 0;

  //----------------------------------------------------------
  // Frame loop
  //----------------------------------------------------------
  virtual void newFrame()
  {
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  // Begin a new frame. Returns false if frame should be skipped (e.g., minimized).
  virtual bool beginFrame(FrameContext& frame) = 0;

  // Render the frame
  virtual void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                           FrameContext const& frame) = 0;

  // Complete the frame
  virtual void endFrame(FrameContext const& frame) = 0;

  // Present the completed frame (no-op for headless backends)
  virtual void present() = 0;

  //----------------------------------------------------------
  // Runtime control
  //----------------------------------------------------------
  virtual void setVsync(bool enabled) { m_vsyncWanted = enabled; };
  virtual bool isVsync() const { return m_vsync; };

  //----------------------------------------------------------
  // Window / output surface control
  //----------------------------------------------------------
  virtual void onResize(const WindowSize& size)
  {
    // Check for DPI scaling and adjust the font size
    float xscale, yscale;
    glfwGetWindowContentScale(m_windowHandle, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale *= xscale / m_dpiScale;
    m_dpiScale = xscale;

    m_viewportSize = {size.width, size.height};
  }

  const WindowSize& getViewportSize() const { return m_viewportSize; }
  virtual void setWindow(GLFWwindow* windowHandle) { m_windowHandle = windowHandle; }
  virtual void setWindowSize(const WindowSize& windowSize) = 0;

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  virtual void requestScreenshot(const std::filesystem::path& filename, int quality = 100) = 0;
  virtual void freeResourcesQueue() {};

protected:
  GLFWwindow* m_windowHandle{nullptr};  // GLFW Window
  WindowSize m_viewportSize{0, 0};      // Size of the viewport
  float m_dpiScale = 1.0f;

  // Vsync
  bool m_vsyncWanted{true};  // Wanting swapchain with vsync
  bool m_vsync{true};

  // Screenshot
  bool m_screenShotRequested = false;
  int m_screenShotFrame = 0;
  std::filesystem::path m_screenShotFilename;

  std::vector<std::vector<std::function<void()>>>
      m_resourceFreeQueue;  // Queue of functions to free resources
};
