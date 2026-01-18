#include "App.hpp"

#include <GLFW/glfw3.h>
#include <volk/volk.h>

#include <nvgui/style.hpp>
#undef APIENTRY

#include <backends/imgui_impl_glfw.h>
#include <fmt/ranges.h>
#include <imgui_internal.h>
#include <implot/implot.h>
#define NVLOGGER_ENABLE_FMT
#include <nvgui/fonts.hpp>
#include <nvutils/file_operations.hpp>
#include <nvutils/logger.hpp>
#include <nvutils/timers.hpp>

#include "backend/FrameContext.hpp"
#include "backend/IRenderBackend.hpp"
#include "common/progress_bar.hpp"

namespace core
{

Application::Application(ApplicationCreateInfo const& info,
                         std::unique_ptr<IRenderBackend> backend) : m_backend(std::move(backend))
{
  glfwInit();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  init(info);
}

void Application::init(ApplicationCreateInfo const& info)
{
  m_useMenubar = info.useMenu;
  m_vsyncWanted = info.vSync;
  m_dockSetup = info.dockSetup;
  m_headless = info.headless;
  m_headlessFrameCount = info.headlessFrameCount;

  // Resolve persistent settings (ini path, window size/pos)
  m_iniFilename = nvutils::utf8FromPath(nvutils::getExecutablePath().replace_extension(".ini"));

  initializeImGuiContextAndSettings(info.imguiConfigFlags);

  // Window setup
  testAndSetWindowSizeAndPos(info.windowSize);

  if (!m_headless)
  {
    initGlfw(info);
  }

  // Initialize backend
  if (m_backend)
  {
    m_backend->setWindow(m_windowHandle);
    m_backend->init(info);
    m_backend->setWindowSize(m_windowSize);
  }

  m_running = true;
}

void Application::shutdown()
{
  m_running = false;

  if (!m_headless)
  {
    // Save window state before destruction
    assert(m_windowHandle);
    glfwGetWindowPos(m_windowHandle, &m_winPos.x, &m_winPos.y);
    int w, h;
    glfwGetWindowSize(m_windowHandle, &w, &h);
    m_winSize = {(uint32_t) w, (uint32_t) h};
  }

  for (auto& e : m_elements)
  {
    e->onDetach();
  }

  m_elements.clear();

  if (m_backend)
  {
    m_backend->deinit();
  }

  if (!m_headless)
  {
    ImGui::SaveIniSettingsToDisk(m_iniFilename.c_str());
    ImGui_ImplGlfw_Shutdown();
  }

  ImGui::DestroyContext();
  if (ImPlot::GetCurrentContext())
  {
    ImPlot::DestroyContext();
  }

  if (!m_headless)
  {
    glfwDestroyWindow(m_windowHandle);
    m_windowHandle = nullptr;
    glfwTerminate();
  }
}

IRenderBackend* Application::getBackend() const
{
  return m_backend.get();
}

void Application::setVsync(bool v)
{
  m_vsyncWanted = v;
  m_backend->setVsync(m_vsyncWanted);
}

bool Application::isVsync() const
{
  return m_vsyncWanted;
}

void Application::addElement(const std::shared_ptr<IAppElement>& element)
{
  m_elements.emplace_back(element);
  element->onAttach(this);
}

void Application::run()
{
  LOGI("Running application\n");
  ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());

  // Handle headless mode
  if (m_headless)
  {
    headlessRun();
    return;
  }
  while (!glfwWindowShouldClose(m_windowHandle) && m_running)
  {
    runOneFrame();
  }
}

