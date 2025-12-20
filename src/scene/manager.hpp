#pragma once

#include <shaders/shaderio.h>

#include <memory>
#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvslang/slang.hpp>
#include <nvutils/camera_manipulator.hpp>
#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

#include "_autogen/rtbasic.slang.h"     // Local shader
#include "_autogen/tonemapper.slang.h"  //   "    "
#include "acceleration.hpp"
#include "nvvk/formats.hpp"
#include "scene/gltf/gltf_utils.hpp"  // GLTF utilities for loading and importing GLTF models
#include "scene/vulkan_raster.hpp"
#include "scene_context.hpp"
#include "scene_resources.hpp"
#include "shaders/compiler/slang.hpp"

class SceneManager
{
public:
  SceneManager(VulkanContext* ctx)
  {
    m_ctx = ctx;
    m_scene_resources.init(m_ctx);

    // Create the G-Buffers
    nvvk::GBufferInitInfo gBufferInit{
        .allocator = m_ctx->allocator,
        .colorFormats = {VK_FORMAT_R32G32B32A32_SFLOAT,
                         VK_FORMAT_R8G8B8A8_UNORM},  // Render target, tonemapped
        .depthFormat = nvvk::findDepthFormat(m_ctx->physicalDevice),
        .imageSampler = m_scene_resources.sampler(),
        .descriptorPool = m_ctx->textureDescriptorPool,
    };
    m_gBuffers.init(gBufferInit);

    m_raster.init(m_ctx);
  }

  void clear()
  {
    m_scene_resources.clear(m_ctx->allocator);
    m_raster.clear(m_ctx->device);
    m_tonemapper.deinit();
    m_gBuffers.deinit();

    // Cleanup acceleration structures
    vkDestroyPipelineLayout(m_ctx->device, m_rtPipelineLayout, nullptr);
    vkDestroyPipeline(m_ctx->device, m_rtPipeline, nullptr);
    m_rtDescPack.deinit();
    m_ctx->allocator->destroyBuffer(m_sbtBuffer);

    m_accel.deinit();
    m_sbtGenerator.deinit();
  }

  void postInit()
  {
    m_scene_resources.updateTextures(m_raster.descPack(), m_ctx);

    // Initialize the tonemapper also with proe-compiled shader
    m_tonemapper.init(m_ctx->allocator, std::span(tonemapper_slang));

    // Get ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_rtProperties;
    vkGetPhysicalDeviceProperties2(m_ctx->physicalDevice, &prop2);

    // Initialize acceleration structure builder
    m_accel.init(m_ctx);

    // Initialize SBT generator
    m_sbtGenerator.init(m_ctx->device, m_rtProperties);

    create_ray_tracing_pipeline();
  }

  void create_ray_tracing_pipeline()
  {
    // Set up acceleration structure infrastructure
    m_accel.buildBLAS(gltf_resources());  // Set up BLAS infrastructure
    m_accel.buildTLAS(gltf_resources());  // Set up TLAS infrastructure

    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout();  // Create descriptor layout
    createRayTracingPipeline();        // Create pipeline structure and SBT
  }

