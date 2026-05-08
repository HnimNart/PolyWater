#include "optimizer.hpp"

#include <meshoptimizer.h>

#include <fstream>
#include <iostream>

#include "core/logger.hpp"
#include "core/shape/primitives.hpp"
#include "core/timers.hpp"
#include "gltf_utils.hpp"
#include "rhi_definitions.hpp"

namespace
{

struct TempMesh
{
  std::vector<uint32_t> indices;
  std::vector<uint8_t> vertexData;  // raw interleaved bytes!

  size_t vertexStride = 0;    // Dynamic size of a single vertex
  size_t positionOffset = 0;  // Where the position starts (usually 0)

  bool hasNormals = false;
  bool hasUVs = false;
  bool hasTangents = false;
  bool hasColors = false;
};

struct MeshletData
{
  std::vector<shaderio::GPUMeshlet> meshlets;
  std::vector<uint32_t>
      meshletVertices;  // Maps meshlet local vertices to global Vertex Buffer
  std::vector<uint8_t>
      meshletTriangles;  // Local indices (0-63) forming triangles
};

// --- Helper Functions ---

/**********************************************************/
shaderio::BoundingBox calculateBBox(const TempMesh& tm)
/**********************************************************/
{
  shaderio::BoundingBox bbox;
  size_t vCount = tm.vertexData.size() / tm.vertexStride;

  for (size_t i = 0; i < vCount; ++i)
  {
    glm::vec3 pos;
    std::memcpy(&pos, &tm.vertexData[i * tm.vertexStride + tm.positionOffset],
                sizeof(glm::vec3));
    bbox.add(pos);
  }
  return bbox;
}

// --- Shared Optimization Logic ---

/**********************************************************/
void optimizeVertexData(TempMesh& tm)
/**********************************************************/
{
  size_t indexCount = tm.indices.size();
  size_t vertexCount = tm.vertexData.size() / tm.vertexStride;

  std::vector<uint32_t> remap(indexCount);
  size_t totalVertices = meshopt_generateVertexRemap(
      remap.data(), tm.indices.data(), indexCount, tm.vertexData.data(),
      vertexCount, tm.vertexStride);

  std::vector<uint8_t> optimizedVertices(totalVertices * tm.vertexStride);
  std::vector<uint32_t> optimizedIndices(indexCount);

  meshopt_remapIndexBuffer(optimizedIndices.data(), tm.indices.data(),
                           indexCount, remap.data());
  meshopt_remapVertexBuffer(optimizedVertices.data(), tm.vertexData.data(),
                            vertexCount, tm.vertexStride, remap.data());

  meshopt_optimizeVertexCache(optimizedIndices.data(), optimizedIndices.data(),
                              indexCount, totalVertices);

  float* positionPtr =
      reinterpret_cast<float*>(&optimizedVertices[tm.positionOffset]);

  meshopt_optimizeOverdraw(optimizedIndices.data(), optimizedIndices.data(),
                           indexCount, positionPtr, totalVertices,
                           tm.vertexStride, 1.05f);

  meshopt_optimizeVertexFetch(optimizedVertices.data(), optimizedIndices.data(),
                              indexCount, optimizedVertices.data(),
                              totalVertices, tm.vertexStride);

  tm.indices = std::move(optimizedIndices);
  tm.vertexData = std::move(optimizedVertices);
}

/**********************************************************/
MeshletData buildMeshlets(const TempMesh& tm)
/**********************************************************/
{
  MeshletData result;

  // If the mesh has no indices, it cannot generate meshlets
  if (tm.indices.empty())
  {
    return result;
  }

  const size_t max_vertices = 64;
  const size_t max_triangles = 124;
  const float cone_weight = 0.0f;

  size_t index_count = tm.indices.size();
  size_t vertex_count = tm.vertexData.size() / tm.vertexStride;
  const float* positions =
      reinterpret_cast<const float*>(&tm.vertexData[tm.positionOffset]);

  size_t max_meshlets =
      meshopt_buildMeshletsBound(index_count, max_vertices, max_triangles);

  std::vector<meshopt_Meshlet> localMeshlets(max_meshlets);
  std::vector<unsigned int> localVertices(max_meshlets * max_vertices);
  std::vector<unsigned char> localTriangles(max_meshlets * max_triangles * 3);

  size_t meshlet_count = meshopt_buildMeshlets(
      localMeshlets.data(), localVertices.data(), localTriangles.data(),
      tm.indices.data(), index_count, positions, vertex_count, tm.vertexStride,
      max_vertices, max_triangles, cone_weight);

  // Guard against meshoptimizer returning 0 meshlets ---
  if (meshlet_count == 0)
  {
    return result;
  }

  const meshopt_Meshlet& last = localMeshlets[meshlet_count - 1];
  localVertices.resize(last.vertex_offset + last.vertex_count);
  localTriangles.resize(last.triangle_offset +
                        ((last.triangle_count * 3 + 3) & ~3));

  result.meshlets.reserve(meshlet_count);
  for (size_t i = 0; i < meshlet_count; ++i)
  {
    shaderio::GPUMeshlet m;
    m.vertexOffset = localMeshlets[i].vertex_offset;
    m.triangleOffset = localMeshlets[i].triangle_offset;
    m.vertexCount = localMeshlets[i].vertex_count;
    m.triangleCount = localMeshlets[i].triangle_count;

    meshopt_Bounds bounds = meshopt_computeMeshletBounds(
        &localVertices[m.vertexOffset], &localTriangles[m.triangleOffset],
        m.triangleCount, positions, vertex_count, tm.vertexStride);

    // Assign to your GPUMeshlet struct
    m.center = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
    m.radius = bounds.radius;
    // Optional: For normal-based backface culling (Cone Culling)
    m.coneAxis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1],
                           bounds.cone_axis[2]);
    m.coneCutoff = bounds.cone_cutoff;
    result.meshlets.push_back(m);
  }

  result.meshletVertices = std::move(localVertices);
  result.meshletTriangles = std::move(localTriangles);

  return result;
}

