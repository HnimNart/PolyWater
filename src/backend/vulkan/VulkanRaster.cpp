#include "VulkanRaster.hpp"

// Implementation Includes
#include <shaders/shaderio.h>

#include <nvutils/camera_manipulator.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "VulkanBackend.hpp"
#include "VulkanRenderResources.hpp"
#include "common/timers.hpp"
#include "scene/Shared.hpp"
#include "shaders/compiler/slang.hpp"

// Generated Shaders
#include "_autogen/foundation.slang.h"
#include "_autogen/sky_simple.slang.h"
#include "scene/gltf/gltf_utils.hpp"

void VulkanRaster::init(core::VulkanBackend* backend)
{
  assert(backend);
  m_backend = backend;
  createDescriptorSetLayout(m_backend->getDevice());
  createPipelineLayout(m_backend->getDevice());
  compileShaders();
}

void VulkanRaster::deinit()
{
  m_descPack.deinit();
  vkDestroyPipelineLayout(m_backend->getDevice(), m_pipelineLayout, nullptr);
  vkDestroyShaderEXT(m_backend->getDevice(), m_vertexShader, nullptr);
  vkDestroyShaderEXT(m_backend->getDevice(), m_fragmentShader, nullptr);
  m_skySimple.deinit();
}

void VulkanRaster::resize(VkCommandBuffer cmd, VkExtent2D size)
{
  // Implementation for resize if needed, otherwise empty as per original code context
}

void VulkanRaster::render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers,
                          const gltf::Scene& sceneResources,
                          const GltfDeviceSceneResources& deviceResources,
                          const std::shared_ptr<nvutils::CameraManipulator>& camera,
                          shaderio::PushConstant& pushConstants) const
{
  NVVK_DBG_SCOPE(cmd);

  const shaderio::GltfSceneInfo& scene_info = sceneResources.sceneInfo;

  // Define push info
  const VkPushConstantsInfo pushInfo{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
      .layout = m_pipelineLayout,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      .offset = 0,
      .size = sizeof(shaderio::PushConstant),
      .pValues = &pushConstants,
  };

  // Rendering the Sky
  VkExtent2D size = {camera->getWindowSize().x, camera->getWindowSize().y};
  if (scene_info.useSky)
  {
    const glm::mat4& viewMatrix = camera->getViewMatrix();
    const glm::mat4& projMatrix = camera->getPerspectiveMatrix();
    m_skySimple.runCompute(cmd, size, viewMatrix, projMatrix, scene_info.skySimpleParam,
                           gBuffers.getDescriptorImageInfo(0));
  }

  // Rendering to the GBuffer - Attachments
  VkRenderingAttachmentInfo colorAttachment = DEFAULT_VkRenderingAttachmentInfo;
  colorAttachment.loadOp =
      scene_info.useSky ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.imageView = gBuffers.getColorImageView(0);
  colorAttachment.clearValue = {.color = {scene_info.backgroundColor.x,
                                          scene_info.backgroundColor.y,
                                          scene_info.backgroundColor.z, 1.0f}};

  VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
  depthAttachment.imageView = gBuffers.getDepthImageView();
  depthAttachment.clearValue = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

  VkRenderingInfo renderingInfo = DEFAULT_VkRenderingInfo;
  renderingInfo.renderArea = DEFAULT_VkRect2D(gBuffers.getSize());
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = &depthAttachment;

  // Transition GBuffer layout
  nvvk::cmdImageMemoryBarrier(cmd, {gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_GENERAL,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

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

  // Dynamic states
  nvvk::GraphicsPipelineState pipelineState{};
  pipelineState.rasterizationState.cullMode = VK_CULL_MODE_NONE;
  pipelineState.cmdApplyAllStates(cmd);
  pipelineState.cmdSetViewportAndScissor(cmd, size);
  vkCmdSetDepthTestEnable(cmd, VK_TRUE);

  // Bind Shaders
  const VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
  const VkShaderEXT shaders[] = {m_vertexShader, m_fragmentShader};
  vkCmdBindShadersEXT(cmd, 2, stages, shaders);

  // Vertex Input (Empty, pulled in shader)
  vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

  // Draw Loop
  for (size_t i = 0; i < sceneResources.instances.size(); i++)
  {
    uint32_t meshIndex = sceneResources.instances[i].meshIndex;
    const shaderio::GltfMesh& gltfMesh = sceneResources.meshes[meshIndex];
    const shaderio::TriangleMesh& triMesh = gltfMesh.triMesh;

    // Push constants
    pushConstants.normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(sceneResources.instances[i].transform)));
    pushConstants.instanceIndex = int(i);
    vkCmdPushConstants2(cmd, &pushInfo);

    // Index Buffer
    uint32_t bufferIndex = deviceResources.meshToBufferIndex[meshIndex];
    const nvvk::Buffer& v = deviceResources.bGltfDatas[bufferIndex];

    vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset, VkIndexType(gltfMesh.indexType));

    // Draw
    vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);
  }

  // ** END RENDERING **
  vkCmdEndRendering(cmd);

  // Transition back to GENERAL
  nvvk::cmdImageMemoryBarrier(cmd,
                              {gBuffers.getColorImage(eImgRendered),
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL});
}

