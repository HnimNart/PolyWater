#pragma once

#include <vulkan/vulkan_core.h>

#include <functional>

#include "backend/interfaces/render_context_interface.hpp"
#include "nvvk/swapchain.hpp"

struct GLFWwindow;

namespace vkb
{

class VulkanContextManager;
class VulkanFrameSynchronizationManager;

class VulkanSwapchainRenderManager
{
public:
  VulkanSwapchainRenderManager() = default;
  ~VulkanSwapchainRenderManager() = default;

  // Initialization & Cleanup
  void init(VulkanContextManager& coreManager, GLFWwindow* windowHandle);
  void deinit(VulkanContextManager& coreManager);

  // Frame Lifecycle
  bool beginFrame(VulkanContextManager& coreManager);
  void present(VulkanContextManager& coreManager);

  // Getters
  VkImage getOutputImage() const;
  VkImageView getOutputImageView() const;
  VkExtent2D getWindowSize() const
  {
    return m_windowSize;
  }
  bool getVsync() const
  {
    return m_vsyncWanted;
  }

  const nvvk::Swapchain& getSwapchain() const
  {
    return m_swapchain;
  }

  // Setters
  void setWindowSize(VkExtent2D size)
  {
    m_windowSize = size;
  }
  void setVsync(bool enabled);

  using RenderCallback = std::function<void(const IRenderContext& ctx)>;
  void setUICallback(const RenderCallback& renderCallback);
  RenderCallback getUICallback() const
  {
    return m_uiCallback;
  }

private:
  void reportSwapchainDiagnostics(VkInstance instance,
                                  nvvk::Swapchain::InitInfo& swapchainParams);
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  nvvk::Swapchain m_swapchain;

  VkExtent2D m_windowSize = {1280, 720};
  bool m_vsyncWanted = true;

  RenderCallback m_uiCallback = nullptr;
};
}  // namespace vkb
