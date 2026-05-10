#pragma once

#include <GLFW/glfw3.h>

#include <functional>
#include <string>
#include <typeinfo>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>

#include <cstdlib>
#endif

#include "app/app_element_interface.hpp"
#include "app/gui_system_interface.hpp"
#include "core/profiler.hpp"
#include "core/types.hpp"
#include "render_context_interface.hpp"

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
//
// Uses the Non-Virtual Interface (NVI) pattern for the frame-loop methods
// (beginFrame, renderFrame, endFrame, present, advance) and initProfiler:
// callers invoke the public non-virtual wrappers, which insert profiler frame
// sections when PROFILE_APP is defined and then forward to the private
// virtual hooks. Subclasses override the private do* virtuals.
class IRenderBackend
{
public:
  virtual ~IRenderBackend() = default;

  /** @brief Returns a human-readable name used in profiler section labels.
   *  Defaults to the demangled class name (e.g. "VulkanBackend").
   *  Override to provide a custom label. */
  virtual std::string getName() const
  {
    const char* mangled = typeid(*this).name();
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (status == 0 && demangled)
    {
      std::string result(demangled);
      std::free(demangled);
      return result;
    }
#elif defined(_MSC_VER)
    std::string result(mangled);
    for (const char* prefix : {"class ", "struct "})
    {
      if (result.rfind(prefix, 0) == 0)
      {
        result.erase(0, std::strlen(prefix));
        break;
      }
    }
    return result;
#endif
    return mangled;
  }

  //----------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------
  virtual void initPresentation(GLFWwindow* window, app::IGUISystemPtr gui) = 0;

  /** @brief NVI wrapper: stores the timeline, caches section names, then
   *  calls doInitProfiler() for backend-specific GPU timer setup. */
  void initProfiler(core::ProfilerTimeline* timeline)
  {
#ifdef PROFILE_APP
    m_profileTimeline = timeline;
    if (timeline)
    {
      const std::string base = getName();
      m_sectionNames.beginFrame = base + "::beginFrame";
      m_sectionNames.renderFrame = base + "::renderFrame";
      m_sectionNames.endFrame = base + "::endFrame";
      m_sectionNames.present = base + "::present";
      m_sectionNames.advance = base + "::advance";
    }
#endif
    doInitProfiler(timeline);
  }

  virtual void deinit() = 0;
  virtual void waitForDeviceIdle() = 0;

  //----------------------------------------------------------
  // Frame loop — NVI wrappers
  //----------------------------------------------------------
  virtual IRenderContext& getCurrentContext() = 0;

  /** @brief Begin a new frame. Returns nullptr if the frame should be
   *  skipped (e.g. window minimized). */
  IRenderContext* beginFrame()
  {
#ifdef PROFILE_APP
    if (m_profileTimeline)
    {
      const auto section =
          m_profileTimeline->frameSection(m_sectionNames.beginFrame);
      return doBeginFrame();
    }
#endif
    return doBeginFrame();
  }

  /** @brief Render the frame by dispatching to all registered elements. */
  void renderFrame(const std::vector<app::IAppElementPtr>& elements,
                   IRenderContext const& ctx)
  {
#ifdef PROFILE_APP
    if (m_profileTimeline)
    {
      const auto section =
          m_profileTimeline->frameSection(m_sectionNames.renderFrame);
      doRenderFrame(elements, ctx);
      return;
    }
#endif
    doRenderFrame(elements, ctx);
  }

  /** @brief Complete the frame (submit GPU work). */
  void endFrame(IRenderContext const& ctx)
  {
#ifdef PROFILE_APP
    if (m_profileTimeline)
    {
      const auto section =
          m_profileTimeline->frameSection(m_sectionNames.endFrame);
      doEndFrame(ctx);
      return;
    }
#endif
    doEndFrame(ctx);
  }

  /** @brief Present the completed frame (do not call for headless backends). */
  void present()
  {
#ifdef PROFILE_APP
    if (m_profileTimeline)
    {
      const auto section =
          m_profileTimeline->frameSection(m_sectionNames.present);
      doPresent();
      return;
    }
#endif
    doPresent();
  }

  /** @brief Advance to the next frame slot. */
  void advance()
  {
#ifdef PROFILE_APP
    if (m_profileTimeline)
    {
      const auto section =
          m_profileTimeline->frameSection(m_sectionNames.advance);
      doAdvance();
      return;
    }
#endif
    doAdvance();
  }

  //----------------------------------------------------------
  // Runtime control
  //----------------------------------------------------------
  virtual void setVsync(bool enabled) { m_vsyncWanted = enabled; };
  virtual bool isVsync() const { return m_vsync; };

  //----------------------------------------------------------
  // Window / output surface control
  //----------------------------------------------------------
  void onResize(const WindowSize& size)
  {
    // Notify the GUI system so it can rescale fonts / layout as needed.
    float xscale, yscale;
    glfwGetWindowContentScale(m_windowHandle, &xscale, &yscale);
    if (m_gui)
      m_gui->onDpiScaleChanged(xscale / m_dpiScale);
    m_dpiScale = xscale;
    m_viewportSize = size;
  }

  const WindowSize& getViewportSize() const { return m_viewportSize; }
  virtual void setWindow(GLFWwindow* windowHandle)
  {
    m_windowHandle = windowHandle;
  }
  void setWindowSize(const WindowSize& windowSize)
  {
    m_windowSize = windowSize;
  };
  const WindowSize& getWindowSize() const { return m_windowSize; }

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  virtual void freeResourcesQueue() {};

protected:
  //  Window stuff
  GLFWwindow* m_windowHandle{nullptr};  // GLFW Window
  WindowSize m_viewportSize{0, 0};      // Size of the viewport
  WindowSize m_windowSize{0, 0};        // Size of window
  float m_dpiScale = 1.0f;

  // Vsync
  bool m_vsyncWanted{true};  // Wanting swapchain with vsync
  bool m_vsync{true};

  std::vector<std::vector<std::function<void()>>>
      m_resourceFreeQueue;  // Queue of functions to free resources

  app::IGUISystemPtr m_gui = nullptr;

#ifdef PROFILE_APP
  core::ProfilerTimeline* m_profileTimeline = nullptr;

  // Section name strings, pre-built once in initProfiler()
  // to avoid per-frame heap allocations.
  struct SectionNames
  {
    std::string beginFrame;
    std::string renderFrame;
    std::string endFrame;
    std::string present;
    std::string advance;
  } m_sectionNames;
#endif

private:
  // -------------------------------------------------------------------------
  // Private virtual hooks — override these in derived classes.
  // -------------------------------------------------------------------------

  /** @brief Backend-specific profiler/GPU timer initialization. */
  virtual void doInitProfiler(core::ProfilerTimeline* /*timeline*/) {}

  /** @brief Begin a new frame; returns nullptr to skip the frame. */
  virtual IRenderContext* doBeginFrame() = 0;

  /** @brief Render all elements into the frame context. */
  virtual void doRenderFrame(const std::vector<app::IAppElementPtr>& elements,
                             IRenderContext const& ctx) = 0;

  /** @brief Submit GPU work for the completed frame. */
  virtual void doEndFrame(IRenderContext const& ctx) = 0;

  /** @brief Present the swapchain image. */
  virtual void doPresent() = 0;

  /** @brief Advance the frame index. */
  virtual void doAdvance() = 0;
};
