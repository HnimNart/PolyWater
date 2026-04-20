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

#include "app/Application.hpp"
#include "backend/vulkan/core/Backend.hpp"
#include "backend/vulkan/gui/ImGuiVulkanSystem.hpp"

int main(int argc, char **argv) {
  app::ApplicationCreateInfo appInfo{};
  appInfo.name = "Minimal App";

  // Initialize the Vulkan backend
  std::unique_ptr<VulkanBackend> backend = VulkanBackend::create(appInfo);
  assert(backend);

  // Initialize the Vulkan ImGui system
  auto gui = std::make_shared<ImGuiVulkanSystem>();
  gui->init(appInfo);

  // Create and run the application — no elements added, nothing is rendered
  app::Application application(appInfo, std::move(backend), gui);

  application.run();      // Blocking loop until the window is closed
  application.shutdown(); // Clean up resources

  return 0;
}