  //--------------------------------------------------------------------------------------------------
  // Create the descriptor set layout for ray tracing
  void createRaytraceDescriptorLayout()
  {
    SCOPED_TIMER(__FUNCTION__);
    nvvk::DescriptorBindings bindings;
    bindings.addBinding({.binding = shaderio::BindingPoints::eTlas,
                         .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_ALL});
    bindings.addBinding({.binding = shaderio::BindingPoints::eOutImage,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                         .descriptorCount = 1,
                         .stageFlags = VK_SHADER_STAGE_ALL});

    // Creating a PUSH descriptor set and set layout from the bindings
    m_rtDescPack.init(bindings, m_ctx->device, 0,
                      VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create ray tracing pipeline structure
  // We create the entries for ray generation, miss, and closest hit shaders.
  // We also create the shader groups and the pipeline layout.
  // The pipeline is used to execute the ray tracing pipeline.
  // We also create the SBT (Shader Binding Table)
  void createRayTracingPipeline()
  {
    SCOPED_TIMER(__FUNCTION__);
    // For re-creation
    vkDestroyPipeline(m_ctx->device, m_rtPipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx->device, m_rtPipelineLayout, nullptr);

    // Creating all shaders
    enum StageIndices
    {
      eRaygen,
      eMiss,
      eClosestHit,
      eShaderGroupCount
    };
    std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
    for (auto& s : stages)
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    // Compile shader, fallback to pre-compiled
    VkShaderModuleCreateInfo shaderCode = m_compiler.compile("rtbasic.slang", rtbasic_slang);

    stages[eRaygen].pNext = &shaderCode;
    stages[eRaygen].pName = "rgenMain";
    stages[eRaygen].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[eMiss].pNext = &shaderCode;
    stages[eMiss].pName = "rmissMain";
    stages[eMiss].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[eClosestHit].pNext = &shaderCode;
    stages[eClosestHit].pName = "rchitMain";
    stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Shader groups
    VkRayTracingShaderGroupCreateInfoKHR group{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;
    // Raygen
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eRaygen;
    shader_groups.push_back(group);

    // Miss
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = eMiss;
    shader_groups.push_back(group);

    // closest hit shader
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = eClosestHit;
    shader_groups.push_back(group);

    // Push constant: we want to be able to update constants used by the shaders
    const VkPushConstantRange push_constant{VK_SHADER_STAGE_ALL, 0, sizeof(shaderio::PushConstant)};

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant;

    // Descriptor sets: one specific to ray tracing, and one shared with the rasterization pipeline
    std::array<VkDescriptorSetLayout, 2> layouts = {
        {m_raster.descPack().getLayout(), m_rtDescPack.getLayout()}};
    pipeline_layout_create_info.setLayoutCount = uint32_t(layouts.size());
    pipeline_layout_create_info.pSetLayouts = layouts.data();
    vkCreatePipelineLayout(m_ctx->device, &pipeline_layout_create_info, nullptr,
                           &m_rtPipelineLayout);
    NVVK_DBG_NAME(m_rtPipelineLayout);

    // Assemble the shader stages and recursion depth info into the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    rtPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());  // Stages are shaders
    rtPipelineInfo.pStages = stages.data();
    rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
    rtPipelineInfo.pGroups = shader_groups.data();
    rtPipelineInfo.maxPipelineRayRecursionDepth =
        std::max(3U, m_rtProperties.maxRayRecursionDepth);  // Ray depth
    rtPipelineInfo.layout = m_rtPipelineLayout;
    vkCreateRayTracingPipelinesKHR(m_ctx->device, {}, {}, 1, &rtPipelineInfo, nullptr,
                                   &m_rtPipeline);
    NVVK_DBG_NAME(m_rtPipeline);

    // Create the shader binding table for this pipeline
    createShaderBindingTable(rtPipelineInfo);
  }

  void render(VkCommandBuffer cmd, bool raytrace)
  {
    // Push constant information
    shaderio::PushConstant pushValues{
        .sceneInfoAddress = (shaderio::GltfSceneInfo*) gltf_resources().bSceneInfo.address,
        .metallicRoughnessOverride = m_metallicRoughnessOverride,
    };

    if (raytrace)
    {
      raytraceScene(cmd, pushValues);
    }
    else
    {
      m_raster.render(cmd, m_gBuffers, m_scene_resources, m_cameraManip, pushValues);
    }
  }

  //---------------------------------------------------------------------------------------------------------------
  // Ray tracing rendering method
  void raytraceScene(VkCommandBuffer cmd, shaderio::PushConstant& pushValues)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Ray trace pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
        .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
        .stageFlags = VK_SHADER_STAGE_ALL,
        .layout = m_rtPipelineLayout,
        .firstSet = 0,
        .descriptorSetCount = 1,
        .pDescriptorSets = m_raster.descPack().getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // Push descriptor sets for ray tracing
    nvvk::WriteSetContainer write{};
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eTlas), m_accel.tlas());
    write.append(m_rtDescPack.makeWrite(shaderio::BindingPoints::eOutImage),
                 m_gBuffers.getColorImageView(eImgRendered), VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipelineLayout, 1,
                              write.size(), write.data());

    const VkPushConstantsInfo pushInfo{.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout = m_rtPipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size = sizeof(shaderio::PushConstant),
                                       .pValues = &pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Ray trace
    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D& size = m_ctx->viewportSize;
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable,
                      size.width, size.height, 1);

    // Barrier to make sure the image is ready for Tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
  {
    SCOPED_TIMER(__FUNCTION__);

    m_ctx->allocator->destroyBuffer(m_sbtBuffer);  // Cleanup when re-creating
    // Calculate required SBT buffer size
    size_t bufferSize = m_sbtGenerator.calculateSBTBufferSize(m_rtPipeline, rtPipelineInfo);

    // Create SBT buffer using the size from above
    NVVK_CHECK(m_ctx->allocator->createBuffer(
        m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        m_sbtGenerator.getBufferAlignment()));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate the SBT buffer with shader handles and data using the CPU-mapped memory pointer
    NVVK_CHECK(
        m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize, m_sbtBuffer.mapping));
  }

