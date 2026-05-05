#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

#include "backend/interfaces/render_context_interface.hpp"
#include "backend/interfaces/rhi_definitions.hpp"
#include "nvvk/gbuffers.hpp"

class VulkanSceneAssetManager;
class VulkanAccelerationStructures;

//------------------------------------------------------------
// VulkanRenderContext
//------------------------------------------------------------
// Concrete Vulkan implementation of IRenderContext for a single frame.
// One primary command buffer is allocated per PassCmdSlot every frame and
// reused across frames by resetting the per-frame command pool.
//
// The RenderGraph calls activatePass() before each pass, which ends the
// previously active command buffer and begins the one for the new slot.
// Passes simply record into cmdBuffer without managing begin/end themselves.
// At end-of-frame, all command buffers in finishedCmdBuffers are submitted
// together in a single vkQueueSubmit2 call.
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

  // Activates the command buffer for the given slot, ending the previously
  // active one first.  Passing PassCmdSlot::Count ends the current buffer
  // without opening a new one (used by RenderGraph at end-of-frame).
  // If the requested slot is already active this is a no-op.
  void activatePass(PassCmdSlot slot) override;

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
  // Per-pass command buffers
  // ------------------------------------------------------------------------
  // Number of pre-allocated command buffer slots per frame.
  static constexpr uint32_t kNumPassCmdSlots =
      static_cast<uint32_t>(PassCmdSlot::Count);

  // One pre-allocated command buffer per slot, all from cmdPool.
  // Pool reset at frame-start implicitly resets all of these.
  std::array<VkCommandBuffer, kNumPassCmdSlots> passCmdBuffers{};

  // The command buffer currently in the recording state.
  // beginFrame() sets this to passCmdBuffers[Main]; activatePass() updates it.
  VkCommandBuffer cmdBuffer{};

  // Slot that owns the currently recording cmdBuffer.
  // PassCmdSlot::Count means no slot is active (cmdBuffer may still be valid
  // if set directly, e.g. by OIDNDenoisePass after its intermediate submit).
  PassCmdSlot activeSlot{PassCmdSlot::Count};

  // Command buffers that have been ended this frame and are ready to submit.
  // Populated by activatePass() as slots are transitioned; cleared by
  // beginFrame() and by OIDNDenoisePass after its intermediate submit.
  std::vector<VkCommandBuffer> finishedCmdBuffers;

  // ------------------------------------------------------------------------
  // Other per-frame Vulkan objects
  // ------------------------------------------------------------------------
  VkCommandPool cmdPool{};
  VkDevice device{};

  const nvvk::GBuffer* gBuffers{};
  // --- Swapchain Integration ---
  // These are updated every frame by the VulkanSwapchainRenderManager
  VkImage swapchainImage{};      // For pipeline barriers (Layout transitions)
  VkImageView swapchainImageView{};  // For VkRenderingAttachmentInfo (Drawing)
  VkExtent2D screenSize{};           // For setting viewports and render areas

private:
  VkImage getResourceImage(RenderOutput resource) const;
};
