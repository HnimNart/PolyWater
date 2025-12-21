#pragma once

#include <cstdint>

namespace core
{

class RenderContext;
struct FrameContext;

//------------------------------------------------------------
// IAppElement
//------------------------------------------------------------
// Represents a pluggable application component ("layer", "system",
// or "module") that participates in the application's lifecycle.
//
// Implementations MUST be backend-agnostic at the interface level.
// Backend-specific logic should be accessed via RenderContext.
class IAppElement
{
public:
  virtual ~IAppElement() = default;

  //----------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------

  // Called once after Application::init()
  virtual void onInitialize() {}

  // Called once before Application::shutdown()
  virtual void onShutdown() {}

  //----------------------------------------------------------
  // Frame loop
  //----------------------------------------------------------

  // Called at the beginning of a frame, before rendering
  virtual void onBeginFrame(FrameContext const& /*frame*/) {}

  // Called during rendering
  virtual void onRender(RenderContext& /*ctx*/, FrameContext const& /*frame*/) {}

  // Called at the end of a frame, after rendering
  virtual void onEndFrame(FrameContext const& /*frame*/) {}

  //----------------------------------------------------------
  // Events (optional, extend as needed)
  //----------------------------------------------------------

  // Window resized
  virtual void onResize(uint32_t /*width*/, uint32_t /*height*/) {}

  // File drop event
  virtual void onFileDrop(char const* /*path*/) {}
};

}  // namespace core
