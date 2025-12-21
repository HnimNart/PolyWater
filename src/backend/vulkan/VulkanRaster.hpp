#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <nvutils/camera_manipulator.hpp>
#include <nvutils/timers.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>
#include <nvvk/graphics_pipeline.hpp>

#include "VulkanContext.hpp"
#include "_autogen/foundation.slang.h"  // Local shader
#include "_autogen/sky_simple.slang.h"  // Local shader
#include "scene/SceneResources.hpp"
#include "scene/Shared.hpp"
#include "shaders/compiler/slang.hpp"

class VulkanRaster
{
public:
  void init(VulkanContext* ctx, SlangShaderCompiler* compiler)
  {
    assert(ctx);
    m_ctx = ctx;
    m_compiler = compiler;
    createDescriptorSetLayout(m_ctx->device);
    createPipelineLayout(m_ctx->device);
    compileShaders(m_ctx);
  }

  void clear(VulkanContext* ctx)
  {
    m_descPack.deinit();
    vkDestroyPipelineLayout(ctx->device, m_pipelineLayout, nullptr);
    vkDestroyShaderEXT(ctx->device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(ctx->device, m_fragmentShader, nullptr);
    m_skySimple.deinit();
  }

  void resize(VkCommandBuffer cmd, VkExtent2D size);

  // Raster //
  //---------------------------------------------------------------------------------------------------------------
  // Recording the commands to render the scene
  //
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers, const CpuSceneResources& scene,
              const std::shared_ptr<nvutils::CameraManipulator>& camera,
              shaderio::PushConstant& push_constants) const
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    const nvsamples::GltfSceneResource& gltf_resources = scene.data();
    const VkPushConstantsInfo pushInfo{
        .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
        .layout = m_pipelineLayout,
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset = 0,
        .size = sizeof(shaderio::PushConstant),
        .pValues = &push_constants,  // Other values are passed later
    };

    // Rendering the Sky
    VkExtent2D size = {camera->getWindowSize().x, camera->getWindowSize().y};
    if (gltf_resources.sceneInfo.useSky)
    {
      const glm::mat4& viewMatrix = camera->getViewMatrix();
      const glm::mat4& projMatrix = camera->getPerspectiveMatrix();
      m_skySimple.runCompute(cmd, size, viewMatrix, projMatrix,
                             gltf_resources.sceneInfo.skySimpleParam,
                             gBuffers.getDescriptorImageInfo(0));
    }

