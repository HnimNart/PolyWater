#pragma once

#include <nvvk/debug_util.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>
#include <string>

#include "backend/vulkan/VulkanContext.hpp"
#include "backend/vulkan/vk_utils.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "shaders/shaderio.h"

class VulkanSceneResources
{
public:
  using MeshID = int;
  using TextureID = uint;

  VulkanSceneResources(VulkanContext* ctx)
  {
    m_ctx = ctx;
    // Acquiring the texture sampler which will be used for displaying the GBuffer
    m_samplerPool.init(ctx->device);
  }

  MeshID upload_gltf_model(const tinygltf::Model& model, nvsamples::GltfSceneResource& m_resources)
  {
    nvsamples::importGltfData(m_resources, model, m_ctx->stagingUploader);
    mesh_id_counter++;
    return mesh_id_counter - 1;
  }

  TextureID upload_texture(const std::string& filepath, VkCommandBuffer cmd)
  {
    nvvk::Image texture =
        nvsamples::loadAndCreateImage(cmd, m_ctx->stagingUploader, m_ctx->device, filepath);

    NVVK_DBG_NAME(texture.image);
    m_samplerPool.acquireSampler(texture.descriptor.sampler);
    m_textures.emplace_back(texture);
    return static_cast<TextureID>(m_textures.size() - 1);
  }

  void update_descriptors(nvvk::DescriptorPack& descriptor_pack, VulkanContext* ctx)
  {
    if (m_textures.empty())
      return;

    nvvk::WriteSetContainer write{};
    auto write_set = descriptor_pack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                                               static_cast<uint32_t>(m_textures.size()));

    write.append(write_set, m_textures.data());

    vkUpdateDescriptorSets(ctx->device, write.size(), write.data(), 0, nullptr);
  }

  void finalizeSceneResources(nvsamples::GltfSceneResource& resources, VkCommandBuffer cmd)
  {
    nvsamples::createGltfSceneInfoBuffer(
        resources,
        m_ctx->stagingUploader);  // Create buffers for the scene data (GPU buffers)
    m_ctx->stagingUploader.cmdUploadAppended(cmd);  // Upload the scene information to the GPU
  }

  void clear(nvsamples::GltfSceneResource& resources)
  {
    for (auto& texture : m_textures)
    {
      m_ctx->allocator->destroyImage(texture);
    }
    m_ctx->allocator->destroyBuffer(resources.bSceneInfo);
    m_ctx->allocator->destroyBuffer(resources.bMeshes);
    m_ctx->allocator->destroyBuffer(resources.bMaterials);
    m_ctx->allocator->destroyBuffer(resources.bInstances);
    for (auto& gltfData : resources.bGltfDatas)
    {
      m_ctx->allocator->destroyBuffer(gltfData);
    }
    m_samplerPool.deinit();
  }

  const std::vector<nvvk::Image>& textures() const { return m_textures; };
  nvvk::SamplerPool& sampler_pool() { return m_samplerPool; }

private:
  VulkanContext* m_ctx = nullptr;
  std::vector<nvvk::Image> m_textures;
  nvvk::SamplerPool m_samplerPool;
  uint mesh_id_counter = 0;
};
