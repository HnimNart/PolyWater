#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>  // Required for m_asBuilder (by value)

// Forward Declarations
struct VulkanContext;
namespace nvsamples
{
struct GltfSceneResource;
}
namespace shaderio
{
struct GltfMesh;
}

class AccelerationStructures
{
public:
  void init(VulkanContext* ctx);
  void deinit();

  void buildBLAS(const nvsamples::GltfSceneResource& scene);
  void buildTLAS(const nvsamples::GltfSceneResource& scene);

  nvvk::AccelerationStructure tlas() const;

private:
  // Helper to convert GltfMesh to BLAS input geometry
  nvvk::AccelerationStructureGeometryInfo primitiveToGeometry(const shaderio::GltfMesh& mesh);

private:
  // Helper to create acceleration structures (held by value)
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
