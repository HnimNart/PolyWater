/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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

#include <glm/gtc/type_ptr.hpp>  // glm::make_vec3
#include <nvutils/logger.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>

#include "common/timers.hpp"
#include "nvutils/primitives.hpp"

namespace
{
// Helper for element byte size calculation
uint32_t getElementByteSize(int type)
{
  return type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2U
         : type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4U
         : type == TINYGLTF_COMPONENT_TYPE_FLOAT        ? 4U
                                                        : 0U;
}

// Helper for type size calculation
uint32_t getTypeSize(int type)
{
  return type == TINYGLTF_TYPE_VEC2   ? 2U
         : type == TINYGLTF_TYPE_VEC3 ? 3U
         : type == TINYGLTF_TYPE_VEC4 ? 4U
         : type == TINYGLTF_TYPE_MAT2 ? 4U * 2U
         : type == TINYGLTF_TYPE_MAT3 ? 4U * 3U
         : type == TINYGLTF_TYPE_MAT4 ? 4U * 4U
                                      : 0U;
}

// Helper for extracting attributes
void extractAttribute(const tinygltf::Model& model,
                      const tinygltf::Primitive& primitive,
                      const std::string& name, shaderio::BufferView& attr)
{
  if (!primitive.attributes.contains(name))
  {
    attr.offset = -1;
    return;
  }
  const tinygltf::Accessor& acc =
      model.accessors[primitive.attributes.at(name)];
  const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
  assert((acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) &&
         "Should be floats");
  attr = {
      .offset = uint32_t(bv.byteOffset + acc.byteOffset),
      .count = uint32_t(acc.count),
      .byteStride =
          uint32_t(bv.byteStride ? uint32_t(bv.byteStride)
                                 : getTypeSize(acc.type) *
                                       getElementByteSize(acc.componentType)),
  };
}
}  // namespace

/**********************************************************/
tinygltf::Model gltf::loadModel(const std::filesystem::path& filename)
/**********************************************************/
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
    LOGE("Unsupported file format: %s\n",
         filename.extension().string().c_str());
    assert(0 && "No fallback");
    return {};
  }
  return model;
}

/**********************************************************/
shaderio::GltfMesh gltf::extractGltfMesh(const tinygltf::Model& model,
                                         uint meshIdx)
/**********************************************************/
{
  shaderio::GltfMesh mesh{};
  const tinygltf::Mesh& tinyMesh = model.meshes[meshIdx];
  const tinygltf::Primitive& primitive = tinyMesh.primitives.front();
  assert((tinyMesh.primitives.size() == 1 &&
          primitive.mode == TINYGLTF_MODE_TRIANGLES) &&
         "Must have one triangle primitive");

  auto& accessor = model.accessors[primitive.indices];
  auto& bufferView = model.bufferViews[accessor.bufferView];
  assert((accessor.count % 3 == 0) && "Should be a multiple of 3");

  mesh.triMesh.indices = {
      .offset = uint32_t(bufferView.byteOffset + accessor.byteOffset),
      .count = uint32_t(accessor.count),
      .byteStride = uint32_t(bufferView.byteStride
                                 ? bufferView.byteStride
                                 : getElementByteSize(accessor.componentType)),
  };
  mesh.indexType =
      accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT
          ? VK_INDEX_TYPE_UINT16
          : VK_INDEX_TYPE_UINT32;

  extractAttribute(model, primitive, "POSITION", mesh.triMesh.positions);
  extractAttribute(model, primitive, "NORMAL", mesh.triMesh.normals);
  extractAttribute(model, primitive, "COLOR_0", mesh.triMesh.colorVert);
  extractAttribute(model, primitive, "TEXCOORD_0", mesh.triMesh.texCoords);
  extractAttribute(model, primitive, "TANGENT", mesh.triMesh.tangents);
  return mesh;
}

/**********************************************************/
shaderio::GltfMesh
gltf::createGltfMeshFromPrimitive(uint64_t bufferAddress, size_t verticesSize,
                                  const nvutils::PrimitiveMesh& primMesh)
/**********************************************************/
{
  shaderio::GltfMesh mesh;
  uint32_t vertexCount = static_cast<uint32_t>(primMesh.vertices.size());

  // Positions
  mesh.triMesh.positions = {.offset = 0,
                            .count = vertexCount,
                            .byteStride = sizeof(nvutils::PrimitiveVertex)};

  // Normals
  mesh.triMesh.normals = {.offset = offsetof(nvutils::PrimitiveVertex, nrm),
                          .count = vertexCount,
                          .byteStride = sizeof(nvutils::PrimitiveVertex)};

  // Texture Coordinates
  mesh.triMesh.texCoords = {.offset = offsetof(nvutils::PrimitiveVertex, tex),
                            .count = vertexCount,
                            .byteStride = sizeof(nvutils::PrimitiveVertex)};

  // Indices
  mesh.triMesh.indices = {
      .offset = static_cast<uint32_t>(verticesSize),
      .count = static_cast<uint32_t>(primMesh.triangles.size() * 3),
      .byteStride = sizeof(uint32_t)};

  // Metadata
  mesh.gltfBuffer = reinterpret_cast<uint8_t*>(bufferAddress);
  mesh.indexType = VK_INDEX_TYPE_UINT32;

  return mesh;
}