  void post_process(VkCommandBuffer cmd)
  {
    // Default post-processing: tonemapping
    // TODO
    m_tonemapper.runCompute(cmd, m_gBuffers.getSize(), m_tonemapperData,
                            m_gBuffers.getDescriptorImageInfo(0),
                            m_gBuffers.getDescriptorImageInfo(1));
  }

  void reload(bool use_raytracing)
  {
    if (use_raytracing)
    {
      createRayTracingPipeline();
    }
    else
    {
      m_raster.reload(m_ctx->device);
    }
  }

  VkImage get_image(int buffer_idx) { return m_gBuffers.getColorImage(buffer_idx); }

  void onResize(VkCommandBuffer cmd, const VkExtent2D& size)
  {
    NVVK_CHECK(m_gBuffers.update(cmd, size));
  }

  std::shared_ptr<nvutils::CameraManipulator> camera() const { return m_cameraManip; }
  nvsamples::GltfSceneResource& gltf_resources() { return m_scene_resources.data(); }
  const nvsamples::GltfSceneResource& gltf_resources() const { return m_scene_resources.data(); }
  SceneResources& scene_resources() { return m_scene_resources; }
  const nvvk::GBuffer& gbuffers() const { return m_gBuffers; }

  shaderio::TonemapperData& tonemapper() { return m_tonemapperData; }
  glm::vec2& metallic_roughness() { return m_metallicRoughnessOverride; }

  void set_camera(std::shared_ptr<nvutils::CameraManipulator> camera)
  {
    m_cameraManip = std::move(camera);
  }

private:
  VulkanContext* m_ctx = nullptr;

  nvvk::GBuffer m_gBuffers{};  // The G-Buffer
  // Camera manipulator
  std::shared_ptr<nvutils::CameraManipulator> m_cameraManip{
      std::make_shared<nvutils::CameraManipulator>()};

  VulkanRaster m_raster;

  SceneResources m_scene_resources{};

  nvshaders::Tonemapper m_tonemapper{};  // Tonemapper for post-processing effects
  shaderio::TonemapperData
      m_tonemapperData{};  // Tonemapper data used to pass parameters to the tonemapper shader
  glm::vec2 m_metallicRoughnessOverride{
      -0.01f, -0.01f};  // Override values for metallic and roughness, used
                        // in the UI to control the material properties

  // Ray Tracing Pipeline Components
  nvvk::DescriptorPack m_rtDescPack;      // Ray tracing descriptor bindings
  VkPipeline m_rtPipeline{};              // Ray tracing pipeline
  VkPipelineLayout m_rtPipelineLayout{};  // Ray tracing pipeline layout

  // Acceleration Structure Components
  AccelerationStructures m_accel;

  nvvk::SBTGenerator m_sbtGenerator;  // Shader binding table wrapper
  nvvk::Buffer m_sbtBuffer;           // Buffer for shader binding table

  // Ray Tracing Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  SlangShaderCompiler m_compiler{nvsamples::getShaderDirs()};
};