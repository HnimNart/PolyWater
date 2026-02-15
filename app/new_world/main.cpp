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

#include <app/cli/parameter_parser.hpp> // Parameter parser
#include <core/timers.hpp>              // Timers for profiling

#include "VulkanRenderElement.hpp"
#include "app/Application.hpp"
#include "app/elements/camera.hpp"
#include "app/elements/default_menu.hpp"
#include "app/elements/default_title.hpp"
#include "app/elements/geometryPicker.hpp"
#include "app/elements/gpu_monitor.hpp"
#include "app/elements/logger.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "backend/vulkan/gui/ImGuiVulkanSystem.hpp"
#include "core/path_utils.hpp"

//---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
  app::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  app::cli::ParameterParser cli(core::getExecutablePath().stem().string());
  app::cli::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  reg.add({"scene", "Scene file"}, &appInfo.sceneFile);
  cli.add(reg);
  cli.parse(argc, argv);

  // Setting up the application
  appInfo.name = "New World";

  // Initialize the Vulkan context
  std::unique_ptr<VulkanBackend> backend = VulkanBackend::create(appInfo);
  assert(backend);

  // Initialize vulkan imgui system
  auto gui = std::make_shared<ImGuiVulkanSystem>();
  gui->init(appInfo);

  // Create the application
  app::Application application(appInfo, std::move(backend), gui);

  // Elements added to the application
  auto renderElement =
      std::make_shared<VulkanRendererElement>(appInfo.sceneFile);
  auto elemCamera = std::make_shared<app::ElementCamera>();
  auto windowTitle = std::make_shared<app::ElementDefaultWindowTitle>();
  auto windowMenu = std::make_shared<app::ElementDefaultMenu>();
  auto logger = std::make_shared<app::ElementLogger>();
  auto monitor = std::make_shared<app::ElementGpuMonitor>();

  // Adding all elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(renderElement);
  application.addElement(elemCamera);
  application.addElement(logger);
  application.addElement(monitor);
  elemCamera->setCameraManipulator(renderElement->getCameraManipulator());
  windowTitle->setRenderer(renderElement->getRenderer());

  auto geometryPicker = std::make_shared<app::GeometryPickerElement>(
      renderElement->getSceneManager().sceneResourceManager(),
      renderElement->getCameraManipulator());
  geometryPicker->setSelectionCallback(
      std::bind(&VulkanRendererElement::onGeometryPicked, renderElement.get(),
                std::placeholders::_1));
  application.addElement(geometryPicker);

  windowMenu->addFileSelectedCallback(
      std::bind(&VulkanRendererElement::onFileDrop, renderElement.get(),
                std::placeholders::_1));

  application.run(); // Start the application, loop until the window is closed
  application.shutdown(); // Closing application

  return 0;
}
