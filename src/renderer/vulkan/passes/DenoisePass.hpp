#pragma once

#include <nvvk/descriptors.hpp>
#include <vulkan/vulkan_core.h>

#include "backend/vulkan/core/ContextManager.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"

class DenoisePass : public IRenderPass {
public:
  DenoisePass(VulkanContextManager *contextManager);
  ~DenoisePass() override = default;

  void init() override;
  void deinit() override;

  // Declares dependencies for the RenderGraph
  void setup(PassBuilder &builder) override;

  // Executes the compute shader
  void execute(const IRenderContext &ctx) override;

private:
  void createDescriptorLayout();
  void createComputePipeline();

  VulkanContextManager *m_context_manager{nullptr};

  VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
  VkPipeline m_pipeline{VK_NULL_HANDLE};

  VkDescriptorSetLayout m_descSetLayout{VK_NULL_HANDLE};

  nvvk::DescriptorPack m_descPack;
};
