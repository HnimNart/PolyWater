#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>

#include <nvvk/profiler_vk.hpp>

#include "context_manager.hpp"
#include "frame_synchronization_manager.hpp"
#include "render_backend_interface.hpp"
#include "renderable_interface.hpp"
#include "swapchain_render_manager.hpp"
#include "app/gui_system_interface.hpp"
#include "nvvk/queue.hpp"

class ImGuiVulkanSystem;

class VulkanBackend final : public IRenderBackend
{
public:
  static std::unique_ptr<VulkanBackend>
  create(const app::ApplicationCreateInfo& appInfo);

  void initPresentation(GLFWwindow* window, app::IGUISystemPtr gui) override;
  void initProfiler(core::ProfilerTimeline* timeline) override;
  void deinit() override;

  // Frame lifecycle
  IRenderContext& getCurrentContext() override;
  IRenderContext* beginFrame() override;
  void renderFrame(const std::vector<app::IAppElementPtr>& elements,
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
  bool initVulkan(const app::ApplicationCreateInfo& appInfo);

  // Profiling
#ifdef PROFILE_APP
  core::ProfilerTimeline* m_profileTimeline = nullptr;
  nvvk::ProfilerGpuTimer m_gpuTimer;
#endif
};
