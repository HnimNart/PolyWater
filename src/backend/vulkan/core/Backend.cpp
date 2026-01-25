#include "Backend.hpp"

#include <backends/imgui_impl_vulkan.h>

#include <nvvk/validation_settings.hpp>

#include "ContextManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "SwapchainRenderManager.hpp"
#include "backend/vulkan/gui/ImGuiVulkanSystem.hpp"
#include "core/application/IGUISystem.hpp"

/**********************************************************/
std::unique_ptr<VulkanBackend>
VulkanBackend::create(const core::ApplicationCreateInfo& appInfo)
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
bool VulkanBackend::initVulkan(const core::ApplicationCreateInfo& appInfo)
/**********************************************************/
{
  m_coreManager = std::make_unique<VulkanContextManager>();
  return m_coreManager->init(appInfo);
}

/**********************************************************/
void VulkanBackend::init(const core::ApplicationCreateInfo& info)
/**********************************************************/
{
  // Initialize managers in correct order
  m_frameSyncManager = std::make_unique<FrameSynchronizationManager>();
  uint32_t numFrames = info.headless ? 2 : 3;
  m_frameSyncManager->init(*m_coreManager, numFrames);

  m_swapchainManager = std::make_unique<SwapchainRenderManager>();
  m_swapchainManager->init(*m_coreManager, *m_frameSyncManager, m_windowHandle,
                           info);
}

/**********************************************************/
void VulkanBackend::initializeGUIBackend(std::shared_ptr<core::IGUISystem> gui)
/**********************************************************/
{
  assert(m_coreManager);
  assert(m_frameSyncManager);
  assert(m_swapchainManager);
  assert(gui);
  auto vulkan_gui = dynamic_pointer_cast<ImGuiVulkanSystem>(gui);
  if (!vulkan_gui)
  {
    throw std::runtime_error(
        "GUI system given VulkanBackend is not a ImGuiVulkanSystem");
  }
  vulkan_gui->initVulkanBackend(*m_coreManager, *m_frameSyncManager,
                                *m_swapchainManager,
                                m_headless ? nullptr : m_windowHandle);

  m_renderRegistry.registerElement(vulkan_gui);
}

/**********************************************************/
void VulkanBackend::deinit()
/**********************************************************/
{
  if (m_coreManager)
  {
    m_coreManager->waitForDeviceIdle();
  }

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
  if (!m_swapchainManager->beginFrame(*m_coreManager))
  {
    return nullptr;
  }
  return m_frameSyncManager->beginFrame();
}

/**********************************************************/
void VulkanBackend::renderFrame(
    const std::vector<std::shared_ptr<core::IAppElement>>& elements,
    IRenderContext const& frame)
/**********************************************************/
{

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onPreRender();
  }

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onRender(frame);
  }

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onEndFrame(frame);
  }

  // Render to swapchain
  VkCommandBuffer cmd = m_frameSyncManager->getActiveCommandBuffer();

  auto callback =
      std::bind(&VulkanBackend::recordRegistryCommands, this, std::cref(frame));
  m_swapchainManager->renderToSwapchain(cmd, callback);

  // Add swapchain semaphores
  if (!m_swapchainManager->isHeadless())
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
  m_frameSyncManager->endFrame(
      static_cast<VulkanRenderContext const&>(frameCtx));

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
VulkanContextManager* VulkanBackend::getCoreManager() const
/**********************************************************/
{
  assert(m_coreManager != nullptr);
  return m_coreManager.get();
}

/**********************************************************/
FrameSynchronizationManager* VulkanBackend::getFrameSyncManager() const
/**********************************************************/
{
  assert(m_frameSyncManager != nullptr);
  return m_frameSyncManager.get();
}

/**********************************************************/
SwapchainRenderManager* VulkanBackend::getSwapchainManager() const
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
  m_swapchainManager->setVsync(enabled);
}
