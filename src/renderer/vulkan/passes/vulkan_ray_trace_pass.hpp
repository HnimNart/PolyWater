#pragma once

#include <vulkan/vulkan.h>

#include "nvvk/descriptors.hpp"
#include "nvvk/sbt_generator.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"
#include "renderer/vulkan/vulkan_acceleration_structures.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

// Forward declarations of non-vkb types
namespace scene
{
class SceneResourcesManager;
}  // namespace scene

namespace nvvk
{
class GBuffer;
}
namespace shaderio
{
struct PushConstant;
}

namespace vkb
{

class VulkanRayTracePass final : public IRenderPass
{
public:
  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  VulkanRayTracePass(VulkanContextManager* contextManager,
               const nvvk::DescriptorPack& descPack,
               ShaderManager* materialManager, VulkanAccelerationStructures* accel);
  ~VulkanRayTracePass() = default;

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
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "RayTrace"; }
#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif

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
  VulkanAccelerationStructures* m_accel{};
#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
}  // namespace vkb
