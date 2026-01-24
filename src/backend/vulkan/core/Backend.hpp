#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>

#include <nvvk/queue.hpp>

#include "CoreManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "SwapchainRenderManager.hpp"
#include "backend/interfaces/IRenderBackend.hpp"

// class VulkanCoreManager;
// class FrameSynchronizationManager;
// class SwapchainRenderManager;

class VulkanBackend : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo& appInfo);
  // ~VulkanBackend() override = default;

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

  // Manager accessors
  VulkanCoreManager* getCoreManager() const;
  FrameSynchronizationManager* getFrameSyncManager() const;
  SwapchainRenderManager* getSwapchainManager() const;

  // Utility
  VkDevice getDevice() const;
  VkPhysicalDevice getPhysicalDevice() const;
  VkInstance getInstance() const;
  const nvvk::QueueInfo& getQueueInfo(uint32_t index) const;

  void waitForDeviceIdle() override;
  void setVsync(bool enabled) override;

  VkCommandBuffer startSingleTimeCmd();
  void endSingleTimeCmd(VkCommandBuffer cmd);

private:
  IRenderContext* getRenderContext();

  VulkanBackend() = default;

  std::unique_ptr<VulkanCoreManager> m_coreManager;
  std::unique_ptr<FrameSynchronizationManager> m_frameSyncManager;
  std::unique_ptr<SwapchainRenderManager> m_swapchainManager;

  bool initVulkan(const core::ApplicationCreateInfo& appInfo);
};
