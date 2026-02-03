#include "RayTracePass.hpp"

#include <shaders/shaderio.h>

#include <algorithm>
#include <array>
#include <vector>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/ISceneRenderer.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "backend/vulkan/core/RenderContext.hpp"
#include "common/timers.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/compiler/slang.hpp"

// Generated Shader
#include "build/_autogen/rtbasic.slang.h"

/**********************************************************/
RayTracePass::RayTracePass(nvvk::DescriptorPack* descPack)
/**********************************************************/
{
  m_sharedDescPack = descPack;
}

/**********************************************************/
void RayTracePass::init(VulkanContextManager* contextManager,
                        const SceneResourcesManager& scene)
/**********************************************************/
{
  m_context_manager = contextManager;
  createScene(scene);
}

/**********************************************************/
void RayTracePass::deinit(VulkanContextManager* /* coreManager */)
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
void RayTracePass::createScene(const SceneResourcesManager& scene)
/**********************************************************/
{
  createPipeline(scene);
}

/**********************************************************/
void RayTracePass::createPipeline(const SceneResourcesManager& scene)
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

  createRayTracingPipeline(scene);
}

/**********************************************************/
void RayTracePass::createRayTracingPipeline(const SceneResourcesManager& scene)
/**********************************************************/
{
  // Set up ray tracing pipeline infrastructure
  createRaytraceDescriptorLayout();  // Create descriptor layout
  createRayTracingPipeline();        // Create pipeline structure and SBT
}

/**********************************************************/
void RayTracePass::createRaytraceDescriptorLayout()
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

  // Creating a PUSH descriptor set and set layout from the bindings
  m_RayTraceDescPack.init(
      bindings, m_context_manager->getDevice(), 0,
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
}

/**********************************************************/
void RayTracePass::createRayTracingPipeline()
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  // For re-creation
  vkDestroyPipeline(m_context_manager->getDevice(), m_pipeline, nullptr);
  vkDestroyPipelineLayout(m_context_manager->getDevice(), m_pipelineLayout,
                          nullptr);

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
  VkShaderModuleCreateInfo shaderCode =
      SlangCompiler::instance().compile("rtbasic.slang", rtbasic_slang);

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

  // Push constant
  const VkPushConstantRange push_constant{VK_SHADER_STAGE_ALL, 0,
                                          sizeof(shaderio::PushConstant)};

  VkPipelineLayoutCreateInfo pipeline_layout_create_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_create_info.pushConstantRangeCount = 1;
  pipeline_layout_create_info.pPushConstantRanges = &push_constant;

  // Descriptor sets
  std::array<VkDescriptorSetLayout, 2> layouts = {
      {m_sharedDescPack->getLayout(), m_RayTraceDescPack.getLayout()}};
  pipeline_layout_create_info.setLayoutCount = uint32_t(layouts.size());
  pipeline_layout_create_info.pSetLayouts = layouts.data();
  vkCreatePipelineLayout(m_context_manager->getDevice(),
                         &pipeline_layout_create_info, nullptr,
                         &m_pipelineLayout);
  NVVK_DBG_NAME(m_pipelineLayout);

  // Assemble the shader stages
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
  NVVK_DBG_NAME(m_pipeline);

  // Create SBT
  createShaderBindingTable(rtPipelineInfo);
}

/**********************************************************/
void RayTracePass::createShaderBindingTable(
    const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  m_context_manager->getAllocator().destroyBuffer(
      m_sbtBuffer);  // Cleanup when re-creating

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
void RayTracePass::execute(const IRenderContext& ctx)
/**********************************************************/
{

  const auto& vkCtx = VulkanRenderContext::get(ctx);
  const nvvk::GBuffer* gBuffers = vkCtx.gBuffers;
  shaderio::PushConstant constants = vkCtx.pushValues;
  const AccelerationStructures* bvh = vkCtx.bvh;

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
  write.append(
      m_RayTraceDescPack.makeWrite(shaderio::BindingPoints::eOutImage),
      gBuffers->getColorImageView(ISceneRenderer::RenderOutput::Linear),
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
  const nvvk::SBTGenerator::Regions& regions = m_sbtGenerator.getSBTRegions();
  const VkExtent2D& size = gBuffers->getSize();
  vkCmdTraceRaysKHR(cmd, &regions.raygen, &regions.miss, &regions.hit,
                    &regions.callable, size.width, size.height, 1);

  nvvk::cmdMemoryBarrier(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}
