#include "VulkanSceneResources.hpp"

// Implementation Includes
#include <tinygltf/tiny_gltf.h>

#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>

#include "VulkanSceneRenderer.hpp"
#include "nvutils/camera_manipulator.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "shaders/shaderio.h"
#include "vk_utils.hpp"

VulkanSceneResources::VulkanSceneResources(core::VulkanBackend* backend)
{
  m_backend = backend;
  // Acquiring the texture sampler which will be used for displaying the GBuffer
  m_samplerPool.init(m_backend->getDevice());
}

void VulkanSceneResources::begin_uploading()
{
  // TODO should ask backend for this
  NVVK_CHECK(
      nvvk::beginSingleTimeCommands(m_cmd, m_backend->getDevice(), m_backend->transientCmdPool()));
}
void VulkanSceneResources::end_uploading()
{
  NVVK_CHECK(nvvk::endSingleTimeCommands(m_cmd, m_backend->getDevice(),
                                         m_backend->transientCmdPool(),
                                         m_backend->getQueueInfo(0).queue));
  // cmd = VkCommandBuffer{};
}

void VulkanSceneResources::deinit()
{
  m_samplerPool.deinit();
}

VulkanSceneResources::MeshID
VulkanSceneResources::upload_gltf_model(const tinygltf::Model& model,
                                        nvsamples::GltfSceneResource& resources)
{
  nvsamples::importGltfData(resources, model, m_backend->stagingUploader());
  mesh_id_counter++;
  return mesh_id_counter - 1;
}

VulkanSceneResources::TextureID VulkanSceneResources::upload_texture(const std::string& filepath)
{
  nvvk::Image texture = vk_utils::loadAndCreateImage(m_cmd, m_backend->stagingUploader(),
                                                     m_backend->getDevice(), filepath);

  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);
  m_textures.emplace_back(texture);
  return static_cast<TextureID>(m_textures.size() - 1);
}

void VulkanSceneResources::update_descriptors(nvvk::DescriptorPack& descriptor_pack)
{
  if (m_textures.empty())
    return;

  nvvk::WriteSetContainer write{};
  auto write_set = descriptor_pack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                                             static_cast<uint32_t>(m_textures.size()));

  write.append(write_set, m_textures.data());

  vkUpdateDescriptorSets(m_backend->getDevice(), write.size(), write.data(), 0, nullptr);
}

void VulkanSceneResources::finalizeSceneResources(nvsamples::GltfSceneResource& resources)
{
  nvsamples::createGltfSceneInfoBuffer(
      resources,
      m_backend->stagingUploader());  // Create buffers for the scene data (GPU buffers)
  m_backend->stagingUploader().cmdUploadAppended(m_cmd);  // Upload the scene information to the GPU
}

void VulkanSceneResources::clear(nvsamples::GltfSceneResource& resources)
{
  for (auto& texture : m_textures)
  {
    m_backend->allocator().destroyImage(texture);
  }
  m_backend->allocator().destroyBuffer(resources.bSceneInfo);
  m_backend->allocator().destroyBuffer(resources.bMeshes);
  m_backend->allocator().destroyBuffer(resources.bMaterials);
  m_backend->allocator().destroyBuffer(resources.bInstances);
  for (auto& gltfData : resources.bGltfDatas)
  {
    m_backend->allocator().destroyBuffer(gltfData);
  }
  m_samplerPool.deinit();
}

const std::vector<nvvk::Image>& VulkanSceneResources::textures() const
{
  return m_textures;
}

nvvk::SamplerPool& VulkanSceneResources::sampler_pool()
{
  return m_samplerPool;
}