// --- Shared Packing Logic ---

/**********************************************************/
void packUniversalPayload(OptimizedPayload& payload, const TempMesh& tm,
                          const MeshletData& mData)
/**********************************************************/
{
  shaderio::MeshPrimitive prim = {};
  prim.bbox = calculateBBox(tm);

  // We are storing standard 32-bit indices for the fallback/shadow pipeline
  prim.indexType = IndexType32;

  uint32_t vCount =
      static_cast<uint32_t>(tm.vertexData.size() / tm.vertexStride);
  uint32_t stride = static_cast<uint32_t>(tm.vertexStride);

  // Helper lambda to pad the rawBuffer to a 4-byte boundary
  auto alignBuffer = [&]()
  {
    size_t remainder = payload.rawBuffer.size() % 4;
    if (remainder != 0)
    {
      size_t padding = 4 - remainder;
      payload.rawBuffer.insert(payload.rawBuffer.end(), padding, 0);
    }
  };

  // ---------------------------------------------------------
  // 1. Pack Interleaved Vertex Data (Shared)
  // ---------------------------------------------------------
  uint32_t currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());
  payload.rawBuffer.insert(payload.rawBuffer.end(), tm.vertexData.begin(),
                           tm.vertexData.end());

  uint32_t offset = 0;
  prim.triMesh.positions = {currentOffset + offset, vCount, stride};
  offset += sizeof(glm::vec3);

  if (tm.hasNormals)
  {
    prim.triMesh.normals = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec3);
  }
  else
  {
    prim.triMesh.normals = {0, 0, 0};
  }

  if (tm.hasUVs)
  {
    prim.triMesh.texCoords = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec2);
  }
  else
  {
    prim.triMesh.texCoords = {0, 0, 0};
  }

  if (tm.hasTangents)
  {
    prim.triMesh.tangents = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec4);
  }
  else
  {
    prim.triMesh.tangents = {0, 0, 0};
  }

  if (tm.hasColors)
  {
    prim.triMesh.colorVert = {currentOffset + offset, vCount, stride};
  }
  else
  {
    prim.triMesh.colorVert = {0, 0, 0};
  }

  alignBuffer();

  // ---------------------------------------------------------
  // 2. Pack Traditional Triangle Indices
  // ---------------------------------------------------------
  currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());
  size_t iBytes = tm.indices.size() * sizeof(uint32_t);
  const uint8_t* iData = reinterpret_cast<const uint8_t*>(tm.indices.data());

  payload.rawBuffer.insert(payload.rawBuffer.end(), iData, iData + iBytes);

  prim.triMesh.indices = {
      currentOffset, static_cast<uint32_t>(tm.indices.size()),
      0  // 0 byteStride indicates tightly packed indices
  };

  alignBuffer();

  // ---------------------------------------------------------
  // 3. Pack Meshlet Structures (GPUMeshlet)
  // ---------------------------------------------------------
  currentOffset =
      static_cast<uint32_t>(payload.rawBuffer.size());  // Fixed: was +=
  size_t meshletsByteSize =
      mData.meshlets.size() * sizeof(shaderio::GPUMeshlet);
  const uint8_t* mDataPtr =
      reinterpret_cast<const uint8_t*>(mData.meshlets.data());

  payload.rawBuffer.insert(payload.rawBuffer.end(), mDataPtr,
                           mDataPtr + meshletsByteSize);

  prim.meshlet.meshlets = {currentOffset,
                           static_cast<uint32_t>(mData.meshlets.size()),
                           sizeof(shaderio::GPUMeshlet)};

  alignBuffer();

  // ---------------------------------------------------------
  // 4. Pack Meshlet Vertices (Global vertex indices)
  // ---------------------------------------------------------
  currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());
  size_t mvByteSize = mData.meshletVertices.size() * sizeof(uint32_t);
  const uint8_t* mvDataPtr =
      reinterpret_cast<const uint8_t*>(mData.meshletVertices.data());

  payload.rawBuffer.insert(payload.rawBuffer.end(), mvDataPtr,
                           mvDataPtr + mvByteSize);

  prim.meshlet.meshletVertices = {
      currentOffset, static_cast<uint32_t>(mData.meshletVertices.size()),
      sizeof(uint32_t)};

  alignBuffer();

  // ---------------------------------------------------------
  // 5. Pack Meshlet Triangles (Local 8-bit indices)
  // ---------------------------------------------------------
  currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());
  size_t mtByteSize = mData.meshletTriangles.size() * sizeof(uint8_t);
  const uint8_t* mtDataPtr =
      reinterpret_cast<const uint8_t*>(mData.meshletTriangles.data());

  payload.rawBuffer.insert(payload.rawBuffer.end(), mtDataPtr,
                           mtDataPtr + mtByteSize);

  prim.meshlet.meshletTriangles = {
      currentOffset, static_cast<uint32_t>(mData.meshletTriangles.size()),
      sizeof(uint8_t)  // Tightly packed 1-byte stride
  };

  alignBuffer();

  // printMeshletBufferView("test", prim);

  // Finally, add the fully packed primitive to the payload
  payload.primitives.push_back(prim);
}

