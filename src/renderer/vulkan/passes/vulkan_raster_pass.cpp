#include "vulkan_raster_pass.hpp"

// Implementation Includes
#include <shaders/shared/structs.h>

#include <core/camera.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "backend/vulkan/compiler/vulkan_slang_compiler.hpp"
#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "backend/vulkan/core/vulkan_render_context.hpp"
#include "core/frustum.hpp"
#include "core/timers.hpp"
#include "renderer/interfaces/renderer_interface.hpp"
#include "renderer/vulkan/vulkan_scene_asset_manager.hpp"

// Generated Shaders
#include "_autogen/gltf_fragment.slang.h"
#include "_autogen/gltf_raster.slang.h"

/**********************************************************/
VulkanRasterPass::VulkanRasterPass(
    VulkanContextManager* contextManager, const nvvk::DescriptorPack& descPack,
    const VulkanSceneAssetManager* assetManager) :
    m_context_manager(contextManager), m_descPack(descPack),
    m_assetManager(assetManager)
/**********************************************************/
{
}

/**********************************************************/
void VulkanRasterPass::init()
/**********************************************************/
{
  createPipelineLayout(m_context_manager->getDevice());
  compileShaders();
}

/**********************************************************/
void VulkanRasterPass::deinit()
/**********************************************************/
{
  vkDestroyPipelineLayout(m_context_manager->getDevice(), m_pipelineLayout,
                          nullptr);
  clearShaders();
}

/**********************************************************/
void VulkanRasterPass::setup(PassBuilder& builder)
/**********************************************************/
{
  // Declare intention to write to the Linear color buffer as a render target
  builder.write(RenderOutput::Linear, PipelineStage::RenderTarget,
                ResourceState::RenderTarget);

  // Declare intention to write to the Depth buffer
  builder.write(RenderOutput::DepthBuffer, PipelineStage::RenderTarget,
                ResourceState::DepthWrite);
}

/**********************************************************/
void VulkanRasterPass::resize(VkCommandBuffer /*cmd*/, VkExtent2D /*size*/)
/**********************************************************/
{
}

