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

#include <glm/glm.hpp>

#include "nvutils/primitives.hpp"
#include "nvvk/resources.hpp"
#include "nvvk/staging.hpp"
#include "scene/gltf/io_gltf.h"  // Contains definitions for GLTF GltfMesh, BufferView, TriangleMesh and more

namespace tinygltf
{
class Model;
}

namespace gltf
{

// Simple host scene resource that holds meshes, instances, and materials
struct Scene
{
  std::vector<shaderio::GltfMesh> meshes;                  // All meshes in the scene
  std::vector<shaderio::GltfInstance> instances;           // All instances in the scene
  std::vector<shaderio::GltfMetallicRoughness> materials;  // All materials in the scene
  shaderio::GltfSceneInfo
      sceneInfo;  // Scene information (camera matrices, meshes, instances, materials, etc.)
};

// This is a utility function to load a GLTF file and return the model data.
tinygltf::Model load(const std::filesystem::path& filename);

}  // namespace gltf
