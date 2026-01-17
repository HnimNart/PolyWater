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

#include "gltf_utils.hpp"

#include <fmt/format.h>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan_core.h>

#include <functional>
#include <span>

#include <glm/gtc/type_ptr.hpp>  // glm::make_vec3
#include <nvutils/logger.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>

#include "common/string_utils.h"
#include "common/timers.hpp"

tinygltf::Model gltf::load(const std::filesystem::path& filename)
{
  std::string baseName = filename.filename().string();
  common::ScopedTimer _timer(fmt::format("Loaded glTF file: {}", baseName));

  tinygltf::TinyGLTF tinyLoader;
  tinygltf::Model model;
  std::string err, warn;
  if (filename.extension() == ".gltf")
  {
    if (!tinyLoader.LoadASCIIFromFile(&model, &err, &warn, filename.string()))
    {
      LOGE("Error loading glTF file: %s\n", err.c_str());
      assert(0 && "No fallback");
      return {};
    }
  }
  else if (filename.extension() == ".glb")
  {
    if (!tinyLoader.LoadBinaryFromFile(&model, &err, &warn, filename.string()))
    {
      LOGE("Error loading glTF file: %s\n", err.c_str());
      assert(0 && "No fallback");
      return {};
    }
  }
  else
  {
    LOGE("Unsupported file format: %s\n", filename.extension().string().c_str());
    assert(0 && "No fallback");
    return {};
  }
  return model;
}
