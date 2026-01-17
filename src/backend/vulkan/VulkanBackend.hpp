#pragma once

#include <vector>

#include <nvvk/context.hpp>
#include <nvvk/resource_allocator.hpp>
#include <nvvk/staging.hpp>
#include <nvvk/swapchain.hpp>

#include "VulkanRenderContext.hpp"
#include "backend/IRenderBackend.hpp"
#include "core/application/App.hpp"

namespace core
{

class VulkanBackend final : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo& appInfo);

  // Lifecycle
  void init(const core::ApplicationCreateInfo& appInfo) override;
  void deinit() override;

  // Frame Loop
  void newFrame() override;

  VkCommandBuffer startSingleTimeCmd();
  void endSingleTimeCmd(VkCommandBuffer cmd);
  void waitForDeviceIdle();

  VkCommandBuffer getActiveCmd() const;

  bool beginFrame(FrameContext& frame) override;
  void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                   FrameContext const& frame) override;
  void endFrame(FrameContext const& frame) override;
  void present() override;

  void requestScreenshot(const std::filesystem::path& filename, int quality) override;

  void setVsync(bool enabled) override;

  // Getters
  uint32_t getFrameCycleSize() const;

  void freeResourcesQueue() override;

  VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
  nvvk::ResourceAllocator& allocator() { return m_allocator; }
  const nvvk::ResourceAllocator& allocator() const { return m_allocator; }
  nvvk::StagingUploader& stagingUploader() { return m_stagingUploader; }
  const nvvk::StagingUploader& stagingUploader() const { return m_stagingUploader; }
  const nvvk::QueueInfo& getQueueInfo(uint32_t index) const
  {
    return m_vkContext.getQueueInfo(index);
  }
  VkCommandPool transientCmdPool() const { return m_transientCmdPool; };  // The command pool
  VkDevice getDevice() const { return m_vkContext.getDevice(); }
  VkPhysicalDevice getPhysicalDevice() const { return m_vkContext.getPhysicalDevice(); }

private:
  VulkanBackend() = default;
  bool initVulkan(const core::ApplicationCreateInfo& appInfo);

  void setupImGuiVulkanBackend(ImGuiConfigFlags configFlags);
  void createFrameSubmission(uint32_t numFrames);
  void waitForFrameCompletion() const;

  void beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const;
  void endDynamicRenderingToSwapchain(VkCommandBuffer cmd);

  // Vulkan resources
  nvvk::Context m_vkContext{};
  nvvk::Swapchain m_swapchain{};
  VkSurfaceKHR m_surface{VK_NULL_HANDLE};
  VkCommandPool m_transientCmdPool{};   // The command pool
  VkDescriptorPool m_descriptorPool{};  // Application descriptor pool
  nvvk::ResourceAllocator m_allocator{};
  nvvk::StagingUploader m_stagingUploader{};
  uint32_t m_maxTexturePool{128};  // Maximum number of textures in the descriptor pool

  // Concrete context storage (one per frame in flight)
  std::vector<std::unique_ptr<VulkanRenderContext>> m_frameData{};
  VkSemaphore m_frameTimelineSemaphore{};  // Timeline semaphore used to synchronize CPU submission
                                           // with GPU completion
  uint32_t m_frameRingCurrent{0};          // Current frame index in the ring buffer (cycles through
                                           // available frames) : static for resource free queue
  // Fine control over the frame submission
  std::vector<VkSemaphoreSubmitInfo> m_waitSemaphores{};  // Possible extra frame wait semaphores
  std::vector<VkSemaphoreSubmitInfo>
      m_signalSemaphores{};  // Possible extra frame signal semaphores
  std::vector<VkCommandBufferSubmitInfo>
      m_commandBuffers{};  // Possible extra frame command buffers
};

}  // namespace core

// namespace nvapp