void VulkanRaster::reload()
{
  clearShaders();
  compileShaders();
}

void VulkanRaster::createDescriptorSetLayout(VkDevice device)
{
  nvvk::DescriptorBindings bindings;
  bindings.addBinding({.binding = shaderio::BindingPoints::eTextures,
                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       .descriptorCount = 10,
                       .stageFlags = VK_SHADER_STAGE_ALL},
                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

  m_descPack.init(bindings, device, 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                  VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

  NVVK_DBG_NAME(m_descPack.getLayout());
  NVVK_DBG_NAME(m_descPack.getPool());
  NVVK_DBG_NAME(m_descPack.getSet(0));
}

void VulkanRaster::createPipelineLayout(VkDevice device)
{
  const VkPushConstantRange pushConstantRange{.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                              .offset = 0,
                                              .size = sizeof(shaderio::PushConstant)};

  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = m_descPack.getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };
  NVVK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout));
  NVVK_DBG_NAME(m_pipelineLayout);
}

void VulkanRaster::clearShaders()
{
  // Cleanup old shaders
  vkDestroyShaderEXT(m_backend->getDevice(), m_vertexShader, nullptr);
  vkDestroyShaderEXT(m_backend->getDevice(), m_fragmentShader, nullptr);
  m_skySimple.deinit();
}

void VulkanRaster::compileShaders()
{
  common::ScopedTimer(__FUNCTION__);

  // Compile Shader
  VkShaderModuleCreateInfo shaderCode =
      SlangCompiler::instance().compile("foundation.slang", foundation_slang);

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
      .pSetLayouts = descPack().getLayoutPtr(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };

  // Vertex Shader
  shaderInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.pName = "vertexMain";
  shaderInfo.codeSize = shaderCode.codeSize;
  shaderInfo.pCode = shaderCode.pCode;
  vkCreateShadersEXT(m_backend->getDevice(), 1U, &shaderInfo, nullptr, &m_vertexShader);
  NVVK_DBG_NAME(m_vertexShader);

  // Fragment Shader
  shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderInfo.nextStage = 0;
  shaderInfo.pName = "fragmentMain";
  shaderInfo.codeSize = shaderCode.codeSize;
  shaderInfo.pCode = shaderCode.pCode;
  vkCreateShadersEXT(m_backend->getDevice(), 1U, &shaderInfo, nullptr, &m_fragmentShader);
  NVVK_DBG_NAME(m_fragmentShader);

  // Sky
  m_skySimple.init(&m_backend->allocator(), std::span(sky_simple_slang));
}
