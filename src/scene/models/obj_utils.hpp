#pragma once

#include <tinyobjloader/tiny_obj_loader.h>

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/shape/primitives.hpp"
#include "shaders/shared/structs.h"

namespace scene
{

struct ObjMaterial
{
  std::string name;                // Material name for debugging
  shaderio::Material pbrData;      // The actual struct for the GPU
  std::string diffuseTexturePath;  // Filename for loading later
};

struct ObjMesh
{
  std::string name;
  core::PrimitiveMesh mesh;
  int materialIndex;
};

struct ObjLoaderResult
{
  std::vector<ObjMesh> meshes;
  std::vector<ObjMaterial> materials;
};

// --- Function Declarations ---

core::PrimitiveMesh flattenObjData(const tinyobj::attrib_t& attrib,
                                   const std::vector<tinyobj::shape_t>& shapes);

ObjLoaderResult loadObjPrimitives(const std::string& filename);

shaderio::MeshPrimitive
createGpuMeshFromPrimitive(const core::PrimitiveMesh& meshData);

shaderio::BoundingBox computeMeshBounds(const core::PrimitiveMesh& mesh);

std::vector<uint8_t> packMeshToBuffer(const core::PrimitiveMesh& meshData);

}  // namespace scene
