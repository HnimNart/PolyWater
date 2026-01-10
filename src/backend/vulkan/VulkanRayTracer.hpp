#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/descriptors.hpp>
#include <nvvk/sbt_generator.hpp>

// Helper class for Acceleration Structures (held by value)
#include "VulkanAcceleration.hpp"

// Forward Declarations
struct VulkanContext;
class VulkanRaster;
class SlangShaderCompiler;
class CpuSceneResources;

namespace nvvk
{
class GBuffer;
}
namespace shaderio
{
struct PushConstant;
}

class VulkanRayTracer
{
public:
  void init(std::shared_ptr<VulkanContext> ctx, VulkanRaster* raster);
  void deinit();

  void createPipeline(const CpuSceneResources& scene);

  // Rendering
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers,
              const shaderio::PushConstant& pushValues) const;

  // Internal helpers (made public as per original class structure, or keep public if intended)
  void create_ray_tracing_pipeline(const CpuSceneResources& scene);
  void createRaytraceDescriptorLayout();
  void createRayTracingPipeline();

private:
  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

private:
  std::shared_ptr<VulkanContext> m_ctx = nullptr;
  std::shared_ptr<SlangShaderCompiler> m_compiler = nullptr;
  VulkanRaster* m_raster = nullptr;

  nvvk::DescriptorPack m_descPack{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_pipelineLayout{};

  // SBT Components
  nvvk::SBTGenerator m_sbtGenerator{};
  nvvk::Buffer m_sbtBuffer{};

  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  // Acceleration Structure Components
  AccelerationStructures m_accel;
};

;
