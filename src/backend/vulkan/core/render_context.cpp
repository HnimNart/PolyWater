#include "render_context.hpp"

#include <volk.h>
#include <vulkan/vulkan.h>

#include "translator.hpp"

/**********************************************************/
VkImage VulkanRenderContext::getResourceImage(RenderOutput resource) const
/**********************************************************/
{
  switch (resource)
  {
    case RenderOutput::Linear:
    case RenderOutput::ToneMapped:
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