/**********************************************************/
void packIntoPayload(OptimizedPayload& payload, const TempMesh& tm)
/**********************************************************/
{
  shaderio::MeshPrimitive prim = {};
  prim.bbox = calculateBBox(tm);
  prim.indexType = IndexType32;

  uint32_t currentOffset = static_cast<uint32_t>(payload.rawBuffer.size());
  uint32_t vCount =
      static_cast<uint32_t>(tm.vertexData.size() / tm.vertexStride);
  uint32_t stride = static_cast<uint32_t>(tm.vertexStride);

  // Copy the interleaved raw bytes directly into the buffer!
  payload.rawBuffer.insert(payload.rawBuffer.end(), tm.vertexData.begin(),
                           tm.vertexData.end());

  // Setup accessors dynamically based on what we packed
  uint32_t offset = 0;

  prim.triMesh.positions = {currentOffset + offset, vCount, stride};
  offset += sizeof(glm::vec3);

  if (tm.hasNormals)
  {
    prim.triMesh.normals = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec3);
  }
  else
    prim.triMesh.normals = {0, 0, 0};

  if (tm.hasUVs)
  {
    prim.triMesh.texCoords = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec2);
  }
  else
    prim.triMesh.texCoords = {0, 0, 0};

  if (tm.hasTangents)
  {
    prim.triMesh.tangents = {currentOffset + offset, vCount, stride};
    offset += sizeof(glm::vec4);
  }
  else
    prim.triMesh.tangents = {0, 0, 0};

  if (tm.hasColors)
  {
    prim.triMesh.colorVert = {currentOffset + offset, vCount, stride};
  }
  else
    prim.triMesh.colorVert = {0, 0, 0};

  currentOffset += static_cast<uint32_t>(tm.vertexData.size());

  // 3. Pack Indices
  size_t iBytes = tm.indices.size() * sizeof(uint32_t);
  const uint8_t* iData = reinterpret_cast<const uint8_t*>(tm.indices.data());
  payload.rawBuffer.insert(payload.rawBuffer.end(), iData, iData + iBytes);

  prim.triMesh.indices = {currentOffset,
                          static_cast<uint32_t>(tm.indices.size()),
                          0};  // 0 stride for tightly packed indices

  payload.primitives.push_back(prim);
}

