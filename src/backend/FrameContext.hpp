#pragma once

#include <cstdint>

//------------------------------------------------------------
// RenderContext
//------------------------------------------------------------
// Opaque base class representing the backend-specific rendering
// context for a single frame.
//
// Concrete backends (Vulkan, D3D12, Metal, etc.) must derive from
// this and expose API-specific data through the derived type.
class FrameContext
{
public:
  virtual ~FrameContext() = default;
  FrameContext() = default;

  FrameContext(FrameContext const&) = delete;
  FrameContext& operator=(FrameContext const&) = delete;
  FrameContext(FrameContext&&) = delete;
  FrameContext& operator=(FrameContext&&) = delete;

public:
  uint64_t frameNumber{0};  // Timeline value for synchronization (increases each frame)
  bool vSyncWanted = 0;     //
};
