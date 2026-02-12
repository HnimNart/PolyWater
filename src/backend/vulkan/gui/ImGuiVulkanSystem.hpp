#pragma once

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IRenderable.hpp"
#include "app/AppInfo.hpp"
#include "app/IGUISystem.hpp"

class VulkanContextManager;
class FrameSynchronizationManager;
class SwapchainRenderManager;
class GLFWwindow;

namespace core {
class IAppElement;
}

class ImGuiVulkanSystem : public core::IGUISystem, public IRenderable {
public:
  ImGuiVulkanSystem() = default;
  ~ImGuiVulkanSystem() override;

  // Lifecycle
  void init(const core::ApplicationCreateInfo &info) override;
  void deinit() override;

  // Frame Operations
  void beginFrame() override;
  void endFrame() override;
  void render() override;

  // UI Rendering
  void renderMenu(const std::vector<core::IAppElementPtr> &elements) override;
  bool getWindowSize(const std::string &windowName, WindowSize &size) override;
  void setWindowSize(const WindowSize &size) override;

  // Configuration
  void setConfigFlags(unsigned int flags) override;
  void loadSettings(const char *filename) override;
  void saveSettings(const char *filename) override;

  // Vulkan Backend
  void initVulkanBackend(VulkanContextManager &coreManager,
                         uint maxFramesInFlight, VkFormat imageFormat,
                         GLFWwindow *windowHandle);

  void onRender(const IRenderContext &ctx) override;

private:
  // Initialization Helpers
  void setupImGui(const core::ApplicationCreateInfo &info);
  void destroyContext();
  void configureImGuiIO(const core::ApplicationCreateInfo &info);
  void initializeFonts();

  // Vulkan Backend Helpers
  void initializeGlfwBackend(GLFWwindow *windowHandle);
  void initializeVulkanBackend(VulkanContextManager &coreManager,
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
