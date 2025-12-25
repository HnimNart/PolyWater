#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include <nvvk/resource_allocator.hpp>
#include <nvvk/sampler_pool.hpp>

struct VulkanContext;
namespace tinygltf
{
class Model;
}
namespace nvsamples
{
struct GltfSceneResource;
}
namespace nvvk
{
class DescriptorPack;
}

class VulkanSceneResources
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  // Lifecycle
  explicit VulkanSceneResources(VulkanContext* ctx);

  // Resources
  MeshID upload_gltf_model(const tinygltf::Model& model, nvsamples::GltfSceneResource& resources);
  TextureID upload_texture(const std::string& filepath, VkCommandBuffer cmd);
  void update_descriptors(nvvk::DescriptorPack& descriptor_pack, VulkanContext* ctx);
  void finalizeSceneResources(nvsamples::GltfSceneResource& resources, VkCommandBuffer cmd);
  void clear(nvsamples::GltfSceneResource& resources);

  // Accessors
  const std::vector<nvvk::Image>& textures() const;
  nvvk::SamplerPool& sampler_pool();

private:
  VulkanContext* m_ctx = nullptr;
  std::vector<nvvk::Image> m_textures;
  nvvk::SamplerPool m_samplerPool;
  uint32_t mesh_id_counter = 0;
};
