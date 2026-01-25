#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>
#include <vector>

#include <nvvk/resource_allocator.hpp>
#include <nvvk/sampler_pool.hpp>

// Project Includes
#include "backend/interfaces/IDeviceAssets.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
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
  explicit VulkanSceneAssetManager(VulkanContextManager* backend);

  void deinit() override;

  // -------------------------------------------------------------------------
  // IDeviceAssets Interface
  // -------------------------------------------------------------------------
  void beginUploading() override;
  void endUploading() override;

  // Meshes
  std::pair<BufferAddr, BufferID>
  uploadGltfBuffer(const tinygltf::Model& model) override;
  std::pair<BufferAddr, BufferID>
  uploadPrimitiveMeshBuffer(const nvutils::PrimitiveMesh& primMesh,
                            uint32_t* vertexOffset = nullptr) override;
  void addMeshes(size_t count, BufferID bufferIndex) override;

  // Textures
  TextureID uploadTexture(const std::string& filepath) override;

  // Wrap up
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
  void createGltfSceneInfoBuffer(gltf::Scene& sceneResources);

  static nvvk::Image loadAndCreateImage(VkCommandBuffer cmd,
                                        nvvk::StagingUploader& staging,
                                        VkDevice device,
                                        const std::filesystem::path& filename,
                                        bool sRgb = true);

  static void processGltfNodes(gltf::Scene& sceneResource,
                               const tinygltf::Model& model,
                               uint32_t meshOffset);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanContextManager* m_core_manager = nullptr;
  VulkanSceneGpuData m_data{};
  std::vector<nvvk::Image> m_textures{};
  nvvk::SamplerPool m_samplerPool{};

  // Upload State
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  uint32_t m_meshIdCounter = 0;
};
