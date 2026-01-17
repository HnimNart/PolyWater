#pragma once

#include <vulkan/vulkan.h>

#include "backend/FrameContext.hpp"

//------------------------------------------------------------
// VulkanFrameContext
//------------------------------------------------------------
// Concrete Vulkan implementation of RenderContext for a single frame.
// Holds the Vulkan command buffer and other per-frame objects.
class VulkanRenderContext final : public FrameContext
{
public:
  VulkanRenderContext() = default;
  ~VulkanRenderContext() override = default;
  // Deleted copy/move
  VulkanRenderContext(const VulkanRenderContext&) = delete;
  VulkanRenderContext& operator=(const VulkanRenderContext&) = delete;
  VulkanRenderContext(VulkanRenderContext&&) = delete;
  VulkanRenderContext& operator=(VulkanRenderContext&&) = delete;

  // ------------------------------------------------------------------------
  // Vulkan-specific per-frame members
  // ------------------------------------------------------------------------
  VkCommandBuffer cmdBuffer{};
  VkCommandPool cmdPool{};
};
