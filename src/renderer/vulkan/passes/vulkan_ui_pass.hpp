#pragma once

#include <shaders/shared/structs.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

#ifdef PROFILE_APP
#include "nvvk/profiler_vk.hpp"
#endif

/**
 * @brief Final pass of the frame.
 * Consumes the ToneMapped HDR/SDR result and renders the UI (ImGui)
 * directly onto the Swapchain image.
 */
namespace vkb
{

class VulkanUIPass final : public IRenderPass
{
public:
  using RenderCallback = std::function<void(const IRenderContext& ctx)>;

  explicit VulkanUIPass(RenderCallback callback);
  ~VulkanUIPass() = default;

  void init() override
  {
  }
  void deinit() override
  {
  }

  void setup(PassBuilder& builder) override;
  void execute(IRenderContext& ctx) override;
  std::string_view name() const override { return "UI"; }
#ifdef PROFILE_APP
  void setGpuTimer(nvvk::ProfilerGpuTimer* t) { m_gpuTimer = t; }
#endif

private:
  RenderCallback m_callback;
#ifdef PROFILE_APP
  nvvk::ProfilerGpuTimer* m_gpuTimer = nullptr;
#endif
};
}  // namespace vkb
