#pragma once

#ifdef __APPLE__

#include <memory>

#include "IRenderable.hpp"
#include "MetalContextManager.hpp"
#include "MetalRenderContext.hpp"
#include "backend/interfaces/IRenderBackend.hpp"
#include "app/IGUISystem.hpp"

class ImGuiMetalSystem;

// Forward declaration of the Objective-C++ implementation struct.
// Defined in MetalBackend.mm.
struct MetalBackendData;

//------------------------------------------------------------
// MetalBackend
//------------------------------------------------------------
// Concrete Metal implementation of IRenderBackend.
// Manages the CAMetalLayer, per-frame command buffer lifecycle,
// and delegates GUI rendering to ImGuiMetalSystem.
class MetalBackend : public IRenderBackend {
public:
  static std::unique_ptr<MetalBackend>
  create(const app::ApplicationCreateInfo &appInfo);

  void initPresentation(GLFWwindow *window, app::IGUISystemPtr gui) override;
  void initProfiler(core::ProfilerTimeline *timeline) override;
  void deinit() override;

  // Frame lifecycle
  IRenderContext &getCurrentContext() override;
  IRenderContext *beginFrame() override;
  void renderFrame(const std::vector<app::IAppElementPtr> &elements,
                   IRenderContext const &ctx) override;
  void endFrame(IRenderContext const &ctx) override;
  void present() override;
  void advance() override;

  void waitForDeviceIdle() override;
  void setVsync(bool enabled) override;

  // Accessors
  MetalContextManager *getContextManager() const;
  RenderRegistry &getRegistry();

private:
  MetalBackend();
  ~MetalBackend();
  bool initMetal(const app::ApplicationCreateInfo &appInfo);

  std::unique_ptr<MetalContextManager> m_contextManager;
  std::unique_ptr<MetalRenderContext>  m_renderContext;
  RenderRegistry                       m_renderRegistry;
  std::unique_ptr<MetalBackendData>    m_data;
};

#endif // __APPLE__
