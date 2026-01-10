#pragma once

#include <vulkan/vulkan.h>

#include "backend/FrameContext.hpp"

//------------------------------------------------------------
// VulkanRenderContext
//------------------------------------------------------------
// Concrete Vulkan implementation of RenderContext for a single frame.
// Holds the Vulkan command buffer and other per-frame objects.
class VulkanFrameContext final : public FrameContext
{
public:
  VulkanFrameContext() = default;
  ~VulkanFrameContext() override = default;
  // Deleted copy/move
  VulkanFrameContext(const VulkanFrameContext&) = delete;
  VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;
  VulkanFrameContext(VulkanFrameContext&&) = delete;
  VulkanFrameContext& operator=(VulkanFrameContext&&) = delete;

  // ------------------------------------------------------------------------
  // Vulkan-specific per-frame members
  // ------------------------------------------------------------------------

  VkCommandPool cmdPool{};      // Command pool for recording commands for this frame
  VkCommandBuffer cmdBuffer{};  // Command buffer containing the frame's rendering commands

  //
  VkSemaphore waitSemaphore{VK_NULL_HANDLE};    // Optional extra per-frame wait semaphore
  VkSemaphore signalSemaphore{VK_NULL_HANDLE};  // Optional extra per-frame signal semaphore

  // Optionally store a pointer back to the backend for resource access
  void* backendUserData{nullptr};
};
