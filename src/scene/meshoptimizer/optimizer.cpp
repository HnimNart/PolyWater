#include "optimizer.hpp"
#include "RHI_definitions.hpp"

#include <fstream>
#include <iostream>
#include <meshoptimizer.h>

#include "core/logger.hpp"
#include "core/timers.hpp"


#include "core/shape/primitives.hpp"

#include "scene/obj/obj_utils.hpp"

namespace {

struct TempMesh {
  std::vector<uint32_t> indices;
  std::vector<Vertex> vertices;
};

// --- Helper Functions ---

shaderio::BoundingBox calculateBBox(const std::vector<Vertex> &vertices) {
  shaderio::BoundingBox bbox;
  for (const auto &v : vertices) {
    bbox.min = glm::min(bbox.min, v.pos);
    bbox.max = glm::max(bbox.max, v.pos);
  }
  return bbox;
}

template <typename T>
bool getGltfAttribute(const tinygltf::Model &model,
                      const tinygltf::Primitive &primitive,
                      const std::string &attributeName, const uint8_t *&dataPtr,
                      size_t &stride, size_t &count) {
  auto it = primitive.attributes.find(attributeName);
  if (it == primitive.attributes.end())
    return false;

  const tinygltf::Accessor &accessor = model.accessors[it->second];
  const tinygltf::BufferView &bufferView =
      model.bufferViews[accessor.bufferView];
  const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

  dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
  stride = accessor.ByteStride(bufferView);
  count = accessor.count;
  return true;
}

// --- Shared Optimization Logic ---

void optimizeVertexData(std::vector<uint32_t> &indices,
                        std::vector<Vertex> &vertices) {
  size_t indexCount = indices.size();
  size_t vertexCount = vertices.size();

  std::vector<uint32_t> remap(indexCount);
  size_t totalVertices =
      meshopt_generateVertexRemap(remap.data(), indices.data(), indexCount,
                                  vertices.data(), vertexCount, sizeof(Vertex));

  std::vector<Vertex> optimizedVertices(totalVertices);
  std::vector<uint32_t> optimizedIndices(indexCount);

  meshopt_remapIndexBuffer(optimizedIndices.data(), indices.data(), indexCount,
                           remap.data());
  meshopt_remapVertexBuffer(optimizedVertices.data(), vertices.data(),
                            vertexCount, sizeof(Vertex), remap.data());

  meshopt_optimizeVertexCache(optimizedIndices.data(), optimizedIndices.data(),
                              indexCount, totalVertices);
  meshopt_optimizeOverdraw(optimizedIndices.data(), optimizedIndices.data(),
                           indexCount, &optimizedVertices[0].pos.x,
                           totalVertices, sizeof(Vertex), 1.05f);
  meshopt_optimizeVertexFetch(optimizedVertices.data(), optimizedIndices.data(),
                              indexCount, optimizedVertices.data(),
                              totalVertices, sizeof(Vertex));

  indices = std::move(optimizedIndices);
  vertices = std::move(optimizedVertices);
}

// --- Shared Packing Logic ---

void packIntoPayload(OptimizedPayload &payload,
                     const std::vector<uint32_t> &indices,
                     const std::vector<Vertex> &vertices) {
  shaderio::MeshPrimitive prim = {};
  prim.bbox = calculateBBox(vertices);
  prim.indexType = IndexType32;

  uint32_t currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());

  // Pack Vertices
  size_t vBytes = vertices.size() * sizeof(Vertex);
  const uint8_t *vData = reinterpret_cast<const uint8_t *>(vertices.data());
  payload.rawBuffer.insert(payload.rawBuffer.end(), vData, vData + vBytes);

  uint32_t vCount = static_cast<uint32_t>(vertices.size());
  uint32_t stride = sizeof(Vertex);

  prim.triMesh.positions = {currentOffset + (uint32_t)offsetof(Vertex, pos),
                            vCount, stride};
  prim.triMesh.normals = {currentOffset + (uint32_t)offsetof(Vertex, normal),
                          vCount, stride};
  prim.triMesh.texCoords = {currentOffset + (uint32_t)offsetof(Vertex, uv),
                            vCount, stride};
  prim.triMesh.tangents = {currentOffset + (uint32_t)offsetof(Vertex, tangent),
                           vCount, stride};
  prim.triMesh.colorVert = {currentOffset + (uint32_t)offsetof(Vertex, color),
                            vCount, stride};

  currentOffset += (uint32_t)vBytes;

  // Pack Indices
  size_t iBytes = indices.size() * sizeof(uint32_t);
  const uint8_t *iData = reinterpret_cast<const uint8_t *>(indices.data());
  payload.rawBuffer.insert(payload.rawBuffer.end(), iData, iData + iBytes);

