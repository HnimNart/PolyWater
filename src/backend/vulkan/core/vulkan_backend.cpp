#include "vulkan_backend.hpp"

// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION

// 2. Include the library headers that need implementation
#include <backends/imgui_impl_vulkan.h>
#include <vk_mem_alloc.h>  // Assuming VMA is included via this or similar

#include "vulkan_context_manager.hpp"
#include "vulkan_frame_synchronization_manager.hpp"
#include "vulkan_swapchain_render_manager.hpp"
#include "app/gui_system_interface.hpp"
#include "backend/vulkan/gui/vulkan_imgui_system.hpp"
#include "core/profiler.hpp"

/**********************************************************/
std::unique_ptr<VulkanBackend>
VulkanBackend::create(const app::ApplicationCreateInfo& appInfo)
/**********************************************************/
{
  auto backend = std::unique_ptr<VulkanBackend>(new VulkanBackend());
  if (!backend->initVulkan(appInfo))
  {
    return nullptr;
  }

  return backend;
}

/**********************************************************/
bool VulkanBackend::initVulkan(const app::ApplicationCreateInfo& appInfo)
/**********************************************************/
{
  m_coreManager = std::make_unique<VulkanContextManager>();
  bool ok = m_coreManager->init(appInfo);
  if (!ok)
  {
    return false;
  }
  m_frameSyncManager = std::make_unique<VulkanFrameSynchronizationManager>();
  uint32_t numFrames = appInfo.headless ? 2 : 3;
  m_frameSyncManager->init(*m_coreManager, numFrames);

  return ok;
}

/**********************************************************/
void VulkanBackend::initPresentation(GLFWwindow* windowHandle,
                                     std::shared_ptr<app::IGUISystem> gui)
/**********************************************************/
{
  m_windowHandle = windowHandle;
  if (m_windowHandle)
  {
    m_swapchainManager = std::make_unique<VulkanSwapchainRenderManager>();
    m_swapchainManager->init(*m_coreManager, m_windowHandle);
    m_swapchainManager->setUICallback(std::bind(
        &VulkanBackend::recordRegistryCommands, this, std::placeholders::_1));
  }

  if (!gui)
  {
    return;
  }
  auto vulkan_gui = dynamic_pointer_cast<VulkanImGuiSystem>(gui);
  if (!vulkan_gui)
  {
    throw std::runtime_error(
        "GUI system given VulkanBackend is not a VulkanImGuiSystem");
  }

  uint numFrames = m_frameSyncManager->getFrameCycleSize();
  VkFormat imageFormat =
      m_windowHandle ? m_swapchainManager->getSwapchain().getImageFormat()
                     : VK_FORMAT_B8G8R8A8_UNORM;
  vulkan_gui->initVulkanBackend(*m_coreManager, numFrames, imageFormat,
                                m_windowHandle);
  m_renderRegistry.registerElement(vulkan_gui);
}

/**********************************************************/
void VulkanBackend::initProfiler(core::ProfilerTimeline* timeline)
/**********************************************************/
{
#ifdef PROFILE_APP
  m_profileTimeline = timeline;
  if (m_profileTimeline)
  {
    m_gpuTimer.init(m_profileTimeline, getDevice(), getPhysicalDevice(),
                    getQueueInfo(0).familyIndex, true);
  }
#endif
}

/**********************************************************/
void VulkanBackend::deinit()
/**********************************************************/
{

  if (m_coreManager)
  {
    m_coreManager->waitForDeviceIdle();
  }

#ifdef PROFILE_APP
  if (m_profileTimeline)
  {
    m_gpuTimer.deinit();
    m_profileTimeline = nullptr;
  }
#endif

  if (m_swapchainManager)
  {
    m_swapchainManager->deinit(*m_coreManager);
  }

  if (m_frameSyncManager)
  {
    m_frameSyncManager->deinit(*m_coreManager);
  }

  if (m_coreManager)
  {
    m_coreManager->deinit();
  }
}

/**********************************************************/
IRenderContext& VulkanBackend::getCurrentContext()
/**********************************************************/
{
  return *m_frameSyncManager->getActiveFrameContext();
}

/**********************************************************/
IRenderContext* VulkanBackend::beginFrame()
/**********************************************************/
{

  m_frameSyncManager->waitForFrameCompletion();
  if (m_swapchainManager && !m_swapchainManager->beginFrame(*m_coreManager))
  {
    return nullptr;
  }
  return m_frameSyncManager->beginFrame();
}

/**********************************************************/
void VulkanBackend::renderFrame(
    const std::vector<std::shared_ptr<app::IAppElement>>& elements,
    IRenderContext const& frame)
