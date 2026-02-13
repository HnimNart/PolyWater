#pragma once

#include <shaders/shared/structs.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IRenderGraph.hpp"
#include "backend/vulkan/core/ContextManager.hpp"

/**
 * @brief Final pass of the frame.
 * Consumes the ToneMapped HDR/SDR result and renders the UI (ImGui)
 * directly onto the Swapchain image.
 */
class UIPass : public IRenderPass {
public:
  using RenderCallback = std::function<void(const IRenderContext &ctx)>;

  UIPass(RenderCallback callback);
  virtual ~UIPass() = default;

  void init(VulkanContextManager * /*core*/) override {};
  void deinit(VulkanContextManager * /*core*/) override {};

  void setup(PassBuilder &builder) override;
  void execute(const IRenderContext &ctx) override;

private:
  RenderCallback m_callback;
};
