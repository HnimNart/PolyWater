#include "RayTracePass.hpp"

#include <shaders/shared/structs.h>

#include <algorithm>
#include <array>
#include <vector>

#include "nvvk/check_error.hpp"
#include "nvvk/debug_util.hpp"
#include "nvvk/gbuffers.hpp"

#include "backend/interfaces/IRenderer.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "backend/vulkan/core/RenderContext.hpp"
#include "compiler/slang.hpp"
#include "core/timers.hpp"
#include "scene/SceneResources.hpp"

/**********************************************************/
RayTracePass::RayTracePass(nvvk::DescriptorPack *descPack,
                           ShaderManager *shaderManager)
/**********************************************************/
{
  m_sharedDescPack = descPack;
  m_shaderManager = shaderManager;
}

/**********************************************************/
void RayTracePass::init(VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;
  createPipeline();
}

/**********************************************************/
void RayTracePass::deinit(VulkanContextManager * /* coreManager */)
/**********************************************************/
{
  vkDestroyPipelineLayout(m_context_manager->getDevice(), m_pipelineLayout,
                          nullptr);
  vkDestroyPipeline(m_context_manager->getDevice(), m_pipeline, nullptr);
  m_RayTraceDescPack.deinit();
  m_context_manager->getAllocator().destroyBuffer(m_sbtBuffer);
  m_sbtGenerator.deinit();
}

/**********************************************************/
void RayTracePass::setup(PassBuilder &builder)
/**********************************************************/
{
  // Ray tracing output: writing to the Linear Color buffer
  // We use General state because it's a Storage Image write
  builder.write(RenderOutput::Linear, PipelineStage::RayTracing,
                ResourceState::General);

  // Ray tracing input: The TLAS is an acceleration structure (Read)
  // Note: If your PassBuilder supports buffer/AS tracking, add it here
}

