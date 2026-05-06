#include "vulkan_acceleration_structures.hpp"

#include <vector>

#include "backend/vulkan/core/vulkan_context_manager.hpp"
#include "core/timers.hpp"
#include "renderer/shader_manager.hpp"
#include "scene/scene_resources.hpp"

/**********************************************************/
std::unique_ptr<VulkanAccelerationStructures>
VulkanAccelerationStructures::create(VulkanContextManager* core)
/**********************************************************/
{
  auto m_accel = std::unique_ptr<VulkanAccelerationStructures>(
      new VulkanAccelerationStructures());
  m_accel->init(core);
  return m_accel;
}

/**********************************************************/
void VulkanAccelerationStructures::clear()
/**********************************************************/
{
  m_asBuilder.deinitAccelerationStructures();
}

/**********************************************************/
VulkanAccelerationStructures::~VulkanAccelerationStructures()
/**********************************************************/
{
  deinit();
}
/**********************************************************/
void VulkanAccelerationStructures::init(VulkanContextManager* coreManager)
/**********************************************************/
{
  m_asBuilder.init(&coreManager->getAllocator(),
                   &coreManager->getStagingUploader(),
                   coreManager->getQueueInfo(0));
}

/**********************************************************/
void VulkanAccelerationStructures::build(const SceneResourcesManager& scene,
                                         const ShaderManager& materialManager)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  buildBLAS(scene);
  auto tlasInstances = buildTLAS(scene, materialManager);
  m_asBuilder.tlasSubmitBuildAndWait(
      tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                         VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
}

/**********************************************************/
void VulkanAccelerationStructures::rebuild(const SceneResourcesManager& scene,
                                           const ShaderManager& materialManager)
/**********************************************************/
{
  auto tlasInstances = buildTLAS(scene, materialManager);
  m_asBuilder.tlasSubmitUpdateAndWait(tlasInstances);
}

/**********************************************************/
void VulkanAccelerationStructures::deinit()
/**********************************************************/
{
  m_asBuilder.deinitAccelerationStructures();
  m_asBuilder.deinit();
}

/**********************************************************/
void VulkanAccelerationStructures::buildBLAS(const SceneResourcesManager& scene)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  const Scene& scene_geometry = scene.data();
  // Prepare geometry information for all meshes
  std::vector<nvvk::AccelerationStructureGeometryInfo> geoInfos(
      scene_geometry.meshes.size());
  for (uint32_t p_idx = 0; p_idx < scene_geometry.meshes.size(); p_idx++)
  {
    geoInfos[p_idx] = primitiveToGeometry(scene_geometry.meshes[p_idx]);
  }

  // Build the bottom-level acceleration structures
  m_asBuilder.blasSubmitBuildAndWait(
      geoInfos, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
}

/**********************************************************/
std::vector<VkAccelerationStructureInstanceKHR>
VulkanAccelerationStructures::buildTLAS(const SceneResourcesManager& scene,
                                        const ShaderManager& shaderManager)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  // Prepare instance data for TLAS
  const Scene& sceneGeometry = scene.data();
  std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
  tlasInstances.reserve(sceneGeometry.instances.size());
  const VkGeometryInstanceFlagsKHR flags{
      VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};
  for (const shaderio::Instance& instance : sceneGeometry.instances)
  {
    VkAccelerationStructureInstanceKHR ray_inst{};
    ray_inst.transform = nvvk::toTransformMatrixKHR(
        instance.transform);  // Position of the instance
    ray_inst.instanceCustomIndex =
        instance.meshIndex;  // gl_InstanceCustomIndexEXT
    ray_inst.accelerationStructureReference =
        m_asBuilder.blasSet[instance.meshIndex].address;
    ray_inst.instanceShaderBindingTableRecordOffset =
        shaderManager.getSbtOffset(instance.hit_group);
    ray_inst.flags = flags;
    ray_inst.mask = 0xFF;
    tlasInstances.emplace_back(ray_inst);
  }
  return tlasInstances;
}

/**********************************************************/
nvvk::AccelerationStructure VulkanAccelerationStructures::tlas() const
/**********************************************************/
{
  return m_asBuilder.tlas;
}

//--------------------------------------------------------------------------------------------------
// Converting a PrimitiveMesh as input for BLAS
//

/**********************************************************/
nvvk::AccelerationStructureGeometryInfo
VulkanAccelerationStructures::primitiveToGeometry(
    const shaderio::MeshPrimitive& mesh)
/**********************************************************/
{
  nvvk::AccelerationStructureGeometryInfo result = {};

  const shaderio::TriangleMesh triMesh = mesh.triMesh;
  const auto triangleCount = static_cast<uint32_t>(triMesh.indices.count / 3U);

  // Describe buffer as array of VertexObj.
  VkAccelerationStructureGeometryTrianglesDataKHR triangles{
      .sType =
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
      .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,  // vec3 vertex position data
      .vertexData = {.deviceAddress = VkDeviceAddress(mesh.buffer.address) +
                                      triMesh.positions.offset},
      .vertexStride = triMesh.positions.byteStride,
      .maxVertex = triMesh.positions.count - 1,
      .indexType = VkIndexType(mesh.indexType),
      .indexData = {.deviceAddress = VkDeviceAddress(mesh.buffer.address) +
                                     triMesh.indices.offset},
  };

  // Identify the above data as containing opaque triangles.
  result.geometry = VkAccelerationStructureGeometryKHR{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .geometry = {.triangles = triangles},
      .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR |
               VK_GEOMETRY_OPAQUE_BIT_KHR,
  };

  result.rangeInfo =
      VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = triangleCount};

  return result;
}