    // Rendering to the GBuffer
    VkRenderingAttachmentInfo colorAttachment = DEFAULT_VkRenderingAttachmentInfo;
    colorAttachment.loadOp =
        gltf_resources.sceneInfo.useSky
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;  // Load the previous content of the GBuffer color
                                            // attachment (Sky rendering)
    colorAttachment.imageView = gBuffers.getColorImageView(0);
    colorAttachment.clearValue = {.color = {gltf_resources.sceneInfo.backgroundColor.x,
                                            gltf_resources.sceneInfo.backgroundColor.y,
                                            gltf_resources.sceneInfo.backgroundColor.z, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment = DEFAULT_VkRenderingAttachmentInfo;
    depthAttachment.imageView = gBuffers.getDepthImageView();
    depthAttachment.clearValue = {.depthStencil = DEFAULT_VkClearDepthStencilValue};

    // Create the rendering info
    VkRenderingInfo renderingInfo = DEFAULT_VkRenderingInfo;
    renderingInfo.renderArea = DEFAULT_VkRect2D(gBuffers.getSize());
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    // Change the GBuffer layout to prepare for rendering (attachment)
    nvvk::cmdImageMemoryBarrier(cmd, {gBuffers.getColorImage(eImgRendered), VK_IMAGE_LAYOUT_GENERAL,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
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

    // All dynamic states are set here
    nvvk::GraphicsPipelineState m_pipeline{};
    m_pipeline.rasterizationState.cullMode =
        VK_CULL_MODE_NONE;  // Don't cull any triangles (double-sided rendering)
    m_pipeline.cmdApplyAllStates(cmd);
    m_pipeline.cmdSetViewportAndScissor(cmd, size);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);

    // Same shader for all meshes
    m_pipeline.cmdBindShaders(cmd, {.vertex = m_vertexShader, .fragment = m_fragmentShader});

    // We don't send vertex attributes, they are pulled in the shader
    VkVertexInputBindingDescription2EXT bindingDescription = {};
    VkVertexInputAttributeDescription2EXT attributeDescription = {};
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    for (size_t i = 0; i < gltf_resources.instances.size(); i++)
    {
      uint32_t meshIndex = gltf_resources.instances[i].meshIndex;
      const shaderio::GltfMesh& gltfMesh = gltf_resources.meshes[meshIndex];
      const shaderio::TriangleMesh& triMesh = gltfMesh.triMesh;

      // Push constant is information that is passed to the shader at each draw call.
      push_constants.normalMatrix =
          glm::transpose(glm::inverse(glm::mat3(gltf_resources.instances[i].transform)));
      push_constants.instanceIndex = int(i);  // The index of the instance in the m_instances vector
      vkCmdPushConstants2(cmd, &pushInfo);

      // Get the buffer directly using the pre-computed mapping
      uint32_t bufferIndex = gltf_resources.meshToBufferIndex[meshIndex];
      const nvvk::Buffer& v = gltf_resources.bGltfDatas[bufferIndex];

      // Bind index buffers
      vkCmdBindIndexBuffer(cmd, v.buffer, triMesh.indices.offset, VkIndexType(gltfMesh.indexType));

      // Draw the mesh
      vkCmdDrawIndexed(cmd, triMesh.indices.count, 1, 0, 0, 0);  // All indices
    }

    // ** END RENDERING **
    vkCmdEndRendering(cmd);
    nvvk::cmdImageMemoryBarrier(cmd, {gBuffers.getColorImage(eImgRendered),
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_GENERAL});
  }

  void reload() { compileShaders(m_ctx); }

  const nvvk::GBuffer& gbuffer() const;

  nvvk::DescriptorPack& descPack() { return m_descPack; }

private:
  void createDescriptorSetLayout(VkDevice device)
  {
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding = shaderio::BindingPoints::eTextures,
                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         .descriptorCount = 10,  // Maximum number of textures used in the scene
                         .stageFlags = VK_SHADER_STAGE_ALL},
                        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
    // Creating the descriptor set and set layout from the bindings
    m_descPack.init(bindings, device, 1, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    NVVK_DBG_NAME(m_descPack.getLayout());
    NVVK_DBG_NAME(m_descPack.getPool());
    NVVK_DBG_NAME(m_descPack.getSet(0));
  }
  //--------------------------------------------------------------------------------------------------
  // The graphic pipeline is all the stages that are used to render a section of the scene.
  // Stages like: vertex shader, fragment shader, rasterization, and blending.
  //
  void createPipelineLayout(VkDevice device)
  {
    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                                .offset = 0,
                                                .size = sizeof(shaderio::PushConstant)};

    // The pipeline layout is used to pass data to the pipeline, anything with "layout" in the
    // shader
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

  //---------------------------------------------------------------------------------------------------------------
  // Compile the graphics shaders and create the shader modules.
  void compileShaders(VulkanContext* ctx)
  {
    SCOPED_TIMER(__FUNCTION__);

    // Use pre-compiled shaders by default
    VkShaderModuleCreateInfo shaderCode = m_compiler->compile("foundation.slang", foundation_slang);
    // SlangCompiler::instance().compiler().compile("foundation.slang", foundation_slang);

    // Destroy the previous shaders if they exist
    vkDestroyShaderEXT(ctx->device, m_vertexShader, nullptr);
    vkDestroyShaderEXT(ctx->device, m_fragmentShader, nullptr);
    m_skySimple.deinit();

    // Push constant is used to pass data to the shader at each frame
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
        .offset = 0,
        .size = sizeof(shaderio::PushConstant),
    };

    // Shader create information, this is used to create the shader modules
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
    shaderInfo.pName = "vertexMain";  // The entry point of the vertex shader
    shaderInfo.codeSize = shaderCode.codeSize;
    shaderInfo.pCode = shaderCode.pCode;
    vkCreateShadersEXT(ctx->device, 1U, &shaderInfo, nullptr, &m_vertexShader);
    NVVK_DBG_NAME(m_vertexShader);

    // Fragment Shader
    shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderInfo.nextStage = 0;
    shaderInfo.pName = "fragmentMain";  // The entry point of the vertex shader
    shaderInfo.codeSize = shaderCode.codeSize;
    shaderInfo.pCode = shaderCode.pCode;
    vkCreateShadersEXT(ctx->device, 1U, &shaderInfo, nullptr, &m_fragmentShader);
    NVVK_DBG_NAME(m_fragmentShader);

    // Initialize the Sky with the pre-compiled shader
    m_skySimple.init(ctx->allocator, std::span(sky_simple_slang));
  }

private:
  VulkanContext* m_ctx = nullptr;
  nvvk::DescriptorPack m_descPack{};
  VkPipelineLayout m_pipelineLayout{};

  VkShaderEXT m_vertexShader{};
  VkShaderEXT m_fragmentShader{};
  nvshaders::SkySimple m_skySimple{};  // Sky rendering

  SlangShaderCompiler* m_compiler = nullptr;
};