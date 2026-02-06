#include "Acceleration.hpp"

#include <vector>

#include "backend/vulkan/core/ContextManager.hpp"
#include "common/timers.hpp"
#include "scene/SceneResources.hpp"
#include "scene/gltf/gltf_utils.hpp"

/**********************************************************/
std::unique_ptr<AccelerationStructures>
AccelerationStructures::create(VulkanContextManager* core)
/**********************************************************/
{
  auto m_accel =
      std::unique_ptr<AccelerationStructures>(new AccelerationStructures());
  m_accel->init(core);

  return m_accel;
}

/**********************************************************/
AccelerationStructures::~AccelerationStructures()
/**********************************************************/
{
  deinit();
}
/**********************************************************/
void AccelerationStructures::init(VulkanContextManager* coreManager)
/**********************************************************/
{
  m_asBuilder.init(&coreManager->getAllocator(),
                   &coreManager->getStagingUploader(),
                   coreManager->getQueueInfo(0));
}

/**********************************************************/
void AccelerationStructures::build(const SceneResourcesManager& scene,
                                   const ShaderManager& materialManager)
/**********************************************************/
{
  // Set up acceleration structure infrastructure
  buildBLAS(scene);                   // Set up BLAS infrastructure
  buildTLAS(scene, materialManager);  // Set up TLAS infrastructure
}

/**********************************************************/
void AccelerationStructures::deinit()
/**********************************************************/
{
  m_asBuilder.deinitAccelerationStructures();
  m_asBuilder.deinit();
}

/**********************************************************/
void AccelerationStructures::buildBLAS(const SceneResourcesManager& scene)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  const gltf::Scene& scene_geometry = scene.data();
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
void AccelerationStructures::buildTLAS(const SceneResourcesManager& scene,
                                       const ShaderManager& materialManager)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  // Prepare instance data for TLAS
  const gltf::Scene& sceneGeometry = scene.data();
  std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
  tlasInstances.reserve(sceneGeometry.instances.size());
  const VkGeometryInstanceFlagsKHR flags{
      VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV};
  for (const shaderio::GltfInstance& instance : sceneGeometry.instances)
  {
    VkAccelerationStructureInstanceKHR ray_inst{};
    ray_inst.transform = nvvk::toTransformMatrixKHR(
        instance.transform);  // Position of the instance
    ray_inst.instanceCustomIndex =
        instance.meshIndex;  // gl_InstanceCustomIndexEXT
    ray_inst.accelerationStructureReference =
        m_asBuilder.blasSet[instance.meshIndex].address;
    ray_inst.instanceShaderBindingTableRecordOffset =
        materialManager.getSbtOffset(instance.hit_group);
    ray_inst.flags = flags;
    ray_inst.mask = 0xFF;
    tlasInstances.emplace_back(ray_inst);
  }

  // Build the top-level acceleration structure
  m_asBuilder.tlasSubmitBuildAndWait(
      tlasInstances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
}

/**********************************************************/
nvvk::AccelerationStructure AccelerationStructures::tlas() const
/**********************************************************/
{
  return m_asBuilder.tlas;
}

//--------------------------------------------------------------------------------------------------
// Converting a PrimitiveMesh as input for BLAS
//

/**********************************************************/
nvvk::AccelerationStructureGeometryInfo
AccelerationStructures::primitiveToGeometry(const shaderio::GltfMesh& mesh)
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
      .vertexData = {.deviceAddress = VkDeviceAddress(mesh.gltfBuffer) +
                                      triMesh.positions.offset},
      .vertexStride = triMesh.positions.byteStride,
      .maxVertex = triMesh.positions.count - 1,
      .indexType = VkIndexType(mesh.indexType),
      .indexData = {.deviceAddress = VkDeviceAddress(mesh.gltfBuffer) +
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
