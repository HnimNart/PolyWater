#include "UIPass.hpp"

#include <nvvk/debug_util.hpp>

#include "backend/vulkan/core/RenderContext.hpp"

/**********************************************************/
UIPass::UIPass(RenderCallback callback)
/**********************************************************/
{
  m_callback = std::move(callback);
}

/**********************************************************/
void UIPass::setup(PassBuilder &builder)
/**********************************************************/
{
  // 1. Read the final HDR/SDR result from the previous pass
  builder.read(RenderOutput::ToneMapped, PipelineStage::Fragment,
               ResourceState::ShaderResource);

  // 2. Write to the Swapchain image
  // Note: Use ResourceState::RenderTarget so the graph transitions it to
  // COLOR_ATTACHMENT_OPTIMAL
  builder.write(RenderOutput::Swapchain, PipelineStage::RenderTarget,
                ResourceState::RenderTarget);
}

/**********************************************************/
void UIPass::execute(const IRenderContext &ctx)
/**********************************************************/
{
  const auto &vkCtx = VulkanRenderContext::get(ctx);

  // The graph has already transitioned the Swapchain image to
  // COLOR_ATTACHMENT_OPTIMAL and the ToneMapped image to
  // SHADER_READ_ONLY_OPTIMAL.

  VkRenderingAttachmentInfo colorAttachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView =
          vkCtx.swapchainImageView, // Provided by SwapchainManager via Context
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

  if (m_callback) {
    m_callback(ctx);
  }

  vkCmdEndRendering(vkCtx.cmdBuffer);

  // IMPORTANT: The graph usually handles the "Next" state.
  // Since this is the final pass, we need the graph to know the final state is
  // 'Present'.
}