/**********************************************************/
void extractFromObjPrimitive(const core::PrimitiveMesh& objMesh, TempMesh& tm)
/**********************************************************/
{
  tm.indices.resize(objMesh.triangles.size() * 3);
  for (size_t i = 0; i < objMesh.triangles.size(); ++i)
  {
    tm.indices[i * 3 + 0] = objMesh.triangles[i].indices.x;
    tm.indices[i * 3 + 1] = objMesh.triangles[i].indices.y;
    tm.indices[i * 3 + 2] = objMesh.triangles[i].indices.z;
  }

  tm.hasNormals = true;
  tm.hasUVs = true;
  tm.hasTangents = false;
  tm.hasColors = false;

  tm.positionOffset = 0;
  tm.vertexStride = sizeof(glm::vec3) + sizeof(glm::vec3) +
                    sizeof(glm::vec2);  // Pos + Norm + UV = 32 bytes

  size_t vertexCount = objMesh.vertices.size();
  tm.vertexData.resize(vertexCount * tm.vertexStride);

  uint8_t* vOut = tm.vertexData.data();
  for (size_t i = 0; i < vertexCount; ++i)
  {
    const auto& v = objMesh.vertices[i];
    std::memcpy(vOut, &v.pos, sizeof(glm::vec3));
    vOut += sizeof(glm::vec3);
    std::memcpy(vOut, &v.nrm, sizeof(glm::vec3));
    vOut += sizeof(glm::vec3);
    std::memcpy(vOut, &v.tex, sizeof(glm::vec2));
    vOut += sizeof(glm::vec2);
  }
}

/**********************************************************/
void extractAndOptimizePrimitive(const tinygltf::Model& model,
                                 const tinygltf::Primitive& primitive,
                                 TempMesh& tm)
