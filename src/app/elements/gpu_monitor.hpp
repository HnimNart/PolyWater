/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <fmt/core.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <implot/implot.h>

#include <numeric>
#include <type_traits>

#include "app/Application.hpp"
#include "app/widgets/settings_handler.hpp"
#include "core/timers.hpp"
#include "nvml_monitor.hpp"

/*-------------------------------------------------------------------------------------------------
# class nvapp::ElementNvml

>  This class is an element of the application that is responsible for the NVML
monitoring. It is using the `NVML` library to get information about the GPU and
display it in the application.
-------------------------------------------------------------------------------------------------*/

namespace app
{

#define SAMPLING_NUM 100  // Show 100 measurements

/// utility structure for averaging values
template <typename T> struct AverageCircularBuffer
{
  int offset = 0;
  T totValue = 0;
  std::vector<T> data;
  AverageCircularBuffer(int max_size = 100) { data.reserve(max_size); }
  void addValue(T x)
  {
    if (data.size() < data.capacity())
    {
      data.push_back(x);
      totValue += x;
    }
    else
    {
      totValue -= data[offset];
      totValue += x;
      data[offset] = x;
      offset = (offset + 1) % data.capacity();
    }
  }

  T average() { return totValue / data.size(); }
};

struct ElementGpuMonitor : public IAppElement
{
  explicit ElementGpuMonitor(bool show = false);
  virtual ~ElementGpuMonitor() = default;

  void onUIRender() override;
  void onUIMenu() override;
  void onAttach(Application* app) override;
  void onDetach() override;

  // attribute set public on purpose so external parameter parser and UI widgets
  // can modify directly with pointer access
  bool showWindow{false};

private:
  void pushThrottleTabColor() const;
  void popThrottleTabColor() const;

  static void imguiCopyableText(const std::string& text, uint64_t uniqueId);

  template <typename T>
  void imguiNvmlField(const NvmlMonitor::NVMLField<T>& field,
                      const std::string& name, const std::string& unit = "");
  void imguiDeviceInfo(uint32_t deviceIndex);
  void imguiDeviceMemory(uint32_t deviceIndex);
  void imguiDevicePerformanceState(uint32_t deviceIndex);
  void imguiDevicePowerState(uint32_t deviceIndex);
  void imguiDeviceUtilization(uint32_t deviceIndex);
  void imguiGraphLines(uint32_t gpuIndex);
  void imguiProgressBars();
  void imguiClockSetup(uint32_t deviceIndex);

  bool m_throttleDetected{false};
  uint64_t m_lastThrottleReason{0ull};
  core::PerformanceTimer m_throttleCooldownTimer;

  uint32_t m_selectedMemClock{0u};
  uint32_t m_selectedGraphicsClock{0u};

  std::unique_ptr<NvmlMonitor> m_nvmlMonitor;
  AverageCircularBuffer<float> m_avgCpu = {SAMPLING_NUM};

  SettingsHandler m_settingsHandler;
};

template <typename T>
void ElementGpuMonitor::imguiNvmlField(const NvmlMonitor::NVMLField<T>& field,
                                       const std::string& name,
                                       const std::string& unit /*= ""*/)
{
  if (field.isSupported)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::Text("%s", fmt::format("{}", name).c_str());
    ImGui::TableNextColumn();
    imguiCopyableText(fmt::format("{} {}", field.get(), unit),
                      reinterpret_cast<uint64_t>(&field));
  }
}

}  // namespace app