/**********************************************************/
void RayTracePass::createPipeline()
/**********************************************************/
{
  // Get ray tracing properties
  VkPhysicalDeviceProperties2 prop2{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  prop2.pNext = &m_properties;
  vkGetPhysicalDeviceProperties2(m_context_manager->getPhysicalDevice(),
                                 &prop2);

  // Initialize SBT generator
  m_sbtGenerator.init(m_context_manager->getDevice(), m_properties);

  createRayTracingPipeline();
}

/**********************************************************/
void RayTracePass::createRayTracingPipeline()
/**********************************************************/
{
  // Set up ray tracing pipeline infrastructure
  createDescriptorLayout(); // Create descriptor layout
  createPipelineSBT();      // Create pipeline structure and SBT
}

/**********************************************************/
void RayTracePass::createDescriptorLayout()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  nvvk::DescriptorBindings bindings;
  bindings.addBinding(
      {.binding = shaderio::BindingPoints::eTlas,
       .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_ALL});
  bindings.addBinding({.binding = shaderio::BindingPoints::eOutImage,
                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       .descriptorCount = 1,
                       .stageFlags = VK_SHADER_STAGE_ALL});
  bindings.addBinding({.binding = shaderio::BindingPoints::eAccumImage,
                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       .descriptorCount = 1,
                       .stageFlags = VK_SHADER_STAGE_ALL});

  // Creating a PUSH descriptor set and set layout from the bindings
  m_RayTraceDescPack.init(
      bindings, m_context_manager->getDevice(), 0,
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
}

/**********************************************************/
void RayTracePass::createPipelineSBT()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  // Cleanup existing pipeline
  vkDestroyPipeline(m_context_manager->getDevice(), m_pipeline, nullptr);
  vkDestroyPipelineLayout(m_context_manager->getDevice(), m_pipelineLayout,
                          nullptr);

  m_shaderCode.clear();
  // Reserving space: Raygen + 2 Miss (Radiance & Shadow) + Registry size
  m_shaderCode.reserve(m_shaderManager->getRegistry().size() + 3);

  std::vector<VkPipelineShaderStageCreateInfo> stages;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

  // --- 1. RAYGEN STAGE (Index 0) ---
  const RaygenEntry &raygen = m_shaderManager->getRaygen();
  m_shaderCode.push_back(
      SlangCompiler::instance().compile(raygen.filename, raygen.spirv));
  stages.push_back(
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = &m_shaderCode.back(),
       .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
       .pName = raygen.entryPoint.c_str()});

  shader_groups.push_back(
      {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
       .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
       .generalShader = 0, // Points to Raygen stage
       .closestHitShader = VK_SHADER_UNUSED_KHR,
       .anyHitShader = VK_SHADER_UNUSED_KHR,
       .intersectionShader = VK_SHADER_UNUSED_KHR});

  // --- 2. MISS STAGES ---

  // Miss Stage 0: Radiance/Sky (Index 1)
  const MissEntry &miss = m_shaderManager->getMiss();
  m_shaderCode.push_back(
      SlangCompiler::instance().compile(miss.filename, miss.spirv));
  stages.push_back(
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = &m_shaderCode.back(),
       .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
       .pName = miss.entryPoint.c_str()});

  shader_groups.push_back(
      {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
       .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
       .generalShader = 1, // Points to Radiance Miss stage
       .closestHitShader = VK_SHADER_UNUSED_KHR,
       .anyHitShader = VK_SHADER_UNUSED_KHR,
       .intersectionShader = VK_SHADER_UNUSED_KHR});

  // Miss Stage 1: Shadow (Index 2)
  const MissEntry &missShadow = m_shaderManager->getShadowMiss();
  m_shaderCode.push_back(
      SlangCompiler::instance().compile(missShadow.filename, missShadow.spirv));

  stages.push_back(
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = &m_shaderCode.back(),
       .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
       .pName = missShadow.entryPoint.c_str()});

  shader_groups.push_back(
      {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
       .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
       .generalShader = 2, // Points to Shadow Miss stage
       .closestHitShader = VK_SHADER_UNUSED_KHR,
       .anyHitShader = VK_SHADER_UNUSED_KHR,
       .intersectionShader = VK_SHADER_UNUSED_KHR});

  // --- 3. HIT GROUPS ---
  for (auto &[type, entry] : m_shaderManager->getRegistry()) {
    uint32_t currentStageIndex = static_cast<uint32_t>(stages.size());
    m_shaderCode.push_back(SlangCompiler::instance().compile(entry.filename));

    stages.push_back(
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = &m_shaderCode.back(),
         .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
         .pName = entry.entryPoint.c_str()});

    // The sbtIndex is relative to the start of the hit group section.
    // We have 1 Raygen group and 2 Miss groups, so we subtract 3.
    entry.sbtIndex = static_cast<uint32_t>(shader_groups.size()) - 3;

    shader_groups.push_back(
        {.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
         .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
         .generalShader = VK_SHADER_UNUSED_KHR,
         .closestHitShader = currentStageIndex,
         .anyHitShader = VK_SHADER_UNUSED_KHR,
         .intersectionShader = VK_SHADER_UNUSED_KHR});
  }

  // --- 4. PIPELINE CREATION ---
  const VkPushConstantRange push_constant{VK_SHADER_STAGE_ALL, 0,
                                          sizeof(shaderio::PushConstant)};
  std::array<VkDescriptorSetLayout, 2> layouts = {
      m_sharedDescPack->getLayout(), m_RayTraceDescPack.getLayout()};

  VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_create_info.pushConstantRangeCount = 1;
  pipeline_layout_create_info.pPushConstantRanges = &push_constant;
  pipeline_layout_create_info.setLayoutCount = uint32_t(layouts.size());
  pipeline_layout_create_info.pSetLayouts = layouts.data();

  vkCreatePipelineLayout(m_context_manager->getDevice(),
                         &pipeline_layout_create_info, nullptr,
                         &m_pipelineLayout);

  VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{
      VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
  rtPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  rtPipelineInfo.pStages = stages.data();
  rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
  rtPipelineInfo.pGroups = shader_groups.data();
  rtPipelineInfo.maxPipelineRayRecursionDepth =
      std::max(3U, m_properties.maxRayRecursionDepth);
  rtPipelineInfo.layout = m_pipelineLayout;

  vkCreateRayTracingPipelinesKHR(m_context_manager->getDevice(), {}, {}, 1,
                                 &rtPipelineInfo, nullptr, &m_pipeline);

  createShaderBindingTable(rtPipelineInfo);
}