/**********************************************************/
{
  if (primitive.indices >= 0)
  {
    const tinygltf::Accessor& indexAccessor =
        model.accessors[primitive.indices];
    const tinygltf::BufferView& bufferView =
        model.bufferViews[indexAccessor.bufferView];
    const uint8_t* dataPtr = model.buffers[bufferView.buffer].data.data() +
                             bufferView.byteOffset + indexAccessor.byteOffset;
    size_t count = indexAccessor.count;

    tm.indices.resize(count);
    if (indexAccessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT)
    {
      for (size_t i = 0; i < count; ++i)
        tm.indices[i] = reinterpret_cast<const uint16_t*>(dataPtr)[i];
    }
    else if (indexAccessor.componentType ==
             TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT)
    {
      for (size_t i = 0; i < count; ++i)
        tm.indices[i] = reinterpret_cast<const uint32_t*>(dataPtr)[i];
    }
    else
    {
      for (size_t i = 0; i < count; ++i)
        tm.indices[i] = reinterpret_cast<const uint8_t*>(dataPtr)[i];
    }
  }

  const uint8_t *posPtr = nullptr, *normPtr = nullptr, *uvPtr = nullptr,
                *tanPtr = nullptr, *colPtr = nullptr;
  size_t posStride = 0, normStride = 0, uvStride = 0, tanStride = 0,
         colStride = 0, vertexCount = 0;

  if (!scene::getGltfAttribute<glm::vec3>(model, primitive, "POSITION", posPtr,
                                         posStride, vertexCount))
  {
    return;
  }

  tm.hasNormals = scene::getGltfAttribute<glm::vec3>(
      model, primitive, "NORMAL", normPtr, normStride, vertexCount);
  tm.hasUVs = scene::getGltfAttribute<glm::vec2>(model, primitive, "TEXCOORD_0",
                                                uvPtr, uvStride, vertexCount);
  tm.hasTangents = scene::getGltfAttribute<glm::vec4>(
      model, primitive, "TANGENT", tanPtr, tanStride, vertexCount);
  tm.hasColors = scene::getGltfAttribute<glm::vec4>(
      model, primitive, "COLOR_0", colPtr, colStride, vertexCount);

  // Dynamically calculate stride based on what attributes exist
  tm.positionOffset = 0;
  tm.vertexStride = sizeof(glm::vec3);
  if (tm.hasNormals)
    tm.vertexStride += sizeof(glm::vec3);
  if (tm.hasUVs)
    tm.vertexStride += sizeof(glm::vec2);
  if (tm.hasTangents)
    tm.vertexStride += sizeof(glm::vec4);
  if (tm.hasColors)
    tm.vertexStride += sizeof(glm::vec4);

  tm.vertexData.resize(vertexCount * tm.vertexStride);
  uint8_t* vOut = tm.vertexData.data();

  for (size_t i = 0; i < vertexCount; ++i)
  {
    glm::vec3 pos =
        *reinterpret_cast<const glm::vec3*>(posPtr + (i * posStride));
    std::memcpy(vOut, &pos, sizeof(glm::vec3));
    vOut += sizeof(glm::vec3);

    if (tm.hasNormals)
    {
      glm::vec3 norm =
          *reinterpret_cast<const glm::vec3*>(normPtr + (i * normStride));
      std::memcpy(vOut, &norm, sizeof(glm::vec3));
      vOut += sizeof(glm::vec3);
    }
    if (tm.hasUVs)
    {
      glm::vec2 uv =
          *reinterpret_cast<const glm::vec2*>(uvPtr + (i * uvStride));
      std::memcpy(vOut, &uv, sizeof(glm::vec2));
      vOut += sizeof(glm::vec2);
    }
    if (tm.hasTangents)
    {
      glm::vec4 tan =
          *reinterpret_cast<const glm::vec4*>(tanPtr + (i * tanStride));
      std::memcpy(vOut, &tan, sizeof(glm::vec4));
      vOut += sizeof(glm::vec4);
    }
    if (tm.hasColors)
    {
      glm::vec4 col =
          *reinterpret_cast<const glm::vec4*>(colPtr + (i * colStride));
      std::memcpy(vOut, &col, sizeof(glm::vec4));
      vOut += sizeof(glm::vec4);
    }
  }
}

// --- Caching ---

