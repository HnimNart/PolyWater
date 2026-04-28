/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <app/cli/parameter_sequencer.hpp>

#include "app/IAppElement.hpp"

namespace app
{

// Element that contains a `ParameterSequencer` and advances it
// if applicable.

class ElementSequencer final : public IAppElement
{
public:
  ElementSequencer(const cli::ParameterSequencer::InitInfo& sequencerInfo) :
      m_sequencerInfo(sequencerInfo)
  {
  }
  void onAttach(Application* app) override;
  void onPreRender() override;

private:
  cli::ParameterSequencer::InitInfo m_sequencerInfo;
  cli::ParameterSequencer m_sequencer;
  Application* m_app = nullptr;
  bool m_doSequences = false;
};
}  // namespace app
