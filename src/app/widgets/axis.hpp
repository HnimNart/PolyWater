/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <glm/glm.hpp>
#include <imgui/imgui.h>

/*  @DOC_START -------------------------------------------------------

Function `Axis(ImVec2 pos, const glm::mat4& modelView, float size = 20.f)`
which display right-handed axis in a ImGui window.

Example

```cpp
{  // Display orientation axis at the bottom left corner of the window
  float  axisSize = 25.F;
  ImVec2 pos      = ImGui::GetWindowPos();
  pos.y += ImGui::GetWindowSize().y;
  pos += ImVec2(axisSize * 1.1F, -axisSize * 1.1F) * ImGui::GetWindowDpiScale();
// Offset ImGuiH::Axis(pos, CameraManip.getMatrix(), axisSize);
}
```

--- @DOC_END ------------------------------------------------------- */

// The API
namespace app {

// This utility is adding the 3D axis at `pos`, using the matrix `modelView`
void Axis(ImVec2 pos, const glm::mat4 &modelView, float size = 20.f);

// Place the axis at the bottom right corner of the window
inline void drawAxis(const glm::mat4 &modelView, float size = 50.f) {
  ImVec2 windowPos = ImGui::GetWindowPos();
  ImVec2 windowSize = ImGui::GetWindowSize();
  float dpiScale = ImGui::GetWindowDpiScale();

  // Padding from the edges
  ImVec2 offset = ImVec2(size * 1.5F * dpiScale, -size * 1.5F * dpiScale);

  // Bottom-Right Calculation:
  // x = Start of window + Total Width - Padding
  // y = Start of window + Total Height - Padding (offset.y is already negative)
  ImVec2 pos = ImVec2(windowPos.x + windowSize.x - offset.x,
                      windowPos.y + windowSize.y + offset.y);

  Axis(pos, modelView, size);
}

}; // namespace app
