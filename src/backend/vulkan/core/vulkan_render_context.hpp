#pragma once

#include <vector>

#include "backend/interfaces/render_context_interface.hpp"
#include "backend/interfaces/rhi_definitions.hpp"
#include "nvvk/gbuffers.hpp"

namespace vkb
{

class VulkanSceneAssetManager;
class VulkanAccelerationStructures;

//------------------------------------------------------------
// VulkanRenderContext
//------------------------------------------------------------
// Concrete Vulkan implementation of IRenderContext for a single frame.
//
// One primary command buffer is allocated per pass in the RenderGraph every
// frame and reused across frames by resetting the per-frame command pool.
// The number of command buffers is determined by RenderGraph::numCmdBuffers()
// and communicated to the backend via VulkanFrameSynchronizationManager::
// resizeCmdBuffers(), which must be called whenever the graph is (re)compiled.
//
// The RenderGraph calls activatePass(i) before each pass, which ends the
// previously active command buffer and begins the one at index i.
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

  // Activates the command buffer at the given index (assigned by RenderGraph
  // during compile()), ending the previously active one first.
  // Passing kEndPassIndex ends the current buffer without opening a new one
  // (used by RenderGraph at end-of-frame and by OIDNDenoisePass).
  // If the requested index is already active this is a no-op.
  void activatePass(uint32_t cmdBufferIndex) override;

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
  // One pre-allocated command buffer per pass, all from cmdPool.
  // Sized by VulkanFrameSynchronizationManager::resizeCmdBuffers() after each
  // graph (re)compile. Pool reset at frame-start implicitly resets all of them.
  std::vector<VkCommandBuffer> passCmdBuffers;

  // The command buffer currently in the recording state.
  // beginFrame() sets this to passCmdBuffers[0]; activatePass() updates it.
  VkCommandBuffer cmdBuffer{};

  // Index of the pass that owns the currently recording cmdBuffer.
  // kEndPassIndex means no pass slot is active (cmdBuffer may still be valid
  // if set directly by OIDNDenoisePass after its intermediate submit).
  uint32_t activeIndex{kEndPassIndex};

  // Command buffers that have been ended this frame and are ready to submit.
  // Populated by activatePass() as passes are transitioned; cleared by
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
  VkImage swapchainImage{};  // For pipeline barriers (Layout transitions)
  VkImageView swapchainImageView{};  // For VkRenderingAttachmentInfo (Drawing)
  VkExtent2D screenSize{};           // For setting viewports and render areas

private:
  VkImage getResourceImage(RenderOutput resource) const;
};
}  // namespace vkb
