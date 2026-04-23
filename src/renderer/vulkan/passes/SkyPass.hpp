#pragma once

#include "backend/vulkan/core/ContextManager.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"

class SkyPass : public IRenderPass {
public:
  explicit SkyPass(VulkanContextManager *core);
  void init() override;
  void setup(PassBuilder &builder) override;
  void deinit() override;
  void execute(const IRenderContext &ctx) override;

private:
  VulkanContextManager *m_core = nullptr;
  VkDevice m_device = VK_NULL_HANDLE;

  // Combined Vulkan Resources
  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkShaderEXT m_shader = VK_NULL_HANDLE;
};
