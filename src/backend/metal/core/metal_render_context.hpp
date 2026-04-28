#pragma once

#ifdef __APPLE__

#include <memory>
#include <vector>

#include "backend/interfaces/render_context_interface.hpp"

// Forward declaration of the Objective-C++ implementation struct.
// Defined in MetalRenderContext.mm.
struct MetalRenderContextData;

//------------------------------------------------------------
// MetalRenderContext
//------------------------------------------------------------
// Concrete Metal implementation of IRenderContext for a single frame.
// Holds opaque handles to the per-frame Metal command buffer, render
// pass descriptor, render command encoder, and drawable.
class MetalRenderContext final : public IRenderContext {
public:
  MetalRenderContext();
  ~MetalRenderContext() override;

  // Deleted copy/move
  MetalRenderContext(const MetalRenderContext &) = delete;
  MetalRenderContext &operator=(const MetalRenderContext &) = delete;
  MetalRenderContext(MetalRenderContext &&) = delete;
  MetalRenderContext &operator=(MetalRenderContext &&) = delete;

  // IRenderContext interface - Metal uses automatic hazard tracking,
  // so Vulkan-style explicit barriers are not needed here.
  void submitBarriers(const std::vector<BarrierInfo> &barriers) const override;

  // Static helpers to safely downcast from the base interface.
  static const MetalRenderContext &get(const IRenderContext &ctx)
  {
    return static_cast<const MetalRenderContext &>(ctx);
  }
  static MetalRenderContext &get(IRenderContext &ctx)
  {
    return static_cast<MetalRenderContext &>(ctx);
  }

  // Called by MetalBackend every frame to update the per-frame Metal objects.
  // Parameters are __bridge void* pointers to the corresponding Metal types.
  void setFrameData(void *commandBuffer, void *renderPassDescriptor,
                    void *renderCommandEncoder, void *drawable);

  // Accessors returning opaque handles (cast to Metal types in .mm files).
  void *getCommandBufferHandle() const;
  void *getRenderPassDescriptorHandle() const;
  void *getRenderCommandEncoderHandle() const;
  void *getDrawableHandle() const;

private:
  std::unique_ptr<MetalRenderContextData> m_data;
};

#endif // __APPLE__
