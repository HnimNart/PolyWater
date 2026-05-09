#include "vulkan_mip_reduction_pass.hpp"

#include <algorithm>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>

#include "_autogen/mip_reduction.slang.h"
#include "backend/vulkan/compiler/vulkan_slang_compiler.hpp"
#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "core/timers.hpp"


namespace vkb
{

/**********************************************************/
VulkanMipReductionPass::VulkanMipReductionPass(
    VulkanContextManager* contextManager, nvvk::Image* texture) :
    m_contextManager(contextManager), m_mipTexture(texture)
/**********************************************************/
{
  assert(m_mipTexture &&
         "VulkanMipReductionPass requires a valid texture pointer.");
}

/**********************************************************/
void VulkanMipReductionPass::init()
/**********************************************************/
{
  createDescriptorLayout();
  createPipelineLayout();
  compileShaders();
}

/**********************************************************/
void VulkanMipReductionPass::deinit()
/**********************************************************/
{
  for (VkImageView view : m_mipViews)
  {
    vkDestroyImageView(m_contextManager->getDevice(), view, nullptr);
  }
  m_mipViews.clear();
  m_mipCache = {};

  vkDestroyPipelineLayout(m_contextManager->getDevice(), m_pipelineLayout,
                          nullptr);
  vkDestroyShaderEXT(m_contextManager->getDevice(), m_computeShader, nullptr);
  m_mipDescPack.deinit();
}

/**********************************************************/
void VulkanMipReductionPass::setup(PassBuilder& builder)
/**********************************************************/
{
  builder.read(RenderOutput::DepthBuffer, PipelineStage::Compute,
               ResourceState::ShaderResource);
}

/**********************************************************/
void VulkanMipReductionPass::createDescriptorLayout()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  nvvk::DescriptorBindings bindings;

  bindings.addBinding({0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                       VK_SHADER_STAGE_COMPUTE_BIT});
  bindings.addBinding(
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT});

  m_mipDescPack.init(bindings, m_contextManager->getDevice(), 0,
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
}

/**********************************************************/
void VulkanMipReductionPass::createPipelineLayout()
/**********************************************************/
{
  VkDescriptorSetLayout layout = m_mipDescPack.getLayout();
  // Updated size to reflect the new struct
  VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(ReductionPushConstants)};

  VkPipelineLayoutCreateInfo info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  info.setLayoutCount = 1;
  info.pSetLayouts = &layout;
  info.pushConstantRangeCount = 1;
  info.pPushConstantRanges = &pushRange;

  NVVK_CHECK(vkCreatePipelineLayout(m_contextManager->getDevice(), &info,
                                    nullptr, &m_pipelineLayout));
  NVVK_DBG_NAME(m_pipelineLayout);
}

/**********************************************************/
void VulkanMipReductionPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  auto computeCode = VulkanSlangCompiler::instance().compile(
      "mip_reduction.slang", mip_reduction_slang);

  VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(ReductionPushConstants)};
  VkDescriptorSetLayout layout = m_mipDescPack.getLayout();

  VkShaderCreateInfoEXT shaderInfo{VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
  shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
  shaderInfo.codeSize = computeCode.codeSize;
  shaderInfo.pCode = computeCode.pCode;
  shaderInfo.pName = "computeMain";
  shaderInfo.pushConstantRangeCount = 1;
  shaderInfo.pPushConstantRanges = &pushRange;
  shaderInfo.setLayoutCount = 1;
  shaderInfo.pSetLayouts = &layout;

  NVVK_CHECK(vkCreateShadersEXT(m_contextManager->getDevice(), 1, &shaderInfo,
                                nullptr, &m_computeShader));
}

/**********************************************************/
void VulkanMipReductionPass::execute(IRenderContext& ctx)
/**********************************************************/
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);
  VkCommandBuffer cmd = vkCtx.cmdBuffer;

  if (!m_mipTexture || m_mipTexture->image == VK_NULL_HANDLE)
    return;

  updateMipViews();

#ifdef PROFILE_APP
  core::ProfilerTimeline::FrameSectionID _profId{};
  const bool _profActive = (m_gpuTimer != nullptr);
  if (_profActive)
    _profId = m_gpuTimer->cmdFrameBeginSection(cmd, std::string(name()));
