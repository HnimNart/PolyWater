#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "nvvk/sampler_pool.hpp"

// Project Includes
#include "backend/interfaces/IDeviceAssets.hpp"
#include "backend/vulkan/core/ContextManager.hpp"
#include "shaders/shared/bindings.h"

// Forward Declarations
namespace tinygltf {
class Model;
}
namespace nvvk {
class DescriptorPack;
class StagingUploader;
} // namespace nvvk
namespace core {
struct PrimitiveMesh;
}

// Holds the GPU-side buffers for the scene geometry and assets
struct VulkanSceneGpuData {
  std::vector<nvvk::Buffer>
      bDatas; // Binary data per scene (Vertex/Index buffers)

  // Shader Storage Buffers (SSBOs)
  nvvk::Buffer bMeshes;    // Mesh metadata array
  nvvk::Buffer bInstances; // Instance metadata array
  nvvk::Buffer bMaterials; // Material data array

  // Uniform Buffers (UBOs)
  nvvk::Buffer bSceneInfo;      // Global SceneInfo struct
  nvvk::Buffer bSceneResources; // Pointers to other buffers (BDA)

  // Mapping: meshToBufferIndex[meshIndex] -> bufferIndex in bGltfDatas
  std::vector<uint32_t> meshToBufferIndex;
};

// Concrete implementation of resource uploading/management for Vulkan.
class VulkanSceneAssetManager final : public IDeviceAssets {
public:
  // -------------------------------------------------------------------------
  // 1. Lifecycle & Context
  // -------------------------------------------------------------------------
  explicit VulkanSceneAssetManager(VulkanContextManager *backend);
  void deinit() override;

  // -------------------------------------------------------------------------
  // 2. Upload Flow Control
  //    Manage the command buffer state for staging uploads.
  // -------------------------------------------------------------------------
  void beginUploading() override;
  void endUploading() override;

  // -------------------------------------------------------------------------
  // 3. Texture Management
  //    Handling image loading, creation, and descriptor slots.
  // -------------------------------------------------------------------------
  TextureID uploadTexture(const std::string &filepath, TextureID = -1) override;
  TextureID reserveTextureSlot() override;
  uint32_t getMaximumNumberOfTextures() const { return MAX_SCENE_TEXTURES; }

  // Accessors
  const std::vector<nvvk::Image> &textures() const { return m_textures; }
  nvvk::SamplerPool &samplerPool() { return m_samplerPool; }

  // -------------------------------------------------------------------------
  // 4. Geometry & Model Management (Initialization)
  //    Uploading static model data (GLTF buffers) and initial mesh setup.
  // -------------------------------------------------------------------------
  std::pair<BufferAddr, BufferID>
  upload(const std::span<const unsigned char> &data) override;
  void addMeshes(size_t count, BufferID bufferIndex) override;
  void finalizeSceneResources(const Scene &resources) override;

  // Accessors
  const VulkanSceneGpuData &deviceResources() const { return m_data; }
  const nvvk::Buffer &getBufferFromIndex(uint32_t meshIndex) const;

  // -------------------------------------------------------------------------
  // 5. Scene Data Updates (Per-Frame / Dynamic)
  //    Updating SSBOs and UBOs when scene state changes (animation, editing).
  // -------------------------------------------------------------------------
  void update(const std::vector<shaderio::MeshPrimitive> &) override;
  void update(const std::vector<shaderio::Instance> &) override;
  void update(const std::vector<shaderio::Material> &) override;

  // UBO Updates (Uniform Buffers - typically per frame)
  shaderio::SceneInfo *update(VkCommandBuffer cmd,
                              const shaderio::SceneInfo &sceneInfo) const;
  void updateSceneResources() const;
  shaderio::SceneResources *getSceneResources() const;

  // -------------------------------------------------------------------------
  // 6. Vulkan Pipeline Integration
  // -------------------------------------------------------------------------
  void updateDescriptors(nvvk::DescriptorPack &descriptorPack);

private:
  // -------------------------------------------------------------------------
  // Internal Helpers
  // -------------------------------------------------------------------------
  void createBuffers(const Scene &sceneResources);
  // Generic buffer update helper
  template <typename T>
  void updateBuffer(nvvk::Buffer &buffer, std::span<T> &&dataSpan);
  void updateSceneResources(VkCommandBuffer cmd) const;

  // Static helpers for complex logic
  static nvvk::Image loadAndCreateImage(VkCommandBuffer cmd,
                                        nvvk::StagingUploader &staging,
                                        VkDevice device,
                                        const std::filesystem::path &filename,
                                        bool sRgb = true);
  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  VulkanContextManager *m_context_manager = nullptr;
  VulkanSceneGpuData m_data{};
  std::vector<nvvk::Image> m_textures{};
  nvvk::SamplerPool m_samplerPool{};

  // Upload State
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  uint32_t m_meshIdCounter = 0;
};
