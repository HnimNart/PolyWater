#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

#include <nvvk/resource_allocator.hpp>
#include <nvvk/sampler_pool.hpp>

// Project Includes
#include "backend/interfaces/IDeviceAssets.hpp"
#include "backend/vulkan/core/CoreManager.hpp"
#include "scene/gltf/gltf_utils.hpp"

// Forward Declarations
namespace tinygltf
{
class Model;
}
namespace nvvk
{
class DescriptorPack;
class StagingUploader;
}  // namespace nvvk
namespace nvutils
{
struct PrimitiveMesh;
}

// Holds the GPU-side buffers for the scene geometry and assets
struct VulkanSceneGpuData
{
  std::vector<nvvk::Buffer> bGltfDatas;  // Binary GLTF data per scene
  nvvk::Buffer bMeshes;                  // GltfMesh array
  nvvk::Buffer bInstances;               // GltfInstance array
  nvvk::Buffer bMaterials;               // GltfMetallicRoughness array
  nvvk::Buffer bSceneInfo;               // GltfSceneInfo struct

  // Mapping: meshToBufferIndex[meshIndex] -> bufferIndex in bGltfDatas
  std::vector<uint32_t> meshToBufferIndex;
};

// Concrete implementation of resource uploading/management for Vulkan.
class VulkanSceneAssetManager final : public IDeviceAssets
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------
  explicit VulkanSceneAssetManager(VulkanCoreManager* backend);

  void deinit() override;

  // -------------------------------------------------------------------------
  // IDeviceAssets Interface
  // -------------------------------------------------------------------------
  void beginUploading() override;
  void endUploading() override;

  MeshID uploadGltfModel(const tinygltf::Model& model, gltf::Scene& resources) override;
  TextureID uploadTexture(const std::string& filepath) override;
  void finalizeSceneResources(gltf::Scene& resources) override;

  // -------------------------------------------------------------------------
  // Vulkan Specific API
  // -------------------------------------------------------------------------
  void updateDescriptors(nvvk::DescriptorPack& descriptorPack);

  // Accessors
  const std::vector<nvvk::Image>& textures() const { return m_textures; }
  const VulkanSceneGpuData& deviceResources() const { return m_data; }
  nvvk::SamplerPool& samplerPool() { return m_samplerPool; }

private:
  // -------------------------------------------------------------------------
  // Internal Static Helpers (Upload Logic)
  // -------------------------------------------------------------------------
  static void importGltfData(gltf::Scene& sceneResource, VulkanSceneGpuData& deviceResource,
                             const tinygltf::Model& model, nvvk::StagingUploader& stagingUploader,
                             bool importInstance = false);

  static void createGltfSceneInfoBuffer(gltf::Scene& sceneResources,
                                        VulkanSceneGpuData& deviceResources,
                                        nvvk::StagingUploader& stagingUploader);

  static void primitiveMeshToResource(gltf::Scene& sceneResource,
                                      VulkanSceneGpuData& deviceResources,
                                      nvvk::StagingUploader& stagingUploader,
                                      const nvutils::PrimitiveMesh& primMesh);

  static nvvk::Image loadAndCreateImage(VkCommandBuffer cmd, nvvk::StagingUploader& staging,
                                        VkDevice device, const std::filesystem::path& filename,
                                        bool sRgb = true);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanCoreManager* m_core_manager = nullptr;
  VulkanSceneGpuData m_data{};
  std::vector<nvvk::Image> m_textures{};
  nvvk::SamplerPool m_samplerPool{};

  // Upload State
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  uint32_t m_meshIdCounter = 0;
};
