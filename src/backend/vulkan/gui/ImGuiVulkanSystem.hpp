#pragma once

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <string>
#include <vector>

#include "IRenderable.hpp"
#include "app/AppInfo.hpp"
#include "app/IGUISystem.hpp"

class VulkanContextManager;
class FrameSynchronizationManager;
class SwapchainRenderManager;
struct GLFWwindow;

namespace core
{
class IAppElement;
}

class ImGuiVulkanSystem : public app::IGUISystem, public IRenderable
{
public:
  ImGuiVulkanSystem() = default;
  ~ImGuiVulkanSystem() override;

  // Lifecycle
  void init(const app::ApplicationCreateInfo& info) override;
  void deinit() override;

  // Frame Operations
  void beginFrame() override;
  void endFrame() override;
  void render() override;

  // UI Rendering
  void renderMenu(const std::vector<app::IAppElementPtr>& elements) override;
  bool getWindowSize(const std::string& windowName, WindowSize& size) override;
  void setWindowSize(const WindowSize& size) override;

  // Configuration
  void setConfigFlags(unsigned int flags) override;
  void loadSettings(const char* filename) override;
  void saveSettings(const char* filename) override;

  // DPI change notification (called by IRenderBackend::onResize).
  void onDpiScaleChanged(float scaleRatio) override;

  // Sets an optional ImGui dock-layout callback, invoked once on the first
  // rendered frame.  The argument is the root ImGuiID of the dock space.
  // If not set, a default layout (viewport + left settings panel) is used.
  void setDockSetup(std::function<void(ImGuiID)> fn);

  // Vulkan Backend
  void initVulkanBackend(VulkanContextManager& coreManager,
                         uint maxFramesInFlight, VkFormat imageFormat,
                         GLFWwindow* windowHandle);

  void onRender(const IRenderContext& ctx) override;

private:
  // Initialization Helpers
  void setupImGui(const app::ApplicationCreateInfo& info);
  void destroyContext();
  void configureImGuiIO(const app::ApplicationCreateInfo& info);
  void initializeFonts();

  // Vulkan Backend Helpers
  void initializeGlfwBackend(GLFWwindow* windowHandle);
  void initializeVulkanBackend(VulkanContextManager& coreManager,
                               uint maxFramesInFlight, VkFormat imageFormat);

  void shutdownVulkanBackend();

  // Docking Helpers
  void setupImguiDock();
  void setupDefaultDockLayout(ImGuiID dockID);
  void createDefaultLayout(ImGuiID dockID);

  // Viewport Helpers
  void renderViewports();

  // State
  bool m_contextCreated = false;
  bool m_vulkanInitialized = false;
  bool m_glfwInitialized = false;

  // Configuration
  std::string m_iniFilename;
  std::function<void(ImGuiID)> m_dockSetup;
};