#endif

  NVVK_DBG_SCOPE(cmd);

  vkCmdBindShadersEXT(cmd, 1,
                      (VkShaderStageFlagBits[]){VK_SHADER_STAGE_COMPUTE_BIT},
                      &m_computeShader);
  VkSampler sampler = m_mipTexture->descriptor.sampler;

  // -----------------------------------------------------------
  // Pass 0: Copy G-Buffer Depth -> Mip 0
  // -----------------------------------------------------------
  transitionImage(cmd, m_mipTexture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_GENERAL, 0, 1);

  ReductionPushConstants pc;
  pc.isFirstPass = 1;
  pc.reductionOp = 0;  // Default to MAX (Change this if you need MIN/AVG)

  vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(ReductionPushConstants), &pc);

  dispatchReduction(cmd, vkCtx.gBuffers->getDepthImageView(), m_mipViews[0],
                    sampler, m_mipTexture->extent.width,
                    m_mipTexture->extent.height);

  transitionImage(cmd, m_mipTexture->image, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);

  // -----------------------------------------------------------
  // Pass 1-N: Downsample chain
  // -----------------------------------------------------------
  pc.isFirstPass = 0;
  // The reductionOp remains the same throughout the chain
  vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(ReductionPushConstants), &pc);

  uint32_t w = m_mipTexture->extent.width;
  uint32_t h = m_mipTexture->extent.height;

  for (uint32_t i = 0; i < m_mipTexture->mipLevels - 1; ++i)
  {
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);

    transitionImage(cmd, m_mipTexture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL, i + 1, 1);

    dispatchReduction(cmd, m_mipViews[i], m_mipViews[i + 1], sampler, w, h);

    transitionImage(cmd, m_mipTexture->image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i + 1, 1);
  }

#ifdef PROFILE_APP
  if (_profActive)
    m_gpuTimer->cmdFrameEndSection(cmd, _profId);
#endif
}

/**********************************************************/
void VulkanMipReductionPass::updateMipViews()
/**********************************************************/
{
  bool needsReset = (m_mipCache.image != m_mipTexture->image) ||
                    (m_mipCache.width != m_mipTexture->extent.width) ||
                    (m_mipCache.mipLevels != m_mipTexture->mipLevels);

  if (!needsReset)
    return;

  for (VkImageView view : m_mipViews)
    vkDestroyImageView(m_contextManager->getDevice(), view, nullptr);
  m_mipViews.clear();

  for (uint32_t i = 0; i < m_mipTexture->mipLevels; ++i)
  {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = m_mipTexture->image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = VK_FORMAT_R32_SFLOAT;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};

    VkImageView view;
    vkCreateImageView(m_contextManager->getDevice(), &info, nullptr, &view);
    m_mipViews.push_back(view);
  }

  m_mipCache = {m_mipTexture->image, m_mipTexture->extent.width,
                m_mipTexture->extent.height, m_mipTexture->mipLevels};
}

/**********************************************************/
void VulkanMipReductionPass::dispatchReduction(VkCommandBuffer cmd,
                                               VkImageView inView,
                                               VkImageView outView,
                                               VkSampler sampler, uint32_t w,
                                               uint32_t h)
/**********************************************************/
{
  VkDescriptorImageInfo inInfo{sampler, inView,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo outInfo{VK_NULL_HANDLE, outView,
                                VK_IMAGE_LAYOUT_GENERAL};

  nvvk::WriteSetContainer write{};
  write.append(m_mipDescPack.makeWrite(0), &inInfo);
  write.append(m_mipDescPack.makeWrite(1), &outInfo);

  vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, write.size(), write.data());
  vkCmdDispatch(cmd, (w + 15) / 16, (h + 15) / 16, 1);
}

/**********************************************************/
void VulkanMipReductionPass::transitionImage(VkCommandBuffer cmd, VkImage image,
                                             VkImageLayout oldL,
                                             VkImageLayout newL, uint32_t mip,
                                             uint32_t count)
/**********************************************************/
{
  VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.oldLayout = oldL;
  barrier.newLayout = newL;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, count, 0, 1};
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dep);
}

}  // namespace vkb
