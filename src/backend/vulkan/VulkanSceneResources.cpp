#include "VulkanSceneResources.hpp"

// Implementation Includes
#include <tinygltf/tiny_gltf.h>

#include <nvvk/debug_util.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>

#include "backend/vulkan/VulkanContext.hpp"
#include "backend/vulkan/vk_utils.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "shaders/shaderio.h"

VulkanSceneResources::VulkanSceneResources(std::shared_ptr<VulkanContext> ctx)
{
  m_ctx = std::move(ctx);
  // Acquiring the texture sampler which will be used for displaying the GBuffer
  m_samplerPool.init(m_ctx->context.getDevice());
}

void VulkanSceneResources::deinit()
{
  m_samplerPool.deinit();
}

VulkanSceneResources::MeshID
VulkanSceneResources::upload_gltf_model(const tinygltf::Model& model,
                                        nvsamples::GltfSceneResource& resources)
{
  nvsamples::importGltfData(resources, model, m_ctx->stagingUploader);
  mesh_id_counter++;
  return mesh_id_counter - 1;
}

VulkanSceneResources::TextureID VulkanSceneResources::upload_texture(const std::string& filepath,
                                                                     VkCommandBuffer cmd)
{
  nvvk::Image texture = vk_utils::loadAndCreateImage(cmd, m_ctx->stagingUploader,
                                                     m_ctx->context.getDevice(), filepath);

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

  vkUpdateDescriptorSets(m_ctx->context.getDevice(), write.size(), write.data(), 0, nullptr);
}

void VulkanSceneResources::finalizeSceneResources(nvsamples::GltfSceneResource& resources,
                                                  VkCommandBuffer cmd)
{
  nvsamples::createGltfSceneInfoBuffer(
      resources,
      m_ctx->stagingUploader);                    // Create buffers for the scene data (GPU buffers)
  m_ctx->stagingUploader.cmdUploadAppended(cmd);  // Upload the scene information to the GPU
}

void VulkanSceneResources::clear(nvsamples::GltfSceneResource& resources)
{
  for (auto& texture : m_textures)
  {
    m_ctx->allocator.destroyImage(texture);
  }
  m_ctx->allocator.destroyBuffer(resources.bSceneInfo);
  m_ctx->allocator.destroyBuffer(resources.bMeshes);
  m_ctx->allocator.destroyBuffer(resources.bMaterials);
  m_ctx->allocator.destroyBuffer(resources.bInstances);
  for (auto& gltfData : resources.bGltfDatas)
  {
    m_ctx->allocator.destroyBuffer(gltfData);
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
