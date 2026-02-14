#include "obj_utils.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace obj {

/**********************************************************/
core::PrimitiveMesh flattenObjData(const tinyobj::attrib_t &attrib,
                                   const std::vector<tinyobj::shape_t> &shapes)
/**********************************************************/
{
  core::PrimitiveMesh result;
  std::unordered_map<core::PrimitiveVertex, uint32_t> uniqueVertices;

  for (const auto &shape : shapes) {
    size_t index_offset = 0;
    for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
      int fv = shape.mesh.num_face_vertices[f];
      if (fv != 3) {
        index_offset += fv;
        continue;
      }

      core::PrimitiveTriangle triangle;
      for (size_t v = 0; v < 3; v++) {
        tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
        core::PrimitiveVertex vertex{};

        vertex.pos = {attrib.vertices[3 * idx.vertex_index + 0],
                      attrib.vertices[3 * idx.vertex_index + 1],
                      attrib.vertices[3 * idx.vertex_index + 2]};

        if (idx.normal_index >= 0) {
          vertex.nrm = {attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]};
        }

        if (idx.texcoord_index >= 0) {
          vertex.tex = {attrib.texcoords[2 * idx.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
        }

        if (uniqueVertices.count(vertex) == 0) {
          uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
          uniqueVertices[vertex] = newIndex;
          result.vertices.push_back(vertex);
        }
        triangle.indices[v] = uniqueVertices[vertex];
      }
      result.triangles.push_back(triangle);
      index_offset += fv;
    }
  }
  return result;
}

/**********************************************************/
ObjLoaderResult loadObjPrimitives(const std::string &filename)
/**********************************************************/
{
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  std::string baseDir = filename.substr(0, filename.find_last_of("/\\") + 1);
  bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              filename.c_str(), baseDir.c_str());

  if (!warn.empty())
    LOGD("OBJ Warning: %s\n", warn.c_str());
  if (!err.empty())
    LOGE("OBJ Error: %s\n", err.c_str());
  if (!ret) {
    LOGE("Failed to load OBJ file: %s\n", filename.c_str());
    return {};
  }

  ObjLoaderResult result;
  for (const auto &tm : materials) {
    TempMaterial temp;
    temp.name = tm.name;
    temp.diffuseTexturePath = tm.diffuse_texname;

    temp.pbrData.baseColorFactor = {tm.diffuse[0], tm.diffuse[1], tm.diffuse[2],
                                    1.0f};
    temp.pbrData.emission = {tm.emission[0], tm.emission[1], tm.emission[2]};
    temp.pbrData.ior = {tm.ior, tm.ior, tm.ior};

    float shininess = glm::clamp(tm.shininess, 0.0f, 1000.0f);
    temp.pbrData.roughnessFactor = 1.0f - (shininess / 1000.0f);
    float spec = (tm.specular[0] + tm.specular[1] + tm.specular[2]) / 3.0f;
    temp.pbrData.metallicFactor = glm::clamp(spec, 0.0f, 1.0f);
    temp.pbrData.baseColorTextureIndex = -1;

    result.materials.push_back(std::move(temp));
  }

  for (const auto &shape : shapes) {
    std::vector<tinyobj::shape_t> singleShapeList = {shape};
    core::PrimitiveMesh meshData = flattenObjData(attrib, singleShapeList);

    int matIdx = -1;
    if (!shape.mesh.material_ids.empty()) {
      matIdx = shape.mesh.material_ids[0];
    }

    result.meshes.push_back({.name = shape.name,
                             .mesh = std::move(meshData),
                             .materialIndex = matIdx});
  }
  return result;
}

/**********************************************************/
shaderio::MeshPrimitive
createGpuMeshFromPrimitive(const core::PrimitiveMesh &meshData)
/**********************************************************/
{
  shaderio::MeshPrimitive gpuMesh = {};
  const uint32_t stride = sizeof(core::PrimitiveVertex);
  size_t vSize = meshData.vertices.size() * sizeof(core::PrimitiveVertex);

  gpuMesh.triMesh.positions = {.offset = 0,
                               .count = (uint32_t)meshData.vertices.size(),
                               .byteStride = stride};
  gpuMesh.triMesh.normals = {.offset =
                                 (uint32_t)offsetof(core::PrimitiveVertex, nrm),
                             .count = (uint32_t)meshData.vertices.size(),
                             .byteStride = stride};
  gpuMesh.triMesh.texCoords = {
      .offset = (uint32_t)offsetof(core::PrimitiveVertex, tex),
      .count = (uint32_t)meshData.vertices.size(),
      .byteStride = stride};
  gpuMesh.triMesh.indices = {.offset = (uint32_t)vSize,
                             .count = (uint32_t)(meshData.triangles.size() * 3),
                             .byteStride = sizeof(uint32_t)};
  gpuMesh.indexType = VK_INDEX_TYPE_UINT32;

  return gpuMesh;
}

/**********************************************************/
std::pair<glm::vec3, glm::vec3>
computeMeshBounds(const core::PrimitiveMesh &mesh)
/**********************************************************/
{
  if (mesh.vertices.empty())
    return {glm::vec3(0.0f), glm::vec3(0.0f)};

  glm::vec3 bmin(std::numeric_limits<float>::max());
  glm::vec3 bmax(std::numeric_limits<float>::lowest());

  for (const auto &vertex : mesh.vertices) {
    bmin = glm::min(bmin, vertex.pos);
    bmax = glm::max(bmax, vertex.pos);
  }
  return {bmin, bmax};
}

/**********************************************************/
std::vector<uint8_t> packMeshToBuffer(const core::PrimitiveMesh &meshData)
/**********************************************************/
{
  size_t vSize = meshData.vertices.size() * sizeof(core::PrimitiveVertex);
  size_t iSize = meshData.triangles.size() * sizeof(core::PrimitiveTriangle);

  std::vector<uint8_t> buffer(vSize + iSize);
  if (vSize > 0)
    std::memcpy(buffer.data(), meshData.vertices.data(), vSize);
  if (iSize > 0)
    std::memcpy(buffer.data() + vSize, meshData.triangles.data(), iSize);

  return buffer;
}

} // namespace obj
