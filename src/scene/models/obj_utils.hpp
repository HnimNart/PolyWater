#pragma once

#include <glm/glm.hpp>
#include <string>
#include <tinyobjloader/tiny_obj_loader.h>
#include <vector>

#include "shaders/shared/structs.h"
#include "core/shape/primitives.hpp"

namespace obj {

struct TempMaterial {
  shaderio::Material pbrData;     // The actual struct for the GPU
  std::string diffuseTexturePath; // Filename for loading later
  std::string name;               // Material name for debugging
};

struct LoadedMesh {
  std::string name;
  core::PrimitiveMesh mesh;
  int materialIndex;
};

struct ObjLoaderResult {
  std::vector<LoadedMesh> meshes;
  std::vector<TempMaterial> materials;
};

// --- Function Declarations ---

core::PrimitiveMesh flattenObjData(const tinyobj::attrib_t &attrib,
                                   const std::vector<tinyobj::shape_t> &shapes);

ObjLoaderResult loadObjPrimitives(const std::string &filename);

shaderio::MeshPrimitive
createGpuMeshFromPrimitive(const core::PrimitiveMesh &meshData);

shaderio::BoundingBox computeMeshBounds(const core::PrimitiveMesh &mesh);

std::vector<uint8_t> packMeshToBuffer(const core::PrimitiveMesh &meshData);

} // namespace obj
