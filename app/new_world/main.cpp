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
// #define USE_NSIGHT_AFTERMATH  // (not always on, as it slows down the application)

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#define VMA_DYNAMIC_VULKAN_FUNCTIONS                                                               \
  1                         // Use dynamic Vulkan functions for VMA (Vulkan Memory Allocator)
#define VMA_IMPLEMENTATION  // Implementation of the Vulkan Memory Allocator
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                           \
  {                                                                                                \
    printf((format), __VA_ARGS__);                                                                 \
    printf("\n");                                                                                  \
  }

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>
#include <tinygltf/tiny_gltf.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nvaftermath/aftermath.hpp>     // Nsight Aftermath for crash tracking and shader debugging
#include <nvapp/application.hpp>         // Application framework
#include <nvapp/elem_camera.hpp>         // Camera manipulator
#include <nvapp/elem_default_menu.hpp>   // Default menu element
#include <nvapp/elem_default_title.hpp>  // Default title element
#include <nvgui/camera.hpp>              // Camera widget
#include <nvgui/sky.hpp>                 // Sky widget
#include <nvgui/tonemapper.hpp>          // Tonemapper widget
#include <nvutils/camera_manipulator.hpp>  // Camera manipulator
#include <nvutils/logger.hpp>              // Logger for debug messages
#include <nvutils/parameter_parser.hpp>    // Parameter parser
#include <nvutils/timers.hpp>              // Timers for profiling
#include <nvvk/check_error.hpp>
#include <nvvk/context.hpp>  // Vulkan context management
#include <nvvk/debug_util.hpp>
#include <nvvk/validation_settings.hpp>  // Validation settings for Vulkan

#include "backend/vulkan/VulkanBackend.hpp"
#include "common/path_utils.hpp"
#include "core/application/App.hpp"
#include "core/application/elements/elem_camera.hpp"
#include "core/application/elements/elem_default_menu.hpp"
#include "core/application/elements/elem_default_title.hpp"

//---------------------------------------------------------------------------------------------------------------
// The main function, entry point of the application
int main(int argc, char** argv)
{
  core::ApplicationCreateInfo appInfo{};

  // Parsing the command line
  nvutils::ParameterParser cli(nvutils::getExecutablePath().stem().string());
  nvutils::ParameterRegistry reg;
  reg.add({"headless", "Run in headless mode"}, &appInfo.headless, true);
  cli.add(reg);
  cli.parse(argc, argv);

  // Initialize the Vulkan context
  std::unique_ptr<core::VulkanBackend> backend =
      core::VulkanBackend::create(appInfo, nvsamples::getShaderDirs());
  assert(backend);

  // Setting up the application
  appInfo.name = "New World";

  // Create the application
  core::Application application(appInfo, std::move(backend));

  // Elements added to the application
  // auto tutorial = std::make_shared<RtBasic>(&vkContext);  // Our tutorial element
  auto elemCamera =
      std::make_shared<core::ElementCamera>();  // Element to control the camera movement
  auto windowTitle =
      std::make_shared<core::ElementDefaultWindowTitle>();  // Element displaying the window title
                                                            // with application name and size
  auto windowMenu =
      std::make_shared<core::ElementDefaultMenu>();  // Element displaying a menu, File->Exit ...
  // auto camManip = tutorial->getCameraManipulator();
  // elemCamera->setCameraManipulator(camManip);

  // Adding all elements
  application.addElement(windowMenu);
  application.addElement(windowTitle);
  application.addElement(elemCamera);
  // application.addElement(tutorial);

  application.run();       // Start the application, loop until the window is closed
  application.shutdown();  // Closing application

  return 0;
}
