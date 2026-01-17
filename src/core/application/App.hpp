#pragma once

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <nvapp/frame_pacer.hpp>
#include <nvgui/settings_handler.hpp>

#include "AppInfo.hpp"
#include "IAppElement.hpp"
#include "backend/IRenderBackend.hpp"
#include "types.h"

// Forward declarations only
struct GLFWwindow;
class FrameContext;

namespace core
{

//------------------------------------------------------------
// Application
//------------------------------------------------------------
class Application
{
public:
  Application(ApplicationCreateInfo const& info, std::unique_ptr<IRenderBackend> backend);
  ~Application() = default;

  Application(Application const&) = delete;
  Application& operator=(Application const&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  //----------------------------------------------------------
  // Initialization / shutdown
  //----------------------------------------------------------
  void init(const ApplicationCreateInfo& info);
  void shutdown();

  //----------------------------------------------------------
  // Execution control
  //----------------------------------------------------------
  void run();  // Runs until close() is called
  void runOneFrame();
  void close();

  //----------------------------------------------------------
  // Elements
  //----------------------------------------------------------
  void addElement(const std::shared_ptr<IAppElement>& element)
  {
    m_elements.emplace_back(element);
    element->onAttach(this);
  }

  //----------------------------------------------------------
  // Runtime queries
  //----------------------------------------------------------
  bool isRunning() const noexcept;
  bool isHeadless() const noexcept;

  //----------------------------------------------------------
  // Rendering control (forwarded to backend)
  //----------------------------------------------------------
  void setVsync(bool v)
  {
    m_vsyncWanted = v;
    m_backend->setVsync(m_vsyncWanted);
  }
  bool isVsync() const { return m_vsyncWanted; }  // Return true if V-Sync is on

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  void requestScreenshot(const std::filesystem::path& filename, int quality = 100);
  inline GLFWwindow* getWindowHandle() const { return m_windowHandle; }
  inline const WindowSize& getViewportSize() const { return m_backend->getViewportSize(); }
  void onFileDrop(const std::filesystem::path& filename);
  void onResize(const WindowSize& size);
  IRenderBackend* getBackend() const
  {
    assert(m_backend);
    return m_backend.get();
  }

private:
  void runFrame();

  void initGlfw(const ApplicationCreateInfo& info);
  void initializeImGuiContextAndSettings();
  void testAndSetWindowSizeAndPos(const glm::uvec2& winSize);
  bool isWindowPosValid(const glm::ivec2& winPos);
  void setupImguiDock();

  std::unique_ptr<IRenderBackend> m_backend{};
  std::vector<std::shared_ptr<IAppElement>> m_elements{};

  bool m_useMenubar{true};    // Will use a menubar
  bool m_vsyncWanted{true};   // Wanting swapchain with vsync
  std::string m_iniFilename;  // Holds an .ini name as UTF-8 since ImGui uses this encoding

  nvapp::FramePacer m_framePacer;  // Low-latency system

  GLFWwindow* m_windowHandle{nullptr};  // GLFW Window
  WindowSize m_windowSize{0, 0};        // Size of the window

  //--
  std::vector<std::vector<std::function<void()>>>
      m_resourceFreeQueue;  // Queue of functions to free resources

  std::function<void(ImGuiID)> m_dockSetup;  // Function to setup the docking

  bool m_running = false;
  uint64_t m_frameCounter = 0;
  bool m_headless{false};
  bool m_headlessClose{false};
  uint32_t m_headlessFrameCount{1};
  bool m_screenShotRequested = false;
  int m_screenShotFrame = 0;
  std::filesystem::path m_screenShotFilename;

  // Use for persist the data
  nvgui::SettingsHandler m_settingsHandler;
  glm::ivec2 m_winPos{};
  glm::uvec2 m_winSize{};
};

}  // namespace core
