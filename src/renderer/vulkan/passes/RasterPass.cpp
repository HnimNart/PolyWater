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
#include "core/timers.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "renderer/vulkan/SceneAssetManager.hpp"

// Generated Shaders
#include "_autogen/gltf_cull.slang.h"
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
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;

  // --- NEW: Instance Map Buffer Setup ---
  // This buffer allows the shader to look up the True Instance ID via DrawID
  VkDeviceSize mapBufferSize = sizeof(uint32_t) * MAX_SCENE_INSTANCES;
  VkBufferUsageFlags2KHR mapUsage =
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR; // match the type

  VkDeviceSize countBufferSize = sizeof(uint32_t);
  VkBufferUsageFlags2KHR countUsage =
      VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR |
      VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR |
      VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR; // Needed to vkCmdFillBuffer
                                              // (clear it)

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

    m_context_manager->getAllocator().createBuffer(
        m_drawCountBuffers[i], countBufferSize, countUsage,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
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
    m_context_manager->getAllocator().destroyBuffer(m_instanceMapBuffers[i]);
    m_context_manager->getAllocator().destroyBuffer(m_drawCountBuffers[i]);
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

  uint32_t totalInstances =
      static_cast<uint32_t>(sceneResources->instances.size());
  if (totalInstances == 0) {
    m_currentFrameIndex = (m_currentFrameIndex + 1) % FRAMES_IN_FLIGHT;
    return;
  }

  // ==========================================================
  // 1. GPU CULLING & INDIRECT COMMAND PREPARATION
  // ==========================================================

  // Clear the draw count buffer to 0 before the compute shader runs
  vkCmdFillBuffer(cmd, m_drawCountBuffers[m_currentFrameIndex].buffer, 0,
                  VK_WHOLE_SIZE, 0);

  // Barrier: Ensure the clear finishes before the compute shader starts
  // executing
  VkMemoryBarrier clearBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  clearBarrier.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &clearBarrier, 0, nullptr, 0, nullptr);

  // Set compute-specific push constants
  constants.indirectCommandsAddress =
      m_indirectCommandsBuffers[m_currentFrameIndex].address;
  constants.instanceMapAddress =
      m_instanceMapBuffers[m_currentFrameIndex].address;
  constants.drawCountAddress = m_drawCountBuffers[m_currentFrameIndex].address;

  // Bind the compute shader object (Assuming you compiled gltf_cull.slang into
  // m_cullShader)
  const VkShaderEXT compShaders[] = {m_cullShader};
  const VkShaderStageFlagBits compStages[] = {VK_SHADER_STAGE_COMPUTE_BIT};
  vkCmdBindShadersEXT(cmd, 1, compStages, compShaders);

  constants.totalSceneInstances = totalInstances;
  const VkPushConstantsInfo compPushInfo{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT |
                    VK_SHADER_STAGE_ALL_GRAPHICS, // Ensure it hits compute
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
      .pValues = &constants,
  };

  vkCmdPushConstants2(cmd, &compPushInfo);

  // Dispatch the compute shader
  uint32_t groupCount = (totalInstances + 63) / 64; // 64 threads per group
  vkCmdDispatch(cmd, groupCount, 1, 1);

  // Barrier: Ensure compute shader finishes writing before the graphics
  // pipeline reads the buffers
  VkMemoryBarrier computeBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  computeBarrier.dstAccessMask =
      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                           VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                       0, 1, &computeBarrier, 0, nullptr, 0, nullptr);

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
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
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

  const VkShaderEXT gfxShaders[] = {
      m_vertexShader,   VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
      m_fragmentShader, VK_NULL_HANDLE, VK_NULL_HANDLE};
  const VkShaderStageFlagBits gfxStages[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_TASK_BIT_EXT,
      VK_SHADER_STAGE_MESH_BIT_EXT};

  vkCmdBindShadersEXT(cmd, 7, gfxStages, gfxShaders);
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
  vkCmdSetPrimitiveTopologyEXT(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  // Re-push constants for the graphics pipeline just in case (though layout
  // overlap might make this redundant, it's safer)
  const VkPushConstantsInfo gfxPushInfo{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
      .pValues = &constants,
  };
  vkCmdPushConstants2(cmd, &gfxPushInfo);

  // ==========================================================
  // 3. THE DRAW LOOP (GPU Driven)
  // ==========================================================

  VkExtent2D fragmentSize = {1, 1};
  VkFragmentShadingRateCombinerOpKHR combinerOps[2] = {
      VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
      VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
  vkCmdSetFragmentShadingRateKHR(cmd, &fragmentSize, combinerOps);

  // Use DrawIndirectCount. The GPU will read the exact number of culled
  // instances from m_drawCountBuffers and execute that many indirect draws!
  vkCmdDrawIndirectCount(
      cmd, m_indirectCommandsBuffers[m_currentFrameIndex].buffer,
      0, // Command Buffer & Offset
      m_drawCountBuffers[m_currentFrameIndex].buffer,
      0,                            // Count Buffer & Offset
      MAX_SCENE_INSTANCES,          // Maximum possible draws
      sizeof(VkDrawIndirectCommand) // Stride between commands
  );

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
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
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
  vkDestroyShaderEXT(m_context_manager->getDevice(), m_cullShader, nullptr);
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
  VkShaderModuleCreateInfo cullCode =
      SlangCompiler::instance().compile("gltf_cull.slang", gltf_cull_slang);

  // UPDATED: Added VK_SHADER_STAGE_COMPUTE_BIT to the push constant range
  const VkPushConstantRange pushConstantRange{
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
  };

  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .pName = "main", // Default, will be overridden below
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };

  // 1. Vertex Shader
  shaderInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.pName = "vertexMain";
  shaderInfo.codeSize = vertexCode.codeSize;
  shaderInfo.pCode = vertexCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_vertexShader);
  NVVK_DBG_NAME(m_vertexShader);

  // 2. Fragment Shader
  shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName = "fragmentMain";
  shaderInfo.codeSize = fragmentCode.codeSize;
  shaderInfo.pCode = fragmentCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_fragmentShader);
  NVVK_DBG_NAME(m_fragmentShader);

  // 3. Compute Culling Shader
  shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName =
      "computeMain"; // Must match the entry point name in gltf_cull.slang
  shaderInfo.codeSize = cullCode.codeSize;
  shaderInfo.pCode = cullCode.pCode;
  vkCreateShadersEXT(m_context_manager->getDevice(), 1U, &shaderInfo, nullptr,
                     &m_cullShader);
  NVVK_DBG_NAME(m_cullShader);
}