void Application::headlessRun()
{
  WindowSize viewportSize = m_windowSize;
  // Set the display for Imgui
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize.x = float(viewportSize.width);
  io.DisplaySize.y = float(viewportSize.height);
  onResize(viewportSize);

  // Need to render the UI twice: the first pass sets up the internal state and layout,
  // and the second pass finalizes the rendering with the updated state.
  {
    m_backend->newFrame();
    ImGui::NewFrame();
    setupImguiDock();

    // Call UI rendering for each element
    for (std::shared_ptr<IAppElement>& e : m_elements)
    {
      e->onUIRender();
    }
    ImGui::EndFrame();
  }

  // Rendering n-times the scene
  ProgressBar progress("Headless");
  for (uint32_t frameID = 0; frameID < m_headlessFrameCount && !m_headlessClose; frameID++)
  {
    progress.update(frameID, m_headlessFrameCount);
    m_backend->newFrame();
    ImGui::NewFrame();  // Even if isn't directly used, helps advancing time if query

    FrameContext frameCtx{};
    frameCtx.frameNumber = frameID;
    frameCtx.vSyncWanted = m_vsyncWanted;

    if (m_backend->beginFrame(frameCtx))
    {
      m_backend->renderFrame(m_elements, frameCtx);
      m_backend->endFrame(frameCtx);
      m_backend->advance();
    }

    ImGui::EndFrame();
  }
  ImGui::Render();  // This is creating the data to draw the UI (not on GPU yet)
  progress.finish();
  // At this point, everything has been rendered. Let it finish.
  m_backend->waitForDeviceIdle();

  // Call back the application, such that it can do something with the rendered image
  for (std::shared_ptr<IAppElement>& e : m_elements)
  {
    e->onLastHeadlessFrame();
  }
}

void Application::runOneFrame()
{
  if (m_vsyncWanted)
  {
    m_framePacer.pace();
  }
  glfwPollEvents();

  // Skip rendering when minimized
  if (glfwGetWindowAttrib(m_windowHandle, GLFW_ICONIFIED))
  {
    return;
  }

  runFrame();
  m_frameCounter++;
}
void Application::close()
{
  if (m_headless)
  {
    m_headlessClose = true;
  }
  else
  {
    glfwSetWindowShouldClose(m_windowHandle, true);
  }
}

void Application::runFrame()
{
  FrameContext frameCtx{};
  frameCtx.frameNumber = m_frameCounter;
  frameCtx.vSyncWanted = m_vsyncWanted;

  // 1. Begin ImGui Frame
  m_backend->newFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // 2. Docking & Menus
  setupImguiDock();
  if (m_useMenubar && ImGui::BeginMainMenuBar())
  {
    for (auto& e : m_elements)
    {
      e->onUIMenu();
    }
    ImGui::EndMainMenuBar();
  }

  // Handle Viewport Updates
  m_windowSize = m_backend->getWindowSize();  // We have to ask the backend as it might resize it
  WindowSize viewportSize = m_windowSize;
  const ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");
  if (viewport)
  {
    viewportSize = {uint32_t(viewport->Size.x), uint32_t(viewport->Size.y)};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // Update viewport if size changed
  if (m_backend->getViewportSize() != viewportSize)
  {
    onResize(viewportSize);
  }

  // // Handle Screenshot Requests
  // if(m_screenShotRequested && (m_frameRingCurrent == m_screenShotFrame))
  // {
  //   saveScreenShot(m_screenShotFilename, k_imageQuality);
  //   m_screenShotRequested = false;
  // }

  if (m_backend->beginFrame(frameCtx))
  {
    m_backend->renderFrame(m_elements, frameCtx);
    m_backend->endFrame(frameCtx);
    m_backend->present();
    m_backend->advance();
  }

  // 5. Finalize ImGui (Viewports)
  ImGui::EndFrame();
  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
}

bool Application::isRunning() const noexcept
{
  return m_running;
}
bool Application::isHeadless() const noexcept
{
  return m_headless;
}

void Application::onResize(const WindowSize& size)
{
  m_backend->onResize(size);
  for (const std::shared_ptr<core::IAppElement>& e : m_elements)
  {
    e->onResize(size);
  }
}

void Application::requestScreenshot(const std::filesystem::path& filename, int quality)
{
  // Forwarded to backend because screenshotting requires GPU-to-CPU transfer logic
  m_backend->requestScreenshot(filename, quality);
}

void core::Application::onFileDrop(const std::filesystem::path& filename)
{
  for (std::shared_ptr<IAppElement>& e : m_elements)
  {
    e->onFileDrop(filename);
  }
}

void core::Application::setupImguiDock()
{
  const ImGuiDockNodeFlags dockFlags =
      ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
  ImGuiID dockID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);
  // Docking layout, must be done only if it doesn't exist
  if (!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() && !ImGui::FindWindowByName("Viewport"))
  {
    ImGui::DockBuilderDockWindow("Viewport", dockID);  // Dock "Viewport" to  central node
    ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |=
        ImGuiDockNodeFlags_NoTabBar;  // Remove "Tab" from the central node
    if (m_dockSetup)
    {
      // This override allow to create the layout of windows by default.
      m_dockSetup(dockID);
    }
    else
    {
      ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Left, 0.2f, nullptr,
                                                   &dockID);  // Split the central node
      ImGui::DockBuilderDockWindow("Settings", leftID);       // Dock "Settings" to the left node
    }
  }
}

