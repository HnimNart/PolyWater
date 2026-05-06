#pragma once

#include <filesystem>
#include <string>
#include <typeinfo>

#include "backend/interfaces/render_context_interface.hpp"
#include "core/types.hpp"

#ifdef PROFILE_APP
#include "core/profiler.hpp"
#endif

namespace app
{

class Application;

/**
 * @brief Interface for application elements (Layers).
 * Elements are attached to the Application and receive callbacks for
 * lifecycle events, OS signals, and rendering phases.
 *
 * Uses the Non-Virtual Interface (NVI) pattern for the hot-path callbacks
 * (onUIMenu, onUIRender, onPreRender, onRender, onEndFrame): callers invoke
 * the public callOn*() dispatch methods, which insert profiler frame sections
 * when PROFILE_APP is defined and then forward to the private virtual hooks.
 * Subclasses override the private virtuals exactly as before — C++ allows
 * overriding private virtual functions in derived classes.
 */
class IAppElement
{
public:
  virtual ~IAppElement() = default;

  /** @brief Returns a human-readable name used in profiler section labels.
   *  Override to provide a friendlier name than the default RTTI string. */
  virtual std::string getName() const { return typeid(*this).name(); }

#ifdef PROFILE_APP
  /** @brief Called by Application::addElement() to supply the shared profiler
   *  timeline. Must not be called after elements enter the frame loop. */
  void setProfilerTimeline(core::ProfilerTimeline* timeline)
  {
    m_profilerTimeline = timeline;
    if (timeline)
    {
      // Cache section names once to avoid per-frame string allocation.
      // Note: typeid(*this).name() returns a compiler-specific mangled name.
      // Override getName() to provide a human-readable label.
      const std::string base = getName();
      m_sectionNames.uiMenu    = base + "::onUIMenu";
      m_sectionNames.uiRender  = base + "::onUIRender";
      m_sectionNames.preRender = base + "::onPreRender";
      m_sectionNames.render    = base + "::onRender";
      m_sectionNames.endFrame  = base + "::onEndFrame";
    }
  }
#endif

  // --- Lifecycle Management ---
  /** @brief Called once when the element is added to the application. */
  virtual void onAttach(Application* /*app*/) {}

  /** @brief Called once before the element is removed or the application shuts
   * down. */
  virtual void onDetach() {}

  // --- Window & OS Events ---
  /** @brief Called when the swapchain or viewport is resized. */
  virtual void onResize(WindowSize /*size*/) {}

  /** @brief Called when a file is dragged and dropped onto the application
   * window. */
  virtual void onFileDrop(const std::filesystem::path& /*filename*/,
                          glm::vec2 /*mousePos*/)
  {
  }

  // --- Special Modes ---
  /** @brief Final callback for headless execution before the application exits. */
  virtual void onLastHeadlessFrame() {}

  // -------------------------------------------------------------------------
  // Public NVI dispatch methods — call these instead of the virtual hooks.
  // -------------------------------------------------------------------------

  /** @brief Dispatch wrapper for onUIMenu(). */
  void callOnUIMenu()
  {
#ifdef PROFILE_APP
    if (m_profilerTimeline)
    {
      const auto section =
          m_profilerTimeline->frameSection(m_sectionNames.uiMenu);
      onUIMenu();
      return;
    }
#endif
    onUIMenu();
  }

  /** @brief Dispatch wrapper for onUIRender(). */
  void callOnUIRender()
  {
#ifdef PROFILE_APP
    if (m_profilerTimeline)
    {
      const auto section =
          m_profilerTimeline->frameSection(m_sectionNames.uiRender);
      onUIRender();
      return;
    }
#endif
    onUIRender();
  }

  /** @brief Dispatch wrapper for onPreRender(). */
  void callOnPreRender()
  {
#ifdef PROFILE_APP
    if (m_profilerTimeline)
    {
      const auto section =
          m_profilerTimeline->frameSection(m_sectionNames.preRender);
      onPreRender();
      return;
    }
#endif
    onPreRender();
  }

  /** @brief Dispatch wrapper for onRender(). */
  void callOnRender(const IRenderContext& frame)
  {
#ifdef PROFILE_APP
    if (m_profilerTimeline)
    {
      const auto section =
          m_profilerTimeline->frameSection(m_sectionNames.render);
      onRender(frame);
      return;
    }
#endif
    onRender(frame);
  }

  /** @brief Dispatch wrapper for onEndFrame(). */
  void callOnEndFrame(const IRenderContext& frame)
  {
#ifdef PROFILE_APP
    if (m_profilerTimeline)
    {
      const auto section =
          m_profilerTimeline->frameSection(m_sectionNames.endFrame);
      onEndFrame(frame);
      return;
    }
#endif
    onEndFrame(frame);
  }

private:
  // -------------------------------------------------------------------------
  // Private virtual hooks — override these in derived classes.
  // -------------------------------------------------------------------------

  /** @brief Called within a GUI frame to define custom menus (e.g., File, Edit). */
  virtual void onUIMenu() {}

  /** @brief Called within a GUI frame to draw windows and widgets. */
  virtual void onUIRender() {}

  /** @brief Logic executed before GPU command recording begins. Use for
   * CPU-side updates. */
  virtual void onPreRender() {}

  /** @brief Called at the start of the frame, before any rendering commands are
   * issued. */
  virtual void onBeginFrame(const IRenderContext& /*frame*/) {}

  /** @brief Primary rendering callback. Record draw calls into the provided
   * context. */
  virtual void onRender(const IRenderContext& /*frame*/) {}

  /** @brief Called after all rendering commands have been recorded for the
   * frame. */
  virtual void onEndFrame(const IRenderContext& /*frame*/) {}

#ifdef PROFILE_APP
  core::ProfilerTimeline* m_profilerTimeline = nullptr;

  // Section name strings, pre-built once in setProfilerTimeline()
  // to avoid per-frame heap allocations.
  struct SectionNames
  {
    std::string uiMenu;
    std::string uiRender;
    std::string preRender;
    std::string render;
    std::string endFrame;
  } m_sectionNames;
#endif
};

using IAppElementPtr = std::shared_ptr<IAppElement>;

}  // namespace app