/**********************************************************/
{

#ifdef PROFILE_APP
  const VulkanRenderContext& vkCtx = VulkanRenderContext::get(frame);
  auto profiledSection =
      m_gpuTimer.cmdFrameSection(vkCtx.cmdBuffer, "renderFrame");
#endif

  for (const std::shared_ptr<app::IAppElement>& e : elements)
  {
    e->onRender(frame);
  }

  for (const std::shared_ptr<app::IAppElement>& e : elements)
  {
    e->onEndFrame(frame);
  }

  // Add swapchain semaphores
  if (m_windowHandle)
  {
    m_frameSyncManager->addWaitSemaphore({
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore =
            m_swapchainManager->getSwapchain().getImageAvailableSemaphore(),
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    });
    m_frameSyncManager->addSignalSemaphore({
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore =
            m_swapchainManager->getSwapchain().getRenderFinishedSemaphore(),
        .stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    });
  }
}

/**********************************************************/
void VulkanBackend::endFrame(IRenderContext const& frameCtx)
/**********************************************************/
{
  const VulkanRenderContext& vkCtx =
      static_cast<VulkanRenderContext const&>(frameCtx);

  // Pre-submit: any command buffers that passes registered before the main
  // buffer (e.g. OIDNDenoisePass GBuffer→OIDN copies).  Submitted without
  // wait semaphores so the GPU can start immediately; the associated signal
  // semaphores (e.g. Vulkan→CUDA timeline) let the next stage proceed.
  if (!vkCtx.preCommandBuffers.empty())
  {
    const VkSubmitInfo2 preSubmit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount =
            static_cast<uint32_t>(vkCtx.preCommandBuffers.size()),
        .pCommandBufferInfos = vkCtx.preCommandBuffers.data(),
        .signalSemaphoreInfoCount =
            static_cast<uint32_t>(vkCtx.preSignalSemaphores.size()),
        .pSignalSemaphoreInfos = vkCtx.preSignalSemaphores.data(),
    };
    NVVK_CHECK(vkQueueSubmit2(getQueueInfo(0).queue, 1, &preSubmit,
                              VK_NULL_HANDLE));
  }

  m_frameSyncManager->endFrame(vkCtx);

  const VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount =
          uint32_t(m_frameSyncManager->getWaitSemaphores().size()),
      .pWaitSemaphoreInfos = m_frameSyncManager->getWaitSemaphores().data(),
      .commandBufferInfoCount =
          uint32_t(m_frameSyncManager->getCommandBuffers().size()),
      .pCommandBufferInfos = m_frameSyncManager->getCommandBuffers().data(),
      .signalSemaphoreInfoCount =
          uint32_t(m_frameSyncManager->getSignalSemaphores().size()),
      .pSignalSemaphoreInfos = m_frameSyncManager->getSignalSemaphores().data(),
  };

  NVVK_CHECK(vkQueueSubmit2(getQueueInfo(0).queue, 1, &submitInfo, nullptr));
}

/**********************************************************/
void VulkanBackend::recordRegistryCommands(IRenderContext const& frame)
/**********************************************************/
{
  for (auto& renderable : m_renderRegistry.getElements())
  {
    renderable->onRender(frame);
  }
}

/**********************************************************/
void VulkanBackend::present()
/**********************************************************/
{
  m_swapchainManager->present(*m_coreManager);
}

/**********************************************************/
void VulkanBackend::advance()
/**********************************************************/
{
  m_frameSyncManager->advance();
}

/**********************************************************/
VkDevice VulkanBackend::getDevice() const
/**********************************************************/
{
  return m_coreManager->getDevice();
}

/**********************************************************/
VkPhysicalDevice VulkanBackend::getPhysicalDevice() const
/**********************************************************/
{
  return m_coreManager->getPhysicalDevice();
}

/**********************************************************/
VkInstance VulkanBackend::getInstance() const
/**********************************************************/
{
  return m_coreManager->getInstance();
}

/**********************************************************/
const nvvk::QueueInfo& VulkanBackend::getQueueInfo(uint32_t index) const
/**********************************************************/
{
  return m_coreManager->getQueueInfo(index);
}

/**********************************************************/
VulkanContextManager* VulkanBackend::getContextManager() const
/**********************************************************/
{
  assert(m_coreManager != nullptr);
  return m_coreManager.get();
}

/**********************************************************/
VulkanFrameSynchronizationManager* VulkanBackend::getFrameSyncManager() const
/**********************************************************/
{
  assert(m_frameSyncManager != nullptr);
  return m_frameSyncManager.get();
}

/**********************************************************/
VulkanSwapchainRenderManager* VulkanBackend::getSwapchainManager() const
/**********************************************************/
{
  assert(m_swapchainManager != nullptr);
  return m_swapchainManager.get();
}

/**********************************************************/
RenderRegistry& VulkanBackend::getRegistry()
/**********************************************************/
{
  return m_renderRegistry;
}

/**********************************************************/
void VulkanBackend::waitForDeviceIdle()
/**********************************************************/
{
  m_coreManager->waitForDeviceIdle();
}

/**********************************************************/
void VulkanBackend::setVsync(bool enabled)
/**********************************************************/
{
  IRenderBackend::setVsync(enabled);
  if (m_swapchainManager)
  {
    m_swapchainManager->setVsync(enabled);
  }
}
