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
#include <functional>

// Use:
//  include this file at the end of all other includes,
//  and add engines
//
// Ex:
//   app->addEngine(std::make_shared<nvapp::ElementDefaultMenu>());
//

namespace app {

/*-------------------------------------------------------------------------------------------------
# class nvapp::ElementDefaultMenu

>  This class is an element of the application that is responsible for the
default menu of the application. It is using the `ImGui` library to create a
menu with File/Exit and View/V-Sync.

To use this class, you need to add it to the `nvapp::Application` using the
`addElement` method.

-------------------------------------------------------------------------------------------------*/

class ElementDefaultMenu : public IAppElement {
public:
  void onAttach(Application *app) override;
  void onUIMenu() override;

  using OnFileSelectedCallback =
      std::function<void(const std::filesystem::path &path)>;

  void addFileSelectedCallback(OnFileSelectedCallback callback) {
    m_onSelect.emplace_back(std::move(callback));
  }

private:
  Application *m_app{nullptr};
  std::vector<OnFileSelectedCallback> m_onSelect{};
};

} // namespace app
