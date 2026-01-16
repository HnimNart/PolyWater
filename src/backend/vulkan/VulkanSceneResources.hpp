#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include <nvvk/resource_allocator.hpp>
#include <nvvk/sampler_pool.hpp>

#include "VulkanBackend.hpp"
#include "nvutils/camera_manipulator.hpp"
#include "vulkan/vulkan_core.h"

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

// struct SceneUpdateParams
// {
//   std::shared_ptr<nvutils::CameraManipulator> camera;
// };

class VulkanSceneResources
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  explicit VulkanSceneResources(core::VulkanBackend* backend);

  void updateSceneBuffers();

  void begin_uploading();
  void end_uploading();

  void deinit();

  // Resources
  MeshID upload_gltf_model(const tinygltf::Model& model, nvsamples::GltfSceneResource& resources);
  TextureID upload_texture(const std::string& filepath);
  void update_descriptors(nvvk::DescriptorPack& descriptor_pack);
  void finalizeSceneResources(nvsamples::GltfSceneResource& resources);
  void clear(nvsamples::GltfSceneResource& resources);

  // Accessors
  const std::vector<nvvk::Image>& textures() const;
  nvvk::SamplerPool& sampler_pool();

private:
  core::VulkanBackend* m_backend = nullptr;
  std::vector<nvvk::Image> m_textures;
  nvvk::SamplerPool m_samplerPool;
  uint32_t mesh_id_counter = 0;

  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};                     
