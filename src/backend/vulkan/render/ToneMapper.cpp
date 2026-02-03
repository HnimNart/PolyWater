#include "ToneMapper.hpp"

#include "_autogen/tonemapper.slang.h"
#include "backend/vulkan/core/RenderContext.hpp"

VulkanToneMapper::VulkanToneMapper()
{
}

VulkanToneMapper::~VulkanToneMapper()
{
}

void VulkanToneMapper::init(VulkanContextManager* core,
                            const SceneResourcesManager& /*scene*/)
{
  if (m_initialized)
  {
    return;
  }

  // Initialize the tonemapper using the shader bytecode from the autogen header
  m_tonemapper.init(
      &core->getAllocator(),
      std::span<const uint32_t>(tonemapper_slang,
                                sizeof(tonemapper_slang) / sizeof(uint32_t)));

  m_initialized = true;
}

void VulkanToneMapper::deinit(VulkanContextManager* /* core */)
{
  m_tonemapper.deinit();
  m_initialized = false;
}

void VulkanToneMapper::execute(const IRenderContext& ctx)
{

  const auto& vkCtx = VulkanRenderContext::get(ctx);

  if (!m_initialized)
  {
    return;
  }

  VkDescriptorImageInfo inputColor = vkCtx.gBuffers->getDescriptorImageInfo(0);
  VkDescriptorImageInfo outputColor = vkCtx.gBuffers->getDescriptorImageInfo(1);
  m_tonemapper.runCompute(vkCtx.cmdBuffer, vkCtx.gBuffers->getSize(),
                          m_tonemapperData, inputColor, outputColor);

  nvvk::cmdMemoryBarrier(vkCtx.cmdBuffer,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}
