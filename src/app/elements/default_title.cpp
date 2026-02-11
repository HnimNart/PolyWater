/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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

#include "default_title.hpp"

#include <GLFW/glfw3.h>
#undef APIENTRY
#include <core/file_operations.hpp>
#include <core/logger.hpp>
#include <fmt/format.h>

#include "app/App.hpp"

/**********************************************************/
core::ElementDefaultWindowTitle::ElementDefaultWindowTitle(
    std::string prefix /*= ""*/, std::string suffix /*= ""*/)
    : m_prefix(std::move(prefix)), m_suffix(std::move(suffix))
/**********************************************************/
{}

/**********************************************************/
void core::ElementDefaultWindowTitle::onAttach(core::Application *app)
/**********************************************************/
{
  LOGI("Adding DefaultWindowTitle\n");
  m_app = app;
}

/**********************************************************/
void core::ElementDefaultWindowTitle::onUIRender()
/**********************************************************/
{
  GLFWwindow *window = m_app->getWindowHandle();
  if (window == nullptr) // This can happen in headless mode
  {
    return;
  }

  // 1. Get the frame count
  uint32_t frameIndex = 0;
  if (m_renderer) {
    frameIndex = m_renderer->getFrameCount();
  }

  // Window Title Logic
  m_dirtyTimer += ImGui::GetIO().DeltaTime;

  if (m_dirtyTimer > 0.1F) // Refresh 0.1 seconds
  {
    const auto &size = m_app->getViewportSize();
    std::string title;

    if (!m_prefix.empty()) {
      title += fmt::format("{} | ", m_prefix.c_str());
    }

    const std::string exeName =
        core2::utf8FromPath(core2::getExecutablePath().stem());

    // 2. Add Frame Count to the format string
    title +=
        fmt::format("{} | {}x{} | {:.0f} FPS / {:.3f}ms | Frame {}", exeName,
                    size.width, size.height, ImGui::GetIO().Framerate,
                    1000.F / ImGui::GetIO().Framerate,
                    frameIndex); // <--- Pass frameIndex here

    if (!m_suffix.empty()) {
      title += fmt::format(" | {}", m_suffix.c_str());
    }

    glfwSetWindowTitle(m_app->getWindowHandle(), title.c_str());
    m_dirtyTimer = 0;
  }
}

/**********************************************************/
void core::ElementDefaultWindowTitle::setPrefix(const std::string &str)
/**********************************************************/
{
  m_prefix = str;
}

/**********************************************************/
void core::ElementDefaultWindowTitle::setSuffix(const std::string &str)
/**********************************************************/
{
  m_suffix = str;
}

/**********************************************************/
void core::ElementDefaultWindowTitle::setRenderer(const IRenderer *renderer)
/**********************************************************/
{
  m_renderer = renderer;
}
