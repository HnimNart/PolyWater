// metalElement.cpp
// A minimal ImGui application using the Metal backend.
// Mirrors the structure of app/new_world/main.cpp but targets macOS/Metal
// instead of the Vulkan backend.

#ifdef __APPLE__

#include "app/application.hpp"
#include "app/elements/default_menu.hpp"
#include "app/elements/default_title.hpp"
#include "app/elements/logger.hpp"
#include "backend/metal/core/metal_backend.hpp"
#include "backend/metal/gui/metal_imgui_system.hpp"
#include "core/logger.hpp"
#include "core/path_utils.hpp"

//---------------------------------------------------------------------------------------------------------------
int main(int /*argc*/, char ** /*argv*/)
//---------------------------------------------------------------------------------------------------------------
{
  app::ApplicationCreateInfo appInfo{};
  appInfo.name = "Metal Element";

  // Initialize the Metal context.
  std::unique_ptr<MetalBackend> backend = MetalBackend::create(appInfo);
  assert(backend && "Failed to create Metal backend");

  // Initialize the Metal ImGui system.
  auto gui = std::make_shared<MetalImGuiSystem>();
  gui->init(appInfo);

  // Create the application (owns the backend and GUI system).
  app::Application application(appInfo, std::move(backend), gui);

  // --- UI Elements ---
  auto windowMenu  = std::make_shared<app::ElementDefaultMenu>();
  auto windowTitle = std::make_shared<app::ElementDefaultWindowTitle>();
  auto logger      = std::make_shared<app::ElementLogger>(true);

  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(logger);

  logger->setLevelFilter(app::ElementLogger::eBitAll);

  // Route core logger output to the on-screen logger panel.
  core::Logger::getInstance().setLogCallback(
      [ptr = logger.get()](core::Logger::LogLevel severity,
                           const std::string &message) {
        ptr->addLog(severity, message.c_str());
      });
  core::Logger::getInstance().setShowFlags(core::Logger::eSHOW_TIME);
  core::Logger::getInstance().setFileFlush(true);

  application.run();      // Blocking loop until the window is closed.
  application.shutdown(); // Clean up in reverse-init order.

  return 0;
}

#else // !__APPLE__

#include <cstdio>

/**********************************************************/
int main()
/**********************************************************/
{
  std::fprintf(stderr,
               "metalElement requires macOS with Metal support.\n"
               "This executable is a no-op on non-Apple platforms.\n");
  return 1;
}

#endif // __APPLE__
