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

// Minimal macOS/Metal application using the metal_backend.
// No Vulkan runtime dependencies are used.

#include "backend/metal/core/ContextManager.hpp"

// metal-cpp headers (implementations live in metal_backend via MetalImpl.cpp)
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

// GLFW – request no graphics-API context so Metal owns the surface
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>

//---------------------------------------------------------------------------------------------------------------
int main(int /*argc*/, char ** /*argv*/) {
  // =========================================================================
  // Windowing – GLFW with no API (Metal will drive the surface)
  // =========================================================================
  if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Failed to initialize GLFW\n";
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  // =========================================================================
  // Metal Initialization
  // =========================================================================
  // appInfo carries the window dimensions used both by GLFW and MetalContextManager.
  app::ApplicationCreateInfo appInfo{};
  appInfo.name       = "Apples";
  appInfo.windowSize = {1280, 720};

  // Width and height are fixed at 1280×720, well within the range of int.
  GLFWwindow *window =
      glfwCreateWindow(static_cast<int>(appInfo.windowSize.width),
                       static_cast<int>(appInfo.windowSize.height),
                       appInfo.name.c_str(), nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    return 1;
  }

  // init() creates the MTL::Device and a MTL::CommandQueue using appInfo.
  MetalContextManager metalCtx;
  if (!metalCtx.init(appInfo)) {
    std::cerr << "Failed to initialize Metal context\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  std::cout << "Metal device: "
            << metalCtx.getDevice()->name()->utf8String() << '\n';

  // =========================================================================
  // Application Loop
  // =========================================================================
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Each frame gets its own autorelease pool to manage short-lived objects.
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

    // Submit an empty command buffer – the minimal unit of GPU work.
    // In a real application, render/compute encoders and draw calls would be
    // recorded into this command buffer before calling commit().
    MTL::CommandBuffer *cmd = metalCtx.getCommandQueue()->commandBuffer();
    if (cmd) {
      cmd->commit();
    }

    pool->release();
  }

  // =========================================================================
  // Shutdown
  // =========================================================================
  // MetalContextManager::deinit() waits for device idle and releases Metal
  // objects. It is also called automatically by the destructor, but invoking it
  // here makes the teardown order explicit.
  metalCtx.deinit();
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
