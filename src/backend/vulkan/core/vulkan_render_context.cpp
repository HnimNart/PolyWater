#include "vulkan_render_context.hpp"

#include <volk.h>
#include <vulkan/vulkan.h>

#include "nvvk/check_error.hpp"
#include "translator.hpp"

namespace vkb
{

/**********************************************************/
VkImage VulkanRenderContext::getResourceImage(RenderOutput resource) const
/**********************************************************/
{
  switch (resource)
  {
    case RenderOutput::Linear:
    case RenderOutput::ToneMapped:
    case RenderOutput::AccumLinear:
    case RenderOutput::Denoised:
    case RenderOutput::Albedo:
    case RenderOutput::Normal:
      return gBuffers->getColorImage(resource);

    case RenderOutput::DepthBuffer:
      return gBuffers->getDepthImage();
    case RenderOutput::Swapchain:
      return this->swapchainImage;

    default:
      // Log warning: "Unknown RenderOutput resource requested"
      return VK_NULL_HANDLE;
  }
}

/**********************************************************/
void VulkanRenderContext::activatePass(uint32_t cmdBufferIndex)
/**********************************************************/
{
  const bool isEnd = (cmdBufferIndex == kEndPassIndex);

  // No-op when the requested index is already the active one.
  if (!isEnd && activeIndex == cmdBufferIndex)
  {
    return;
  }

  // End the currently recording command buffer (if any) and queue it for
  // submission at end-of-frame.
  if (cmdBuffer != VK_NULL_HANDLE)
  {
    NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));
    finishedCmdBuffers.push_back(cmdBuffer);
    cmdBuffer = VK_NULL_HANDLE;
    activeIndex = kEndPassIndex;
  }

  if (isEnd)
  {
    return;  // Just ending the current pass, not starting a new one.
  }

  // Begin the pre-allocated command buffer for the requested pass index.
  cmdBuffer = passCmdBuffers[cmdBufferIndex];
  activeIndex = cmdBufferIndex;

  const VkCommandBufferBeginInfo beginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
}

/**********************************************************/
void VulkanRenderContext::submitBarriers(
    const std::vector<BarrierInfo>& barriers) const
/**********************************************************/
{
  if (barriers.empty())
    return;

  std::vector<VkImageMemoryBarrier2> vkBarriers;
  vkBarriers.reserve(barriers.size());

  for (const auto& b : barriers)
  {
    VkImage imageHandle = getResourceImage(b.resource);
    if (imageHandle == VK_NULL_HANDLE)
      continue;

    VkImageMemoryBarrier2 vkB = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

    auto srcInfo = toVulkan(b.oldState);
    auto dstInfo = toVulkan(b.newState);

    vkB.srcStageMask = toVulkan(b.srcStage);
    vkB.srcAccessMask = srcInfo.access;
    vkB.oldLayout = srcInfo.layout;

    vkB.dstStageMask = toVulkan(b.dstStage);
    vkB.dstAccessMask = dstInfo.access;
    vkB.newLayout = dstInfo.layout;

    vkB.image = imageHandle;

    // --- Aspect Mask Logic ---
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (b.resource == RenderOutput::DepthBuffer)
    {
      aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
      // If your depth format has stencil, you might need:
      // aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    vkB.subresourceRange = {.aspectMask = aspect,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1};

    vkBarriers.push_back(vkB);
  }

  if (vkBarriers.empty())
    return;

  VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  depInfo.imageMemoryBarrierCount = (uint32_t) vkBarriers.size();
  depInfo.pImageMemoryBarriers = vkBarriers.data();

  vkCmdPipelineBarrier2(cmdBuffer, &depInfo);
}

}  // namespace vkb