  prim.triMesh.indices = {currentOffset, (uint32_t)indices.size(), 0};

  payload.primitives.push_back(prim);
}

void extractAndOptimizePrimitive(const tinygltf::Model &model,
                                 const tinygltf::Primitive &primitive,
                                 std::vector<uint32_t> &indices,
                                 std::vector<Vertex> &vertices) {
  // 1. Extract Indices
  if (primitive.indices >= 0) {
    const tinygltf::Accessor &indexAccessor =
        model.accessors[primitive.indices];
    const tinygltf::BufferView &bufferView =
        model.bufferViews[indexAccessor.bufferView];
    const uint8_t *dataPtr = model.buffers[bufferView.buffer].data.data() +
                             bufferView.byteOffset + indexAccessor.byteOffset;
    size_t count = indexAccessor.count;

    std::vector<uint32_t> rawIndices(count);
    if (indexAccessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT) {
      for (size_t i = 0; i < count; ++i)
        rawIndices[i] = reinterpret_cast<const uint16_t *>(dataPtr)[i];
    } else if (indexAccessor.componentType ==
               TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT) {
      for (size_t i = 0; i < count; ++i)
        rawIndices[i] = reinterpret_cast<const uint32_t *>(dataPtr)[i];
    } else {
      for (size_t i = 0; i < count; ++i)
        rawIndices[i] = reinterpret_cast<const uint8_t *>(dataPtr)[i];
    }
    indices = std::move(rawIndices);
  }

  // 2. Extract Vertices
  const uint8_t *posPtr = nullptr, *normPtr = nullptr, *uvPtr = nullptr;
  size_t posStride = 0, normStride = 0, uvStride = 0, vertexCount = 0;

  if (!getGltfAttribute<glm::vec3>(model, primitive, "POSITION", posPtr,
                                   posStride, vertexCount))
    return;
  bool hasNorm = getGltfAttribute<glm::vec3>(model, primitive, "NORMAL",
                                             normPtr, normStride, vertexCount);
  bool hasUV = getGltfAttribute<glm::vec2>(model, primitive, "TEXCOORD_0",
                                           uvPtr, uvStride, vertexCount);

  std::vector<Vertex> rawVertices(vertexCount);
  for (size_t i = 0; i < vertexCount; ++i) {
    rawVertices[i].pos =
        *reinterpret_cast<const glm::vec3 *>(posPtr + (i * posStride));
    rawVertices[i].normal =
        hasNorm
            ? *reinterpret_cast<const glm::vec3 *>(normPtr + (i * normStride))
            : glm::vec3(0, 1, 0);
    rawVertices[i].uv =
        hasUV ? *reinterpret_cast<const glm::vec2 *>(uvPtr + (i * uvStride))
              : glm::vec2(0, 0);
    rawVertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Fallback
    rawVertices[i].color = glm::vec4(1.0f);                     // Fallback
  }

  // 3. MeshOptimizer Pipeline
  size_t indexCount = indices.size();
  std::vector<uint32_t> remap(indexCount);
  size_t totalVertices = meshopt_generateVertexRemap(
      remap.data(), indices.data(), indexCount, rawVertices.data(), vertexCount,
      sizeof(Vertex));

  vertices.resize(totalVertices);
  meshopt_remapIndexBuffer(indices.data(), indices.data(), indexCount,
                           remap.data());
  meshopt_remapVertexBuffer(vertices.data(), rawVertices.data(), vertexCount,
                            sizeof(Vertex), remap.data());

  meshopt_optimizeVertexCache(indices.data(), indices.data(), indexCount,
                              totalVertices);
  meshopt_optimizeOverdraw(indices.data(), indices.data(), indexCount,
                           &vertices[0].pos.x, totalVertices, sizeof(Vertex),
                           1.05f);
  meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indexCount,
                              vertices.data(), totalVertices, sizeof(Vertex));
}

// --- Caching ---

bool loadMeshCache(const std::filesystem::path &filepath,
                   std::vector<uint32_t> &indices,
                   std::vector<Vertex> &vertices) {
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open())
    return false;
  uint32_t vCount = 0, iCount = 0;
  file.read((char *)&vCount, sizeof(uint32_t));
  file.read((char *)&iCount, sizeof(uint32_t));
  vertices.resize(vCount);
  indices.resize(iCount);
  file.read((char *)vertices.data(), vCount * sizeof(Vertex));
  file.read((char *)indices.data(), iCount * sizeof(uint32_t));
  return true;
}

