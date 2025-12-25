#pragma once

#include <vector>

#include <nvvk/context.hpp>
#include <nvvk/swapchain.hpp>

#include "VulkanRenderContext.hpp"  // We need the concrete type here
#include "backend/IRenderBackend.hpp"
#include "core/application/App.hpp"

class GLFWwindow;

namespace core
{

class VulkanBackend final : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend> create(const core::ApplicationCreateInfo appInfo);
  VulkanBackend(nvvk::Context& vkContext, GLFWwindow* window);
  ~VulkanBackend() override = default;

  // Lifecycle
  void init() override;
  void shutdown() override;

  // Frame Loop
  bool beginFrame(FrameContext& frame) override;
  void renderFrame(FrameContext const& frame) override;
  void endFrame(FrameContext const& frame) override;
  void present() override;

  // Controls
  void setVsync(bool enabled) override { m_vsyncWanted = enabled; }
  bool isVsync() const override { return m_vsync; }
  void resize(const WindowSize& size) override;
  void requestScreenshot(const std::filesystem::path& filename, int quality) override;
  inline uint32_t getFrameCycleSize() const { return uint32_t(m_frameData.size()); }
  const WindowSize getViewportSize() const override
  {
    return {m_viewportSize.width, m_viewportSize.height};
  }

private:
  void createFrameSubmission(uint32_t numFrames);
  void waitForFrameCompletion() const;

  bool m_vsyncWanted{true};  // Wanting swapchain with vsync

  GLFWwindow* m_windowHandle{nullptr};  // GLFW Window
  VkExtent2D m_windowSize{0, 0};        // Size of the window
  VkExtent2D m_viewportSize{0, 0};      // Size of the viewport
  float m_dpiScale = 1.0f;

  // Vulkan resources
  nvvk::Context& m_vkContext;
  nvvk::Swapchain m_swapchain;
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
  std::vector<VkSemaphoreSubmitInfo> m_waitSemaphores;    // Possible extra frame wait semaphores
  std::vector<VkSemaphoreSubmitInfo> m_signalSemaphores;  // Possible extra frame signal semaphores
  std::vector<VkCommandBufferSubmitInfo> m_commandBuffers;  // Possible extra frame command buffers
  bool m_vsync{true};

  // Misc
  bool m_screenShotRequested = false;
  int m_screenShotFrame = 0;
  std::filesystem::path m_screenShotFilename;
};

}  // namespace core

// namespace nvapp