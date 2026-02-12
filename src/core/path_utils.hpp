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

#pragma once

#include <filesystem>
#include <vector>

#include <core/file_operations.hpp>

// WARNING: These functions use CMake-defined macros
// (TARGET_EXE_TO_ROOT_DIRECTORY, etc.) Only include this header from .cpp files
// that have the proper CMake target definitions (i.e., from tutorial sample
// .cpp files, not from other header files)

namespace common {

inline static std::vector<std::filesystem::path> getResourcesDirs() {
  std::filesystem::path rootDir = ROOT_DIR;
  return {std::filesystem::absolute(rootDir / "assets")};
}

inline static std::vector<std::filesystem::path> getShaderDirs() {
  std::filesystem::path exePath = core::getExecutablePath().parent_path();
  std::filesystem::path rootDir = ROOT_DIR;
  // Define the common base path once
  const auto entryBase = rootDir / "src" / "shaders" / "entrypoints";

  return {
      // 1. The Root Shader Directory (CRITICAL)
      std::filesystem::absolute(rootDir / "src" / "shaders"),
      std::filesystem::absolute(rootDir / "src" / "shaders / shared"),
      std::filesystem::absolute(entryBase / "raytrace" / "raygen"),
      std::filesystem::absolute(entryBase / "raytrace" / "hit"),
      std::filesystem::absolute(entryBase / "raytrace" / "miss"),
      std::filesystem::absolute(entryBase / "raster"),

      // 2. The Legacy/Binary Directory
      // Often used when shaders are copied next to the executable during build
      std::filesystem::absolute(exePath / "shaders"),

      // 4. Fallback/Root (Optional)
      std::filesystem::absolute(rootDir),
      std::filesystem::absolute(exePath),
  };
}

} // namespace common
