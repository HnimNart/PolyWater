#pragma once

#include <vulkan/vulkan_core.h>

#include <nvvk/descriptors.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

class VulkanDenoisePass final : public IRenderPass
{
public:
  explicit VulkanDenoisePass(VulkanContextManager* contextManager);
  ~VulkanDenoisePass() override = default;

  void init() override;
  void deinit() override;

  // Declares dependencies for the RenderGraph
  void setup(PassBuilder& builder) override;

  // Executes the compute shader
  void execute(IRenderContext& ctx) override;

private:
  void createDescriptorLayout();
  void createComputePipeline();

  VulkanContextManager* m_context_manager{nullptr};

  VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
  VkPipeline m_pipeline{VK_NULL_HANDLE};

  VkDescriptorSetLayout m_descSetLayout{VK_NULL_HANDLE};

  nvvk::DescriptorPack m_descPack;
};
