#include "RasterPass.hpp"

// Implementation Includes
#include <shaders/shared/structs.h>

#include <core/Camera.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "backend/vulkan/core/ContextManager.hpp"
#include "backend/vulkan/core/RenderContext.hpp"
#include "compiler/slang.hpp"
#include "core/Frustum.hpp"
#include "core/timers.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "renderer/vulkan/SceneAssetManager.hpp"

// Generated Shaders
#include "_autogen/gltf_fragment.slang.h"
#include "_autogen/gltf_raster.slang.h"

/**********************************************************/
RasterPass::RasterPass(const nvvk::DescriptorPack &descPack)
    : m_descPack(descPack)
/**********************************************************/
{}

/**********************************************************/
void RasterPass::init(VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;
  createPipelineLayout(m_context_manager->getDevice());
  compileShaders();

  // Indirect Commands Buffer Setup
  VkDeviceSize indirectBufferSize =
      sizeof(VkDrawIndirectCommand) * MAX_SCENE_INSTANCES;
  VkBufferUsageFlags2KHR indirectUsage =
      VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;

  // --- NEW: Instance Map Buffer Setup ---
  // This buffer allows the shader to look up the True Instance ID via DrawID
  VkDeviceSize mapBufferSize = sizeof(uint32_t) * MAX_SCENE_INSTANCES;
  VkBufferUsageFlags2KHR mapUsage =
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR; // match the type

  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    // Create Indirect Command Buffers
    m_context_manager->getAllocator().createBuffer(
        m_indirectCommandsBuffers[i], indirectBufferSize, indirectUsage,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);

    m_context_manager->getAllocator().createBuffer(
        m_instanceMapBuffers[i], mapBufferSize,
        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, // drop SHADER_DEVICE_ADDRESS
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);
  }
}

/**********************************************************/
void RasterPass::deinit(VulkanContextManager *coreManager)
/**********************************************************/
{
  // Clean up all buffers
  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    m_context_manager->getAllocator().destroyBuffer(
        m_indirectCommandsBuffers[i]);
    m_context_manager->getAllocator().destroyBuffer(
        m_instanceMapBuffers[i]); // Clean up the map!
  }

  vkDestroyPipelineLayout(coreManager->getDevice(), m_pipelineLayout, nullptr);
  clearShaders();
}

/**********************************************************/
void RasterPass::setup(PassBuilder &builder)
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
void RasterPass::resize(VkCommandBuffer /*cmd*/, VkExtent2D /*size*/)
/**********************************************************/
{}

