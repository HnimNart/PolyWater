#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>

#include <nvvk/profiler_vk.hpp>

#include "vulkan_context_manager.hpp"
#include "vulkan_frame_synchronization_manager.hpp"
#include "render_backend_interface.hpp"
#include "renderable_interface.hpp"
#include "vulkan_swapchain_render_manager.hpp"
#include "app/gui_system_interface.hpp"
#include "nvvk/queue.hpp"

class VulkanImGuiSystem;

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
  VulkanFrameSynchronizationManager* getFrameSyncManager() const;
  VulkanSwapchainRenderManager* getSwapchainManager() const;
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
  std::unique_ptr<VulkanFrameSynchronizationManager> m_frameSyncManager;
  std::unique_ptr<VulkanSwapchainRenderManager> m_swapchainManager;

  RenderRegistry m_renderRegistry{};
  bool initVulkan(const app::ApplicationCreateInfo& appInfo);

  // Profiling
#ifdef PROFILE_APP
  core::ProfilerTimeline* m_profileTimeline = nullptr;
  nvvk::ProfilerGpuTimer m_gpuTimer;
#endif
};
