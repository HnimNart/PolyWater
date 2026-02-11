#pragma once

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <imgui.h>
#include <vulkan/vulkan_core.h>

#include <filesystem>
#include <functional>
#include <vector>

#include "IRenderContext.hpp"
#include "app/AppInfo.hpp"
#include "app/IAppElement.hpp"
#include "app/IGUISystem.hpp"
#include "core/Types.hpp"

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
class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

  //----------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------
  virtual void initPresentation(GLFWwindow *window,
                                core::IGUISystemPtr gui) = 0;
  virtual void deinit() = 0;
  virtual void waitForDeviceIdle() = 0;

  //----------------------------------------------------------
  // Frame loop
  //----------------------------------------------------------
  virtual IRenderContext &getCurrentContext() = 0;
  // Begin a new frame. Returns a nullptr if frame should be skipped (e.g.,
  // minimized).
  virtual IRenderContext *beginFrame() = 0;

  // Render the frame
  virtual void renderFrame(const std::vector<core::IAppElementPtr> &elements,
                           IRenderContext const &ctx) = 0;

  // Complete the frame
  virtual void endFrame(IRenderContext const &ctx) = 0;

  // Present the completed frame (dont' call for headless backends)
  virtual void present() = 0;
  virtual void advance() = 0;

  //----------------------------------------------------------
  // Runtime control
  //----------------------------------------------------------
  virtual void setVsync(bool enabled) { m_vsyncWanted = enabled; };
  virtual bool isVsync() const { return m_vsync; };

  //----------------------------------------------------------
  // Window / output surface control
  //----------------------------------------------------------
  void onResize(const WindowSize &size) {
    // Check for DPI scaling and adjust the font size
    float xscale, yscale;
    glfwGetWindowContentScale(m_windowHandle, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale *= xscale / m_dpiScale;
    m_dpiScale = xscale;
    m_viewportSize = size;
  }

  const WindowSize &getViewportSize() const { return m_viewportSize; }
  virtual void setWindow(GLFWwindow *windowHandle) {
    m_windowHandle = windowHandle;
  }
  void setWindowSize(const WindowSize &windowSize) {
    m_windowSize = windowSize;
  };
  const WindowSize &getWindowSize() const { return m_windowSize; }

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  virtual void freeResourcesQueue() {};

protected:
  //  Window stuff
  GLFWwindow *m_windowHandle{nullptr}; // GLFW Window
  WindowSize m_viewportSize{0, 0};     // Size of the viewport
  WindowSize m_windowSize{0, 0};       // Size of window
  float m_dpiScale = 1.0f;

  // Vsync
  bool m_vsyncWanted{true}; // Wanting swapchain with vsync
  bool m_vsync{true};

  std::vector<std::vector<std::function<void()>>>
      m_resourceFreeQueue; // Queue of functions to free resources

  core::IGUISystemPtr m_gui = nullptr;
};
