#include "SkyPass.hpp"

#include <shaders/shaderio.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "_autogen/foundation.slang.h"
#include "backend/interfaces/ISceneRenderer.hpp"
#include "backend/vulkan/core/RenderContext.hpp"

// ============================================================================
// SKY PASS IMPLEMENTATION
// ============================================================================

void SkyPass::init(VulkanContextManager* core,
                   const SceneResourcesManager& /*scene*/)
{
  m_core = core;
  m_skySimple.init(&m_core->getAllocator(), std::span(sky_simple_slang));
}

void SkyPass::deinit(VulkanContextManager* core)
{
  m_skySimple.deinit();
}

void SkyPass::execute(const IRenderContext& ctx)
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);

  // Early exit if sky is disabled in scene settings
  const auto& sceneInfo = vkCtx.sceneResources->sceneInfo;
  if (!sceneInfo.useSky)
  {
    return;
  }

  NVVK_DBG_SCOPE(vkCtx.cmdBuffer);

  const VkExtent2D& size = vkCtx.gBuffers->getSize();

  // Run Compute
  m_skySimple.runCompute(vkCtx.cmdBuffer, size, sceneInfo.viewMatrix,
                         sceneInfo.projMatrix, sceneInfo.skySimpleParam,
                         vkCtx.gBuffers->getDescriptorImageInfo(
                             ISceneRenderer::RenderOutput::Linear));

  // We need to ensure the Compute Shader is fully finished writing to the image
  // before the next pass (likely Raster or ToneMap) tries to transition the
  // layout or read from it.
  //
  // Src Stage: COMPUTE_SHADER (where we just wrote)
  // Dst Stage: ALL_GRAPHICS (if next is Raster) | COMPUTE (if next is ToneMap)
  // -----------------------------------------------------------------------
  nvvk::cmdMemoryBarrier(vkCtx.cmdBuffer,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}
