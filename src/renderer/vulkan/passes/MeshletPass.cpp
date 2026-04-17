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
MeshletPass::MeshletPass(const nvvk::DescriptorPack &descPack,
                         const nvvk::Image *hizTexture)
    : m_descPack(descPack), m_hiZTexture(hizTexture)
/**********************************************************/
{}

/**********************************************************/
void MeshletPass::init(VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;

  nvvk::DescriptorBindings passBindings;
  passBindings.addBinding({.binding = shaderio::BindRaster::eHiZTexture,
                           .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                           .descriptorCount = 1,
                           .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT});

  passBindings.addBinding({.binding = shaderio::BindRaster::eHiZSampler,
                           .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                           .descriptorCount = 1,
                           .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT});

  m_passDescPack.init(passBindings, m_context_manager->getDevice(), 0,
                      VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  // -------------------------------------------------------------

  createPipelineLayout(m_context_manager->getDevice());
  compileShaders();
  allocateDynamicBuffers(m_context_manager->getAllocator());
}

/**********************************************************/
void MeshletPass::allocateDynamicBuffers(nvvk::ResourceAllocator &allocator)
/**********************************************************/
{
  VkDeviceSize bufferSize =
      sizeof(shaderio::GlobalMeshletRef) * MAX_SCENE_MESHLETS;

  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    m_context_manager->getAllocator().createBuffer(
        m_globalMeshletRefsBuffers[i], bufferSize,
        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);
  }
}

/**********************************************************/
void MeshletPass::deinit(VulkanContextManager *coreManager)
/**********************************************************/
{
  vkDestroyPipelineLayout(coreManager->getDevice(), m_pipelineLayout, nullptr);
  clearShaders();
  for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    coreManager->getAllocator().destroyBuffer(m_globalMeshletRefsBuffers[i]);
  }

  // Clean up the new descriptor pack
  m_passDescPack.deinit();
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

  // --- BIND SET 0 (Global Textures) ---
  const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
      .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
      .stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT |
                    VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .layout = m_pipelineLayout,
      .firstSet = 0,
      .descriptorSetCount = 1,
      .pDescriptorSets = m_descPack.getSetPtr()};
  vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

  // --- PUSH SET 1 (Hi-Z Pass Data) ---
  if (m_hiZTexture && m_hiZTexture->image != VK_NULL_HANDLE) {
    VkDescriptorImageInfo hizTexInfo = {
        VK_NULL_HANDLE, m_hiZTexture->descriptor.imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkDescriptorImageInfo hizSampInfo = {m_hiZTexture->descriptor.sampler,
                                         VK_NULL_HANDLE,
                                         VK_IMAGE_LAYOUT_UNDEFINED};

    nvvk::WriteSetContainer write{};
    write.append(m_passDescPack.makeWrite(shaderio::BindRaster::eHiZTexture),
                 &hizTexInfo);
    write.append(m_passDescPack.makeWrite(shaderio::BindRaster::eHiZSampler),
                 &hizSampInfo);

    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipelineLayout, 1, write.size(), write.data());
  }

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

  // --- Flatten the scene into a 1D array of meshlet references ---
  std::vector<shaderio::GlobalMeshletRef> globalMeshlets;
  globalMeshlets.reserve(MAX_SCENE_MESHLETS / 10);

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

    uint32_t count = meshPrim.meshlet.meshlets.count;
    for (uint32_t m = 0; m < count; m++) {
      globalMeshlets.push_back({static_cast<uint32_t>(i), m});
    }
  }

  if (globalMeshlets.empty()) {
    vkCmdEndRendering(cmd);
    return;
  }

  if (globalMeshlets.size() > MAX_SCENE_MESHLETS) {
    LOGE("Too many meshlets! Max: %u, Trying to draw: %zu", MAX_SCENE_MESHLETS,
         globalMeshlets.size());
    vkCmdEndRendering(cmd);
    return;
  }

  // --- Upload to VMA mapped pointer ---
  size_t uploadByteSize =
      globalMeshlets.size() * sizeof(shaderio::GlobalMeshletRef);
  std::memcpy(m_globalMeshletRefsBuffers[m_currentFrameIndex].mapping,
              globalMeshlets.data(), uploadByteSize);

  // --- 3. Push constants & Dispatch ---
  constants.totalSceneMeshlets = static_cast<uint32_t>(globalMeshlets.size());
  constants.globalMeshletRefsAddress =
      reinterpret_cast<shaderio::GlobalMeshletRef *>(
          m_globalMeshletRefsBuffers[m_currentFrameIndex].address);

  // Issue the push constants right before the draw call
  vkCmdPushConstants2(cmd, &pushInfo);

  // THE SINGLE GLOBAL DISPATCH
  uint32_t taskGroupCount = (constants.totalSceneMeshlets + 31) / 32;
  vkCmdDrawMeshTasksEXT(cmd, taskGroupCount, 1, 1);

  // Advance frame index for the next execute
  m_currentFrameIndex = (m_currentFrameIndex + 1) % FRAMES_IN_FLIGHT;

  if (culledCount > 0) {
    LOGD("MeshletPass: CPU Culled %u / %zu instances", culledCount,
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

  // --- parsing an array of 2 layouts ---
  VkDescriptorSetLayout layouts[] = {m_descPack.getLayout(),
                                     m_passDescPack.getLayout()};

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 2,
      .pSetLayouts = layouts,
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

  const VkShaderStageFlags pipelineStages = VK_SHADER_STAGE_TASK_BIT_EXT |
                                            VK_SHADER_STAGE_MESH_BIT_EXT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT;

  const VkPushConstantRange identicalRange{
      .stageFlags = pipelineStages,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
  };

  // --- CHANGED: Now parsing an array of 2 layouts ---
  VkDescriptorSetLayout layouts[] = {m_descPack.getLayout(),
                                     m_passDescPack.getLayout()};

  VkShaderCreateInfoEXT shaderInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
      .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
      .setLayoutCount = 2,
      .pSetLayouts = layouts,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &identicalRange,
  };

  // --- TASK SHADER ---
  shaderInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
  shaderInfo.nextStage = VK_SHADER_STAGE_MESH_BIT_EXT;
  shaderInfo.pName = "taskMain";
  shaderInfo.codeSize = taskCode.codeSize;
  shaderInfo.pCode = taskCode.pCode;
  shaderInfo.flags = 0;

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
