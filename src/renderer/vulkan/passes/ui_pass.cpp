#include "ui_pass.hpp"

#include <nvvk/debug_util.hpp>

#include "backend/vulkan/core/render_context.hpp"

/**********************************************************/
UIPass::UIPass(RenderCallback callback)
/**********************************************************/
{
  m_callback = std::move(callback);
}

/**********************************************************/
void UIPass::setup(PassBuilder& builder)
/**********************************************************/
{
  // 1. Read the ToneMapped image during the pass
  builder.read(RenderOutput::ToneMapped, PipelineStage::Fragment,
               ResourceState::General);
  // 2. Write to the Swapchain DURING the pass
  builder.write(RenderOutput::Swapchain, PipelineStage::RenderTarget,
                ResourceState::RenderTarget);
  // 3. Declare the EXPORT state AFTER the pass is over
  builder.setFinalState(RenderOutput::Swapchain, ResourceState::Present);
}

/**********************************************************/
void UIPass::execute(const IRenderContext& ctx)
/**********************************************************/
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);
  VkRenderingAttachmentInfo colorAttachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView =
          vkCtx.swapchainImageView,  // Provided by SwapchainManager via Context
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
  };

  VkRenderingInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, vkCtx.screenSize},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachment,
  };

  NVVK_DBG_SCOPE(vkCtx.cmdBuffer);

  // Begin Rendering
  vkCmdBeginRendering(vkCtx.cmdBuffer, &renderingInfo);
  if (m_callback)
  {
    m_callback(ctx);
  }
  vkCmdEndRendering(vkCtx.cmdBuffer);
}
