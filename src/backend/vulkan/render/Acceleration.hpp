#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>  // Required for m_asBuilder (by value)

// Forward Declarations
namespace gltf
{
struct Scene;
}
namespace shaderio
{
struct GltfMesh;
}
class VulkanContextManager;

class AccelerationStructures
{
public:
  void init(VulkanContextManager* backend);
  void deinit();

  void buildBLAS(const gltf::Scene& scene);
  void buildTLAS(const gltf::Scene& scene);

  nvvk::AccelerationStructure tlas() const;

private:
  nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& mesh);
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
