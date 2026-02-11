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

#include <app/widgets/fonts.hpp>
#include <core/logger.hpp>

#include "app/App.hpp"

void core::ElementDefaultMenu::onAttach(core::Application *app) {
  LOGI("Adding Default Menu\n");
  m_app = app;
}

void core::ElementDefaultMenu::onUIMenu() {
  static bool close_app{false};
  bool v_sync = m_app->isVsync();

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem(ICON_MS_POWER_SETTINGS_NEW " Exit", "ESC")) {
      close_app = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem(ICON_MS_BOTTOM_PANEL_OPEN " V-Sync", "Ctrl+Shift+V",
                    &v_sync);
    ImGui::EndMenu();
  }

  // Shortcuts
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    close_app = true;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_V) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
      ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
    v_sync = !v_sync;
  }

  if (close_app) {
    m_app->close();
  }
  if (m_app->isVsync() != v_sync) {
    m_app->setVsync(v_sync);
  }
}
