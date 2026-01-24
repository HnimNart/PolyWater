#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "RenderContext.hpp"
class VulkanCoreManager;

class FrameSynchronizationManager
{
public:
  void init(VulkanCoreManager& coreManager, uint32_t numFrames);
  void deinit(VulkanCoreManager& coreManager);

  // Frame lifecycle
  void waitForFrameCompletion() const;
  void beginFrame();
  void endFrame();
  void advance();

  // Accessors
  VkCommandBuffer getActiveCommandBuffer() const;
  VulkanRenderContext* getActiveFrameContext();
  const VulkanRenderContext* getActiveFrameContext() const;
  uint32_t getFrameCycleSize() const { return static_cast<uint32_t>(m_frameData.size()); }
  uint32_t getCurrentFrameIndex() const { return m_frameRingCurrent; }

  // Semaphore management
  void addWaitSemaphore(const VkSemaphoreSubmitInfo& semaphore);
  void addSignalSemaphore(const VkSemaphoreSubmitInfo& semaphore);
  void addCommandBuffer(const VkCommandBufferSubmitInfo& cmdBuffer);

  const std::vector<VkSemaphoreSubmitInfo>& getWaitSemaphores() const { return m_waitSemaphores; }
  const std::vector<VkSemaphoreSubmitInfo>& getSignalSemaphores() const
  {
    return m_signalSemaphores;
  }
  const std::vector<VkCommandBufferSubmitInfo>& getCommandBuffers() const
  {
    return m_commandBuffers;
  }

  void clearSemaphoresAndBuffers();
  VkSemaphore getFrameTimelineSemaphore() const { return m_frameTimelineSemaphore; }

private:
  std::vector<std::unique_ptr<VulkanRenderContext>> m_frameData;
  uint32_t m_frameRingCurrent = 0;

  VkSemaphore m_frameTimelineSemaphore = VK_NULL_HANDLE;

  std::vector<VkSemaphoreSubmitInfo> m_waitSemaphores;
  std::vector<VkSemaphoreSubmitInfo> m_signalSemaphores;
  std::vector<VkCommandBufferSubmitInfo> m_commandBuffers;

  void createFrameData(VulkanCoreManager& coreManager, uint32_t numFrames);
};
