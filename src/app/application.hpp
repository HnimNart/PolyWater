#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <app/widgets/settings_handler.hpp>
#include <glm/vec2.hpp>

#include "app_element_interface.hpp"
#include "app_info.hpp"
#include "backend/interfaces/render_backend_interface.hpp"
#include "core/types.hpp"
#include "frame_pacer.hpp"

// Forward declarations
struct GLFWwindow;
class IRenderContext;

namespace app
{

class Application
{
public:
  // ---------------------------------------------------------------------------
  // Lifecycle & Constructors
  // ---------------------------------------------------------------------------
  Application(ApplicationCreateInfo const& info,
              std::unique_ptr<IRenderBackend> backend,
              std::shared_ptr<IGUISystem> gui);
  ~Application() = default;

  // Non-copyable / Non-movable
  Application(Application const&) = delete;
  Application& operator=(Application const&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  // Explicit Initialization/Shutdown
  void init(const ApplicationCreateInfo& info);
  void shutdown();

  // ---------------------------------------------------------------------------
  // Execution Control
  // ---------------------------------------------------------------------------
  void run();          // Blocking run loop (runs until close() is called)
  void runOneFrame();  // Runs a single frame iteration
  void close();        // Signals the app to stop running

  [[nodiscard]] bool isRunning() const noexcept;
  [[nodiscard]] bool isHeadless() const noexcept;

  // ---------------------------------------------------------------------------
  // Element Management
  // ---------------------------------------------------------------------------
  void addElement(const std::shared_ptr<IAppElement>& element);

  // ---------------------------------------------------------------------------
  // Rendering & Backend Control
  // ---------------------------------------------------------------------------
  void setVsync(bool v);
  [[nodiscard]] bool isVsync() const noexcept;
  void setPause(bool v);
  [[nodiscard]] bool isPaused() const noexcept;

  IRenderBackend* getBackend() const;

  // ---------------------------------------------------------------------------
  // Event Handlers & Input
  // ---------------------------------------------------------------------------
  void onResize(const WindowSize& size);
  void onFileDrop(const std::filesystem::path& filename, glm::vec2 mousePos);

  // ---------------------------------------------------------------------------
  // Accessors
  // ---------------------------------------------------------------------------
  GLFWwindow* getWindowHandle() const { return m_windowHandle; }
  const WindowSize& getViewportSize() const
  {
    return m_backend->getViewportSize();
  }

#ifdef PROFILE_APP
  core::ProfilerManager* getProfiler() const { return m_profilerManager.get(); }
#endif

private:
  // ---------------------------------------------------------------------------
  // Internal Logic
  // ---------------------------------------------------------------------------
  void runFrame();
  void headlessRun();

  // ---------------------------------------------------------------------------
  // Initialization Helpers
  // ---------------------------------------------------------------------------
  void initGlfw(const ApplicationCreateInfo& info);
  void initializeBackend(const ApplicationCreateInfo& info);

  // Window placement logic
  void testAndSetWindowSizeAndPos(const glm::uvec2& winSize);
  bool isWindowPosValid(const glm::ivec2& winPos);
  void setupDefaultSettings();

  // ---------------------------------------------------------------------------
  // Member Variables
  // ---------------------------------------------------------------------------

  // Core Systems
  std::unique_ptr<IRenderBackend> m_backend{};
  std::vector<std::shared_ptr<IAppElement>> m_elements{};
  FramePacer m_framePacer;  // Low-latency system

  // Windowing State
  GLFWwindow* m_windowHandle{nullptr};
  WindowSize m_windowSize{0, 0};
  glm::ivec2 m_winPos{};
  glm::uvec2 m_winSize{};  // Persisted window size

  // Runtime State
  bool m_running = false;
  uint64_t m_frameCounter = 0;
  bool m_useMenubar{true};
  bool m_vsyncWanted{true};

  // Headless Mode
  bool m_headless{false};
  bool m_headlessClose{false};
  uint32_t m_headlessFrameCount{1};

  // Imgui
  std::shared_ptr<IGUISystem> m_gui;
  SettingsHandler m_settingsHandler;
  bool m_pause = false;

  // Queue of functions to free resources
  std::vector<std::function<void()>> m_resourceFreeQueue;

  // Profile
  std::unique_ptr<core::ProfilerManager> m_profilerManager;
  core::ProfilerTimeline* m_profileTimeline = nullptr;
};

}  // namespace app
