#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>  // Required for m_asBuilder (by value)

// Forward Declarations
struct VulkanContext;
namespace gltf
{
struct Scene;
}
namespace shaderio
{
struct GltfMesh;
}
namespace core
{
class VulkanBackend;
}

class AccelerationStructures
{
public:
  void init(core::VulkanBackend* backend);
  void deinit();

  void buildBLAS(const gltf::Scene& scene);
  void buildTLAS(const gltf::Scene& scene);

  nvvk::AccelerationStructure tlas() const;

private:
  nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& mesh);

private:
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
