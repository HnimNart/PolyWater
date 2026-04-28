#pragma once

#include <vulkan/vulkan.h>

#include "nvvk/descriptors.hpp"
#include "nvvk/sbt_generator.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"
#include "renderer/vulkan/Acceleration.hpp"

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

class RayTracePass final : public IRenderPass
{
public:
  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  RayTracePass(VulkanContextManager* contextManager,
               const nvvk::DescriptorPack& descPack,
               ShaderManager* materialManager, AccelerationStructures* accel);
  ~RayTracePass() = default;

  void init() override;
  void setup(PassBuilder& builder) override;
  void deinit() override;

  // -------------------------------------------------------------------------
  // Setup & Configuration
  // -------------------------------------------------------------------------
  // Pipeline Creation Methods
  void createPipeline();

  // Specific internal pipeline creators (exposed public as per original)
  void createRayTracingPipeline();
  void createDescriptorLayout();
  void createPipelineSBT();

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
  VulkanContextManager* m_context_manager = nullptr;
  const nvvk::DescriptorPack& m_sharedDescPack;

  // Pipeline State
  nvvk::DescriptorPack m_RayTraceDescPack{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_pipelineLayout{};
  std::vector<VkShaderModuleCreateInfo> m_shaderCode;

  // Shader Binding Table (SBT)
  nvvk::SBTGenerator m_sbtGenerator{};
  nvvk::Buffer m_sbtBuffer{};

  // Properties
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};

  ShaderManager* m_shaderManager = nullptr;
  AccelerationStructures* m_accel{};
};
