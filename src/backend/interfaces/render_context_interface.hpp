#pragma once

#include <cstdint>
#include <vector>

#include "rhi_definitions.hpp"
#include "shaders/shared/structs.h"

namespace scene
{
struct Scene;
}

//------------------------------------------------------------
// FrameContext
//------------------------------------------------------------
// Opaque base class representing the backend-specific rendering
// context for a single frame.
//
// Concrete backends (Vulkan, D3D12, Metal, etc.) must derive from
// this and expose API-specific data through the derived type.
class IRenderContext
{
public:
  virtual ~IRenderContext() = default;
  IRenderContext() = default;

  IRenderContext(IRenderContext const&) = delete;
  IRenderContext& operator=(IRenderContext const&) = delete;
  IRenderContext(IRenderContext&&) = delete;
  IRenderContext& operator=(IRenderContext&&) = delete;

  virtual void
  submitBarriers(const std::vector<BarrierInfo>& barriers) const = 0;

  // Called by the RenderGraph before each pass to switch to that pass's
  // dedicated command buffer (identified by the index assigned during
  // RenderGraph::compile()).  Passing kEndPassIndex ends the currently active
  // command buffer without opening a new one (used at end-of-frame and for
  // intermediate CPU-sync points such as the OIDN denoiser).
  // Backends without explicit command buffer management (e.g. Metal) implement
  // this as a no-op.
  virtual void activatePass(uint32_t cmdBufferIndex) = 0;

public:
  uint64_t frameNumber{0};  // Timeline value for synchronization
  scene::Scene* sceneResources = nullptr;
  shaderio::PushConstant pushValues;
};
