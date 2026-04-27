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

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <core/Camera.hpp>

#include "app/IAppElement.hpp"

/*-------------------------------------------------------------------------------------------------
# class nvvkhl::ElementCamera

This class is an element of the application that is responsible for the camera
manipulation. It is using the `core::CameraManipulator` to handle the camera
movement and interaction.

To use this class, you need to add it to the `nvvkhl::Application` using the
`addElement` method.

-------------------------------------------------------------------------------------------------*/

namespace app
{

struct ElementCamera : public IAppElement
{
  /**********************************************************/
  ElementCamera(std::shared_ptr<core::CameraManipulator> camera = nullptr)
  /**********************************************************/
  {
    m_cameraManip = std::move(camera);
  }

  /**********************************************************/
  void setCameraManipulator(std::shared_ptr<core::CameraManipulator> pCamera)
  /**********************************************************/
  {
    m_cameraManip = std::move(pCamera);
  }
  void onAttach(Application* app) override;
  void onUIRender() override;
  void onResize(WindowSize size) override;

  /**********************************************************/
  std::shared_ptr<core::CameraManipulator> getCameraManipulator() const
  /**********************************************************/
  {
    return m_cameraManip;
  }

  // Can be called independently
  static void
  updateCamera(std::shared_ptr<core::CameraManipulator> m_cameraManip,
               ImGuiWindow* viewportWindow);

private:
  std::shared_ptr<core::CameraManipulator> m_cameraManip{};
};

}  // namespace app
