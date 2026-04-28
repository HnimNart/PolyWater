#pragma once

#include <vulkan/vulkan.h>

#include "backend/interfaces/IRenderContext.hpp"
#include "backend/interfaces/RHI_definitions.hpp"
#include "nvvk/gbuffers.hpp"

class VulkanSceneAssetManager;
class AccelerationStructures;

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

  const nvvk::GBuffer* gBuffers{};
  // --- Swapchain Integration ---
  // These are updated every frame by the SwapchainRenderManager
  VkImage swapchainImage{};  // For pipeline barriers (Layout transitions)
  VkImageView swapchainImageView{};  // For VkRenderingAttachmentInfo (Drawing)
  VkExtent2D screenSize{};           // For setting viewports and render areas

private:
  VkImage getResourceImage(RenderOutput resource) const;
};
