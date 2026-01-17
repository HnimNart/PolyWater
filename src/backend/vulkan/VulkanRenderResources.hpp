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
  MeshID upload_gltf_model(const tinygltf::Model& model,
                           nvsamples::GltfSceneResource& resources) override;
  TextureID upload_texture(const std::string& filepath) override;
  void finalizeSceneResources(nvsamples::GltfSceneResource& resources) override;

  void update_descriptors(nvvk::DescriptorPack& descriptor_pack);

  // Accessors
  const std::vector<nvvk::Image>& textures() const;
  nvvk::SamplerPool& sampler_pool();
  // nvsamples::GltfDeviceSceneResources& device_resources() { return m_device_resources; };
  const nvsamples::GltfDeviceSceneResources& device_resources() const
  {
    return m_device_resources;
  };

private:
  nvsamples::GltfDeviceSceneResources m_device_resources;
  core::VulkanBackend* m_backend = nullptr;
  std::vector<nvvk::Image> m_textures;
  nvvk::SamplerPool m_samplerPool;
  uint32_t mesh_id_counter = 0;

  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};
