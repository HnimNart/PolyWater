#pragma once

#include <vector>

#include <nvvk/context.hpp>
#include <nvvk/resource_allocator.hpp>
#include <nvvk/staging.hpp>
#include <nvvk/swapchain.hpp>

#include "VulkanFrameContext.hpp"
#include "backend/IRenderBackend.hpp"
#include "core/application/App.hpp"
#include "shaders/compiler/slang.hpp"

namespace core
{

class VulkanBackend final : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo& appInfo);

  bool initVulkan(const core::ApplicationCreateInfo& appInfo);

  ~VulkanBackend() override = default;

  // Lifecycle
  void init(const core::ApplicationCreateInfo& appInfo) override;
  void deinit() override;

  // Frame Loop
  void newFrame() override;

  VkCommandBuffer start_single_time_cmd();
  void end_single_time_cmd(VkCommandBuffer cmd);

  VkCommandBuffer get_active_cmd() const;

  bool beginFrame(FrameContext& frame) override;
  void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                   FrameContext const& frame) override;
  void endFrame(FrameContext const& frame) override;
  void present() override;

  void requestScreenshot(const std::filesystem::path& filename, int quality) override;

  // Getters
  uint32_t getFrameCycleSize() const;
  void setWindowSize(const WindowSize& windowSize) override;

  void freeResourcesQueue() override;

  [[deprecated]] const nvvk::Context& get_context() const { return m_vkContext; }
  [[deprecated]] nvvk::Context& get_context() { return m_vkContext; }
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

  const VkExtent2D& get_view_port_size() const { return m_windowSize; }

private:
  VulkanBackend() = default;

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
  std::vector<std::unique_ptr<VulkanFrameContext>> m_frameData{};
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

  // Misc
  VkExtent2D m_windowSize{0, 0};
};

}  // namespace core

// namespace nvapp
