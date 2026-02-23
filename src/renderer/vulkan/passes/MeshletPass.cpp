#include "MeshletPass.hpp"

// Implementation Includes
#include <core/Camera.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>
#include <shaders/shared/structs.h>

#include "backend/vulkan/core/ContextManager.hpp"
#include "backend/vulkan/core/RenderContext.hpp"
#include "compiler/slang.hpp"
#include "core/Frustum.hpp"
#include "core/timers.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "renderer/vulkan/SceneAssetManager.hpp"

// Generated Shaders (You will need to create gltf_meshlet.slang)
#include "_autogen/gltf_meshlet.slang.h"

/**********************************************************/
MeshletPass::MeshletPass(const nvvk::DescriptorPack &descPack)
    : m_descPack(descPack)
/**********************************************************/
{}

/**********************************************************/
void MeshletPass::init(VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;
  createPipelineLayout(m_context_manager->getDevice());
  compileShaders();
}

/**********************************************************/
void MeshletPass::deinit(VulkanContextManager *coreManager)
/**********************************************************/
{
  vkDestroyPipelineLayout(coreManager->getDevice(), m_pipelineLayout, nullptr);
  clearShaders();
}

/**********************************************************/
void MeshletPass::setup(PassBuilder &builder)
/**********************************************************/
{
  builder.write(RenderOutput::Linear, PipelineStage::RenderTarget,
                ResourceState::RenderTarget);
  builder.write(RenderOutput::DepthBuffer, PipelineStage::RenderTarget,
                ResourceState::DepthWrite);
}

/**********************************************************/
void MeshletPass::resize(VkCommandBuffer /*cmd*/, VkExtent2D /*size*/)
/**********************************************************/
{}

/**********************************************************/
void MeshletPass::execute(const IRenderContext &ctx)
/**********************************************************/
{
  const auto &vkCtx = VulkanRenderContext::get(ctx);

  VkCommandBuffer cmd = vkCtx.cmdBuffer;
  const nvvk::GBuffer *gBuffers = vkCtx.gBuffers;
  const VulkanSceneAssetManager *assetManager = vkCtx.assetManager;

  shaderio::PushConstant constants = vkCtx.pushValues;
  const Scene *sceneResources = vkCtx.sceneResources;
  const shaderio::SceneInfo &scene_info = sceneResources->sceneInfo;
  const VkExtent2D &size = gBuffers->getSize();
  const shaderio::RasterParams &rasterParams = constants.rasterParams;

  NVVK_DBG_SCOPE(cmd);

  // --- CULLING SETUP ---
  const glm::mat4 &viewProj = scene_info.viewProjMatrix;
  core::Frustum cameraFrustum = core::extractFrustumPlanes(viewProj);
  uint32_t culledCount = 0;

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

  VkPolygonMode polyMode =
      rasterParams.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  vkCmdSetPolygonModeEXT(cmd, polyMode);

  if (rasterParams.wireframe) {
    vkCmdSetLineWidth(cmd, rasterParams.wireframeLineWidth);
  }

  // --- BIND MESH & FRAGMENT SHADERS ---
  const VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_MESH_BIT_EXT,
                                          VK_SHADER_STAGE_FRAGMENT_BIT};
  const VkShaderEXT shaders[] = {m_meshShader, m_fragmentShader};
  vkCmdBindShadersEXT(cmd, 2, stages, shaders);

  // Vertex Input is explicitly NOT needed for Mesh Shaders
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

  // Draw Loop
  for (size_t i = 0; i < sceneResources->instances.size(); i++) {

    const shaderio::Instance &instance = sceneResources->instances[i];
    uint32_t meshIndex = instance.meshIndex;
    const shaderio::MeshPrimitive &meshPrim = sceneResources->meshes[meshIndex];

    // CPU Frustum Culling (Instance Level)
    if (!core::isAABBInsideFrustum(cameraFrustum, meshPrim.bbox.min,
                                   meshPrim.bbox.max, instance.transform)) {
      culledCount++;
      continue;
    }

    // Push constants
    constants.normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(instance.transform)));
    constants.instanceIndex = int(i);
    vkCmdPushConstants2(cmd, &pushInfo);

    // Get the number of meshlets to draw
    uint32_t meshletCount = meshPrim.meshlet.meshlets.count;

    // --- DRAW MESH TASKS ---
    if (meshletCount > 0) {
      // Dispatch 1 workgroup per meshlet
      // Note: Requires VK_EXT_mesh_shader enabled on your Vulkan Device!
      vkCmdDrawMeshTasksEXT(cmd, meshletCount, 1, 1);
    }
  }

  if (culledCount > 0) {
    LOGD("MeshletPass: Culled %u / %zu instances", culledCount,
         sceneResources->instances.size());
  }

  // ** END RENDERING **
  vkCmdEndRendering(cmd);
}

/**********************************************************/
void MeshletPass::reload()
/**********************************************************/
{
  clearShaders();
  compileShaders();
}

/**********************************************************/
void MeshletPass::createPipelineLayout(VkDevice device)
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
void MeshletPass::clearShaders()
/**********************************************************/
{
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_meshShader, nullptr);
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_fragmentShader, nullptr);
}

/**********************************************************/
void MeshletPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  // Make sure you create this slang file!
  VkShaderModuleCreateInfo shaderCode = SlangCompiler::instance().compile(
      "gltf_meshlet.slang", gltf_meshlet_slang);

  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
  };

  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .pName = "main", // Default fallback
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };

  // --- MESH SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
  shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.pName = "meshMain"; // Entry point in Slang
  shaderInfo.codeSize = shaderCode.codeSize;
  shaderInfo.pCode = shaderCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_meshShader);
  NVVK_DBG_NAME(m_meshShader);

  // --- FRAGMENT SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName = "fragmentMain"; // Entry point in Slang
  shaderInfo.codeSize = shaderCode.codeSize;
  shaderInfo.pCode = shaderCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_fragmentShader);
  NVVK_DBG_NAME(m_fragmentShader);
}
