#include "App.hpp"

#include <GLFW/glfw3.h>
#include <volk/volk.h>

#include <nvgui/style.hpp>
#undef APIENTRY

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <fmt/ranges.h>
#include <imgui_internal.h>
#include <implot/implot.h>
#define NVLOGGER_ENABLE_FMT
#include <nvgui/fonts.hpp>
#include <nvutils/file_operations.hpp>
#include <nvutils/logger.hpp>
#include <nvutils/timers.hpp>
#include <nvvk/barriers.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/helpers.hpp>

#include "backend/IRenderBackend.hpp"

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

Application::~Application()
{
  shutdown();
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
  initializeImGuiContextAndSettings();

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
  }

  m_running = true;
}

void Application::run()
{
  LOGI("Running application\n");
  // Re-load ImGui settings from disk, as there might be application elements with settings to
  // restore.
  ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());
  while (!glfwWindowShouldClose(m_windowHandle) && m_running)
  {
    runOneFrame();
  }
}

void Application::runOneFrame()
{
  if (!m_headless)
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
  FrameContext frameCtx{m_frameCounter, m_vsyncWanted};

  // 1. Begin ImGui Frame
  m_backend->newFrame();

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
    m_backend->onResize(viewportSize);
  }

  // // Handle Screenshot Requests
  // if(m_screenShotRequested && (m_frameRingCurrent == m_screenShotFrame))
  // {
  //   saveScreenShot(m_screenShotFilename, k_imageQuality);
  //   m_screenShotRequested = false;
  // }

  if (m_backend->beginFrame(frameCtx))
  {
    // Draw frame
    {
      // 3. UI Phase
      for (auto& e : m_elements)
      {
        e->onUIRender();
      }
      // This is creating the data to draw the UI (not on GPU yet)
      ImGui::Render();

      // Call onPreRender for each element with the command buffer of the frame
      for (std::shared_ptr<IAppElement>& e : m_elements)
      {
        e->onPreRender();
      }
    }

    // 4. Backend Logic & GPU Command Recording
    m_backend->renderFrame(m_elements, frameCtx);
    m_backend->endFrame(frameCtx);
    m_backend->present();
  }

  // 5. Finalize ImGui (Viewports)
  ImGui::EndFrame();
  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
}

void Application::shutdown()
{
  m_running = false;

  // Save window state before destruction
  if (m_windowHandle)
  {
    glfwGetWindowPos(m_windowHandle, &m_winPos.x, &m_winPos.y);
    int w, h;
    glfwGetWindowSize(m_windowHandle, &w, &h);
    m_winSize = {(uint32_t) w, (uint32_t) h};
  }

  for (auto& e : m_elements)
    e->onDetach();
  m_elements.clear();

  ImGui::SaveIniSettingsToDisk(m_iniFilename.c_str());

  if (m_backend)
  {
    m_backend->shutdown();
  }

  if (ImPlot::GetCurrentContext())
  {
    ImPlot::DestroyContext();
  }
  ImGui::DestroyContext();

  if (m_windowHandle)
  {
    glfwDestroyWindow(m_windowHandle);
    m_windowHandle = nullptr;
  }
  glfwTerminate();
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
  for (std::shared_ptr<IAppElement>& e : m_elements)
  {
    e->onResize(size.width, size.height);
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

void Application::initializeImGuiContextAndSettings()
{
  // Setup ImGui persistent settings
  nvgui::setStyle(false);
  m_settingsHandler.setHandlerName("Application");
  m_settingsHandler.setSetting("Size", &m_winSize);
  m_settingsHandler.setSetting("Pos", &m_winPos);
  m_settingsHandler.addImGuiHandler();
  ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());
  ImGuiIO& io = ImGui::GetIO();
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