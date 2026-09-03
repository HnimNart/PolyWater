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

#include "sequencer.hpp"

#include "app/application.hpp"

namespace app
{

void ElementSequencer::onAttach(Application* app)
{
  m_app = app;
  m_doSequences = m_sequencer.init(m_sequencerInfo);
}

void ElementSequencer::onPreRender()
{
  if (m_doSequences)
  {
    bool finished = m_sequencer.prepareFrame();
    if (finished)
    {
      m_app->close();
    }
  }
}

}  // namespace app
