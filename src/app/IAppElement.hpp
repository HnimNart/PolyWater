#pragma once

#include <filesystem>

#include "backend/interfaces/IRenderContext.hpp"
#include "core/Types.hpp"

namespace app
{

class Application;

/**
 * @brief Interface for application elements (Layers).
 * Elements are attached to the Application and receive callbacks for
 * lifecycle events, OS signals, and rendering phases.
 */
class IAppElement
{
public:
  virtual ~IAppElement() = default;

  // --- Lifecycle Management ---
  /** @brief Called once when the element is added to the application. */
  /**********************************************************/
  virtual void onAttach(Application* /*app*/)
  /**********************************************************/
  {
  }

  /** @brief Called once before the element is removed or the application shuts
   * down. */
  /**********************************************************/
  virtual void onDetach()
  /**********************************************************/
  {
  }

  // --- Window & OS Events ---
  /** @brief Called when the swapchain or viewport is resized. */
  /**********************************************************/
  virtual void onResize(WindowSize /*size*/)
  /**********************************************************/
  {
  }

  /** @brief Called when a file is dragged and dropped onto the application
   * window. */
  /**********************************************************/
  virtual void onFileDrop(const std::filesystem::path& /*filename*/,
                          glm::vec2 mousePos)
  /**********************************************************/
  {
  }

  // --- UI Callbacks (ImGui) ---
  /** @brief Called within the ImGui frame to define custom menus (e.g., File,
   * Edit). */
  /**********************************************************/
  virtual void onUIMenu()
  /**********************************************************/
  {
  }

  /** @brief Called within the ImGui frame to draw windows and widgets. */
  /**********************************************************/
  virtual void onUIRender()
  /**********************************************************/
  {
  }

  // --- The Render Loop ---
  /** @brief Logic executed before the GPU command recording begins. Use for
   * CPU-side updates. */
  /**********************************************************/
  virtual void onPreRender()
  /**********************************************************/
  {
  }

  /** @brief Called at the start of the frame, before any rendering commands are
   * issued. */
  /**********************************************************/
  virtual void onBeginFrame(const IRenderContext& /*frame*/)
  /**********************************************************/
  {
  }

  /** @brief Primary rendering callback. Record draw calls into the provided
   * context. */
  /**********************************************************/
  virtual void onRender(const IRenderContext& /* frame */)
  /**********************************************************/
  {
  }

  /** @brief Called after all rendering commands have been recorded for the
   * frame. */
  /**********************************************************/
  virtual void onEndFrame(const IRenderContext& /* frame */)
  /**********************************************************/
  {
  }

  // --- Special Modes ---
  /** @brief Final callback for headless execution before the application exits.
   */
  /**********************************************************/
  virtual void onLastHeadlessFrame()
  /**********************************************************/
  {
  }
};

using IAppElementPtr = std::shared_ptr<IAppElement>;

}  // namespace app
