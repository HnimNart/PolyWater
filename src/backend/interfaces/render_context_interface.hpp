#pragma once

#include <cstdint>
#include <vector>

#include "rhi_definitions.hpp"
#include "shaders/shared/structs.h"

struct Scene;
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

  // Called by the RenderGraph before each pass to switch the active per-pass
  // command buffer.  Passing PassCmdSlot::Count ends the current pass without
  // opening a new one (used at the end of a frame to close the last buffer).
  // Backends that do not track per-pass command buffers (e.g. Metal) may
  // implement this as a no-op.
  virtual void activatePass(PassCmdSlot slot) = 0;

public:
  uint64_t frameNumber{0};  // Timeline value for synchronization
  Scene* sceneResources = nullptr;
  shaderio::PushConstant pushValues;
};
