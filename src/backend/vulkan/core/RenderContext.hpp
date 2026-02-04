#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/gbuffers.hpp>

#include "backend/interfaces/IRenderContext.hpp"
#include "backend/interfaces/RHI_definitions.hpp"
#include "backend/vulkan/render/Acceleration.hpp"
#include "backend/vulkan/render/SceneAssetManager.hpp"

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
  void submitBarriers(const std::vector<BarrierInfo>& barriers) override;

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
  const VulkanSceneGpuData* deviceResources{};
  const AccelerationStructures* bvh{};

private:
  VkImage getResourceImage(RenderOutput resource) const;
};
