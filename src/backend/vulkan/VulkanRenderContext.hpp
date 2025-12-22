#pragma once

#include <vulkan/vulkan.h>

#include "backend/RenderContext.hpp"

//------------------------------------------------------------
// VulkanRenderContext
//------------------------------------------------------------
// Concrete Vulkan implementation of RenderContext for a single frame.
// Holds the Vulkan command buffer and other per-frame objects.
class VulkanRenderContext final : public RenderContext
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

  VkCommandPool cmdPool{};      // Command pool for recording commands for this frame
  VkCommandBuffer cmdBuffer{};  // Command buffer containing the frame's rendering commands
  uint64_t frameNumber{0};      // Timeline value for synchronization (increases each frame)

  //
  VkSemaphore waitSemaphore{VK_NULL_HANDLE};    // Optional extra per-frame wait semaphore
  VkSemaphore signalSemaphore{VK_NULL_HANDLE};  // Optional extra per-frame signal semaphore

  // Optionally store a pointer back to the backend for resource access
  void* backendUserData{nullptr};
};
