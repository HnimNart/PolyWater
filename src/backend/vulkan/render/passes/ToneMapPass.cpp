#include "ToneMapPass.hpp"

#include <nvvk/debug_util.hpp>

#include "_autogen/tonemapper.slang.h"
#include "backend/vulkan/core/RenderContext.hpp"

/**********************************************************/
ToneMapPass::ToneMapPass()
/**********************************************************/
{}

/**********************************************************/
ToneMapPass::~ToneMapPass()
/**********************************************************/
{}

/**********************************************************/
void ToneMapPass::init(VulkanContextManager *core)
/**********************************************************/
{
  if (m_initialized) {
    return;
  }

  // Initialize the tonemapper using the shader bytecode from the autogen header
  m_tonemapper.init(
      &core->getAllocator(),
      std::span<const uint32_t>(tonemapper_slang,
                                sizeof(tonemapper_slang) / sizeof(uint32_t)));

  m_initialized = true;
}

/**********************************************************/
void ToneMapPass::setup(PassBuilder &builder)
/**********************************************************/
{
  // 1. Read the HDR "Linear" color buffer produced by Raster/RayTrace
  // We need it in 'ShaderResource' state so the compute shader can sample it.
  builder.read(RenderOutput::Linear, PipelineStage::Compute,
               ResourceState::ShaderResource);

  // 2. Write to the LDR "ToneMapped" color buffer
  builder.write(RenderOutput::ToneMapped, PipelineStage::Compute,
                ResourceState::General);
}

/**********************************************************/
void ToneMapPass::deinit(VulkanContextManager * /* core */)
/**********************************************************/
{
  m_tonemapper.deinit();
  m_initialized = false;
}

/**********************************************************/
void ToneMapPass::execute(const IRenderContext &ctx)
/**********************************************************/
{
  const auto &vkCtx = VulkanRenderContext::get(ctx);

  if (!m_initialized) {
    return;
  }
  NVVK_DBG_SCOPE(vkCtx.cmdBuffer);
  VkDescriptorImageInfo inputColor =
      vkCtx.gBuffers->getDescriptorImageInfo(RenderOutput::Linear);
  VkDescriptorImageInfo outputColor =
      vkCtx.gBuffers->getDescriptorImageInfo(RenderOutput::ToneMapped);
  m_tonemapper.runCompute(vkCtx.cmdBuffer, vkCtx.gBuffers->getSize(),
                          m_tonemapperData, inputColor, outputColor);
}