/**********************************************************/
bool loadMeshCache(const std::filesystem::path& filepath, TempMesh& tm)
/**********************************************************/
{
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open())
    return false;

  uint32_t flags = 0, vCount = 0, iCount = 0;
  uint32_t stride = 0, posOffset = 0;

  file.read((char*) &flags, sizeof(uint32_t));
  file.read((char*) &stride, sizeof(uint32_t));
  file.read((char*) &posOffset, sizeof(uint32_t));
  file.read((char*) &vCount, sizeof(uint32_t));
  file.read((char*) &iCount, sizeof(uint32_t));

  tm.hasNormals = (flags & 1) != 0;
  tm.hasUVs = (flags & 2) != 0;
  tm.hasTangents = (flags & 4) != 0;
  tm.hasColors = (flags & 8) != 0;

  tm.vertexStride = stride;
  tm.positionOffset = posOffset;

  tm.vertexData.resize(vCount * stride);
  tm.indices.resize(iCount);
  file.read((char*) tm.vertexData.data(), vCount * stride);
  file.read((char*) tm.indices.data(), iCount * sizeof(uint32_t));

  LOGD("Loaded cached mesh %s (Vertices %zu - Indices %zu)\n",
       filepath.filename().c_str(), tm.vertexData.size(), tm.indices.size());
  return true;
}

/**********************************************************/
void saveMeshCache(const std::filesystem::path& filepath, const TempMesh& tm)
/**********************************************************/
{
  if (filepath.has_parent_path())
  {
    std::error_code ec;
    std::filesystem::create_directories(filepath.parent_path(), ec);
    if (ec)
    {
      LOGE("Failed to create cache directory: %s", ec.message().c_str());
      return;
    }
  }

  std::ofstream file(filepath, std::ios::binary);
  if (!file.is_open())
  {
    LOGE("Failed to open cache file for writing: %s",
         filepath.string().c_str());
    return;
  }

  uint32_t flags = (tm.hasNormals ? 1 : 0) | (tm.hasUVs ? 2 : 0) |
                   (tm.hasTangents ? 4 : 0) | (tm.hasColors ? 8 : 0);

  uint32_t stride = static_cast<uint32_t>(tm.vertexStride);
  uint32_t posOffset = static_cast<uint32_t>(tm.positionOffset);

  uint32_t vCount = (uint32_t) (tm.vertexData.size() / tm.vertexStride);
  uint32_t iCount = (uint32_t) tm.indices.size();

  file.write((char*) &flags, sizeof(uint32_t));
  file.write((char*) &stride, sizeof(uint32_t));
  file.write((char*) &posOffset, sizeof(uint32_t));
  file.write((char*) &vCount, sizeof(uint32_t));
  file.write((char*) &iCount, sizeof(uint32_t));

  file.write((char*) tm.vertexData.data(), vCount * stride);
  file.write((char*) tm.indices.data(), iCount * sizeof(uint32_t));
}

}  // namespace


namespace scene
{

// --- Public API ---

/**********************************************************/
OptimizedPayload processAndOptimizeGltf(const std::string& name,
                                        const tinygltf::Model& model,
                                        const std::filesystem::path& cachePath)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  OptimizedPayload payload;
  size_t origV = 0, optV = 0;
  size_t totalBytesSaved = 0;

  int primitiveId = 0;
  for (const auto& mesh : model.meshes)
  {
    for (const auto& primitive : mesh.primitives)
    {
      std::filesystem::path cacheFile =
          cachePath / (name + std::to_string(primitiveId++) + ".meshcache");

      TempMesh tm;
      // Load or build the optimized shared vertex data and indices
      if (!loadMeshCache(cacheFile, tm))
      {
        extractAndOptimizePrimitive(model, primitive, tm);
        optimizeVertexData(tm);
        saveMeshCache(cacheFile, tm);
      }

      if (tm.indices.size() < 3 || tm.indices.size() % 3 != 0)
      {
        LOGW("Skipping GLTF primitive in '%s': Invalid triangle index count "
             "(%zu).",
             name.c_str(), tm.indices.size());
        continue;
      }

      auto posIt = primitive.attributes.find("POSITION");
      if (posIt != primitive.attributes.end())
      {
        size_t rawCount = model.accessors[posIt->second].count;
        origV += rawCount;

        size_t newCount = tm.vertexData.size() / tm.vertexStride;
        optV += newCount;

        // Calculate bytes saved using our new tightly packed stride!
        totalBytesSaved += (rawCount - newCount) * tm.vertexStride;
      }

      // generate meshlets from the optimized geometry
      MeshletData mData = buildMeshlets(tm);
      if (mData.meshlets.empty())
      {
        LOGW("Skipping Gltf primitive %s in '%s': Optimizer produced 0 "
             "meshlets.\n",
             mesh.name.c_str(), name.c_str());
        continue;
      }
      packUniversalPayload(payload, tm, mData);
    }
  }

