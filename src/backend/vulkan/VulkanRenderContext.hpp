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

  VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
  uint32_t frameIndex{0};                       // Frame index in the swapchain / ring buffer
  VkSemaphore waitSemaphore{VK_NULL_HANDLE};    // Optional extra per-frame wait semaphore
  VkSemaphore signalSemaphore{VK_NULL_HANDLE};  // Optional extra per-frame signal semaphore

  // Optionally store a pointer back to the backend for resource access
  void* backendUserData{nullptr};

  // Other per-frame Vulkan objects can be added as needed:
  // - framebuffers
  // - render passes
  // - descriptor sets
  // - etc.
};
