#pragma once

#include <cstdint>

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

public:
  uint64_t frameNumber{
      0};  // Timeline value for synchronization (increases each frame)
};
