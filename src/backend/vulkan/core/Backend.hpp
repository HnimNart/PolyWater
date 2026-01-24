#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>

#include <nvvk/queue.hpp>

#include "ContextManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "SwapchainRenderManager.hpp"
#include "backend/interfaces/IRenderBackend.hpp"

class VulkanBackend : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo& appInfo);

  void init(const core::ApplicationCreateInfo& info) override;
  void deinit() override;

  // Frame lifecycle
  void newFrame() override;
  bool beginFrame(IRenderContext& frame) override;
  void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                   IRenderContext const& frame) override;
  void endFrame(IRenderContext const& frame) override;
  void present() override;
  void advance() override;

  void waitForDeviceIdle() override;
  void setVsync(bool enabled) override;

  // Manager accessors
  VulkanContextManager* getCoreManager() const;
  FrameSynchronizationManager* getFrameSyncManager() const;
  SwapchainRenderManager* getSwapchainManager() const;

private:
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

  bool initVulkan(const core::ApplicationCreateInfo& appInfo);
};
