#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>

#include <nvvk/queue.hpp>

#include "ContextManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "SwapchainRenderManager.hpp"
#include "backend/interfaces/IRenderBackend.hpp"
#include "backend/vulkan/gui/IRenderable.h"
#include "app/IGUISystem.hpp"

class ImGuiVulkanSystem;

class VulkanBackend : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend>
  create(const core::ApplicationCreateInfo& appInfo);

  void initPresentation(GLFWwindow* window, core::IGUISystemPtr gui) override;
  void deinit() override;

  // Frame lifecycle
  IRenderContext& getCurrentContext() override;
  IRenderContext* beginFrame() override;
  void renderFrame(const std::vector<core::IAppElementPtr>& elements,
                   IRenderContext const& ctx) override;
  void endFrame(IRenderContext const& ctx) override;
  void present() override;
  void advance() override;

  void waitForDeviceIdle() override;
  void setVsync(bool enabled) override;

  // Manager accessors
  VulkanContextManager* getContextManager() const;
  FrameSynchronizationManager* getFrameSyncManager() const;
  SwapchainRenderManager* getSwapchainManager() const;
  RenderRegistry& getRegistry();

private:
  void recordRegistryCommands(IRenderContext const& frame);
  IRenderContext* getRenderContext();
  // Utility
  VkDevice getDevice() const;
  VkPhysicalDevice getPhysicalDevice() const;
  VkInstance getInstance() const;
  const nvvk::QueueInfo& getQueueInfo(uint32_t index) const;

  VulkanBackend() = default;

  std::unique_ptr<VulkanContextManager> m_coreManager;
  std::unique_ptr<FrameSynchronizationManager> m_frameSyncManager;
  std::unique_ptr<SwapchainRenderManager> m_swapchainManager;

  RenderRegistry m_renderRegistry{};
  bool initVulkan(const core::ApplicationCreateInfo& appInfo);
};
