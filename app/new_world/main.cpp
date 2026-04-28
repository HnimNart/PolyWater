/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/cli/parameter_parser.hpp>
#include <core/timers.hpp>

#include "vulkan_render_element.hpp"
#include "app/application.hpp"
#include "app/elements/camera.hpp"
#include "app/elements/default_menu.hpp"
#include "app/elements/default_title.hpp"
#include "app/elements/geometry_picker.hpp"
#include "app/elements/gpu_monitor.hpp"
#include "app/elements/logger.hpp"
#include "app/elements/profiler.hpp"
#include "backend/vulkan/core/vulkan_backend.hpp"
#include "backend/vulkan/gui/vulkan_imgui_system.hpp"
#include "core/path_utils.hpp"

//---------------------------------------------------------------------------------------------------------------
/**********************************************************/
int main(int argc, char **argv)
/**********************************************************/
{
  // =========================================================================
  // Configuration & CLI Parsing
  // =========================================================================
  app::ApplicationCreateInfo appInfo{};
  appInfo.name = "New World";

  app::cli::ParameterParser cli(core::getExecutablePath().stem().string());
  app::cli::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  reg.add({"scene", "Scene file"}, &appInfo.sceneFile);
  cli.add(reg);
  cli.parse(argc, argv);

  // =========================================================================
  // Core System Initialization
  // =========================================================================
  std::unique_ptr<VulkanBackend> backend = VulkanBackend::create(appInfo);
  assert(backend);

  auto gui = std::make_shared<VulkanImGuiSystem>();
  gui->init(appInfo);

  app::Application application(appInfo, std::move(backend), gui);

  // =========================================================================
  // Create Application Elements
  // =========================================================================
  auto logger = std::make_shared<app::ElementLogger>(true);
  auto renderElement =
      std::make_shared<VulkanRendererElement>(appInfo.sceneFile);
  auto elemCamera = std::make_shared<app::ElementCamera>();
  auto windowTitle = std::make_shared<app::ElementDefaultWindowTitle>();
  auto windowMenu = std::make_shared<app::ElementDefaultMenu>();
  auto geometryPicker = std::make_shared<app::GeometryPickerElement>(
      renderElement->getSceneManager().sceneResourceManager(),
      renderElement->getCameraManipulator());

  // =========================================================================
  // Register Elements with the Application
  // =========================================================================
  application.addElement(windowTitle);
  application.addElement(windowMenu);
  application.addElement(logger);
  application.addElement(renderElement);
  application.addElement(elemCamera);
  application.addElement(geometryPicker);

  // =========================================================================
  // Connect Elements & Set up Callbacks (Wiring)
  // =========================================================================
  elemCamera->setCameraManipulator(renderElement->getCameraManipulator());
  windowTitle->setRenderer(renderElement->getRenderer());
  renderElement->setGeometryPicker(geometryPicker);

  geometryPicker->setSelectionCallback(
      std::bind(&VulkanRendererElement::onGeometryPicked, renderElement.get(),
                std::placeholders::_1));

  windowMenu->addFileSelectedCallback(
      std::bind(&VulkanRendererElement::onFileSelected, renderElement.get(),
                std::placeholders::_1));

  // =========================================================================
  // Configure Global Logging
  // =========================================================================
  logger->setLevelFilter(app::ElementLogger::eBitAll);
  core::Logger::getInstance().setLogCallback(
      [ptr = logger.get()](core::Logger::LogLevel severity,
                           const std::string &message) {
        ptr->addLog(severity, message.c_str());
      });
  core::Logger::getInstance().setShowFlags(core::Logger::eSHOW_TIME);
  core::Logger::getInstance().setFileFlush(true);

#ifdef PROFILE_APP
  core::ProfilerManager *profilerManager = application.getProfiler();
  auto profiler = std::make_shared<app::ElementProfiler>(profilerManager);
  auto monitor = std::make_shared<app::ElementGpuMonitor>(true);

  application.addElement(profiler);
  application.addElement(monitor);
#endif

  // =========================================================================
  // Execution
  // =========================================================================
  application.run();      // Start the application, loop until window is closed
  application.shutdown(); // Cleanup

  return 0;
}
