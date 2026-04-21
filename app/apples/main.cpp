// main.cpp
// Metal renderer application.
// Renders a simple scene using the Metal rasterisation backend.

#ifdef __APPLE__

#include "MetalRendererElement.hpp"
#include "app/Application.hpp"
#include "app/elements/camera.hpp"
#include "app/elements/default_menu.hpp"
#include "app/elements/default_title.hpp"
#include "app/elements/logger.hpp"
#include "backend/metal/core/MetalBackend.hpp"
#include "backend/metal/gui/ImGuiMetalSystem.hpp"
#include "core/logger.hpp"
#include "core/path_utils.hpp"

//---------------------------------------------------------------------------------------------------------------
int main(int /*argc*/, char ** /*argv*/)
//---------------------------------------------------------------------------------------------------------------
{
  app::ApplicationCreateInfo appInfo{};
  appInfo.name = "Metal Renderer";

  // Initialize the Metal context.
  std::unique_ptr<MetalBackend> backend = MetalBackend::create(appInfo);
  assert(backend && "Failed to create Metal backend");

  // Initialize the Metal ImGui system.
  auto gui = std::make_shared<ImGuiMetalSystem>();
  gui->init(appInfo);

  // Create the application (owns the backend and GUI system).
  app::Application application(appInfo, std::move(backend), gui);

  // --- Application Elements ---
  auto windowMenu  = std::make_shared<app::ElementDefaultMenu>();
  auto windowTitle = std::make_shared<app::ElementDefaultWindowTitle>();
  auto logger      = std::make_shared<app::ElementLogger>(true);
  auto renderElem  = std::make_shared<MetalRendererElement>("default_scene.json");
  auto elemCamera  = std::make_shared<app::ElementCamera>();

  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(logger);
  application.addElement(renderElem);
  application.addElement(elemCamera);

  // --- Wire elements together ---
  elemCamera->setCameraManipulator(renderElem->getCameraManipulator());

  windowMenu->addFileSelectedCallback(
      [ptr = renderElem.get()](const std::filesystem::path &f) {
        ptr->onFileSelected(f);
      });

  // Route core logger output to the on-screen logger panel.
  logger->setLevelFilter(app::ElementLogger::eBitAll);
  core::Logger::getInstance().setLogCallback(
      [ptr = logger.get()](core::Logger::LogLevel severity,
                           const std::string &message) {
        ptr->addLog(severity, message.c_str());
      });
  core::Logger::getInstance().setShowFlags(core::Logger::eSHOW_TIME);
  core::Logger::getInstance().setFileFlush(true);

  application.run();
  application.shutdown();

  return 0;
}

#else // !__APPLE__

#include <cstdio>

int main()
{
  std::fprintf(stderr,
               "metalElement requires macOS with Metal support.\n"
               "This executable is a no-op on non-Apple platforms.\n");
  return 1;
}

#endif // __APPLE__
