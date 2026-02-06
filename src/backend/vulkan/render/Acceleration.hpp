#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>  // Required for m_asBuilder (by value)

// Forward Declarations

class SceneResourcesManager;
class ShaderManager;
namespace shaderio
{
struct GltfMesh;
}
class VulkanContextManager;

class AccelerationStructures
{
public:
  static std::unique_ptr<AccelerationStructures>
  create(VulkanContextManager* core);

  ~AccelerationStructures();
  void build(const SceneResourcesManager& scene,
             const ShaderManager& materialManager);
  nvvk::AccelerationStructure tlas() const;

private:
  void init(VulkanContextManager* backend);
  void deinit();

  void buildBLAS(const SceneResourcesManager& scene);
  void buildTLAS(const SceneResourcesManager& scene,
                 const ShaderManager& materialManager);

  nvvk::AccelerationStructureGeometryInfo
  primitiveToGeometry(const shaderio::GltfMesh& mesh);
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
