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

// Various Application utilities
// - Display a menu with File/Quit
// - Display basic information in the window title

#pragma once

#include "app/IAppElement.hpp"
#include "renderer/interfaces/IRenderer.hpp"

namespace app {

/*-------------------------------------------------------------------------------------------------
# class core::ElementDefaultWindowTitle

>  This class is an element of the application that is responsible for the
default window title of the application. It is using the `GLFW` library to set
the window title with the application name, the size of the window and the frame
rate.

To use this class, you need to add it to the `core::Application` using the
`addElement` method.

-------------------------------------------------------------------------------------------------*/

class ElementDefaultWindowTitle : public IAppElement {
public:
  ElementDefaultWindowTitle(std::string prefix = "", std::string suffix = "");

  void onAttach(Application *app) override;
  void onUIRender() override;
  void setPrefix(const std::string &str);
  void setSuffix(const std::string &str);

  void setRenderer(const IRenderer *renderer);

private:
  Application *m_app{nullptr};
  const IRenderer *m_renderer{nullptr};

  float m_dirtyTimer{0.0F};
  std::string m_prefix;
  std::string m_suffix;
};

} // namespace app
