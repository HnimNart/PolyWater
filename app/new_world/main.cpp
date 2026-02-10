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

// Enable the use of Nsight Aftermath for crash tracking and shader debugging
// #define USE_NSIGHT_AFTERMATH  // (not always on, as it slows down the
// application)
#include "app/elements/elem_logger.hpp"
#include "backend/vulkan/gui/ImGuiVulkanSystem.hpp"
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)                                       \
  {                                                                            \
    printf((format), __VA_ARGS__);                                             \
    printf("\n");                                                              \
  }

// 2. Include the library headers that need implementation
#include <vk_mem_alloc.h> // Assuming VMA is included via this or similar

#include <nvutils/parameter_parser.hpp> // Parameter parser
#include <nvutils/timers.hpp>           // Timers for profiling

#include "VulkanRenderElement.hpp"
#include "app/App.hpp"
#include "app/elements/camera.hpp"
#include "app/elements/default_menu.hpp"
#include "app/elements/default_title.hpp"
#include "app/elements/geometryPicker.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "core/path_utils.hpp"
#include "shaders/compiler/slang.hpp"

//---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
  core::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  nvutils::ParameterParser cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Initialize compiler
  SlangCompiler::instance().init(common::getShaderDirs());

  // Setting up the application
  appInfo.name = "New World";

  // Initialize the Vulkan context
  std::unique_ptr<VulkanBackend> backend = VulkanBackend::create(appInfo);
  assert(backend);

  // Initialize vulkan imgui system
  auto gui = std::make_shared<ImGuiVulkanSystem>();
  gui->init(appInfo);

  // Create the application
  core::Application application(appInfo, std::move(backend), gui);

  // Elements added to the application
  auto renderElement = std::make_shared<VulkanRendererElement>();
  auto elemCamera = std::make_shared<core::ElementCamera>();
  auto windowTitle = std::make_shared<core::ElementDefaultWindowTitle>();
  auto windowMenu = std::make_shared<core::ElementDefaultMenu>();
  auto logger = std::make_shared<core::ElementLogger>();

  // Adding all elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(renderElement);
  application.addElement(elemCamera);
  application.addElement(logger);
  elemCamera->setCameraManipulator(renderElement->getCameraManipulator());
  windowTitle->setRenderer(renderElement->getRenderer());

  auto geometryPicker = std::make_shared<core::GeometryPickerElement>(
      renderElement->getSceneManager().sceneResourceManager(),
      renderElement->getCameraManipulator());
  geometryPicker->setSelectionCallback(
      std::bind(&VulkanRendererElement::onGeometryPicked, renderElement.get(),
                std::placeholders::_1));
  application.addElement(geometryPicker);

  application.run(); // Start the application, loop until the window is closed
  application.shutdown(); // Closing application

  return 0;
}
