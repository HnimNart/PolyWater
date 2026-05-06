#include "vulkan_sky_pass.hpp"

#include <shaders/shared/structs.h>

#include <backend/vulkan/core/vulkan_render_context.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>

// Shader bytecode
#include "_autogen/sky_simple.slang.h"
#include "scene/scene.hpp"

/**********************************************************/
VulkanSkyPass::VulkanSkyPass(VulkanContextManager* context) : m_core(context)
/**********************************************************/
{
}

/**********************************************************/
void VulkanSkyPass::init()
/**********************************************************/
{
  m_device = m_core->getDevice();

  // 1. Descriptor Set Layout (Push Descriptors)
  const auto layoutBindings = std::to_array<VkDescriptorSetLayoutBinding>({
      {.binding = shaderio::SkyBindings::eSkyOutImage,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
  });

  const VkDescriptorSetLayoutCreateInfo descLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = uint32_t(layoutBindings.size()),
      .pBindings = layoutBindings.data(),
  };
  NVVK_CHECK(vkCreateDescriptorSetLayout(m_device, &descLayoutInfo, nullptr,
                                         &m_descriptorSetLayout));

  // 2. Pipeline Layout
  auto pushConstantRanges = std::to_array<VkPushConstantRange>({
      {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
       .offset = 0,
       .size = sizeof(shaderio::SkySimpleParameters) + sizeof(glm::mat4)},
  });

  const VkPipelineLayoutCreateInfo pipeLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &m_descriptorSetLayout,
      .pushConstantRangeCount = uint32_t(pushConstantRanges.size()),
      .pPushConstantRanges = pushConstantRanges.data(),
  };
  NVVK_CHECK(vkCreatePipelineLayout(m_device, &pipeLayoutInfo, nullptr,
                                    &m_pipelineLayout));

  const std::span<const uint32_t> sky_simple_slang_spirv(sky_simple_slang);
  // 3. Shader Object (using VK_EXT_shader_object)
  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .codeSize = sky_simple_slang_spirv.size_bytes(),
      .pCode = sky_simple_slang_spirv.data(),
      .pName = "main",
      .setLayoutCount = 1,
      .pSetLayouts = &m_descriptorSetLayout,
      .pushConstantRangeCount = uint32_t(pushConstantRanges.size()),
      .pPushConstantRanges = pushConstantRanges.data(),
  };
  vkCreateShadersEXT(m_device, 1U, &shaderInfo, nullptr, &m_shader);
}

/**********************************************************/
void VulkanSkyPass::setup(PassBuilder& builder)
/**********************************************************/
{
  builder.write(RenderOutput::Linear, PipelineStage::Compute,
                ResourceState::General);
}

/**********************************************************/
void VulkanSkyPass::execute(IRenderContext& ctx)
/**********************************************************/
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);
  const auto& sceneInfo = vkCtx.sceneResources->sceneInfo;

  if (!sceneInfo.useSky)
    return;

#ifdef PROFILE_APP
  core::ProfilerTimeline::FrameSectionID _profId{};
  const bool _profActive = (m_gpuTimer != nullptr);
  if (_profActive)
    _profId =
        m_gpuTimer->cmdFrameBeginSection(vkCtx.cmdBuffer, std::string(name()));
#endif

  NVVK_DBG_SCOPE(vkCtx.cmdBuffer);
  const VkExtent2D size = vkCtx.gBuffers->getSize();

  // Bind Shader Object
  const VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_COMPUTE_BIT};
  vkCmdBindShadersEXT(vkCtx.cmdBuffer, 1, stages, &m_shader);

  // Calculate View-Projection Inverse (Camera directions)
  glm::mat4 viewNoTrans = sceneInfo.viewMatrix;
  viewNoTrans[3] = {0.0f, 0.0f, 0.0f, 1.0f};
  glm::mat4 invMvp = glm::inverse(sceneInfo.projMatrix * viewNoTrans);

  // Push Constants
  vkCmdPushConstants(
      vkCtx.cmdBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
      sizeof(shaderio::SkySimpleParameters), &sceneInfo.skySimpleParam);
  vkCmdPushConstants(
      vkCtx.cmdBuffer, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
      sizeof(shaderio::SkySimpleParameters), sizeof(glm::mat4), &invMvp);

  // Update Push Descriptor
  VkDescriptorImageInfo ioInfo =
      vkCtx.gBuffers->getDescriptorImageInfo(RenderOutput::Linear);
  VkWriteDescriptorSet write{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstBinding = shaderio::SkyBindings::eSkyOutImage,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &ioInfo,
  };
  vkCmdPushDescriptorSetKHR(vkCtx.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &write);

  vkCmdDispatch(vkCtx.cmdBuffer, (size.width + 15) / 16,
                (size.height + 15) / 16, 1);

#ifdef PROFILE_APP
  if (_profActive)
    m_gpuTimer->cmdFrameEndSection(vkCtx.cmdBuffer, _profId);
#endif
}

/**********************************************************/
void VulkanSkyPass::deinit()
/**********************************************************/
{
  vkDestroyShaderEXT(m_device, m_shader, nullptr);
  vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
  vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
}
