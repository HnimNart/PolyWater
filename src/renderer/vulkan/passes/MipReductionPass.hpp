#pragma once

#include <vulkan/vulkan_core.h>

#include <nvvk/descriptors.hpp>
#include <nvvk/resources.hpp>

#include "backend/vulkan/core/ContextManager.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"

class MipReductionPass : public IRenderPass
{
public:
  MipReductionPass(VulkanContextManager* contextManager, nvvk::Image* texture);
  void init() override;
  void deinit() override;
  void setup(PassBuilder& builder) override;
  void execute(const IRenderContext& ctx) override;

private:
  // --- Internal Logic ---
  void createDescriptorLayout();
  void createPipelineLayout();
  void compileShaders();
  void updateMipViews();

  // Helpers to keep execute() readable
  void dispatchReduction(VkCommandBuffer cmd, VkImageView inView,
                         VkImageView outView, VkSampler sampler, uint32_t width,
                         uint32_t height);

  void transitionImage(VkCommandBuffer cmd, VkImage image,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       uint32_t baseMip, uint32_t mipCount);

  VulkanContextManager* m_contextManager = nullptr;
  nvvk::Image* m_mipTexture = nullptr;

  // --- Vulkan Objects ---
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkShaderEXT m_computeShader = VK_NULL_HANDLE;
  nvvk::DescriptorPack m_mipDescPack;

  // --- Cached Resources ---
  struct TextureCache
  {
    VkImage image = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
  } m_mipCache;

  std::vector<VkImageView> m_mipViews;

  struct ReductionPushConstants
  {
    int isFirstPass;
    int reductionOp;  // 0: Max, 1: Min, 2: Avg
  };
};
