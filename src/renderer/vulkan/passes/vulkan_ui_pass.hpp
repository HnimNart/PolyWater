#pragma once

#include <shaders/shared/structs.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "renderer/interfaces/render_graph_interface.hpp"

/**
 * @brief Final pass of the frame.
 * Consumes the ToneMapped HDR/SDR result and renders the UI (ImGui)
 * directly onto the Swapchain image.
 */
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

private:
  RenderCallback m_callback;
};
