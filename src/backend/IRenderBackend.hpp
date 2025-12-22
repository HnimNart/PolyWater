#pragma once

#include <filesystem>

#include "core/application/types.h"

//------------------------------------------------------------
// IRenderBackend
//------------------------------------------------------------
// Abstract interface for a render backend. Concrete implementations
// include Vulkan, D3D12, Metal, OpenGL, or headless backends.
//
// Responsibilities:
// - Manage frame lifecycle
// - Provide a per-frame RenderContext
// - Allow scene renderers to draw via the context
// - Support vsync, resizing, and screenshots
class IRenderBackend
{
public:
  virtual ~IRenderBackend() = default;

  //----------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------
  virtual void init() = 0;
  virtual void shutdown() = 0;

  //----------------------------------------------------------
  // Frame loop
  //----------------------------------------------------------

  // Begin a new frame. Returns false if frame should be skipped (e.g., minimized).
  virtual bool beginFrame(FrameContext& frame) = 0;

  // Render the frame
  virtual void renderFrame(FrameContext const& frame) = 0;

  // Complete the frame
  virtual void endFrame(FrameContext const& frame) = 0;

  // Present the completed frame (no-op for headless backends)
  virtual void present() = 0;

  //----------------------------------------------------------
  // Runtime control
  //----------------------------------------------------------
  virtual void setVsync(bool enabled) = 0;
  virtual bool isVsync() const = 0;

  //----------------------------------------------------------
  // Window / output surface control
  //----------------------------------------------------------
  virtual void resize(const WindowSize& size) = 0;
  virtual const WindowSize getViewportSize() const = 0;

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  virtual void requestScreenshot(const std::filesystem::path& filename, int quality = 100) = 0;
};
