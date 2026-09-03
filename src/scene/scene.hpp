#pragma once

#include <vector>

#include <backend/interfaces/rhi_definitions.hpp>

#include "shaders/shared/structs.h"


namespace scene
{

struct Scene
{
  std::vector<std::vector<uint8_t>> meshData{};
  std::vector<shaderio::MeshPrimitive> meshes{};  // All meshes in the scene
  std::vector<shaderio::Instance> instances;      // All instances in the scene
  std::vector<shaderio::Material> materials;      // All materials in the scene
  shaderio::SceneInfo sceneInfo;                  // Camera, lights etc.
  shaderio::SceneResources sceneResources;  // Device resources of the scene

  // Misc
  float radius = 100.0f;                            // Scene Radius
  float crossSectionArea = M_PI * radius * radius;  //
};

/**********************************************************/
template <typename T>
inline T getAttribute(const shaderio::MeshPrimitive& mesh,
                      const uint8_t* buffer, uint32_t vertexID)
/**********************************************************/
{
  const auto& view = mesh.triMesh.positions;
  uint32_t stride = view.byteStride == 0 ? sizeof(T) : view.byteStride;
  const uint8_t* ptr = buffer + view.offset + (vertexID * stride);
  return *reinterpret_cast<const T*>(ptr);
}

/**********************************************************/
inline glm::uvec3 getTriangleIndices(const shaderio::MeshPrimitive& mesh,
                                     const uint8_t* buffer,
                                     uint32_t primitiveID)
/**********************************************************/
{
  const auto& view = mesh.triMesh.indices;
  bool is32Bit = (mesh.indexType == IndexType32);
  uint32_t elementSize = is32Bit ? 4 : 2;
  uint32_t stride = view.byteStride == 0 ? elementSize : view.byteStride;
  const uint8_t* ptr = buffer + view.offset + (primitiveID * 3 * stride);

  if (is32Bit)
  {
    const uint32_t* iPtr = reinterpret_cast<const uint32_t*>(ptr);
    if (view.byteStride == 0 || view.byteStride == 4)
    {
      return glm::uvec3(iPtr[0], iPtr[1], iPtr[2]);
    }
    else
    {
      return glm::uvec3(*reinterpret_cast<const uint32_t*>(ptr + 0 * stride),
                        *reinterpret_cast<const uint32_t*>(ptr + 1 * stride),
                        *reinterpret_cast<const uint32_t*>(ptr + 2 * stride));
    }
  }
  else
  {
    const uint16_t* iPtr = reinterpret_cast<const uint16_t*>(ptr);
    if (view.byteStride == 0 || view.byteStride == 2)
    {
      return glm::uvec3(iPtr[0], iPtr[1], iPtr[2]);
    }
    else
    {
      return glm::uvec3(*reinterpret_cast<const uint16_t*>(ptr + 0 * stride),
                        *reinterpret_cast<const uint16_t*>(ptr + 1 * stride),
                        *reinterpret_cast<const uint16_t*>(ptr + 2 * stride));
    }
  }
}

}  // namespace scene
