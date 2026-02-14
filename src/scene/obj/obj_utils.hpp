#pragma once

#include <cstddef> // for offsetof
#include <cstring>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include <tinyobjloader/tiny_obj_loader.h>

#include "core/logger.hpp"
#include "shaders/shared/structs.h"
#include "shape/primitives.hpp"
#include <vulkan/vulkan_core.h>

namespace obj {

/**********************************************************/
inline core::PrimitiveMesh
flattenObjData(const tinyobj::attrib_t &attrib,
               const std::vector<tinyobj::shape_t> &shapes)
/**********************************************************/
{
  core::PrimitiveMesh result;

  // Maps a unique Vertex to its index in the 'result.vertices' array
  std::unordered_map<core::PrimitiveVertex, uint32_t> uniqueVertices;

  for (const auto &shape : shapes) {
    // Loop over faces (polygons)
    size_t index_offset = 0;

    // tinyobjloader stores face counts (e.g., 3 for triangle, 4 for quad)
    // We assume triangulation was enabled during loading (default in most
    // loaders), so every face has 3 vertices.
    for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {

      // Although we assume triangles, let's be safe and check
      int fv = shape.mesh.num_face_vertices[f];
      if (fv != 3) {
        // Skip non-triangles or handle triangulation here if needed
        index_offset += fv;
        continue;
      }

      core::PrimitiveTriangle triangle;

      // Process the 3 vertices of this triangle
      for (size_t v = 0; v < 3; v++) {
        tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

        core::PrimitiveVertex vertex{};

        // 1. Position
        vertex.pos = {attrib.vertices[3 * idx.vertex_index + 0],
                      attrib.vertices[3 * idx.vertex_index + 1],
                      attrib.vertices[3 * idx.vertex_index + 2]};

        // 2. Normal (Check validity)
        if (idx.normal_index >= 0) {
          vertex.nrm = {attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]};
        }

        // 3. TexCoord (Check validity)
        if (idx.texcoord_index >= 0) {
          vertex.tex = {attrib.texcoords[2 * idx.texcoord_index + 0],
                        // Flip V for Vulkan convention
                        1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
        }

        // 4. Deduplicate
        if (uniqueVertices.count(vertex) == 0) {
          // New unique vertex found
          uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
          uniqueVertices[vertex] = newIndex;
          result.vertices.push_back(vertex);
        }

        // 5. Assign index to the current corner of the triangle
        // triangle.indices is a glm::uvec3, access via [v] or .x .y .z
        triangle.indices[v] = uniqueVertices[vertex];
      }

      // Push the completed triangle
      result.triangles.push_back(triangle);

      index_offset += fv;
    }
  }

  return result;
}

// Return type: Name of the shape + The flattened mesh data
using LoadedObjResult = std::pair<std::string, core::PrimitiveMesh>;
/**********************************************************/
inline std::vector<LoadedObjResult>
loadObjPrimitives(const std::string &filename)
/**********************************************************/
{
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  // 1. Load the OBJ file
  std::string baseDir = filename.substr(0, filename.find_last_of("/\\") + 1);

  bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              filename.c_str(), baseDir.c_str());

  if (!warn.empty()) {
    LOGD("OBJ Warning: %s\n", warn.c_str());
  }
  if (!err.empty()) {
    LOGE("OBJ Error: %s\n", err.c_str());
  }
  if (!ret) {
    LOGE("Failed to load OBJ file: %s\n", filename.c_str());
    return {};
  }

  if (shapes.empty()) {
    LOGE("Error: OBJ file %s contains no shapes.\n", filename.c_str());
    return {};
  }

  // 2. Process Shapes
  std::vector<LoadedObjResult> results;
  results.reserve(shapes.size());

  for (const auto &shape : shapes) {
    std::vector<tinyobj::shape_t> singleShapeList = {shape};
    core::PrimitiveMesh mesh = flattenObjData(attrib, singleShapeList);
    results.emplace_back(shape.name, std::move(mesh));
  }

  return results;
}

/**********************************************************/
inline shaderio::MeshPrimitive
createGpuMeshFromPrimitive(const core::PrimitiveMesh &meshData)
/**********************************************************/
{
  shaderio::MeshPrimitive gpuMesh = {};
  const uint32_t stride = sizeof(core::PrimitiveVertex);

  // Calculate the size of the vertex block so we know where indices start
  size_t vSize = meshData.vertices.size() * sizeof(core::PrimitiveVertex);

  // --- Vertex Attributes (Interleaved) ---
  // All attributes share the same stride and count, but have different offsets
  // within the struct.
  // Position: Offset 0
  gpuMesh.triMesh.positions = {
      .offset = 0,
      .count = static_cast<uint32_t>(meshData.vertices.size()),
      .byteStride = stride};

  // Normal: Offset = offsetof(nrm)
  gpuMesh.triMesh.normals = {
      .offset = offsetof(core::PrimitiveVertex, nrm),
      .count = static_cast<uint32_t>(meshData.vertices.size()),
      .byteStride = stride};

  // TexCoord: Offset = offsetof(tex)
  gpuMesh.triMesh.texCoords = {
      .offset = offsetof(core::PrimitiveVertex, tex),
      .count = static_cast<uint32_t>(meshData.vertices.size()),
      .byteStride = stride};

  // --- Indices ---
  // Indices are packed immediately after the vertex block in the buffer.
  gpuMesh.triMesh.indices = {
      .offset = static_cast<uint32_t>(vSize),
      .count = static_cast<uint32_t>(meshData.triangles.size() *
                                     3), // 3 indices per triangle
      .byteStride = sizeof(uint32_t)};

  // --- Metadata ---
  gpuMesh.indexType = VK_INDEX_TYPE_UINT32;

  return gpuMesh;
}

/**********************************************************/
inline std::pair<glm::vec3, glm::vec3>
computeMeshBounds(const core::PrimitiveMesh &mesh)
/**********************************************************/
{
  glm::vec3 bmin(std::numeric_limits<float>::max());
  glm::vec3 bmax(std::numeric_limits<float>::lowest());

  for (const auto &vertex : mesh.vertices) {
    bmin = glm::min(bmin, vertex.pos);
    bmax = glm::max(bmax, vertex.pos);
  }

  // Handle empty mesh case safely
  if (mesh.vertices.empty()) {
    return {glm::vec3(0.0f), glm::vec3(0.0f)};
  }

  return {bmin, bmax};
}

/**********************************************************/
inline std::vector<uint8_t>
packMeshToBuffer(const core::PrimitiveMesh &meshData)
/**********************************************************/
{
  // 1. Calculate Sizes in Bytes
  size_t vSize = meshData.vertices.size() * sizeof(core::PrimitiveVertex);
  size_t iSize = meshData.triangles.size() * sizeof(core::PrimitiveTriangle);
  size_t totalSize = vSize + iSize;

  // 2. Allocate Staging Buffer
  std::vector<uint8_t> buffer(totalSize);

  // 3. Copy Data
  // Copy Vertices to the beginning (Offset 0)
  if (vSize > 0) {
    std::memcpy(buffer.data(), meshData.vertices.data(), vSize);
  }

  // Copy Indices immediately after Vertices (Offset vSize)
  if (iSize > 0) {
    std::memcpy(buffer.data() + vSize, meshData.triangles.data(), iSize);
  }

  return buffer;
}

} // namespace obj
