#pragma once

#include <cstdint>
#include <filesystem>

class RenderContext;
struct FrameContext;

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
  virtual void initialize() = 0;
  virtual void shutdown() = 0;

  //----------------------------------------------------------
  // Frame loop
  //----------------------------------------------------------

  // Begin a new frame. Returns false if frame should be skipped (e.g., minimized).
  virtual bool beginFrame(FrameContext& frame) = 0;

  // Access the per-frame render context.
  // The reference remains valid until endFrame() is called.
  virtual RenderContext& getRenderContext() = 0;

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
  virtual void resize(uint32_t width, uint32_t height) = 0;

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  virtual void requestScreenshot(const std::filesystem::path& filename, int quality = 100) = 0;
};