void Application::initGlfw(const ApplicationCreateInfo& info)
{
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  m_windowHandle = glfwCreateWindow(m_windowSize.width, m_windowSize.height, info.name.c_str(),
                                    nullptr, nullptr);

  glfwSetWindowSize(m_windowHandle, m_windowSize.width,
                    m_windowSize.height);  // Sets the size of the window using the DPI scaling
  glfwSetWindowPos(m_windowHandle, m_winPos.x, m_winPos.y);

  // Link to file drop callback (standard GLFW logic)
  glfwSetWindowUserPointer(m_windowHandle, this);
  glfwSetDropCallback(m_windowHandle,
                      [](GLFWwindow* window, int count, const char** paths)
                      {
                        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
                        for (int i = 0; i < count; i++)
                          app->onFileDrop(paths[i]);
                      });
}

void Application::initializeImGuiContextAndSettings(ImGuiConfigFlags configFlags)
{
  // Setup ImGui persistent settings
  nvgui::setStyle(false);
  m_settingsHandler.setHandlerName("Application");
  m_settingsHandler.setSetting("Size", &m_winSize);
  m_settingsHandler.setSetting("Pos", &m_winPos);
  m_settingsHandler.addImGuiHandler();
  ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags = configFlags;
  if (m_headless)
  {
    io.ConfigFlags &=
        ~ImGuiConfigFlags_ViewportsEnable;  // In headless mode, we don't allow other viewport
  }
  // Set the ini file name
  io.IniFilename = m_iniFilename.c_str();

  // Initialize fonts
  nvgui::addDefaultFont();
  io.FontDefault = nvgui::getDefaultFont();
  nvgui::addMonospaceFont();
}

void core::Application::testAndSetWindowSizeAndPos(const glm::uvec2& winSize)
{
  bool centerWindow = false;
  // If winSize is provided, use it
  if (winSize.x != 0 && winSize.y != 0)
  {
    m_winSize = winSize;
    centerWindow = true;  // When the window size is requested, it will be centered
  }

  // If m_winSize is still (0,0), set defaults
  // Could be not zero if the user set it in the settings (see loading of the ini file)
  if (m_winSize.x == 0 && m_winSize.y == 0)
  {
    if (m_headless)
    {
      m_winSize = {800, 600};
    }
    else
    {
      // Get 80% of primary monitor
      const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winSize.x = static_cast<int>(mode->width * 0.8f);
      m_winSize.y = static_cast<int>(mode->height * 0.8f);
    }
    // Center the window
    if (!m_headless)
    {
      int monX, monY;
      glfwGetMonitorPos(glfwGetPrimaryMonitor(), &monX, &monY);
      const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winPos.x = monX + (mode->width - m_winSize.x) / 2;
      m_winPos.y = monY + (mode->height - m_winSize.y) / 2;
    }
  }
  else if (!m_headless)
  {
    // If m_winPos was retrieved, check if it is valid
    if (!isWindowPosValid(m_winPos) || centerWindow)
    {
      // Center the window
      int monX, monY;
      glfwGetMonitorPos(glfwGetPrimaryMonitor(), &monX, &monY);
      const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
      m_winPos.x = monX + (mode->width - m_winSize.x) / 2;
      m_winPos.y = monY + (mode->height - m_winSize.y) / 2;
    }
  }

  m_windowSize = {m_winSize.x, m_winSize.y};
}

// Check if window position is within visible monitor bounds
bool Application::isWindowPosValid(const glm::ivec2& winPos)
{
  int monitorCount;
  GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

  // For each connected monitor
  for (int i = 0; i < monitorCount; i++)
  {
    GLFWmonitor* monitor = monitors[i];
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    int monX, monY;
    glfwGetMonitorPos(monitor, &monX, &monY);

    // Check if window position is within this monitor's bounds
    // Add some margin to account for window decorations
    if (winPos.x >= monX && winPos.x < monX + mode->width && winPos.y >= monY &&
        winPos.y < monY + mode->height)
    {
      return true;
    }
  }

  return false;
}

}  // namespace core
