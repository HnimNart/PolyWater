#pragma once

#ifdef __APPLE__

#include <memory>

#include "renderable_interface.hpp"
#include "metal_context_manager.hpp"
#include "metal_render_context.hpp"
#include "app/gui_system_interface.hpp"
#include "backend/interfaces/render_backend_interface.hpp"

namespace mtl
{

class MetalImGuiSystem;

// Forward declaration of the Objective-C++ implementation struct.
// Defined in MetalBackend.mm.
struct MetalBackendData;

//------------------------------------------------------------
// MetalBackend
//------------------------------------------------------------
// Concrete Metal implementation of IRenderBackend.
// Manages the CAMetalLayer, per-frame command buffer lifecycle,
// and delegates GUI rendering to MetalImGuiSystem.
class MetalBackend final : public IRenderBackend {
public:
  static std::unique_ptr<MetalBackend>
  create(const app::ApplicationCreateInfo &appInfo);

  ~MetalBackend();

  void initPresentation(GLFWwindow *window, app::IGUISystemPtr gui) override;
  void deinit() override;

  // Frame lifecycle
  IRenderContext &getCurrentContext() override;

  void waitForDeviceIdle() override;
  void setVsync(bool enabled) override;

  // Accessors
  MetalContextManager *getContextManager() const;
  RenderRegistry &getRegistry();

private:
  MetalBackend();
  bool initMetal(const app::ApplicationCreateInfo &appInfo);

  // IRenderBackend private virtual hooks
  void doInitProfiler(core::ProfilerTimeline *timeline) override;
  IRenderContext *doBeginFrame() override;
  void doRenderFrame(const std::vector<app::IAppElementPtr> &elements,
                     IRenderContext const &ctx) override;
  void doEndFrame(IRenderContext const &ctx) override;
  void doPresent() override;
  void doAdvance() override;

  std::unique_ptr<MetalContextManager> m_contextManager;
  std::unique_ptr<MetalRenderContext> m_renderContext;
  RenderRegistry m_renderRegistry;
  std::unique_ptr<MetalBackendData> m_data;
};

}  // namespace mtl

#endif // __APPLE__
