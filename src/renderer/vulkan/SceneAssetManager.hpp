#pragma once

#include <vulkan/vulkan.h>

#include <map>
#include <span>
#include <vector>

#include <nvvk/descriptors.hpp>
#include <nvvk/sampler_pool.hpp>

// Project Includes
#include "backend/vulkan/core/ContextManager.hpp"
#include "core/Image.hpp"
#include "renderer/interfaces/IDeviceAssets.hpp"
#include "shaders/shared/bindings.h"

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
namespace core
{
struct PrimitiveMesh;
}

// Holds the GPU-side buffers for the scene geometry and assets
struct VulkanSceneGpuData
{
  // Shader Storage Buffers (SSBOs)
  nvvk::Buffer bMeshes;     // Mesh metadata array
  nvvk::Buffer bInstances;  // Instance metadata array
  nvvk::Buffer bMaterials;  // Material data array

  // Uniform Buffers (UBOs)
  nvvk::Buffer bSceneInfo;       // Global SceneInfo struct
  nvvk::Buffer bSceneResources;  // Pointers to other buffers (BDA)

  std::vector<nvvk::Buffer>
      bDatas;  // Binary data per scene (Vertex/Index buffers)
  // Mapping: meshToBufferIndex[meshIndex] -> bufferIndex in bGltfDatas
  std::unordered_map<IDeviceAssets::MeshID, uint32_t> meshToBufferIndex;
};

// Concrete implementation of resource uploading/management for Vulkan.
class VulkanSceneAssetManager final : public IDeviceAssets
{
public:
  // -------------------------------------------------------------------------
  // 1. Lifecycle & Upload Flow
  //    Init/Deinit and command buffer management for staging uploads.
  // -------------------------------------------------------------------------
  explicit VulkanSceneAssetManager(VulkanContextManager* backend);
  void deinit() override;
  void clear();

  void beginUploading() override;
  void endUploading() override;

  // -------------------------------------------------------------------------
  // 2. Geometry & Model Management (Initialization)
  //    Uploading static model data (GLTF buffers) and initial mesh setup.
  // -------------------------------------------------------------------------
  IDeviceAssets::BufferHandle
  upload(const std::span<const uint8_t>& data) override;
  void destroyBuffer(BufferID id) override;

  void linkMeshToBuffer(MeshID id, BufferID bufferIndex) override;
  void uploadSceneResoures(const Scene& resources) override;

  const VulkanSceneGpuData& deviceResources() const
  {
    return m_data;
  }
  const nvvk::Buffer& getBufferFromIndex(MeshID meshIndex) const;

  // -------------------------------------------------------------------------
  // 3. Scene Data Updates (Per-Frame / Dynamic)
  //    Updating SSBOs and UBOs when scene state changes (animation, editing).
  // -------------------------------------------------------------------------
  void update(const std::vector<shaderio::MeshPrimitive>&) override;
  void update(const std::vector<shaderio::Instance>&) override;
  void update(const std::vector<shaderio::Material>&) override;

  VkDeviceAddress update(VkCommandBuffer cmd,
                         const shaderio::SceneInfo& sceneInfo) const;
  void updateSceneResources() const;
  VkDeviceAddress getSceneResources() const;

  // -------------------------------------------------------------------------
  // 4. Texture & Bindless Descriptor Management
  //    Handling image creation, GPU upload, and descriptor set manipulation.
  // -------------------------------------------------------------------------
  bool destroyTexture(TextureID id) override;
  bool addAndUploadTexture(const core::Image& image, TextureID& id,
                           bool immediate = false) override;
  TextureID reserveTextureSlot() override;
  uint64_t getTextureHandle(TextureID id) override;

  // Bindless Array Updates

  // Accessors
  uint32_t getMaximumNumberOfTextures() const
  {
    return MAX_SCENE_TEXTURES;
  }
  const std::map<TextureID, nvvk::Image>& textures() const
  {
    return m_textures;
  }
  nvvk::SamplerPool& samplerPool()
  {
    return m_samplerPool;
  }
  const nvvk::DescriptorPack& getDesriptorPack() const
  {
    return m_descPack;
  }

private:
  // -------------------------------------------------------------------------
  // 5. Internal Texture & Descriptor Helpers
  // -------------------------------------------------------------------------
  void createDesctriptorLayout();
  void pushTextureUpdates(const std::map<TextureID, nvvk::Image>& updates);
  nvvk::Image createImageFromRaw(const core::Image& raw,
                                 nvvk::StagingUploader& staging,
                                 bool sRgb = true);

  // -------------------------------------------------------------------------
  // 6. Internal Buffer Lifecycle Helpers
  // -------------------------------------------------------------------------
  bool registerTexture(const core::Image& image, TextureID& id);
  void createSceneBuffers(const Scene& sceneResources);
  void clearSceneBuffers();
  void updateSceneResources(VkCommandBuffer cmd) const;
  void uploadTextures();  // Legacy/Batch update
  void updateTextureDescriptorSets(const std::vector<TextureID>& indices);

  void allocBuffer(nvvk::Buffer& buffer, size_t bytes,
                   VkBufferUsageFlags2KHR usage);
  void destroyBuffer(nvvk::Buffer& buffer);

  template <typename T>
  void createBuffer(nvvk::Buffer& buffer, const std::span<T>& dataSpan,
                    VkBufferUsageFlags2KHR usage);

  template <typename T>
  void updateBuffer(nvvk::Buffer& buffer, std::span<T>&& dataSpan);

  // -------------------------------------------------------------------------
  // Constants
  // -------------------------------------------------------------------------
  static constexpr VkBufferUsageFlags2KHR storageUsage =
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;

  static constexpr VkBufferUsageFlags2KHR uniformUsage =
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;

  static constexpr VkBufferUsageFlags2KHR meshBufferUsage =
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanContextManager* m_context_manager = nullptr;
  VulkanSceneGpuData m_data{};

  // Textures & Descriptors
  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  nvvk::DescriptorPack m_descPack{};
  std::map<TextureID, nvvk::Image> m_textures{};
  nvvk::SamplerPool m_samplerPool{};

  // Upload State
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  uint32_t m_meshIdCounter = 0;
  std::vector<BufferID> m_freeBufferIndices;
  std::vector<TextureID> m_freeTextureIndices;
  uint m_nextTextureId{1};  // 0 is reserved for system
};
