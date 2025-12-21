#pragma once

//------------------------------------------------------------
// RenderContext
//------------------------------------------------------------
// Opaque base class representing the backend-specific rendering
// context for a single frame.
//
// Concrete backends (Vulkan, D3D12, Metal, etc.) must derive from
// this and expose API-specific data through the derived type.
class RenderContext
{
public:
  virtual ~RenderContext() = default;

protected:
  RenderContext() = default;

  RenderContext(RenderContext const&) = delete;
  RenderContext& operator=(RenderContext const&) = delete;
  RenderContext(RenderContext&&) = delete;
  RenderContext& operator=(RenderContext&&) = delete;
};
