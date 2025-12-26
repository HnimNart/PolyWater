#pragma once

#include <vector>

#include <nvvk/context.hpp>
#include <nvvk/swapchain.hpp>

#include "VulkanRenderContext.hpp"  // We need the concrete type here
#include "backend/IRenderBackend.hpp"
#include "backend/vulkan/VulkanContext.hpp"
#include "core/application/App.hpp"

class VulkanSceneRenderer;

namespace core
{

class VulkanBackend final : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend>
  create(const core::ApplicationCreateInfo& appInfo,  // Pass by const reference
         const std::vector<std::filesystem::path>& shaderDirs);

  bool initVulkan(const core::ApplicationCreateInfo& appInfo);

  ~VulkanBackend() override = default;

  // Lifecycle
  void init(const core::ApplicationCreateInfo& appInfo) override;
  void shutdown() override;

  // Frame Loop
  void newFrame() override;

  bool beginFrame(FrameContext& frame) override;
  void renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                   FrameContext const& frame) override;
  void endFrame(FrameContext const& frame) override;
  void present() override;

  void onResize(const WindowSize& size) override;
  void requestScreenshot(const std::filesystem::path& filename, int quality) override;

  // Getters
  uint32_t getFrameCycleSize() const;
  void setWindowSize(const WindowSize& windowSize) override;

  void freeResourcesQueue() override;

private:
  VulkanBackend(std::shared_ptr<SlangShaderCompiler> compiler);

  void setupImGuiVulkanBackend(ImGuiConfigFlags configFlags);
  void createFrameSubmission(uint32_t numFrames);
  void waitForFrameCompletion() const;

  void beginDynamicRenderingToSwapchain(VkCommandBuffer cmd) const;
  void endDynamicRenderingToSwapchain(VkCommandBuffer cmd);

  std::shared_ptr<VulkanSceneRenderer> m_render{};
  std::shared_ptr<VulkanContext> m_ctx{};
  std::shared_ptr<SlangShaderCompiler> m_compiler{};

  // Vulkan resources
  nvvk::Context m_vkContext{};
  nvvk::Swapchain m_swapchain{};
  VkSurfaceKHR m_surface{VK_NULL_HANDLE};
  VkCommandPool m_transientCmdPool{};   // The command pool
  VkDescriptorPool m_descriptorPool{};  // Application descriptor pool
  uint32_t m_maxTexturePool{128};       // Maximum number of textures in the descriptor pool

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

  // Misc
  VkExtent2D m_windowSize{0, 0};
};

}  // namespace core

// namespace nvapp