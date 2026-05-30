#include "vulkan_shadow_pass.hpp"

#include <shaders/shared/structs.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "_autogen/shadow_depth.slang.h"
#include "backend/vulkan/compiler/vulkan_slang_compiler.hpp"
#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "core/timers.hpp"
#include "renderer/vulkan/vulkan_scene_asset_manager.hpp"

namespace vkb
{

/**********************************************************/
VulkanShadowPass::VulkanShadowPass(VulkanContextManager*          context,
                                   const VulkanSceneAssetManager* assetManager)
    : m_context(context),
      m_assetManager(assetManager)
/**********************************************************/
{
  // Allocate the shadow map image in the constructor so that downstream passes
  // can get a stable pointer to it before init() is called.
  createShadowImage();
  createCompareSampler();
}

/**********************************************************/
void VulkanShadowPass::init()
/**********************************************************/
{
  createPipelineLayout();
  compileShaders();

  // Prime the shadow map to a valid samplable layout so the fragment shader
  // can bind it safely even on frames when the shadow pass is skipped.
  VkCommandBuffer cmd = m_context->startSingleTimeCmd();

  VkImageMemoryBarrier2 primeBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  primeBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  primeBarrier.srcAccessMask       = 0;
  primeBarrier.dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  primeBarrier.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT;
  primeBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
  primeBarrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  primeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  primeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  primeBarrier.image               = m_shadowMap.image;
  primeBarrier.subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers    = &primeBarrier;
  vkCmdPipelineBarrier2(cmd, &dep);

  m_context->endSingleTimeCmd(cmd);
  m_shadowMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

/**********************************************************/
void VulkanShadowPass::deinit()
/**********************************************************/
{
  clearShaders();

  if (m_pipelineLayout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
  }

  if (m_compareSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_context->getDevice(), m_compareSampler, nullptr);
    m_compareSampler = VK_NULL_HANDLE;
  }

  if (m_shadowMap.image != VK_NULL_HANDLE)
  {
    m_context->getAllocator().destroyImage(m_shadowMap);
    m_shadowMap = {};
  }
}

/**********************************************************/
void VulkanShadowPass::setup(PassBuilder& /*builder*/)
/**********************************************************/
{
  // The shadow pass does not produce a GBuffer output; it writes into its own
  // managed depth image.
}

/**********************************************************/
void VulkanShadowPass::execute(IRenderContext& ctx)
/**********************************************************/
{
  const auto& vkCtx = VulkanRenderContext::get(ctx);
  VkCommandBuffer cmd = vkCtx.cmdBuffer;

#ifdef PROFILE_APP
  core::ProfilerTimeline::FrameSectionID _profId{};
  const bool _profActive = (m_gpuTimer != nullptr);
  if (_profActive)
    _profId = m_gpuTimer->cmdFrameBeginSection(cmd, std::string(name()));
#endif

  const scene::Scene*    sceneResources = vkCtx.sceneResources;
  const shaderio::SceneInfo& sceneInfo  = sceneResources->sceneInfo;

  // Skip shadow pass if no light produces a shadow map.
  if (sceneInfo.hasShadowMap == 0)
  {
#ifdef PROFILE_APP
    if (_profActive)
      m_gpuTimer->cmdFrameEndSection(cmd, _profId);
#endif
    return;
  }

  NVVK_DBG_SCOPE(cmd);

  // --- Transition: current layout → DEPTH_STENCIL_ATTACHMENT_OPTIMAL ---
  // Using UNDEFINED as oldLayout discards existing content, which is fine since
  // we always clear the shadow map with loadOp = CLEAR.
  {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = m_shadowMap.image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
  }

  // --- Depth attachment ---
  VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
  depthAttachment.imageView   = m_shadowMap.descriptor.imageView;
  depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachment.clearValue  = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

  VkRenderingInfo renderingInfo = DEFAULT_VkRenderingInfo;
  renderingInfo.renderArea      = {{0, 0}, {kShadowMapSize, kShadowMapSize}};
  renderingInfo.colorAttachmentCount = 0;      // depth-only
  renderingInfo.pColorAttachments    = nullptr;
  renderingInfo.pDepthAttachment     = &depthAttachment;

  vkCmdBeginRendering(cmd, &renderingInfo);

  // --- Viewport / scissor and all other required dynamic states ---
  // VK_EXT_shader_object requires ALL dynamic states to be set before a draw.
  // Use GraphicsPipelineState to set them correctly in one call, then override
  // the shadow-specific settings.
  nvvk::GraphicsPipelineState pipelineState{};
  // Front-face culling reduces self-shadowing (peter-panning) artefacts.
  pipelineState.rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
  // Depth-only pass: clear the default 1-attachment color blend vectors so
  // cmdApplyAllStates does not emit colour-blend commands for non-existent
  // attachments.
  pipelineState.colorBlendEnables.clear();
  pipelineState.colorWriteMasks.clear();
  pipelineState.colorBlendEquations.clear();

  pipelineState.cmdApplyAllStates(cmd);
  nvvk::GraphicsPipelineState::cmdSetViewportAndScissor(
      cmd, {kShadowMapSize, kShadowMapSize});
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

  // --- Bind shaders (vertex only; fragment disabled) ---
  const VkShaderStageFlagBits stages[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT};

  const VkShaderEXT shaders[] = {
      m_vertexShader,  // vertex
      VK_NULL_HANDLE,  // tess control
      VK_NULL_HANDLE,  // tess eval
      VK_NULL_HANDLE,  // geometry
      VK_NULL_HANDLE,  // fragment (disabled — depth-only)
      VK_NULL_HANDLE,  // task
      VK_NULL_HANDLE   // mesh
  };
  vkCmdBindShadersEXT(cmd, 7, stages, shaders);
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

  // --- Push constants shared with the raster pass ---
  shaderio::PushConstant constants = vkCtx.pushValues;

  const VkPushConstantsInfo pushInfo{
      .sType      = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout     = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset     = 0,
      .size       = sizeof(shaderio::PushConstant),
      .pValues    = &constants,
  };

  // --- Draw all instances (simple loop — same geometry as raster pass) ---
  for (size_t i = 0; i < sceneResources->instances.size(); i++)
  {
    const shaderio::Instance& instance = sceneResources->instances[i];
    uint32_t meshIndex = instance.meshIndex;
    const shaderio::MeshPrimitive& meshPrim = sceneResources->meshes[meshIndex];
    const shaderio::TriangleMesh&  triMesh  = meshPrim.triMesh;

    constants.normalMatrix  = glm::transpose(glm::inverse(glm::mat3(instance.transform)));
    constants.instanceIndex = static_cast<int>(i);
    vkCmdPushConstants2(cmd, &pushInfo);

    const nvvk::Buffer& v = m_assetManager->getBufferFromIndex(meshIndex);
    vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset,
                         VkIndexType(meshPrim.indexType));
    vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);
  }

  vkCmdEndRendering(cmd);

  // --- Transition: DEPTH_STENCIL_ATTACHMENT_OPTIMAL → DEPTH_STENCIL_READ_ONLY_OPTIMAL ---
  {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = m_shadowMap.image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
  }

  // Update the stored layout so the next frame's transition is from the
  // correct source layout.
  m_shadowMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

