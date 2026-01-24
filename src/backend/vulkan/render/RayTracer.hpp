#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/descriptors.hpp>
#include <nvvk/sbt_generator.hpp>

#include "Acceleration.hpp"

// Forward Declarations
class SceneResourcesManager;
class VulkanCoreManager;

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
  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  explicit VulkanRayTracer(VulkanCoreManager* coreManager);
  ~VulkanRayTracer();

  void init(const SceneResourcesManager& scene);

  // -------------------------------------------------------------------------
  // Setup & Configuration
  // -------------------------------------------------------------------------
  void setDescriptorPack(nvvk::DescriptorPack* descPack);

  // Pipeline Creation Methods
  void createRaytraceDescriptorLayout();
  void createPipeline(const SceneResourcesManager& scene);

  // Specific internal pipeline creators (exposed public as per original)
  void createRayTracingPipeline(const SceneResourcesManager& scene);
  void createRayTracingPipeline();

  // -------------------------------------------------------------------------
  // Execution
  // -------------------------------------------------------------------------
  void render(VkCommandBuffer cmd, const nvvk::GBuffer& gBuffers,
              const shaderio::PushConstant& pushValues) const;

private:
  // -------------------------------------------------------------------------
  // Internal Helpers
  // -------------------------------------------------------------------------
  void deinit();
  void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

  // -------------------------------------------------------------------------
  // Member Variables
  // -------------------------------------------------------------------------
  VulkanCoreManager* m_core_manager = nullptr;
  nvvk::DescriptorPack* m_sharedDescPack = nullptr;  // Pointer to external Scene descriptor

  // Pipeline State
  nvvk::DescriptorPack m_RayTraceDescPack{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_pipelineLayout{};

  // Shader Binding Table (SBT)
  nvvk::SBTGenerator m_sbtGenerator{};
  nvvk::Buffer m_sbtBuffer{};

  // Acceleration Structure
  AccelerationStructures m_accel;

  // Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
};
