#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/descriptors.hpp>
#include <nvvk/sbt_generator.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/vulkan/render/Acceleration.hpp"

// Forward Declarations
class SceneResourcesManager;
class VulkanContextManager;

namespace nvvk
{
class GBuffer;
}
namespace shaderio
{
struct PushConstant;
}

class RayTracePass : public IRenderPass
{
public:
  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  RayTracePass(nvvk::DescriptorPack* descPack);
  ~RayTracePass() = default;

  void init(VulkanContextManager* coreManager,
            const SceneResourcesManager& scene) override;
  void deinit(VulkanContextManager* coreManager) override;

  // -------------------------------------------------------------------------
  // Setup & Configuration
  // -------------------------------------------------------------------------
  void createScene(const SceneResourcesManager& scene);

  // Pipeline Creation Methods
  void createRaytraceDescriptorLayout();
  void createPipeline(const SceneResourcesManager& scene);

  // Specific internal pipeline creators (exposed public as per original)
  void createRayTracingPipeline(const SceneResourcesManager& scene);
  void createRayTracingPipeline();

  // -------------------------------------------------------------------------
  // Execution
  // -------------------------------------------------------------------------
  void execute(const IRenderContext& ctx) override;

private:
  // -------------------------------------------------------------------------
  // Internal Helpers
  // -------------------------------------------------------------------------
  void createShaderBindingTable(
      const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

  // -------------------------------------------------------------------------
  // Member Variables
  // -------------------------------------------------------------------------
  VulkanContextManager* m_core_manager = nullptr;
  nvvk::DescriptorPack* m_sharedDescPack =
      nullptr;  // Pointer to external Scene descriptor

  // Pipeline State
  nvvk::DescriptorPack m_RayTraceDescPack{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_pipelineLayout{};

  // Shader Binding Table (SBT)
  nvvk::SBTGenerator m_sbtGenerator{};
  nvvk::Buffer m_sbtBuffer{};

  // Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
};