#ifdef PROFILE_APP
  if (_profActive)
    m_gpuTimer->cmdFrameEndSection(cmd, _profId);
#endif
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/**********************************************************/
void VulkanShadowPass::createShadowImage()
/**********************************************************/
{
  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType   = VK_IMAGE_TYPE_2D;
  imageInfo.format      = VK_FORMAT_D32_SFLOAT;
  imageInfo.extent      = {kShadowMapSize, kShadowMapSize, 1};
  imageInfo.mipLevels   = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format                          = VK_FORMAT_D32_SFLOAT;
  viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel   = 0;
  viewInfo.subresourceRange.levelCount     = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount     = 1;

  NVVK_CHECK(m_context->getAllocator().createImage(m_shadowMap, imageInfo, viewInfo));
  NVVK_DBG_NAME(m_shadowMap.image);

  // Track the initial layout (will be transitioned to readable after first render).
  m_shadowMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

/**********************************************************/
void VulkanShadowPass::createCompareSampler()
/**********************************************************/
{
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter        = VK_FILTER_LINEAR;
  samplerInfo.minFilter        = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // outside = lit
  samplerInfo.compareEnable    = VK_TRUE;
  samplerInfo.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
  samplerInfo.minLod           = 0.0f;
  samplerInfo.maxLod           = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;

  NVVK_CHECK(vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr,
                              &m_compareSampler));
  NVVK_DBG_NAME(m_compareSampler);
}

/**********************************************************/
void VulkanShadowPass::createPipelineLayout()
/**********************************************************/
{
  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset     = 0,
      .size       = sizeof(shaderio::PushConstant)};

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = 0,
      .pSetLayouts            = nullptr,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges    = &pushConstantRange,
  };
  NVVK_CHECK(vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
                                    nullptr, &m_pipelineLayout));
  NVVK_DBG_NAME(m_pipelineLayout);
}

/**********************************************************/
void VulkanShadowPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  VkShaderModuleCreateInfo vertCode = VulkanSlangCompiler::instance().compile(
      "shadow_depth.slang", shadow_depth_slang);

  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset     = 0,
      .size       = sizeof(shaderio::PushConstant)};

  VkShaderCreateInfoEXT shaderInfo{
      .sType              = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .stage              = VK_SHADER_STAGE_VERTEX_BIT,
      .nextStage          = 0,
      .codeType           = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .codeSize           = vertCode.codeSize,
      .pCode              = vertCode.pCode,
      .pName              = "shadowVertexMain",
      .setLayoutCount     = 0,
      .pSetLayouts        = nullptr,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges    = &pushConstantRange,
  };
  NVVK_CHECK(vkCreateShadersEXT(m_context->getDevice(), 1, &shaderInfo, nullptr,
                                 &m_vertexShader));
  NVVK_DBG_NAME(m_vertexShader);
}

/**********************************************************/
void VulkanShadowPass::clearShaders()
/**********************************************************/
{
  if (m_vertexShader != VK_NULL_HANDLE)
  {
    vkDestroyShaderEXT(m_context->getDevice(), m_vertexShader, nullptr);
    m_vertexShader = VK_NULL_HANDLE;
  }
}

}  // namespace vkb