/**********************************************************/
void RayTracePass::createShaderBindingTable(
    const VkRayTracingPipelineCreateInfoKHR &rtPipelineInfo)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  m_context_manager->getAllocator().destroyBuffer(
      m_sbtBuffer); // Cleanup when re-creating

  // Calculate required SBT buffer size
  size_t bufferSize =
      m_sbtGenerator.calculateSBTBufferSize(m_pipeline, rtPipelineInfo);

  // Create SBT buffer
  NVVK_CHECK(m_context_manager->getAllocator().createBuffer(
      m_sbtBuffer, bufferSize, VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR,
      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      VMA_ALLOCATION_CREATE_MAPPED_BIT |
          VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
      m_sbtGenerator.getBufferAlignment()));
  NVVK_DBG_NAME(m_sbtBuffer.buffer);

  // Populate the SBT buffer
  NVVK_CHECK(m_sbtGenerator.populateSBTBuffer(m_sbtBuffer.address, bufferSize,
                                              m_sbtBuffer.mapping));
}

/**********************************************************/
void RayTracePass::execute(const IRenderContext &ctx)
/**********************************************************/
{
  const auto &vkCtx = VulkanRenderContext::get(ctx);
  const nvvk::GBuffer *gBuffers = vkCtx.gBuffers;
  const AccelerationStructures *bvh = vkCtx.bvh;

  VkCommandBuffer cmd = vkCtx.cmdBuffer;

  NVVK_DBG_SCOPE(cmd);

  // Ray trace pipeline binding
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);

  // Bind shared descriptor sets (Raster)
  const VkBindDescriptorSetsInfo bindDescriptorSetsInfo{
      .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO,
      .stageFlags = VK_SHADER_STAGE_ALL,
      .layout = m_pipelineLayout,
      .firstSet = 0,
      .descriptorSetCount = 1,
      .pDescriptorSets = m_sharedDescPack->getSetPtr()};
  vkCmdBindDescriptorSets2(cmd, &bindDescriptorSetsInfo);

  // Push descriptor sets for ray tracing
  nvvk::WriteSetContainer write{};
  write.append(m_RayTraceDescPack.makeWrite(shaderio::BindingPoints::eTlas),
               bvh->tlas());
  write.append(m_RayTraceDescPack.makeWrite(shaderio::BindingPoints::eOutImage),
               gBuffers->getColorImageView(RenderOutput::Linear),
               VK_IMAGE_LAYOUT_GENERAL);
  write.append(
      m_RayTraceDescPack.makeWrite(shaderio::BindingPoints::eAccumImage),
      gBuffers->getColorImageView(RenderOutput::AccumLinear),
      VK_IMAGE_LAYOUT_GENERAL);
  vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_pipelineLayout, 1, write.size(), write.data());

  // Push constants
  const VkPushConstantsInfo pushInfo{.sType =
                                         VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                     .layout = m_pipelineLayout,
                                     .stageFlags = VK_SHADER_STAGE_ALL,
                                     .size = sizeof(shaderio::PushConstant),
                                     .pValues = &vkCtx.pushValues};
  vkCmdPushConstants2(cmd, &pushInfo);

  // Ray trace
  const nvvk::SBTGenerator::Regions &regions = m_sbtGenerator.getSBTRegions();
  const VkExtent2D &size = gBuffers->getSize();
  vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit,
                    &regions.callable, size.width, size.height, 1);
}