/**********************************************************/
void RasterPass::execute(const IRenderContext &ctx)
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

  // ==========================================================
  // 1. CPU CULLING & INDIRECT COMMAND PREPARATION
  // ==========================================================
  core::Frustum cameraFrustum =
      core::extractFrustumPlanes(scene_info.viewProjMatrix);

  // Pointer to the indirect commands buffer
  VkDrawIndirectCommand *indirectCommands =
      reinterpret_cast<VkDrawIndirectCommand *>(
          m_indirectCommandsBuffers[m_currentFrameIndex].mapping);

  uint32_t *instanceMap = reinterpret_cast<uint32_t *>(
      m_instanceMapBuffers[m_currentFrameIndex].mapping);

  uint32_t drawCount = 0;
  for (size_t i = 0; i < sceneResources->instances.size(); i++) {
    const shaderio::Instance &instance = sceneResources->instances[i];
    const shaderio::MeshPrimitive &meshPrim =
        sceneResources->meshes[instance.meshIndex];

    if (!core::isAABBInsideFrustum(cameraFrustum, meshPrim.bbox.min,
                                   meshPrim.bbox.max, instance.transform)) {
      continue;
    }

    if (drawCount >= MAX_SCENE_INSTANCES)
      break;

    // Record this instance as visible
    instanceMap[drawCount] = static_cast<uint32_t>(i);

    // Fill the indirect command
    const shaderio::TriangleMesh &triMesh = meshPrim.triMesh;
    VkDrawIndirectCommand &cmdDraw = indirectCommands[drawCount];
    cmdDraw.vertexCount = triMesh.indices.count;
    cmdDraw.instanceCount = 1;
    cmdDraw.firstVertex = 0;
    cmdDraw.firstInstance = drawCount;
    drawCount++;
  }

  if (drawCount == 0) {
    m_currentFrameIndex = (m_currentFrameIndex + 1) % FRAMES_IN_FLIGHT;
    return;
  }
  LOGD("drawCount = %u / %zu instances\n", drawCount,
       sceneResources->instances.size());

  // // Ensure GPU sees the command buffer writes
  vmaFlushAllocation(
      m_context_manager->getAllocator(),
      m_indirectCommandsBuffers[m_currentFrameIndex].allocation, 0,
      sizeof(VkDrawIndirectCommand) * drawCount); // not VK_WHOLE_SIZE

  vmaFlushAllocation(m_context_manager->getAllocator(),
                     m_instanceMapBuffers[m_currentFrameIndex].allocation, 0,
                     sizeof(uint32_t) * drawCount); // not VK_WHOLE_SIZE

  // ==========================================================
  // 2. VULKAN RENDERING SETUP
  // ==========================================================
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

  const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
      .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .layout = m_pipelineLayout,
      .firstSet = 0,
      .descriptorSetCount = 1,
      .pDescriptorSets = m_descPack.getSetPtr()};

  vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);
  vkCmdBeginRendering(cmd, &renderingInfo);

  // Apply base state
  nvvk::GraphicsPipelineState pipelineState{};
  pipelineState.rasterizationState.cullMode = VK_CULL_MODE_NONE;
  pipelineState.cmdApplyAllStates(cmd);
  pipelineState.cmdSetViewportAndScissor(cmd, size);

  // Set required dynamic states
  vkCmdSetDepthTestEnable(cmd, VK_TRUE);
  vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
  vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS_OR_EQUAL);
  vkCmdSetPolygonModeEXT(cmd, rasterParams.wireframe ? VK_POLYGON_MODE_LINE
                                                     : VK_POLYGON_MODE_FILL);

  const VkShaderEXT shaders[] = {
      m_vertexShader,   VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
      m_fragmentShader, VK_NULL_HANDLE, VK_NULL_HANDLE};
  const VkShaderStageFlagBits stages[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT};

  vkCmdBindShadersEXT(cmd, 7, stages, shaders);
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
  vkCmdSetPrimitiveTopologyEXT(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  constants.instanceMapAddress =
      m_instanceMapBuffers[m_currentFrameIndex].address;
  const VkPushConstantsInfo pushInfo{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
      .pValues = &constants,
  };
  vkCmdPushConstants2(cmd, &pushInfo);

  // ==========================================================
  // 3. THE DRAW LOOP (The "Old Reliable" Method)
  // ==========================================================

  VkExtent2D fragmentSize = {1, 1};
  VkFragmentShadingRateCombinerOpKHR combinerOps[2] = {
      VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
      VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
  vkCmdSetFragmentShadingRateKHR(cmd, &fragmentSize, combinerOps);
  vkCmdDrawIndirect(cmd, m_indirectCommandsBuffers[m_currentFrameIndex].buffer,
                    0, drawCount, sizeof(VkDrawIndirectCommand));
  // for (uint32_t d = 0; d < drawCount; ++d) {
  //   // 1. Update push constant with THIS specific instance ID
  //   constants.instanceIndex = visibleInstanceIndices[d];

  //   // 2. Push constants to GPU
  //   constants.instanceMapAddress =
  //       m_instanceMapBuffers[m_currentFrameIndex].address;
  //   const VkPushConstantsInfo pushInfo{
  //       .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
  //       .layout = m_pipelineLayout,
  //       .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
  //       .offset = 0,
  //       .size = sizeof(shaderio::PushConstant),
  //       .pValues = &constants,
  //   };
  //   vkCmdPushConstants2(cmd, &pushInfo);

  //   // 3. Indirect Draw for exactly one command at a time
  //   vkCmdDrawIndirect(
  //       cmd, m_indirectCommandsBuffers[m_currentFrameIndex].buffer,
  //       d * sizeof(VkDrawIndirectCommand), // Offset steps through the buffer
  //       1,                                 // Draw exactly one
  //       sizeof(VkDrawIndirectCommand));
  // }

  vkCmdEndRendering(cmd);
  m_currentFrameIndex = (m_currentFrameIndex + 1) % FRAMES_IN_FLIGHT;
}

/**********************************************************/
void RasterPass::reload()
/**********************************************************/
{
  clearShaders();
  compileShaders();
}

/**********************************************************/
void RasterPass::createPipelineLayout(VkDevice device)
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
void RasterPass::clearShaders()
/**********************************************************/
{
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_vertexShader, nullptr);
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_fragmentShader, nullptr);
}

/**********************************************************/
void RasterPass::compileShaders()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  VkShaderModuleCreateInfo vertexCode =
      SlangCompiler::instance().compile("gltf_raster.slang", gltf_raster_slang);
  VkShaderModuleCreateInfo fragmentCode = SlangCompiler::instance().compile(
      "gltf_fragment.slang", gltf_fragment_slang);

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
