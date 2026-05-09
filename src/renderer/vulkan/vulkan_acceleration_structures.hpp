#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/acceleration_structures.hpp>

// Forward Declarations
namespace scene
{
class SceneResourcesManager;
}  // namespace scene
class ShaderManager;
namespace shaderio
{
struct MeshPrimitive;
}

namespace vkb
{

class VulkanContextManager;

class VulkanAccelerationStructures
{
public:
  static std::unique_ptr<VulkanAccelerationStructures>
  create(VulkanContextManager* core);
  void clear();

  ~VulkanAccelerationStructures();
  void build(const scene::SceneResourcesManager& scene,
             const ShaderManager& materialManager);
  void rebuild(const scene::SceneResourcesManager& scene,
               const ShaderManager& materialManager);
  nvvk::AccelerationStructure tlas() const;

private:
  void init(VulkanContextManager* backend);
  void deinit();

  void buildBLAS(const scene::SceneResourcesManager& scene);
  std::vector<VkAccelerationStructureInstanceKHR>
  buildTLAS(const scene::SceneResourcesManager& scene,
            const ShaderManager& materialManager);

  nvvk::AccelerationStructureGeometryInfo
  primitiveToGeometry(const shaderio::MeshPrimitive& mesh);
  nvvk::AccelerationStructureHelper m_asBuilder{};
};
}  // namespace vkb
