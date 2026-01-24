#include "Backend.hpp"

#include <imgui/backends/imgui_impl_vulkan.h>

#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/helpers.hpp>
#include <nvvk/swapchain.hpp>
#include <nvvk/validation_settings.hpp>

#include "CoreManager.hpp"
#include "FrameSynchronizationManager.hpp"
#include "SwapchainRenderManager.hpp"
#include "backend/vulkan/core/RenderContext.hpp"

std::unique_ptr<VulkanBackend> VulkanBackend::create(const core::ApplicationCreateInfo& appInfo)
{
  auto backend = std::unique_ptr<VulkanBackend>(new VulkanBackend());
  if (!backend->initVulkan(appInfo))
  {
    return nullptr;
  }
  return backend;
}

bool VulkanBackend::initVulkan(const core::ApplicationCreateInfo& appInfo)
{
  m_coreManager = std::make_unique<VulkanCoreManager>();
  return m_coreManager->init(appInfo);
}

void VulkanBackend::init(const core::ApplicationCreateInfo& info)
{
  // Initialize managers in correct order
  m_frameSyncManager = std::make_unique<FrameSynchronizationManager>();
  uint32_t numFrames = info.headless ? 2 : 3;
  m_frameSyncManager->init(*m_coreManager, numFrames);

  m_swapchainManager = std::make_unique<SwapchainRenderManager>();
  m_swapchainManager->init(*m_coreManager, *m_frameSyncManager, m_windowHandle, info);
}

void VulkanBackend::deinit()
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

void VulkanBackend::newFrame()
{
  ImGui_ImplVulkan_NewFrame();
}

bool VulkanBackend::beginFrame(IRenderContext& /* frame */)
{
  m_frameSyncManager->waitForFrameCompletion();

  if (!m_swapchainManager->beginFrame(*m_coreManager))
  {
    return false;
  }

  m_frameSyncManager->beginFrame();

  return true;
}

void VulkanBackend::renderFrame(const std::vector<std::shared_ptr<core::IAppElement>>& elements,
                                IRenderContext const& /* frame */)
{
  for (auto& e : elements)
  {
    e->onUIRender();
  }
  ImGui::Render();

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onPreRender();
  }

  VkCommandBuffer cmd = m_frameSyncManager->getActiveCommandBuffer();

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onRender(getRenderContext());
  }

  for (const std::shared_ptr<core::IAppElement>& e : elements)
  {
    e->onEndFrame(getRenderContext());
  }

  // Render to swapchain and setup synchronization
  m_swapchainManager->renderToSwapchain(cmd);

  // Add swapchain semaphores
  if (!m_swapchainManager->isHeadless())
  {
    m_frameSyncManager->addWaitSemaphore({
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_swapchainManager->getSwapchain().getImageAvailableSemaphore(),
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    });
    m_frameSyncManager->addSignalSemaphore({
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_swapchainManager->getSwapchain().getRenderFinishedSemaphore(),
        .stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    });
  }
}

void VulkanBackend::endFrame(IRenderContext const& /* frame */)
{
  m_frameSyncManager->endFrame();

  const VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = uint32_t(m_frameSyncManager->getWaitSemaphores().size()),
      .pWaitSemaphoreInfos = m_frameSyncManager->getWaitSemaphores().data(),
      .commandBufferInfoCount = uint32_t(m_frameSyncManager->getCommandBuffers().size()),
      .pCommandBufferInfos = m_frameSyncManager->getCommandBuffers().data(),
      .signalSemaphoreInfoCount = uint32_t(m_frameSyncManager->getSignalSemaphores().size()),
      .pSignalSemaphoreInfos = m_frameSyncManager->getSignalSemaphores().data(),
  };

  NVVK_CHECK(vkQueueSubmit2(getQueueInfo(0).queue, 1, &submitInfo, nullptr));
}

void VulkanBackend::present()
{
  m_swapchainManager->present(*m_coreManager);
}

void VulkanBackend::advance()
{
  m_frameSyncManager->advance();
}

IRenderContext* VulkanBackend::getRenderContext()
{
  return dynamic_cast<IRenderContext*>(m_frameSyncManager->getActiveFrameContext());
}

VkDevice VulkanBackend::getDevice() const
{
  return m_coreManager->getDevice();
}

VkPhysicalDevice VulkanBackend::getPhysicalDevice() const
{
  return m_coreManager->getPhysicalDevice();
}

VkInstance VulkanBackend::getInstance() const
{
  return m_coreManager->getInstance();
}

const nvvk::QueueInfo& VulkanBackend::getQueueInfo(uint32_t index) const
{
  return m_coreManager->getQueueInfo(index);
}

VulkanCoreManager* VulkanBackend::getCoreManager() const
{
  assert(m_coreManager != nullptr);
  return m_coreManager.get();
}

FrameSynchronizationManager* VulkanBackend::getFrameSyncManager() const
{
  assert(m_frameSyncManager != nullptr);
  return m_frameSyncManager.get();
}

SwapchainRenderManager* VulkanBackend::getSwapchainManager() const
{
  assert(m_swapchainManager != nullptr);
  return m_swapchainManager.get();
}

void VulkanBackend::waitForDeviceIdle()
{
  m_coreManager->waitForDeviceIdle();
}

void VulkanBackend::setVsync(bool enabled)
{
  IRenderBackend::setVsync(enabled);
  m_swapchainManager->setVsync(enabled);
}

VkCommandBuffer VulkanBackend::startSingleTimeCmd()
{
  return m_coreManager->startSingleTimeCmd();
}

void VulkanBackend::endSingleTimeCmd(VkCommandBuffer cmd)
{
  m_coreManager->endSingleTimeCmd(cmd);
}
