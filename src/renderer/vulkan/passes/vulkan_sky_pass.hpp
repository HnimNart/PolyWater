#pragma once

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

namespace vkb
{

class VulkanSkyPass final : public IRenderPass
{
public:
  explicit VulkanSkyPass(VulkanContextManager* context);
  void init() override;
  void setup(PassBuilder& builder) override;
  void deinit() override;
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "Sky"; }
#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif

private:
  VulkanContextManager* m_core = nullptr;
  VkDevice m_device = VK_NULL_HANDLE;

  // Combined Vulkan Resources
  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkShaderEXT m_shader = VK_NULL_HANDLE;
#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
}  // namespace vkb
