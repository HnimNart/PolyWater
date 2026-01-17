#include "VulkanRenderResources.hpp"

// Implementation Includes
#include <tinygltf/tiny_gltf.h>

#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>

#include "nvutils/camera_manipulator.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "shaders/shaderio.h"
#include "vk_utils.hpp"

VulkanRenderResources::VulkanRenderResources(core::VulkanBackend* backend)
{
  m_backend = backend;
  // Acquiring the texture sampler which will be used for displaying the GBuffer
  m_samplerPool.init(m_backend->getDevice());
}

void VulkanRenderResources::begin_uploading()
{
  m_cmd = m_backend->start_single_time_cmd();
}
void VulkanRenderResources::end_uploading()
{
  assert(m_cmd != VK_NULL_HANDLE);
  m_backend->end_single_time_cmd(m_cmd);
  m_cmd = VK_NULL_HANDLE;
}

void VulkanRenderResources::deinit()
{
  for (auto& texture : m_textures)
  {
    m_backend->allocator().destroyImage(texture);
  }
  m_backend->allocator().destroyBuffer(m_device_resources.bSceneInfo);
  m_backend->allocator().destroyBuffer(m_device_resources.bMeshes);
  m_backend->allocator().destroyBuffer(m_device_resources.bMaterials);
  m_backend->allocator().destroyBuffer(m_device_resources.bInstances);
  for (auto& gltfData : m_device_resources.bGltfDatas)
  {
    m_backend->allocator().destroyBuffer(gltfData);
  }
  m_samplerPool.deinit();
}

VulkanRenderResources::MeshID
VulkanRenderResources::upload_gltf_model(const tinygltf::Model& model,
                                         nvsamples::GltfSceneResource& resources)
{
  nvsamples::importGltfData(resources, m_device_resources, model, m_backend->stagingUploader());
  mesh_id_counter++;
  return mesh_id_counter - 1;
}

VulkanRenderResources::TextureID VulkanRenderResources::upload_texture(const std::string& filepath)
{
  nvvk::Image texture = vk_utils::loadAndCreateImage(m_cmd, m_backend->stagingUploader(),
                                                     m_backend->getDevice(), filepath);

  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);
  m_textures.emplace_back(texture);
  return static_cast<TextureID>(m_textures.size() - 1);
}

void VulkanRenderResources::update_descriptors(nvvk::DescriptorPack& descriptor_pack)
{
  if (m_textures.empty())
  {
    return;
  }

  nvvk::WriteSetContainer write{};
  auto write_set = descriptor_pack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                                             static_cast<uint32_t>(m_textures.size()));

  write.append(write_set, m_textures.data());

  vkUpdateDescriptorSets(m_backend->getDevice(), write.size(), write.data(), 0, nullptr);
}

void VulkanRenderResources::finalizeSceneResources(nvsamples::GltfSceneResource& resources)
{
  nvsamples::createGltfSceneInfoBuffer(
      resources, m_device_resources,
      m_backend->stagingUploader());  // Create buffers for the scene data (GPU buffers)
  m_backend->stagingUploader().cmdUploadAppended(m_cmd);  // Upload the scene information to the GPU

  // Update the pointers to uploaded data
  resources.sceneInfo.instances =
      (shaderio::GltfInstance*)
          m_device_resources.bInstances.address;  // Address of the instance buffer
  resources.sceneInfo.meshes =
      (shaderio::GltfMesh*) m_device_resources.bMeshes.address;  // Address of the mesh buffer
  resources.sceneInfo.materials =
      (shaderio::GltfMetallicRoughness*)
          m_device_resources.bMaterials.address;  // Address of the material buffer
}

const std::vector<nvvk::Image>& VulkanRenderResources::textures() const
{
  return m_textures;
}

nvvk::SamplerPool& VulkanRenderResources::sampler_pool()
{
  return m_samplerPool;
}
