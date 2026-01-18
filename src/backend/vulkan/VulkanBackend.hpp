#pragma once

#include <filesystem>
#include <memory>
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
  // ---------------------------------------------------------------------------
  // Lifecycle & Initialization
  // ---------------------------------------------------------------------------
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo& appInfo);

  void init(const core::ApplicationCreateInfo& appInfo) override;
  void deinit() override;
  void freeResourcesQueue() override;

  // ---------------------------------------------------------------------------
  // Core Frame Loop (Execution Flow)
  // ---------------------------------------------------------------------------
  void newFrame() override;
  bool beginFrame(FrameContext& frame) override;
  void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                   FrameContext const& frame) override;
  void endFrame(FrameContext const& frame) override;
  void present() override;
  void advance() override;

  // ---------------------------------------------------------------------------
  // Synchronization & Commands
  // ---------------------------------------------------------------------------
  void waitForDeviceIdle() override;

  // Single-Time Commands (e.g., used for resource uploading or layout transitions)
  VkCommandBuffer startSingleTimeCmd();
  void endSingleTimeCmd(VkCommandBuffer cmd);

  VkCommandBuffer getActiveCmd() const;

  // ---------------------------------------------------------------------------
  // Configuration & IO
  // ---------------------------------------------------------------------------
  void setVsync(bool enabled) override;
  void requestScreenshot(const std::filesystem::path& filename, int quality) override;

  // ---------------------------------------------------------------------------
  // Accessors (Getters)
  // ---------------------------------------------------------------------------
  // Backend & Device Properties
  uint32_t getFrameCycleSize() const;
  VkDevice getDevice() const { return m_vkContext.getDevice(); }
  VkPhysicalDevice getPhysicalDevice() const { return m_vkContext.getPhysicalDevice(); }
  const nvvk::QueueInfo& getQueueInfo(uint32_t index) const
  {
    return m_vkContext.getQueueInfo(index);
  }

  // Resource Management Accessors
  nvvk::ResourceAllocator& allocator() { return m_allocator; }
  const nvvk::ResourceAllocator& allocator() const { return m_allocator; }

  nvvk::StagingUploader& stagingUploader() { return m_stagingUploader; }
  const nvvk::StagingUploader& stagingUploader() const { return m_stagingUploader; }

  VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
  VkCommandPool transientCmdPool() const { return m_transientCmdPool; }

private:
  // Constructor is private to enforce use of create() factory
  VulkanBackend() = default;

  // ---------------------------------------------------------------------------
  // Internal Initialization Helpers
  // ---------------------------------------------------------------------------
  bool initVulkan(const core::ApplicationCreateInfo& appInfo);
  void setupImGuiVulkanBackend();
  void createFrameSubmission(uint32_t numFrames);

  // ---------------------------------------------------------------------------
  // Internal Rendering Helpers
  // ---------------------------------------------------------------------------
  void waitForFrameCompletion() const;
  void beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const;
  void endDynamicRenderingToSwapchain(VkCommandBuffer cmd);
  void renderToSwapchain(VkCommandBuffer cmd);

  // ---------------------------------------------------------------------------
  // Member Variables
  // ---------------------------------------------------------------------------

  // 1. Core Vulkan Resources
  nvvk::Context m_vkContext{};
  nvvk::Swapchain m_swapchain{};
  VkSurfaceKHR m_surface{VK_NULL_HANDLE};
  VkCommandPool m_transientCmdPool{};
  VkDescriptorPool m_descriptorPool{};
  nvvk::ResourceAllocator m_allocator{};
  nvvk::StagingUploader m_stagingUploader{};
  uint32_t m_maxTexturePool{128};

  // 2. Frame Synchronization (Ring Buffer)
  std::vector<std::unique_ptr<VulkanRenderContext>> m_frameData{};
  VkSemaphore m_frameTimelineSemaphore{};
  uint32_t m_frameRingCurrent{0};

  // 3. Submission Control
  std::vector<VkSemaphoreSubmitInfo> m_waitSemaphores{};
  std::vector<VkSemaphoreSubmitInfo> m_signalSemaphores{};
  std::vector<VkCommandBufferSubmitInfo> m_commandBuffers{};
};

}  // namespace core
