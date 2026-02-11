#pragma once
#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "shaders/shared/sky_io.h.slang"

class SkyPass : public IRenderPass {
public:
  void init(VulkanContextManager *core) override;
  void setup(PassBuilder &builder) override;
  void deinit(VulkanContextManager *core) override;
  void execute(const IRenderContext &ctx) override;

private:
  VulkanContextManager *m_core = nullptr;
  VkDevice m_device = VK_NULL_HANDLE;

  // Combined Vulkan Resources
  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkShaderEXT m_shader = VK_NULL_HANDLE;
};
