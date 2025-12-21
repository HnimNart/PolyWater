#pragma once

#include <imgui/imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/vec2.hpp>
#include <memory>
#include <string>
#include <vector>

namespace core
{

// Forward declarations only (no backend headers here)
class IRenderBackend;
class IAppElement;
class RenderContext;

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
  void close();

  //----------------------------------------------------------
  // Elements
  //----------------------------------------------------------
  void addElement(std::shared_ptr<IAppElement> element);

  //----------------------------------------------------------
  // Runtime queries
  //----------------------------------------------------------
  bool isRunning() const noexcept;
  bool isHeadless() const noexcept;

  //----------------------------------------------------------
  // Rendering control (forwarded to backend)
  //----------------------------------------------------------
  void setVsync(bool enabled);
  bool isVsync() const;

  //----------------------------------------------------------
  // Utilities
  //----------------------------------------------------------
  void requestScreenshot(const std::filesystem::path& filename, int quality = 100);

private:
  void runFrame();

private:
  ApplicationCreateInfo m_info{};
  std::unique_ptr<IRenderBackend> m_backend;

  std::vector<std::shared_ptr<IAppElement>> m_elements;

  bool m_running = false;
  uint64_t m_frameCounter = 0;
};

}  // namespace core
