#pragma once

#include <shaders/shared/structs.h>
#include <volk/volk.h>

#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "core/timers.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"
#include "renderer/interfaces/tone_mapper_interface.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

namespace vkb
{

class VulkanToneMapPass final : public IToneMapper, public IRenderPass
{
public:
  VulkanToneMapPass(VulkanContextManager* core, RenderOutput input);
  ~VulkanToneMapPass() override;

  void init() override;
  void deinit() override;

  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "ToneMap"; }
#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif

  // Explicitly non-copyable
  VulkanToneMapPass(const VulkanToneMapPass&) = delete;
  VulkanToneMapPass& operator=(const VulkanToneMapPass&) = delete;

  VkResult init(nvvk::ResourceAllocator* alloc,
                std::span<const uint32_t> spirv);

  void runCompute(VkCommandBuffer cmd, const VkExtent2D& size,
                  const shaderio::TonemapperData& tonemapper,
                  const VkDescriptorImageInfo& inImage,
                  const VkDescriptorImageInfo& outImage);

private:
  VulkanContextManager* m_core = nullptr;
  RenderOutput m_input;
  void runAutoExposureHistogram(VkCommandBuffer cmd, const VkExtent2D& size,
                                const VkDescriptorImageInfo& inImage);
  void runAutoExposure(VkCommandBuffer cmd);
  void clearHistogram(VkCommandBuffer cmd);

  nvvk::ResourceAllocator* m_alloc{};

  VkDevice m_device{};
  nvvk::DescriptorPack m_descriptorPack;
  VkPipelineLayout m_pipelineLayout{};
  VkPipeline m_tonemapPipeline{};
  VkPipeline m_histogramPipeline{};
  VkPipeline m_exposurePipeline{};

  core::PerformanceTimer m_timer;  // Timer for performance measurement

  // Auto-Exposure
  nvvk::Buffer m_exposureBuffer;
  nvvk::Buffer m_histogramBuffer;
  bool m_initialized = false;
#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
}  // namespace vkb
