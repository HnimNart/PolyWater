#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include <nvvk/resource_allocator.hpp>
#include <nvvk/sampler_pool.hpp>

#include "VulkanBackend.hpp"
#include "backend/IDeviceResources.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "vulkan/vulkan_core.h"

namespace tinygltf
{
class Model;
}

namespace nvvk
{
class DescriptorPack;
}

// GPU buffers for the scene data
struct GltfDeviceSceneResources
{
  std::vector<nvvk::Buffer>
      bGltfDatas;           // Buffers containing the GLTF binary data for each loaded scene
  nvvk::Buffer bMeshes;     // Buffer containing all GltfMesh data
  nvvk::Buffer bInstances;  // Buffer containing all GltfInstance data
  nvvk::Buffer bMaterials;  // Buffer containing all GltfMetallicRoughness data
  nvvk::Buffer bSceneInfo;  // Buffer containing GltfSceneInfo

  // Mapping from mesh index to buffer index in bGltfDatas
  std::vector<uint32_t> meshToBufferIndex;  // meshToBufferIndex[meshIndex] = bufferIndex
};

class VulkanRenderResources : public IDeviceResources
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  explicit VulkanRenderResources(core::VulkanBackend* backend);
  void deinit() override;
  void begin_uploading() override;
  void end_uploading() override;

  // Resources
  MeshID upload_gltf_model(const tinygltf::Model& model, gltf::Scene& resources) override;
  TextureID upload_texture(const std::string& filepath) override;
  void finalizeSceneResources(gltf::Scene& resources) override;

  void update_descriptors(nvvk::DescriptorPack& descriptor_pack);

  // Accessors
  const std::vector<nvvk::Image>& textures() const;
  nvvk::SamplerPool& sampler_pool();
  const GltfDeviceSceneResources& device_resources() const { return m_device_resources; };

private:
  // This is a utility function to import the GLTF data into the scene resource.
  static void importGltfData(gltf::Scene& sceneResource, GltfDeviceSceneResources& deviceResource,
                             const tinygltf::Model& model, nvvk::StagingUploader& stagingUploader,
                             bool importInstance = false);

  // This is a utility function to create the scene info buffer.
  static void createGltfSceneInfoBuffer(gltf::Scene& sceneResources,
                                        GltfDeviceSceneResources& deviceResources,
                                        nvvk::StagingUploader& stagingUploader);

  // This is a utility function to convert a primitive mesh to a GltfMeshResource.
  static void primitiveMeshToResource(gltf::Scene& sceneResource,
                                      GltfDeviceSceneResources& deviceResources,
                                      nvvk::StagingUploader& stagingUploader,
                                      const nvutils::PrimitiveMesh& primMesh);

  GltfDeviceSceneResources m_device_resources;
  core::VulkanBackend* m_backend = nullptr;
  std::vector<nvvk::Image> m_textures;
  nvvk::SamplerPool m_samplerPool;
  uint32_t mesh_id_counter = 0;

  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};
