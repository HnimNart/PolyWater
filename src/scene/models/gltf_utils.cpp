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

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <fmt/format.h>

#include <core/logger.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include "backend/interfaces/RHI_definitions.hpp"
#include "core/timers.hpp"

namespace {

// Helper for element byte size calculation
/**********************************************************/
uint32_t getElementByteSize(int type)
/**********************************************************/
{
  return type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2U
         : type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4U
         : type == TINYGLTF_COMPONENT_TYPE_FLOAT        ? 4U
                                                        : 0U;
}

// Helper for type size calculation
/**********************************************************/
uint32_t getTypeSize(int type)
/**********************************************************/
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
/**********************************************************/
void extractAttribute(const tinygltf::Model &model,
                      const tinygltf::Primitive &primitive,
                      const std::string &name, shaderio::BufferView &attr)
/**********************************************************/
{
  if (!primitive.attributes.contains(name)) {
    attr.offset = -1;
    return;
  }
  const tinygltf::Accessor &acc =
      model.accessors[primitive.attributes.at(name)];
  const tinygltf::BufferView &bv = model.bufferViews[acc.bufferView];
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

// Helper to expand the global bounds by a transformed local box
/**********************************************************/
void expandBounds(const glm::vec3 &localMin, const glm::vec3 &localMax,
                  const glm::mat4 &transform, glm::vec3 &globalMin,
                  glm::vec3 &globalMax)
/**********************************************************/
{
  // The 8 corners of the local bounding box
  std::vector<glm::vec3> corners = {{localMin.x, localMin.y, localMin.z},
                                    {localMin.x, localMin.y, localMax.z},
                                    {localMin.x, localMax.y, localMin.z},
                                    {localMin.x, localMax.y, localMax.z},
                                    {localMax.x, localMin.y, localMin.z},
                                    {localMax.x, localMin.y, localMax.z},
                                    {localMax.x, localMax.y, localMin.z},
                                    {localMax.x, localMax.y, localMax.z}};

  // Transform each corner and expand the global bounds
  for (const auto &corner : corners) {
    // Apply transform (w=1.0 for points)
    glm::vec4 worldPos = transform * glm::vec4(corner, 1.0f);

    // Perspective divide is usually not needed for affine transforms
    // (Scale/Rot/Trans), but good practice if you ever use projection matrices
    // here.
    glm::vec3 p = glm::vec3(worldPos) / worldPos.w;

    globalMin = glm::min(globalMin, p);
    globalMax = glm::max(globalMax, p);
  }
}

// Recursive function to traverse nodes
/**********************************************************/
void processNode(const tinygltf::Model &model, int nodeIndex,
                 const glm::mat4 &parentTransform, glm::vec3 &minBound,
                 glm::vec3 &maxBound)
/**********************************************************/
{
  const tinygltf::Node &node = model.nodes[nodeIndex];

  // 1. Calculate Local Transform
  glm::mat4 localTransform(1.0f);

  if (!node.matrix.empty()) {
    // Node has a raw matrix
    // GLTF stores column-major, GLM accepts column-major.
    // We must cast double (tinygltf) to float (glm).
    double *m = const_cast<double *>(node.matrix.data());
    localTransform = glm::make_mat4(m);
  } else {
    // Node has TRS (Translation, Rotation, Scale)
    if (!node.translation.empty()) {
      localTransform = glm::translate(
          localTransform, glm::vec3(node.translation[0], node.translation[1],
                                    node.translation[2]));
    }
    if (!node.rotation.empty()) {
      // GLTF quaternion: (x, y, z, w) -> GLM quat constructor: (w, x, y, z)
      glm::quat q(node.rotation[3], node.rotation[0], node.rotation[1],
                  node.rotation[2]);
      localTransform = localTransform * glm::mat4_cast(q);
    }
    if (!node.scale.empty()) {
      localTransform =
          glm::scale(localTransform,
                     glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }
  }

  // 2. Calculate Global Transform for this node
  glm::mat4 globalTransform = parentTransform * localTransform;

  // 3. Process Mesh (if present)
  if (node.mesh > -1) {
    const tinygltf::Mesh &mesh = model.meshes[node.mesh];

    for (const auto &primitive : mesh.primitives) {
      // Look for the "POSITION" attribute
      auto it = primitive.attributes.find("POSITION");
      if (it != primitive.attributes.end()) {
        const tinygltf::Accessor &accessor = model.accessors[it->second];

        // GLTF Accessors *must* have min/max values for POSITION
        if (!accessor.minValues.empty() && !accessor.maxValues.empty()) {
          glm::vec3 localMin(accessor.minValues[0], accessor.minValues[1],
                             accessor.minValues[2]);
          glm::vec3 localMax(accessor.maxValues[0], accessor.maxValues[1],
                             accessor.maxValues[2]);

          expandBounds(localMin, localMax, globalTransform, minBound, maxBound);
        }
      }
    }
  }

  // 4. Recurse Children
  for (int childIndex : node.children) {
    processNode(model, childIndex, globalTransform, minBound, maxBound);
  }
}
} // namespace

/**********************************************************/
tinygltf::Model gltf::loadModel(const std::filesystem::path &filename)
/**********************************************************/
{
  std::string baseName = filename.filename().string();
  SCOPED_TIMER(fmt::format("Loaded glTF file: {}", baseName));

  tinygltf::TinyGLTF tinyLoader;
  tinygltf::Model model;
  std::string err, warn;
  if (filename.extension() == ".gltf") {
    if (!tinyLoader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) {
      LOGE("Error loading glTF file: %s\n", err.c_str());
      assert(0 && "No fallback");
      return {};
    }
  } else if (filename.extension() == ".glb") {
    if (!tinyLoader.LoadBinaryFromFile(&model, &err, &warn,
                                       filename.string())) {
      LOGE("Error loading glTF file: %s\n", err.c_str());
      assert(0 && "No fallback");
      return {};
    }
  } else {
    LOGE("Unsupported file format: %s\n",
         filename.extension().string().c_str());
    assert(0 && "No fallback");
    return {};
  }
  return model;
}

/**********************************************************/
shaderio::MeshPrimitive gltf::extractGltfMesh(const tinygltf::Model &model,
                                              uint meshIdx)
/**********************************************************/
{
  shaderio::MeshPrimitive mesh{};
  const tinygltf::Mesh &tinyMesh = model.meshes[meshIdx];
  const tinygltf::Primitive &primitive = tinyMesh.primitives.front();
  assert((tinyMesh.primitives.size() == 1 &&
          primitive.mode == TINYGLTF_MODE_TRIANGLES) &&
         "Must have one triangle primitive");

  auto &accessor = model.accessors[primitive.indices];
  auto &bufferView = model.bufferViews[accessor.bufferView];
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
          ? IndexType16
          : IndexType32;

  extractAttribute(model, primitive, "POSITION", mesh.triMesh.positions);
  extractAttribute(model, primitive, "NORMAL", mesh.triMesh.normals);
  extractAttribute(model, primitive, "COLOR_0", mesh.triMesh.colorVert);
  extractAttribute(model, primitive, "TEXCOORD_0", mesh.triMesh.texCoords);
  extractAttribute(model, primitive, "TANGENT", mesh.triMesh.tangents);
  return mesh;
}

/**********************************************************/
std::pair<glm::vec3, glm::vec3>
gltf::computeModelBounds(const tinygltf::Model &model)
/**********************************************************/
{
  glm::vec3 minBound(std::numeric_limits<float>::max());
  glm::vec3 maxBound(std::numeric_limits<float>::lowest());

  // Iterate over the scenes (usually just the default one)
  const tinygltf::Scene &scene =
      model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];

  // Traverse the root nodes of the scene
  for (int nodeIndex : scene.nodes) {
    processNode(model, nodeIndex, glm::mat4(1.0f), minBound, maxBound);
  }

  return {minBound, maxBound};
}

/**********************************************************/
shaderio::BoundingBox gltf::getMeshBounds(const tinygltf::Model &model,
                                          uint meshIdx)
/**********************************************************/
{
  shaderio::BoundingBox bbox;
  const tinygltf::Mesh &mesh = model.meshes.at(meshIdx);
  for (const auto &primitive : mesh.primitives) {
    auto it = primitive.attributes.find("POSITION");
    if (it != primitive.attributes.end()) {
      const tinygltf::Accessor &accessor = model.accessors[it->second];

      // tinygltf accessors for POSITION usually already contain the min/max
      if (accessor.minValues.size() == 3 && accessor.maxValues.size() == 3) {

        bbox.add(glm::vec3(accessor.minValues[0], accessor.minValues[1],
                           accessor.minValues[2]));
        bbox.add(glm::vec3(accessor.maxValues[0], accessor.maxValues[1],
                           accessor.maxValues[2]));
      }
    }
  }
  return bbox;
}


