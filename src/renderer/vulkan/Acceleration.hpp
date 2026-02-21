#pragma once

#include <nvvk/acceleration_structures.hpp>
#include <vulkan/vulkan.h>

// Forward Declarations
class SceneResourcesManager;
class ShaderManager;
namespace shaderio {
struct MeshPrimitive;
}
class VulkanContextManager;

class AccelerationStructures {
public:
  static std::unique_ptr<AccelerationStructures>
  create(VulkanContextManager *core);
  void clear();

  ~AccelerationStructures();
  void build(const SceneResourcesManager &scene,
             const ShaderManager &materialManager);
  void rebuild(const SceneResourcesManager &scene,
               const ShaderManager &materialManager);
  nvvk::AccelerationStructure tlas() const;

private:
  void init(VulkanContextManager *backend);
  void deinit();

  void buildBLAS(const SceneResourcesManager &scene);
  std::vector<VkAccelerationStructureInstanceKHR>
  buildTLAS(const SceneResourcesManager &scene,
            const ShaderManager &materialManager);

  nvvk::AccelerationStructureGeometryInfo
  primitiveToGeometry(const shaderio::MeshPrimitive &mesh);
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
