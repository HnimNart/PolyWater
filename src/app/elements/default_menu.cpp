/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "default_menu.hpp"

#include <imgui/imgui.h>
#include <implot/implot.h>

#include <app/widgets/file_dialog.hpp>
#include <app/widgets/fonts.hpp>
#include <core/logger.hpp>
#include <core/path_utils.hpp>

#include "app/application.hpp"

/**********************************************************/
void app::ElementDefaultMenu::onAttach(Application* app)
/**********************************************************/
{
  LOGI("Adding Default Menu\n");
  m_app = app;
}

/**********************************************************/
void app::ElementDefaultMenu::onUIMenu()
/**********************************************************/
{
  static bool close_app{false};
  bool v_sync = m_app->isVsync();
  bool isPaused = m_app->isPaused();
  bool denoise = m_renderer->denoise();
  std::filesystem::path file = "";

  if (ImGui::BeginMenu("File"))
  {
    if (ImGui::MenuItem(ICON_MS_POWER_SETTINGS_NEW " Exit", "ESC"))
    {
      close_app = true;
    }
    if (ImGui::MenuItem(ICON_MS_FOLDER_OPEN " Open File", "Ctrl+O"))
    {
      file = windowOpenFileDialog(m_app->getWindowHandle(), "Open File", "*",
                                  core::getSceneDir()[0]);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View"))
  {
    ImGui::MenuItem(ICON_MS_BOTTOM_PANEL_OPEN " V-Sync", "Ctrl+Shift+V",
                    &v_sync);
    ImGui::MenuItem(ICON_MS_PAUSE_CIRCLE " Pause", "Ctrl+P", &isPaused);
    ImGui::MenuItem(ICON_MS_BLUR_LINEAR " Denoise", "Ctrl+Shift+D", &denoise);
    ImGui::EndMenu();
  }

  // Shortcuts
  if (ImGui::IsKeyPressed(ImGuiKey_Escape))
  {
    close_app = true;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_P) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
  {
    isPaused = !isPaused;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_O) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
  {
    file = windowOpenFileDialog(m_app->getWindowHandle(), "Open File", "*",
                                core::getSceneDir()[0]);
  }

  if (ImGui::IsKeyPressed(ImGuiKey_V) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
      ImGui::IsKeyDown(ImGuiKey_LeftShift))
  {
    v_sync = !v_sync;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_D) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
      ImGui::IsKeyDown(ImGuiKey_LeftShift))
  {
    denoise = !denoise;
  }

  if (!file.empty())
  {
    for (auto& cb : m_onSelect)
    {
      cb(file);
    }
  }

  if (close_app)
  {
    m_app->close();
  }

  if (m_app->isVsync() != v_sync)
  {
    m_app->setVsync(v_sync);
  }

  if (m_app->isPaused() != isPaused)
  {
    m_app->setPause(isPaused);
  }
  if (m_renderer->denoise() != denoise)
  {
    m_renderer->setDenoise(denoise);
  }
}
