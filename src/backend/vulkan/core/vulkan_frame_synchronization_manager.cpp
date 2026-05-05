
#include "vulkan_frame_synchronization_manager.hpp"

#include <limits>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>

#include "vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_render_context.hpp"

/**********************************************************/
void VulkanFrameSynchronizationManager::init(VulkanContextManager& coreManager,
                                       uint32_t numFrames)
/**********************************************************/
{
  assert(numFrames >= 2);  // Must have at least 2 frames in flight
  VkDevice device = coreManager.getDevice();

  // Initialize timeline semaphore with (numFrames - 1) to allow concurrent
  // frame submission
  const uint64_t initialValue = (static_cast<uint64_t>(numFrames) - 1);
  VkSemaphoreTypeCreateInfo timelineCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext = nullptr,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initialValue,
  };

  const VkSemaphoreCreateInfo semaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timelineCreateInfo};
  NVVK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                               &m_frameTimelineSemaphore));
  NVVK_DBG_NAME(m_frameTimelineSemaphore);

  createFrameData(coreManager, numFrames);
}

/**********************************************************/
void VulkanFrameSynchronizationManager::createFrameData(
    VulkanContextManager& coreManager, uint32_t numFrames)
/**********************************************************/
{
  VkDevice device = coreManager.getDevice();
  const VkCommandPoolCreateInfo cmdPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = coreManager.getQueueInfo(0).familyIndex,
  };

  m_frameData.resize(numFrames);
  for (uint32_t i = 0; i < numFrames; i++)
  {
    m_frameData[i] = std::make_unique<VulkanRenderContext>();
    m_frameData[i]->frameNumber = i;
    m_frameData[i]->device = coreManager.getDevice();

    NVVK_CHECK(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr,
                                   &m_frameData[i]->cmdPool));
    NVVK_DBG_NAME(m_frameData[i]->cmdPool);

    // Allocate one command buffer per pass slot from the single pool.
    // Pool reset at frame-start implicitly resets all of them.
    const VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_frameData[i]->cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VulkanRenderContext::kNumPassCmdSlots,
    };
    NVVK_CHECK(
        vkAllocateCommandBuffers(device, &commandBufferAllocateInfo,
                                 m_frameData[i]->passCmdBuffers.data()));

    for (uint32_t s = 0; s < VulkanRenderContext::kNumPassCmdSlots; ++s)
    {
      NVVK_DBG_NAME(m_frameData[i]->passCmdBuffers[s]);
    }
  }
}

/**********************************************************/
void VulkanFrameSynchronizationManager::waitForFrameCompletion() const
/**********************************************************/
{
  VkDevice device = m_frameData[m_frameRingCurrent]->device;
  const VkSemaphoreWaitInfo waitInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &m_frameTimelineSemaphore,
      .pValues = &m_frameData[m_frameRingCurrent]->frameNumber,
  };
  vkWaitSemaphores(device, &waitInfo, std::numeric_limits<uint64_t>::max());
}

/**********************************************************/
VulkanRenderContext* VulkanFrameSynchronizationManager::beginFrame()
/**********************************************************/
{
  auto& frame = m_frameData[m_frameRingCurrent];
  frame->frameNumber += m_frameData.size();
  VkDevice device = frame->device;

  // Reset the pool — this implicitly resets all command buffers allocated
  // from it (all kNumPassCmdSlots slot buffers from last frame).
  NVVK_CHECK(vkResetCommandPool(device, frame->cmdPool, 0));

  // Clear per-frame transient state.
  frame->finishedCmdBuffers.clear();
  frame->cmdBuffer  = VK_NULL_HANDLE;
  frame->activeSlot = PassCmdSlot::Count;

  clearSemaphoresAndBuffers();

  // Pre-open the Main slot so that pre-graph work in VulkanRenderer::render()
  // (e.g., scene resource uploads) can record immediately without an explicit
  // activatePass() call.
  frame->cmdBuffer  = frame->passCmdBuffers[static_cast<uint32_t>(PassCmdSlot::Main)];
  frame->activeSlot = PassCmdSlot::Main;

  const VkCommandBufferBeginInfo beginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  NVVK_CHECK(vkBeginCommandBuffer(frame->cmdBuffer, &beginInfo));

  return frame.get();
}

/**********************************************************/
void VulkanFrameSynchronizationManager::endFrame(const VulkanRenderContext& frameCtx)
/**********************************************************/
{
  // RenderGraph::execute() has already ended all per-pass command buffers via
  // activatePass(Count) and populated finishedCmdBuffers.  Collect them into
  // the submission list.
  for (VkCommandBuffer cmd : frameCtx.finishedCmdBuffers)
  {
    m_commandBuffers.push_back(
        {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = cmd});
  }

  m_signalSemaphores.push_back({
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frameTimelineSemaphore,
      .value = frameCtx.frameNumber,
      .stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
  });
}

/**********************************************************/
void VulkanFrameSynchronizationManager::advance()
/**********************************************************/
{
  // TODO make this thread-saef?
  m_frameRingCurrent = (m_frameRingCurrent + 1) % m_frameData.size();
}

/**********************************************************/
VkCommandBuffer VulkanFrameSynchronizationManager::getActiveCommandBuffer() const
/**********************************************************/
{
  assert(m_frameData[m_frameRingCurrent]->cmdBuffer != VK_NULL_HANDLE);
  return m_frameData[m_frameRingCurrent]->cmdBuffer;
}

/**********************************************************/
VulkanRenderContext* VulkanFrameSynchronizationManager::getActiveFrameContext()
/**********************************************************/
{
  return m_frameData[m_frameRingCurrent].get();
}

/**********************************************************/
const VulkanRenderContext*
VulkanFrameSynchronizationManager::getActiveFrameContext() const
/**********************************************************/
{
  return m_frameData[m_frameRingCurrent].get();
}

/**********************************************************/
void VulkanFrameSynchronizationManager::addWaitSemaphore(
    const VkSemaphoreSubmitInfo& semaphore)
/**********************************************************/
{
  m_waitSemaphores.push_back(semaphore);
}

/**********************************************************/
void VulkanFrameSynchronizationManager::addSignalSemaphore(
    const VkSemaphoreSubmitInfo& semaphore)
/**********************************************************/
{
  m_signalSemaphores.push_back(semaphore);
}

/**********************************************************/
void VulkanFrameSynchronizationManager::addCommandBuffer(
    const VkCommandBufferSubmitInfo& cmdBuffer)
/**********************************************************/
{
  m_commandBuffers.push_back(cmdBuffer);
}

/**********************************************************/
void VulkanFrameSynchronizationManager::clearSemaphoresAndBuffers()
/**********************************************************/
{
  m_waitSemaphores.clear();
  m_signalSemaphores.clear();
  m_commandBuffers.clear();
}

/**********************************************************/
void VulkanFrameSynchronizationManager::deinit(VulkanContextManager& coreManager)
/**********************************************************/
{
  VkDevice device = coreManager.getDevice();
  NVVK_CHECK(vkDeviceWaitIdle(device));

  for (auto& data : m_frameData)
  {
    vkFreeCommandBuffers(device, data->cmdPool,
                         VulkanRenderContext::kNumPassCmdSlots,
                         data->passCmdBuffers.data());
    vkDestroyCommandPool(device, data->cmdPool, nullptr);
  }
  m_frameData.clear();

  vkDestroySemaphore(device, m_frameTimelineSemaphore, nullptr);
}
