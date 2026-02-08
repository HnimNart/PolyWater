#pragma once

#include <imgui/imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <glm/vec2.hpp>
#include <nvapp/frame_pacer.hpp>
#include <nvgui/settings_handler.hpp>

#include "AppInfo.hpp"
#include "IAppElement.hpp"
#include "backend/interfaces/IRenderBackend.hpp"
#include "types.h"

// Forward declarations
struct GLFWwindow;
class IRenderContext;

namespace core {

class Application {
public:
  // ---------------------------------------------------------------------------
  // Lifecycle & Constructors
  // ---------------------------------------------------------------------------
  Application(ApplicationCreateInfo const &info,
              std::unique_ptr<IRenderBackend> backend,
              std::shared_ptr<IGUISystem> gui);
  ~Application() = default;

  // Non-copyable / Non-movable
  Application(Application const &) = delete;
  Application &operator=(Application const &) = delete;
  Application(Application &&) = delete;
  Application &operator=(Application &&) = delete;

  // Explicit Initialization/Shutdown (if separated from constructor/destructor)
  void init(const ApplicationCreateInfo &info);
  void shutdown();

  // ---------------------------------------------------------------------------
  // Execution Control
  // ---------------------------------------------------------------------------
  void run();         // Blocking run loop (runs until close() is called)
  void runOneFrame(); // Runs a single frame iteration
  void close();       // Signals the app to stop running

  bool isRunning() const noexcept;
  bool isHeadless() const noexcept;

  // ---------------------------------------------------------------------------
  // Element Management
  // ---------------------------------------------------------------------------
  void addElement(const std::shared_ptr<IAppElement> &element);

  // ---------------------------------------------------------------------------
  // Rendering & Backend Control
  // ---------------------------------------------------------------------------
  void setVsync(bool v);
  bool isVsync() const;

  IRenderBackend *getBackend() const;

  // ---------------------------------------------------------------------------
  // Event Handlers & Input
  // ---------------------------------------------------------------------------
  void onResize(const WindowSize &size);
  void onFileDrop(const std::filesystem::path &filename);

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  GLFWwindow *getWindowHandle() const { return m_windowHandle; }
  const WindowSize &getViewportSize() const {
    return m_backend->getViewportSize();
  }

private:
  // ---------------------------------------------------------------------------
  // Internal Logic
  // ---------------------------------------------------------------------------
  void runFrame();
  void headlessRun();

  // ---------------------------------------------------------------------------
  // Initialization Helpers
  // ---------------------------------------------------------------------------
  void initGlfw(const ApplicationCreateInfo &info);
  void initializeBackend(const ApplicationCreateInfo &info);

  // Window placement logic
  void testAndSetWindowSizeAndPos(const glm::uvec2 &winSize);
  bool isWindowPosValid(const glm::ivec2 &winPos);
  void setupDefaultSettings();

  // ---------------------------------------------------------------------------
  // Member Variables
  // ---------------------------------------------------------------------------

  // 1. Core Systems
  std::unique_ptr<IRenderBackend> m_backend{};
  std::vector<std::shared_ptr<IAppElement>> m_elements{};
  nvapp::FramePacer m_framePacer; // Low-latency system

  // 2. Windowing State
  GLFWwindow *m_windowHandle{nullptr};
  WindowSize m_windowSize{0, 0};
  glm::ivec2 m_winPos{};
  glm::uvec2 m_winSize{}; // Persisted window size

  // 3. Runtime State
  bool m_running = false;
  uint64_t m_frameCounter = 0;
  bool m_useMenubar{true};
  bool m_vsyncWanted{true}; // TODO figure out if this needed

  // 4. Headless Mode
  bool m_headless{false};
  bool m_headlessClose{false};
  uint32_t m_headlessFrameCount{1};

  // 5. Utilities (Screenshots, Settings)
  bool m_screenShotRequested = false;
  int m_screenShotFrame = 0;
  std::filesystem::path m_screenShotFilename;

  // Imgui
  std::shared_ptr<IGUISystem> m_gui;
  nvgui::SettingsHandler m_settingsHandler;

  // Queue of functions to free resources (double buffered or per-frame)
  std::vector<std::vector<std::function<void()>>> m_resourceFreeQueue;
};

} // namespace core
