#include "Application.hpp"

#include <volk/volk.h>

#include <app/widgets/style.hpp>
#undef APIENTRY

#include <backends/imgui_impl_glfw.h>
#include <fmt/ranges.h>
#include <imgui_internal.h>
#define NVLOGGER_ENABLE_FMT
#include <utility>

#include <app/widgets/fonts.hpp>
#include <core/file_operations.hpp>
#include <core/logger.hpp>

#include "IGUISystem.hpp"
#include "backend/interfaces/IRenderBackend.hpp"
#include "backend/interfaces/IRenderContext.hpp"
#include "core/progress_bar.hpp"

namespace app {

/**********************************************************/
Application::Application(ApplicationCreateInfo const &info,
                         std::unique_ptr<IRenderBackend> backend,
                         std::shared_ptr<IGUISystem> gui)
    : m_backend(std::move(backend)), m_gui(std::move(gui))
/**********************************************************/
{
  if (glfwInit() != GLFW_TRUE) {
    throw std::runtime_error("failed to initialize GLFW");
  }
  init(info);
}

/**********************************************************/
void Application::init(ApplicationCreateInfo const &info)
/**********************************************************/
{
  m_useMenubar = info.useMenu;
  m_vsyncWanted = info.vSync;
  m_headless = info.headless;
  m_headlessFrameCount = info.headlessFrameCount;

  // Window setup
  testAndSetWindowSizeAndPos(info.windowSize);

  if (!m_headless) {
    initGlfw(info);
  }

  setupDefaultSettings();
  initializeBackend(info);

  m_running = true;
}

/**********************************************************/
void Application::initializeBackend(const ApplicationCreateInfo &info)
/**********************************************************/
{
  assert(m_backend);
  m_backend->initPresentation(m_windowHandle, m_gui);

#ifdef PROFILE_APP
  m_profilerManager = std::make_unique<core::ProfilerManager>();
  m_profileTimeline = m_profilerManager->createTimeline({.name = info.name});
  m_backend->initProfiler(m_profileTimeline);
#endif
}

/**********************************************************/
void Application::setupDefaultSettings()
/**********************************************************/
{
  m_settingsHandler.setHandlerName("Application");
  m_settingsHandler.setSetting("Size", &m_winSize);
  m_settingsHandler.setSetting("Pos", &m_winPos);
  m_settingsHandler.addImGuiHandler();
}

/**********************************************************/
void Application::shutdown()
/**********************************************************/
{
  m_running = false;

  if (!m_headless) {
    // Save window state before destruction
    assert(m_windowHandle);
    glfwGetWindowPos(m_windowHandle, &m_winPos.x, &m_winPos.y);
    int w, h;
    glfwGetWindowSize(m_windowHandle, &w, &h);
    m_winSize = {(uint32_t)w, (uint32_t)h};
  }

  for (auto &e : m_elements) {
    e->onDetach();
  }
  m_elements.clear();

  // Shutdown order: GUI -> Backend -> GLFW
  if (m_gui) {
    m_gui->deinit();
  }

  if (m_backend) {
    m_backend->deinit();
  }

#ifdef PROFILE_APP
  if (m_profilerManager) {
    m_profileTimeline->clear();
    m_profilerManager.reset();
  }
#endif

  if (!m_headless) {
    glfwDestroyWindow(m_windowHandle);
    m_windowHandle = nullptr;
  }
  glfwTerminate();
}

/**********************************************************/
IRenderBackend *Application::getBackend() const
/**********************************************************/
{
  return m_backend.get();
}

/**********************************************************/
void Application::setVsync(bool v)
/**********************************************************/
{
  m_vsyncWanted = v;
  m_backend->setVsync(m_vsyncWanted);
}

/**********************************************************/
bool Application::isVsync() const
/**********************************************************/
{
  return m_vsyncWanted;
}

/**********************************************************/
void Application::setPause(bool v)
/**********************************************************/
{
  m_pause = v;
}

/**********************************************************/
bool Application::isPaused() const
/**********************************************************/
{
  return m_pause;
}

/**********************************************************/
void Application::addElement(const std::shared_ptr<IAppElement> &element)
/**********************************************************/
{
  m_elements.emplace_back(element);
  element->onAttach(this);
}

/**********************************************************/
void Application::run()
/**********************************************************/
{
  LOGI("Running application\n");

  // Handle headless mode
  if (m_headless) {
    headlessRun();
    return;
  }
  while (!glfwWindowShouldClose(m_windowHandle) && m_running) {
    runOneFrame();
  }
}

/**********************************************************/
void Application::headlessRun()
/**********************************************************/
{
  m_gui->setWindowSize(m_windowSize);
  onResize(m_windowSize);

  // Need to render the UI twice: the first pass sets up the internal state
  // and layout, and the second pass finalizes the rendering with the
  // updated state.
  {
    m_gui->beginFrame();
    for (std::shared_ptr<IAppElement> &e : m_elements) {
      e->onUIRender();
    }
    m_gui->render();
    m_gui->endFrame();
  }

  // Rendering n-times the scene
  ProgressBar progress("Rendering");
  for (uint32_t frameID = 0; frameID < m_headlessFrameCount && !m_headlessClose;
       frameID++) {
    progress.update(frameID, m_headlessFrameCount);
    IRenderContext *frameCtx = nullptr;
    if ((frameCtx = m_backend->beginFrame())) {
      m_backend->renderFrame(m_elements, *frameCtx);
      m_backend->endFrame(*frameCtx);
      m_backend->advance();
    }
  }
  progress.finish();
  // At this point, everything has been rendered. Let it finish.
  m_backend->waitForDeviceIdle();

  // Call back the application, such that it can do something with the
  // rendered image
  for (std::shared_ptr<IAppElement> &e : m_elements) {
    e->onLastHeadlessFrame();
  }
}

/**********************************************************/
void Application::runOneFrame()
/**********************************************************/
{
#ifdef PROFILE_APP
  const auto profiledSection = m_profileTimeline->frameSection(__func__);
#endif

  if (m_vsyncWanted) {
    m_framePacer.pace();
  }
  glfwPollEvents();

  // Skip rendering when minimized
  if (glfwGetWindowAttrib(m_windowHandle, GLFW_ICONIFIED)) {
    return;
  }

  runFrame();
  m_frameCounter++;
#ifdef PROFILE_APP
  m_profileTimeline->frameAdvance();
#endif
}

/**********************************************************/
void Application::close()
/**********************************************************/
{
  if (m_headless) {
    m_headlessClose = true;
  } else {
    glfwSetWindowShouldClose(m_windowHandle, true);
  }
}

/**********************************************************/
void Application::runFrame()
/**********************************************************/
{
#ifdef PROFILE_APP
  const auto profiledSection = m_profileTimeline->frameSection(__func__);
#endif

  // 1. Begin GUI Frame
  m_gui->beginFrame();

  // 2. Docking & Menus
  if (m_useMenubar) {
    m_gui->renderMenu(m_elements);
  }

  // Handle Viewport Updates
  WindowSize viewportSize = m_windowSize;
  bool ok = m_gui->getWindowSize("Viewport", viewportSize);

  // Update viewport if size changed
  if (m_backend->getViewportSize() != viewportSize) {
    onResize(viewportSize);
  }

  for (auto &e : m_elements) {
    e->onUIRender();
  }

  m_gui->render();
  IRenderContext *frameCtx = nullptr;
  if ((frameCtx = m_backend->beginFrame())) {
    m_backend->renderFrame(m_elements, *frameCtx);
    m_backend->endFrame(*frameCtx);
    m_backend->present();
    m_backend->advance();
  }

  // 5. Finalize ImGui (Viewports)
  m_gui->endFrame();
}

/**********************************************************/
bool Application::isRunning() const noexcept
/**********************************************************/
{
  return m_running;
}

/**********************************************************/
bool Application::isHeadless() const noexcept
/**********************************************************/
{
  return m_headless;
}

/**********************************************************/
void Application::onResize(const WindowSize &size)
/**********************************************************/
{
  m_backend->onResize(size);
  for (const std::shared_ptr<IAppElement> &e : m_elements) {
    e->onResize(size);
  }
}

/**********************************************************/
void Application::onFileDrop(const std::filesystem::path &filename)
/**********************************************************/
{
  for (std::shared_ptr<IAppElement> &e : m_elements) {
    e->onFileDrop(filename);
  }
}

/**********************************************************/
void Application::initGlfw(const ApplicationCreateInfo &info)
/**********************************************************/
{
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  m_windowHandle = glfwCreateWindow(m_windowSize.width, m_windowSize.height,
                                    info.name.c_str(), nullptr, nullptr);

  glfwSetWindowSize(
      m_windowHandle, m_windowSize.width,
      m_windowSize.height); // Sets the size of the window using the DPI scaling
  glfwSetWindowPos(m_windowHandle, m_winPos.x, m_winPos.y);

  // Link to file drop callback (standard GLFW logic)
  glfwSetWindowUserPointer(m_windowHandle, this);
  glfwSetDropCallback(m_windowHandle, [](GLFWwindow *window, int count,
                                         const char **paths) {
    auto *app = static_cast<Application *>(glfwGetWindowUserPointer(window));
    for (int i = 0; i < count; i++)
      app->onFileDrop(paths[i]);
  });
}

/**********************************************************/
void Application::testAndSetWindowSizeAndPos(const glm::uvec2 &winSize)
/**********************************************************/
{
  bool centerWindow = false;
  // If winSize is provided, use it
  if (winSize.x != 0 && winSize.y != 0) {
    m_winSize = winSize;
    centerWindow =
        true; // When the window size is requested, it will be centered
  }

  // If m_winSize is still (0,0), set defaults
  // Could be not zero if the user set it in the settings (see loading of
  // the ini file)
  if (m_winSize.x == 0 && m_winSize.y == 0) {
    if (m_headless) {
      m_winSize = {800, 600};
    } else {
      // Get 80% of primary monitor
      const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winSize.x = static_cast<int>(mode->width * 0.8f);
      m_winSize.y = static_cast<int>(mode->height * 0.8f);
    }
    // Center the window
    if (!m_headless) {
      int monX, monY;
      glfwGetMonitorPos(glfwGetPrimaryMonitor(), &monX, &monY);
      const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winPos.x = monX + (mode->width - m_winSize.x) / 2;
      m_winPos.y = monY + (mode->height - m_winSize.y) / 2;
    }
  } else if (!m_headless) {
    // If m_winPos was retrieved, check if it is valid
    if (!isWindowPosValid(m_winPos) || centerWindow) {
      // Center the window
      int monX, monY;
      glfwGetMonitorPos(glfwGetPrimaryMonitor(), &monX, &monY);
      const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winPos.x = monX + (mode->width - m_winSize.x) / 2;
      m_winPos.y = monY + (mode->height - m_winSize.y) / 2;
    }
  }

  m_windowSize = {m_winSize.x, m_winSize.y};
}

/**********************************************************/
// Check if window position is within visible monitor bounds
bool Application::isWindowPosValid(const glm::ivec2 &winPos)
/**********************************************************/
{
  int monitorCount;
  GLFWmonitor **monitors = glfwGetMonitors(&monitorCount);

  // For each connected monitor
  for (int i = 0; i < monitorCount; i++) {
    GLFWmonitor *monitor = monitors[i];
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);

    int monX, monY;
    glfwGetMonitorPos(monitor, &monX, &monY);

    // Check if window position is within this monitor's bounds
    // Add some margin to account for window decorations
    if (winPos.x >= monX && winPos.x < monX + mode->width && winPos.y >= monY &&
        winPos.y < monY + mode->height) {
      return true;
    }
  }

  return false;
}

} // namespace app
