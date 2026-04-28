#pragma once

#ifdef __APPLE__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "IRenderable.hpp"
#include "app/AppInfo.hpp"
#include "app/IGUISystem.hpp"

class MetalContextManager;
struct GLFWwindow;

//------------------------------------------------------------
// ImGuiMetalSystem
//------------------------------------------------------------
// Implements IGUISystem and IRenderable for the Metal backend.
// Uses imgui_impl_glfw (platform) and imgui_impl_metal (renderer).
//
// Usage mirrors ImGuiVulkanSystem:
//   1. Call init() to create the ImGui context.
//   2. Call initMetalBackend() (from MetalBackend::initPresentation) to
//      connect the Metal device and GLFW window.
//   3. The Application framework drives the frame loop:
//      beginFrame() → renderMenu() / UI → render() → (backend renders) → endFrame()
class ImGuiMetalSystem : public app::IGUISystem, public IRenderable {
public:
  ImGuiMetalSystem()  = default;
  ~ImGuiMetalSystem() override;

  // Lifecycle
  void init(const app::ApplicationCreateInfo &info) override;
  void deinit() override;

  // Frame operations
  void beginFrame() override;
  void endFrame() override;
  void render() override;

  // UI rendering
  void renderMenu(const std::vector<std::shared_ptr<app::IAppElement>> &elements) override;
  bool getWindowSize(const std::string &windowName, WindowSize &size) override;
  void setWindowSize(const WindowSize &size) override;

  // Configuration
  void setConfigFlags(unsigned int flags) override;
  void loadSettings(const char *filename) override;
  void saveSettings(const char *filename) override;

  // DPI change notification (called by IRenderBackend::onResize).
  void onDpiScaleChanged(float scaleRatio) override;

  // Sets an optional ImGui dock-layout callback, invoked once on the first
  // rendered frame.  The argument is the root ImGuiID of the dock space.
  // If not set, a default layout (viewport + left settings panel) is used.
  void setDockSetup(std::function<void(ImGuiID)> fn);

  // Metal-specific initialization (called by MetalBackend::initPresentation)
  void initMetalBackend(MetalContextManager &contextManager,
                        GLFWwindow *windowHandle);

private:
  // Initialization helpers
  void setupImGui(const app::ApplicationCreateInfo &info);
  void destroyContext();
  void configureImGuiIO(const app::ApplicationCreateInfo &info);
  void initializeFonts();

  // Metal backend helpers
  void initializeGlfwBackend(GLFWwindow *windowHandle);
  void initializeMetalBackend(MetalContextManager &contextManager);
  void shutdownMetalBackend();

  // Docking helpers
  void setupImguiDock();
  void setupDefaultDockLayout(ImGuiID dockID);
  void createDefaultLayout(ImGuiID dockID);

  // IRenderable - records ImGui draw commands into the Metal render encoder.
  // Note: ImGui_ImplMetal_NewFrame is called here (not in beginFrame) because
  // the render pass descriptor (needed to determine the pixel format) is only
  // available after MetalBackend::beginFrame() has acquired a drawable.
  void onRender(const IRenderContext &ctx) override;

  // State
  bool m_contextCreated   = false;
  bool m_metalInitialized = false;
  bool m_glfwInitialized  = false;

  // Configuration
  std::string                    m_iniFilename;
  std::function<void(ImGuiID)>   m_dockSetup;
};

#endif // __APPLE__
