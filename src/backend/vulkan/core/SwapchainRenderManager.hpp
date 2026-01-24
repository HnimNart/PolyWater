#pragma once

#include <vulkan/vulkan_core.h>

#include <nvvk/swapchain.hpp>

#include "core/application/AppInfo.hpp"

class VulkanCoreManager;
class FrameSynchronizationManager;

class SwapchainRenderManager
{
public:
  void init(VulkanCoreManager& coreManager, FrameSynchronizationManager& frameSyncManager,
            GLFWwindow* windowHandle, const core::ApplicationCreateInfo& appInfo);
  void deinit(VulkanCoreManager& coreManager);

  // Swapchain management
  bool beginFrame(VulkanCoreManager& coreManager);
  void renderToSwapchain(VkCommandBuffer cmd);
  void present(VulkanCoreManager& coreManager);

  // Accessors
  bool isHeadless() const { return m_headless; }
  VkExtent2D getWindowSize() const { return m_windowSize; }
  void setWindowSize(VkExtent2D size) { m_windowSize = size; }

  // Vsync control
  void setVsync(bool enabled);
  bool getVsync() const { return m_vsyncWanted; }

  const nvvk::Swapchain& getSwapchain() const { return m_swapchain; };

private:
  void reportSwapchainDiagnostics(VkInstance instance, nvvk::Swapchain::InitInfo& swapchainParams);
  GLFWwindow* m_windowHandle = nullptr;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  nvvk::Swapchain m_swapchain;

  VkExtent2D m_windowSize = {1280, 720};
  bool m_vsyncWanted = true;
  bool m_headless = false;

  void setupImGuiVulkanBackend(VulkanCoreManager& coreManager, uint32_t framesInFlight);
  void beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const;
  void endDynamicRenderingToSwapchain(VkCommandBuffer cmd) const;
  void rebuildSwapchainIfNeeded();
};