void saveMeshCache(const std::filesystem::path &filepath,
                   const std::vector<uint32_t> &indices,
                   const std::vector<Vertex> &vertices) {
  std::ofstream file(filepath, std::ios::binary);
  if (!file.is_open())
    return;
  uint32_t vCount = (uint32_t)vertices.size();
  uint32_t iCount = (uint32_t)indices.size();
  file.write((char *)&vCount, sizeof(uint32_t));
  file.write((char *)&iCount, sizeof(uint32_t));
  file.write((char *)vertices.data(), vCount * sizeof(Vertex));
  file.write((char *)indices.data(), iCount * sizeof(uint32_t));
}

} // namespace

// --- Public API ---

void extractFromObjPrimitive(const core::PrimitiveMesh &objMesh,
                             std::vector<uint32_t> &indices,
                             std::vector<Vertex> &vertices) {
  // 1. Flatten the Triangle array into a raw index buffer
  indices.resize(objMesh.triangles.size() * 3);
  for (size_t i = 0; i < objMesh.triangles.size(); ++i) {
    // Assuming PrimitiveTriangle has v1, v2, v3 or v[3]. Adjust if your struct
    // differs.
    indices[i * 3 + 0] = objMesh.triangles[i].indices.x;
    indices[i * 3 + 1] = objMesh.triangles[i].indices.y;
    indices[i * 3 + 2] = objMesh.triangles[i].indices.z;
  }

  // 2. Convert PrimitiveVertex to your unified Vertex struct
  vertices.resize(objMesh.vertices.size());
  for (size_t i = 0; i < objMesh.vertices.size(); ++i) {
    const auto &v = objMesh.vertices[i];
    vertices[i].pos = v.pos;
    vertices[i].normal = v.nrm;
    vertices[i].uv = v.tex;
    vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Fallback
    vertices[i].color = glm::vec4(1.0f);                     // Fallback
  }
}

OptimizedPayload
processAndOptimizeGltf(const std::string &name, const tinygltf::Model &model,
                       const std::filesystem::path &cachePath) {
  SCOPED_TIMER_FUNC();
  OptimizedPayload payload;
  size_t origV = 0, optV = 0;

  int primitiveId = 0;
  for (const auto &mesh : model.meshes) {
    for (const auto &primitive : mesh.primitives) {
      std::filesystem::path cacheFile =
          cachePath / (name + std::to_string(primitiveId++) + ".meshcache");
      std::vector<uint32_t> indices;
      std::vector<Vertex> vertices;

      if (!loadMeshCache(cacheFile, indices, vertices)) {
        extractAndOptimizePrimitive(model, primitive, indices, vertices);
        saveMeshCache(cacheFile, indices, vertices);
      }

      auto posIt = primitive.attributes.find("POSITION");
      if (posIt != primitive.attributes.end())
        origV += model.accessors[posIt->second].count;
      optV += vertices.size();

      packIntoPayload(payload, indices, vertices);
    }
  }

  if (origV > 0) {
    float savedMB = (float)(origV - optV) * sizeof(Vertex) / (1024.f * 1024.f);
    LOGD("--- %s Optimized: %.2f MB saved (%.1f%% reduction) ---\n",
         name.c_str(), savedMB, (1.0f - (float)optV / origV) * 100.f);
  }
  return payload;
}

OptimizedPayload
processAndOptimizeObj(const std::string &name,
                      const std::vector<obj::LoadedMesh> &loadedMeshes,
                      const std::filesystem::path &cachePath) {
  SCOPED_TIMER_FUNC();
  OptimizedPayload payload;
  size_t origV = 0, optV = 0;

  for (size_t i = 0; i < loadedMeshes.size(); ++i) {
    std::filesystem::path cacheFile =
        cachePath / (name + "_obj_p" + std::to_string(i) + ".meshcache");

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    // Grab the raw CPU mesh data from your OBJ loader
    const core::PrimitiveMesh &rawMesh = loadedMeshes[i].mesh;

    // We can confidently add to the original count here because we
    // always have the parsed OBJ data in memory before the GPU upload phase.
    origV += rawMesh.vertices.size();

    if (!loadMeshCache(cacheFile, indices, vertices)) {
      extractFromObjPrimitive(rawMesh, indices, vertices);
      optimizeVertexData(indices, vertices);
      saveMeshCache(cacheFile, indices, vertices);
    }

    optV += vertices.size();
    packIntoPayload(payload, indices, vertices);
  }

  if (origV > 0) {
    double savedBytes = static_cast<double>(origV - optV) * sizeof(Vertex);
    double savedMB = savedBytes / (1024.0 * 1024.0);
    double percentSaved = (static_cast<double>(origV - optV) / origV) * 100.0;

    LOGI("--- OBJ Optimizer Stats for %s ---", name.c_str());
    LOGI("Original Vertices:  %zu", origV);
    LOGI("Optimized Vertices: %zu", optV);
    LOGI("Memory Saved:       %.2f MB (%.1f%% reduction)", savedMB,
         percentSaved);
    LOGI("-----------------------------------");
  }

  return payload;
}
