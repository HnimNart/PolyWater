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

#include <tiny_gltf.h>

#include <filesystem>

#include <core/shape/primitives.hpp>
#include <glm/glm.hpp>

#include "shaders/shared/structs.h"

namespace gltf
{

// This is a utility function to load a GLTF file and return the model data.
tinygltf::Model loadModel(const std::filesystem::path& filename);
shaderio::MeshPrimitive extractGltfMesh(const tinygltf::Model& model,
                                        uint meshIdx);
std::pair<glm::vec3, glm::vec3>
computeModelBounds(const tinygltf::Model& model);

shaderio::BoundingBox getMeshBounds(const tinygltf::Model& model, uint meshIdx);

template <typename T>
bool getGltfAttribute(const tinygltf::Model& model,
                      const tinygltf::Primitive& primitive,
                      const std::string& attributeName, const uint8_t*& dataPtr,
                      size_t& stride, size_t& count);

}  // namespace gltf
