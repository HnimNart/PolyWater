#include "denoise_pass.hpp"

#include <backend/vulkan/core/context_manager.hpp>

#include "backend/vulkan/core/render_context.hpp"
#include "compiler/slang.hpp"
#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"

/**********************************************************/
DenoisePass::DenoisePass(VulkanContextManager* contextManager) :
    m_context_manager(contextManager)
/**********************************************************/
{
}

/**********************************************************/
void DenoisePass::init()
/**********************************************************/
{
  createDescriptorLayout();
  createComputePipeline();
}

/**********************************************************/
void DenoisePass::deinit()
/**********************************************************/
{
  VkDevice device = m_context_manager->getDevice();
  vkDestroyPipeline(device, m_pipeline, nullptr);
  vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, m_descSetLayout, nullptr);
  m_descPack.deinit();
}

/**********************************************************/
void DenoisePass::setup(PassBuilder& builder)
/**********************************************************/
{
  builder.read(RenderOutput::Linear, PipelineStage::Compute,
               ResourceState::General);

  builder.write(RenderOutput::Denoised, PipelineStage::Compute,
                ResourceState::General);
}

/**********************************************************/
void DenoisePass::createDescriptorLayout()
/**********************************************************/
{
  nvvk::DescriptorBindings bindings;
  bindings.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                      VK_SHADER_STAGE_COMPUTE_BIT);  // In: Noisy
  bindings.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                      VK_SHADER_STAGE_COMPUTE_BIT);  // Out: Denoised

  // (Add bindings for Normal/Depth here if using an edge-aware filter)

  m_descPack.init(bindings, m_context_manager->getDevice(), 0,
                  VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
}

/**********************************************************/
void DenoisePass::createComputePipeline()
/**********************************************************/
{
  VkDevice device = m_context_manager->getDevice();

  // --- FIX 1: Get the initialized layout from your descriptor pack ---
  VkDescriptorSetLayout dsetLayout = m_descPack.getLayout();

  // --- FIX 2: Define your Push Constants for the pipeline ---
  VkPushConstantRange pcRange{};
  pcRange.stageFlags =
      VK_SHADER_STAGE_ALL;  // Make sure this matches what you use in execute()
  pcRange.offset = 0;
  pcRange.size = sizeof(shaderio::PushConstant);

  // Pipeline Layout
  VkPipelineLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &dsetLayout;  // Use the valid layout here!
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pcRange;

  NVVK_CHECK(
      vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout));

  // Compile Shader
  VkShaderModuleCreateInfo moduleInfo =
      SlangCompiler::instance().compile("denoise.slang");

  // Create Compute Pipeline
  VkComputePipelineCreateInfo pipelineInfo{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

  pipelineInfo.stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipelineInfo.stage.pNext = &moduleInfo;
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = VK_NULL_HANDLE;
  pipelineInfo.stage.pName = "main";
  pipelineInfo.layout = m_pipelineLayout;

  NVVK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                      nullptr, &m_pipeline));
}

/**********************************************************/
void DenoisePass::execute(const IRenderContext& ctx)
/**********************************************************/
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);
  VkCommandBuffer cmd = vkCtx.cmdBuffer;
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;

  NVVK_DBG_SCOPE(cmd);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

  // Bind push descriptors
  nvvk::WriteSetContainer write{};
  write.append(m_descPack.makeWrite(0),
               gBuffers->getColorImageView(RenderOutput::Linear),
               VK_IMAGE_LAYOUT_GENERAL);
  write.append(m_descPack.makeWrite(1),
               gBuffers->getColorImageView(RenderOutput::Denoised),
               VK_IMAGE_LAYOUT_GENERAL);

  vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, write.size(), write.data());

  const VkPushConstantsInfo pushInfo{.sType =
                                         VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                     .layout = m_pipelineLayout,
                                     .stageFlags = VK_SHADER_STAGE_ALL,
                                     .size = sizeof(shaderio::PushConstant),
                                     .pValues = &vkCtx.pushValues};
  vkCmdPushConstants2(cmd, &pushInfo);

  // Dispatch Compute Shader
  // Assuming a standard 16x16 local workgroup size in your shader
  const VkExtent2D& size = gBuffers->getSize();
  uint32_t groupCountX = (size.width + 15) / 16;
  uint32_t groupCountY = (size.height + 15) / 16;

  vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
}
