#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <nvvk/descriptors.hpp>
#include <nvvk/sbt_generator.hpp>
#include <scene/scene_context.hpp>
#include <scene/scene_resources.hpp>

#include "_autogen/rtbasic.slang.h"  // Local shader
#include "acceleration.hpp"
#include "scene/shared.hpp"
#include "scene/vulkan_raster.hpp"
#include "shaders/compiler/slang.hpp"

class VulkanRayTracer
{
public:
  void init(VulkanContext* ctx, SlangShaderCompiler* compiler, VulkanRaster* raster)
  {
    m_compiler = compiler;
    m_raster = raster;
  }

  void createPipeline(VulkanContext* ctx, const SceneResources& scene)
  {
    // Get ray tracing properties
    VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    prop2.pNext = &m_properties;
    vkGetPhysicalDeviceProperties2(ctx->physicalDevice, &prop2);
    // Initialize acceleration structure builder
    m_accel.init(ctx);

    // Initialize SBT generator
    m_sbtGenerator.init(ctx->device, m_properties);

    create_ray_tracing_pipeline(ctx, scene);
  }

  void create_ray_tracing_pipeline(VulkanContext* ctx, const SceneResources& scene)
  {
    // Set up acceleration structure infrastructure
    m_accel.buildBLAS(scene.data());  // Set up BLAS infrastructure
    m_accel.buildTLAS(scene.data());  // Set up TLAS infrastructure

    // Set up ray tracing pipeline infrastructure
    createRaytraceDescriptorLayout(ctx);  // Create descriptor layout
    createRayTracingPipeline(ctx);        // Create pipeline structure and SBT
  }

  //--------------------------------------------------------------------------------------------------
  // Create the descriptor set layout for ray tracing
  void createRaytraceDescriptorLayout(VulkanContext* ctx)
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
    m_descPack.init(bindings, ctx->device, 0,
                    VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
  }

  //--------------------------------------------------------------------------------------------------
  // Create ray tracing pipeline structure
  // We create the entries for ray generation, miss, and closest hit shaders.
  // We also create the shader groups and the pipeline layout.
  // The pipeline is used to execute the ray tracing pipeline.
  // We also create the SBT (Shader Binding Table)
  void createRayTracingPipeline(VulkanContext* ctx)
  {
    SCOPED_TIMER(__FUNCTION__);
    // For re-creation
    vkDestroyPipeline(ctx->device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(ctx->device, m_pipelineLayout, nullptr);

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
    VkShaderModuleCreateInfo shaderCode = m_compiler->compile("rtbasic.slang", rtbasic_slang);

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
        {m_raster->descPack().getLayout(), m_descPack.getLayout()}};
    pipeline_layout_create_info.setLayoutCount = uint32_t(layouts.size());
    pipeline_layout_create_info.pSetLayouts = layouts.data();
    vkCreatePipelineLayout(ctx->device, &pipeline_layout_create_info, nullptr, &m_pipelineLayout);
    NVVK_DBG_NAME(m_pipelineLayout);

    // Assemble the shader stages and recursion depth info into the ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    rtPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());  // Stages are shaders
    rtPipelineInfo.pStages = stages.data();
    rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
    rtPipelineInfo.pGroups = shader_groups.data();
    rtPipelineInfo.maxPipelineRayRecursionDepth =
        std::max(3U, m_properties.maxRayRecursionDepth);  // Ray depth
    rtPipelineInfo.layout = m_pipelineLayout;
    vkCreateRayTracingPipelinesKHR(ctx->device, {}, {}, 1, &rtPipelineInfo, nullptr, &m_pipeline);
    NVVK_DBG_NAME(m_pipeline);

    // Create the shader binding table for this pipeline
    createShaderBindingTable(ctx, rtPipelineInfo);
  }

  void createShaderBindingTable(VulkanContext* ctx,
                                const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
  {
    SCOPED_TIMER(__FUNCTION__);

    ctx->allocator->destroyBuffer(m_sbtBuffer);  // Cleanup when re-creating
    // Calculate required SBT buffer size
    size_t bufferSize = m_sbtGenerator.calculateSBTBufferSize(m_pipeline, rtPipelineInfo);

    // Create SBT buffer using the size from above
    NVVK_CHECK(ctx->allocator->createBuffer(
        m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        m_sbtGenerator.getBufferAlignment()));
    NVVK_DBG_NAME(m_sbtBuffer.buffer);

    // Populate the SBT buffer with shader handles and data using the CPU-mapped memory pointer
    NVVK_CHECK(
        m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize, m_sbtBuffer.mapping));
  }

  //---------------------------------------------------------------------------------------------------------------
  // Ray tracing rendering method
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers, VulkanContext* ctx,
              shaderio::PushConstant& pushValues)
  {
    NVVK_DBG_SCOPE(cmd);  // <-- Helps to debug in NSight

    // Ray trace pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);

    // Bind the descriptor sets for the graphics pipeline (making textures available to the shaders)
    const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
        .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
        .stageFlags = VK_SHADER_STAGE_ALL,
        .layout = m_pipelineLayout,
        .firstSet = 0,
        .descriptorSetCount = 1,
        .pDescriptorSets = m_raster->descPack().getSetPtr()};
    vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

    // Push descriptor sets for ray tracing
    nvvk::WriteSetContainer write{};
    write.append(m_descPack.makeWrite(shaderio::BindingPoints::eTlas), m_accel.tlas());
    write.append(m_descPack.makeWrite(shaderio::BindingPoints::eOutImage),
                 gBuffers.getColorImageView(eImgRendered), VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 1,
                              write.size(), write.data());

    const VkPushConstantsInfo pushInfo{.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                       .layout = m_pipelineLayout,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .size = sizeof(shaderio::PushConstant),
                                       .pValues = &pushValues};
    vkCmdPushConstants2(cmd, &pushInfo);

    // Ray trace
    const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
    const VkExtent2D& size = ctx->viewportSize;
    vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit, &regions.callable,
                      size.width, size.height, 1);

    // Barrier to make sure the image is ready for Tonemapping
    nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  }

  void clear(VulkanContext* ctx)
  {
    vkDestroyPipelineLayout(ctx->device, m_pipelineLayout, nullptr);
    vkDestroyPipeline(ctx->device, m_pipeline, nullptr);
    m_descPack.deinit();
    ctx->allocator->destroyBuffer(m_sbtBuffer);
    m_sbtGenerator.deinit();
    m_accel.deinit();
  }

private:
  nvvk::DescriptorPack m_descPack{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_pipelineLayout{};
  nvvk::SBTGenerator m_sbtGenerator{};
  nvvk::Buffer m_sbtBuffer{};

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  // Acceleration Structure Components
  AccelerationStructures m_accel;
  VulkanRaster* m_raster = nullptr;

  SlangShaderCompiler* m_compiler = nullptr;
};