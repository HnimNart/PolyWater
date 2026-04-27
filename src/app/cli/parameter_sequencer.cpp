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

#include "parameter_sequencer.hpp"

#include "core/logger.hpp"

namespace app::cli
{

void ParameterSequencer::InitInfo::registerScriptParameters(
    ParameterRegistry& registry, ParameterParser& parser)
{
  parser.add(registry.add({.name = "sequencefile",
                           .help = "filename for text file containing "
                                   "sequences of parameters to be set."},
                          &scriptFilename));
  parser.add(registry.add(
      {.name = "sequencestring",
       .help = "string containing sequences of parameters to be set."},
      &scriptContent));
}

bool ParameterSequencer::init(const InitInfo& info)
{
  m_info = info;

  if (!m_info.scriptContent.empty())
  {
    assert(m_info.scriptFilename.empty());

    m_tokenizedScript.initFromString(m_info.scriptContent, {});
  }
  else if (!m_info.scriptFilename.empty())
  {
    if (!m_tokenizedScript.initFromFile(m_info.scriptFilename))
    {
      return false;
    }
  }
  else
  {
    return false;
  }

  if (strcmp(m_tokenizedScript.getArgs()[0], "SEQUENCE") != 0)
  {
    return false;
  }
  // skip first SEQUENCE
  m_currentArgument = 1;

  assert(m_info.parameterParser && "Parameter parser must be specified");
  assert(m_info.parameterRegistry && "Parameter registry must be specified");
  ParameterParser& parser = *m_info.parameterParser;
  ParameterRegistry& registry = *m_info.parameterRegistry;
  parser.add(
      registry.add({.name = "sequenceframes",
                    .help = "number of frames to run each parameter sequence"},
                   &m_info.sequenceFrameCount));
  parser.add(registry.add({.name = "sequenceaverages",
                           .help = "number of last frames to use for averaging "
                                   "in the profiler. 0 averages all"},
                          &m_info.profilerAverageCount, 0,
                          core::ProfilerTimeline::MAX_LAST_FRAMES));
  parser.add(registry.add(
      {.name = "sequenceresetframes",
       .help =
           "number of frames to delay the reset of the profiler per sequence"},
      &m_info.profilerResetFrameCount, 0, 8));

  m_frameCount = 0;
  m_completed = false;

  return true;
}

bool ParameterSequencer::prepareFrame()
{
  if (m_completed)
    return true;

  if ((m_frameCount % m_info.sequenceFrameCount) == 0)
  {
    // print old
    if (m_currentArgument > 2)
    {
      std::string statsFrame;
      std::string statsSingle;
      if (m_info.profilerManager)
      {
        m_info.profilerManager->appendPrint(statsFrame, statsSingle, true);
        // print old stats
        core::Logger::getInstance().log(
            core::Logger::eSTATS, "ParameterSequence %d \"%s\" = {\n%s\n%s}\n",
            m_sequenceState.index, m_sequenceState.description.c_str(),
            statsFrame.c_str(), statsSingle.c_str());
      }

      // Callback all registered functions
      for (auto& func : m_info.postCallbacks)
        func(m_sequenceState);

      m_sequenceState.index++;
    }

    // test if done
    m_completed = (m_currentArgument >= m_tokenizedScript.getArgs().size());

    if (!m_completed)
    {
      m_sequenceState.description =
          m_tokenizedScript.getArgs(m_currentArgument)[0];
      m_currentArgument++;

      auto args = m_tokenizedScript.getArgs(m_currentArgument);
      size_t stopOffset = m_info.parameterParser->parse(
          args, false, m_tokenizedScript.getFilenameBasePath(), "SEQUENCE");

      if (m_info.profilerManager)
      {
        m_info.profilerManager->setFrameAveragingCount(
            m_info.profilerAverageCount);
        m_info.profilerManager->resetFrameSections(
            m_info.profilerResetFrameCount);
      }

      m_currentArgument = m_currentArgument + stopOffset;
    }
  }

  m_frameCount++;

  return m_completed;
}

}  // namespace app::cli