/**********************************************************/
void VulkanRasterPass::execute(IRenderContext& ctx)
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
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;

  shaderio::PushConstant constants = vkCtx.pushValues;
  const Scene* sceneResources = vkCtx.sceneResources;
  const shaderio::SceneInfo& scene_info = sceneResources->sceneInfo;
  const VkExtent2D& size = gBuffers->getSize();
  const shaderio::RasterParams& rasterParams = constants.rasterParams;

  NVVK_DBG_SCOPE(cmd);

  // --- CULLING SETUP ---
  const glm::mat4& viewProj = scene_info.viewProjMatrix;
  core::Frustum cameraFrustum = core::extractFrustumPlanes(viewProj);
  uint32_t culledCount = 0;

  // Define push info
  const VkPushConstantsInfo pushInfo{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
      .pValues = &constants,
  };

  // Rendering to the GBuffer - Attachments
  VkRenderingAttachmentInfo colorAttachment = DEFAULT_VkRenderingAttachmentInfo;
  colorAttachment.loadOp = scene_info.useSky ? VK_ATTACHMENT_LOAD_OP_LOAD
                                             : VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.imageView = gBuffers->getColorImageView(RenderOutput::Linear);
  colorAttachment.clearValue = {
      .color = VkClearColorValue{scene_info.backgroundColor.x,
                                 scene_info.backgroundColor.y,
                                 scene_info.backgroundColor.z, 1.0f}};

  VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
  depthAttachment.imageView = gBuffers->getDepthImageView();
  depthAttachment.clearValue = {.depthStencil =
                                    DEFAULT_VkClearDepthStencilValue};

  VkRenderingInfo renderingInfo = DEFAULT_VkRenderingInfo;
  renderingInfo.renderArea = DEFAULT_VkRect2D(gBuffers->getSize());
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = &depthAttachment;

  // Bind Descriptor Sets
  const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
      .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .layout = m_pipelineLayout,
      .firstSet = 0,
      .descriptorSetCount = 1,
      .pDescriptorSets = m_descPack.getSetPtr()};
  vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

  // ** BEGIN RENDERING **
  vkCmdBeginRendering(cmd, &renderingInfo);

  nvvk::GraphicsPipelineState pipelineState{};
  pipelineState.rasterizationState.cullMode = VK_CULL_MODE_NONE;
  pipelineState.cmdApplyAllStates(cmd);
  pipelineState.cmdSetViewportAndScissor(cmd, size);

  // --- Dynamic State Setups ---
  vkCmdSetDepthTestEnable(cmd, VK_TRUE);
  vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
  vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS_OR_EQUAL);

  // Wireframe Toggle Logic
  VkPolygonMode polyMode =
      rasterParams.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  vkCmdSetPolygonModeEXT(cmd, polyMode);

  if (rasterParams.wireframe)
  {
    vkCmdSetLineWidth(cmd, rasterParams.wireframeLineWidth);
  }

  // Define all 7 possible graphics stages
  const VkShaderStageFlagBits stages[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT};

  const VkShaderEXT shaders[] = {
      m_vertexShader,    // Maps to VERTEX
      VK_NULL_HANDLE,    // Maps to TESS_CONTROL (Disabled)
      VK_NULL_HANDLE,    // Maps to TESS_EVALUATION (Disabled)
      VK_NULL_HANDLE,    // Maps to GEOMETRY (Disabled)
      m_fragmentShader,  // Maps to FRAGMENT
      VK_NULL_HANDLE,    // Maps to TASK (Disabled)
      VK_NULL_HANDLE     // Maps to MESH (Disabled)
  };

  // 3. Bind all 7 explicitly
  vkCmdBindShadersEXT(cmd, 7, stages, shaders);

  // Vertex Input (Empty, pulled in shader)
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

  // Draw Loop
  for (size_t i = 0; i < sceneResources->instances.size(); i++)
  {

    const shaderio::Instance& instance = sceneResources->instances[i];
    uint32_t meshIndex = instance.meshIndex;
    const shaderio::MeshPrimitive& meshPrim = sceneResources->meshes[meshIndex];

    if (!core::isAABBInsideFrustum(cameraFrustum, meshPrim.bbox.min,
                                   meshPrim.bbox.max, instance.transform))
    {
      culledCount++;
      continue;  // Skip drawing this instance!
    }

    const shaderio::TriangleMesh& triMesh = meshPrim.triMesh;
    // Push constants
    constants.normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(instance.transform)));
    constants.instanceIndex = int(i);
    vkCmdPushConstants2(cmd, &pushInfo);

    // Index Buffer
    const nvvk::Buffer& v = m_assetManager->getBufferFromIndex(meshIndex);
    vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset,
                         VkIndexType(meshPrim.indexType));

    // Draw
    vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);
  }

  if (culledCount > 0)
  {
    LOGD("Rasterizer: Culled %u / %zu instances", culledCount,
         sceneResources->instances.size());
  }

  // ** END RENDERING **
  vkCmdEndRendering(cmd);

#ifdef PROFILE_APP
  if (_profActive)
    m_gpuTimer->cmdFrameEndSection(cmd, _profId);
#endif
}

/**********************************************************/
void VulkanRasterPass::reload()
/**********************************************************/
{
  clearShaders();
  compileShaders();
}

/**********************************************************/
void VulkanRasterPass::createPipelineLayout(VkDevice device)
/**********************************************************/
{
  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant)};

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };
  NVVK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                    &m_pipelineLayout));
  NVVK_DBG_NAME(m_pipelineLayout);
}

/**********************************************************/
void VulkanRasterPass::clearShaders()
/**********************************************************/
{
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_vertexShader, nullptr);
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_fragmentShader, nullptr);
}

/**********************************************************/
void VulkanRasterPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  VkShaderModuleCreateInfo vertexCode = VulkanSlangCompiler::instance().compile(
      "gltf_raster.slang", gltf_raster_slang);
  VkShaderModuleCreateInfo fragmentCode =
      VulkanSlangCompiler::instance().compile("gltf_fragment.slang",
                                              gltf_fragment_slang);

  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
  };

  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .pName = "main",
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };

  // Vertex Shader
  shaderInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.pName = "vertexMain";
  shaderInfo.codeSize = vertexCode.codeSize;
  shaderInfo.pCode = vertexCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_vertexShader);
  NVVK_DBG_NAME(m_vertexShader);

  // Fragment Shader
  shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName = "fragmentMain";
  shaderInfo.codeSize = fragmentCode.codeSize;
  shaderInfo.pCode = fragmentCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_fragmentShader);
  NVVK_DBG_NAME(m_fragmentShader);
}
