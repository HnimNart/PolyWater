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
class SceneResourcesManager;

namespace core
{
class VulkanBackend;
}

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
  VulkanRayTracer(core::VulkanBackend* backend, VulkanRaster* raster);
  ~VulkanRayTracer();

  void createPipeline(const SceneResourcesManager& scene);

  // Rendering
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers,
              const shaderio::PushConstant& pushValues) const;

  // Internal helpers (made public as per original class structure, or keep public if intended)
  void createRayTracingPipeline(const SceneResourcesManager& scene);
  void createRaytraceDescriptorLayout();
  void createRayTracingPipeline();

private:
  void deinit();
  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

  core::VulkanBackend* m_backend = nullptr;
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
