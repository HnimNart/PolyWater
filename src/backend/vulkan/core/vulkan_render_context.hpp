#pragma once

#include <vulkan/vulkan.h>

#include "backend/interfaces/render_context_interface.hpp"
#include "backend/interfaces/rhi_definitions.hpp"
#include "nvvk/gbuffers.hpp"

class VulkanSceneAssetManager;
class VulkanAccelerationStructures;

//------------------------------------------------------------
// VulkanFrameContext
//------------------------------------------------------------
// Concrete Vulkan implementation of RenderContext for a single frame.
// Holds the Vulkan command buffer and other per-frame objects.
class VulkanRenderContext final : public IRenderContext
{
public:
  VulkanRenderContext() = default;
  ~VulkanRenderContext() override = default;
  // Deleted copy/move
  VulkanRenderContext(const VulkanRenderContext&) = delete;
  VulkanRenderContext& operator=(const VulkanRenderContext&) = delete;
  VulkanRenderContext(VulkanRenderContext&&) = delete;
  VulkanRenderContext& operator=(VulkanRenderContext&&) = delete;

  // API funcs
  void submitBarriers(const std::vector<BarrierInfo>& barriers) const override;

  // Static helper to cast from the interface
  static const VulkanRenderContext& get(const IRenderContext& ctx)
  {
    return static_cast<const VulkanRenderContext&>(ctx);
  }

  static VulkanRenderContext& get(IRenderContext& ctx)
  {
    return static_cast<VulkanRenderContext&>(ctx);
  }

  // ------------------------------------------------------------------------
  // Vulkan-specific per-frame members
  // ------------------------------------------------------------------------
  VkCommandBuffer cmdBuffer{};
  VkCommandPool cmdPool{};
  VkDevice device{};

  // Index into the frame-ring array (0..numFrames-1). Stable per slot —
  // used by OIDNDenoisePass to select the correct per-slot OIDN buffers.
  uint32_t frameRingIndex{0};

  // Set by OIDNDenoisePass::execute() when the OIDN pass is active.
  // VulkanBackend::endFrame() adds this semaphore as a GPU-side wait condition
  // on the CB3 submission so OIDN can run concurrently without blocking the CPU.
  VkSemaphore oidnSemaphore{VK_NULL_HANDLE};
  uint64_t oidnWaitValue{0};

  const nvvk::GBuffer* gBuffers{};
  // --- Swapchain Integration ---
  // These are updated every frame by the VulkanSwapchainRenderManager
  VkImage swapchainImage{};  // For pipeline barriers (Layout transitions)
  VkImageView swapchainImageView{};  // For VkRenderingAttachmentInfo (Drawing)
  VkExtent2D screenSize{};           // For setting viewports and render areas

private:
  VkImage getResourceImage(RenderOutput resource) const;
};