  if (origV > 0)
  {
    double savedMB = static_cast<double>(totalBytesSaved) / (1024.0 * 1024.0);
    double percentSaved = (1.0 - static_cast<double>(optV) / origV) * 100.0;

    LOGD("--- GLTF Optimizer Stats for %s ---", name.c_str());
    LOGD("Output Mode:        Universal (Triangles + Meshlets)");
    LOGD("Original Vertices:  %zu", origV);
    LOGD("Optimized Vertices: %zu", optV);
    LOGD("Memory Saved:       %.2f MB (%.1f%% reduction)", savedMB,
         percentSaved);
    LOGD("-----------------------------------");
  }
  return payload;
}

/**********************************************************/
OptimizedPayload
processAndOptimizeObj(const std::string& name,
                      const std::vector<ObjMesh>& loadedMeshes,
                      const std::filesystem::path& cachePath)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  OptimizedPayload payload;
  size_t origV = 0, optV = 0;
  size_t totalBytesSaved = 0;

  for (size_t i = 0; i < loadedMeshes.size(); ++i)
  {
    std::filesystem::path cacheFile =
        cachePath / (name + "_obj_p" + std::to_string(i) + ".meshcache");

    TempMesh tm;
    const core::PrimitiveMesh& rawMesh = loadedMeshes[i].mesh;

    size_t rawCount = rawMesh.vertices.size();
    origV += rawCount;

    // Load or build the optimized shared vertex data and indices
    if (!loadMeshCache(cacheFile, tm))
    {
      extractFromObjPrimitive(rawMesh, tm);
      optimizeVertexData(tm);
      saveMeshCache(cacheFile, tm);
    }

    // Skip empty or invalid meshes before generating meshlets
    if (tm.indices.size() < 3 || tm.indices.size() % 3 != 0)
    {
      LOGW("Skipping OBJ primitive %zu in '%s': Invalid triangle index count "
           "(%zu).",
           i, name.c_str(), tm.indices.size());
      continue;
    }

    size_t newCount = tm.vertexData.size() / tm.vertexStride;
    optV += newCount;
    totalBytesSaved += (rawCount - newCount) * tm.vertexStride;

    // generate meshlets from the optimized geometry
    MeshletData mData = buildMeshlets(tm);
    if (mData.meshlets.empty())
    {
      LOGW("Skipping OBJ primitive %zu in '%s': Optimizer produced 0 "
           "meshlets.\n",
           i, name.c_str());
      continue;
    }
    packUniversalPayload(payload, tm, mData);
  }

  if (origV > 0)
  {
    double savedMB = static_cast<double>(totalBytesSaved) / (1024.0 * 1024.0);
    double percentSaved = (static_cast<double>(origV - optV) / origV) * 100.0;

    LOGD("--- OBJ Optimizer Stats for %s ---", name.c_str());
    LOGD("Output Mode:        Universal (Triangles + Meshlets)");
    LOGD("Original Vertices:  %zu", origV);
    LOGD("Optimized Vertices: %zu", optV);
    LOGD("Memory Saved:       %.2f MB (%.1f%% reduction)", savedMB,
         percentSaved);
    LOGD("-----------------------------------");
  }

  return payload;
}

}  // namespace scene
