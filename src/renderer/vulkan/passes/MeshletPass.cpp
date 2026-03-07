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

#include "_autogen/gltf_fragment.slang.h"
#include "_autogen/gltf_meshlet.slang.h"
#include "_autogen/gltf_task.slang.h"

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
  builder.write(
      RenderOutput::Linear, PipelineStage::RenderTarget,
      ResourceState::RenderTarget); // Translates to COLOR_ATTACHMENT_OPTIMAL

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
      .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT |
                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
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
      .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT |
                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
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
  vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS_OR_EQUAL);
  vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
  vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
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
  // Bind Shaders
  const VkShaderStageFlagBits stages[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT};

  const VkShaderEXT shaders[] = {
      VK_NULL_HANDLE, // No vertex shader
      VK_NULL_HANDLE, // No Tessellation Control
      VK_NULL_HANDLE, // No Tessellation Eval
      VK_NULL_HANDLE, // No Geometry
      m_fragmentShader, m_taskShader, m_meshShader,
  };
  vkCmdBindShadersEXT(cmd, 7, stages, shaders);

  // Vertex Input is explicitly NOT needed for Mesh Shaders
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
  vkCmdSetPrimitiveTopologyEXT(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

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

    // Get the number of meshlets to draw
    uint32_t meshletCount = meshPrim.meshlet.meshlets.count;
    if (meshletCount > 0) {
      // Push constants
      constants.normalMatrix =
          glm::transpose(glm::inverse(glm::mat3(instance.transform)));
      constants.instanceIndex = int(i);
      vkCmdPushConstants2(cmd, &pushInfo);

      uint32_t taskGroupCount = (meshletCount + 31) / 32;
      vkCmdDrawMeshTasksEXT(cmd, taskGroupCount, 1, 1);
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
      .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT |
                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
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
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_taskShader, nullptr);
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_meshShader, nullptr);
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_fragmentShader, nullptr);
}

/**********************************************************/
void MeshletPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  VkShaderModuleCreateInfo taskCode =
      SlangCompiler::instance().compile("gltf_task.slang", gltf_task_slang);
  VkShaderModuleCreateInfo meshletCode = SlangCompiler::instance().compile(
      "gltf_meshlet.slang", gltf_meshlet_slang);
  VkShaderModuleCreateInfo fragmentCode = SlangCompiler::instance().compile(
      "gltf_fragment.slang", gltf_fragment_slang);

  // Use the UNION of all stages that will use this push constant block
  const VkShaderStageFlags pipelineStages = VK_SHADER_STAGE_TASK_BIT_EXT |
                                            VK_SHADER_STAGE_MESH_BIT_EXT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT;

  const VkPushConstantRange identicalRange{
      .stageFlags = pipelineStages,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
  };

  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &identicalRange,
  };

  // --- TASK SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
  shaderInfo.nextStage = VK_SHADER_STAGE_MESH_BIT_EXT;
  shaderInfo.pName = "taskMain";
  shaderInfo.codeSize = taskCode.codeSize;
  shaderInfo.pCode = taskCode.pCode;
  shaderInfo.flags = 0; // Standard flags

  NVVK_CHECK(vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo,
                                nullptr, &m_taskShader));

  // --- MESH SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
  shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.pName = "meshMain";
  shaderInfo.codeSize = meshletCode.codeSize;
  shaderInfo.pCode = meshletCode.pCode;
  shaderInfo.flags = 0;

  NVVK_CHECK(vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo,
                                nullptr, &m_meshShader));

  // --- FRAGMENT SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName = "fragmentMain";
  shaderInfo.codeSize = fragmentCode.codeSize;
  shaderInfo.pCode = fragmentCode.pCode;
  shaderInfo.flags = 0;

  NVVK_CHECK(vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo,
                                nullptr, &m_fragmentShader));
}
