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

#include "IAppElement.hpp"

// Forward declarations only (no backend headers here)
struct GLFWwindow;
class IRenderBackend;
class RenderContext;

namespace core
{

struct WindowSize
{
  uint32_t width;
  uint32_t height;
};

//------------------------------------------------------------
// FrameContext
//------------------------------------------------------------
struct FrameContext
{
  uint32_t frameIndex = 0;   // Index within the frame ring
  uint32_t frameCount = 0;   // Total frames in flight
  uint64_t frameNumber = 0;  // Monotonic frame counter
};

//------------------------------------------------------------
// ApplicationCreateInfo
//------------------------------------------------------------
struct ApplicationCreateInfo
{
  // General
  std::string name{"Application"};

  // Window / runtime
  glm::uvec2 windowSize{0, 0};  // Window size (width, height) or Viewport size (headless)
  bool vSync{true};             // Enable V-Sync by default

  // Headless
  bool headless = false;
  uint32_t headlessFrameCount = 1;

  // UI
  bool useMenu{true};                      // Include a menubar
  bool hasUndockableViewport{false};       // Allow floating windows
  std::function<void(ImGuiID)> dockSetup;  // Dock layout setup
  ImGuiConfigFlags imguiConfigFlags{ImGuiConfigFlags_NavEnableKeyboard |
                                    ImGuiConfigFlags_DockingEnable};

  // Called once after initialization
  std::function<void()> onInitialized;
};

//------------------------------------------------------------
// Application
//------------------------------------------------------------
class Application
{
public:
  Application();
  ~Application();

  Application(Application const&) = delete;
  Application& operator=(Application const&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  //----------------------------------------------------------
  // Initialization / shutdown
  //----------------------------------------------------------
  void init(ApplicationCreateInfo const& info, std::unique_ptr<IRenderBackend> backend);

  void shutdown();

  //----------------------------------------------------------
  // Execution control
  //----------------------------------------------------------
  void run();  // Runs until close() is called
  void runOneFrame();
  void close()
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
  void setVsync(bool v) { m_vsyncWanted = v; }
  bool isVsync() const { return m_vsyncWanted; }  // Return true if V-Sync is on

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  void requestScreenshot(const std::filesystem::path& filename, int quality = 100);
  inline GLFWwindow* getWindowHandle() const { return m_windowHandle; }
  inline const WindowSize& getViewportSize() const { return m_viewportSize; }

private:
  void runFrame();

  std::unique_ptr<IRenderBackend> m_backend;
  std::vector<std::shared_ptr<IAppElement>> m_elements;

  bool m_useMenubar{true};    // Will use a menubar
  bool m_vsyncWanted{true};   // Wanting swapchain with vsync
  std::string m_iniFilename;  // Holds an .ini name as UTF-8 since ImGui uses this encoding

  nvapp::FramePacer m_framePacer;  // Low-latency system

  GLFWwindow* m_windowHandle{nullptr};  // GLFW Window
  WindowSize m_viewportSize{0, 0};
  WindowSize m_windowSize{0, 0};  // Size of the window
  float m_dpiScale{1.f};          // Current scaling due to DPI.

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
