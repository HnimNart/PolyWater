#pragma once

#include <vulkan/vulkan.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>
#include <src/common/path_utils.hpp>
#include <vector>

#include "_autogen/sky_simple.slang.h"
#include "nvshaders_host/sky.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "scene/scene_context.hpp"
#include "src/backend/vulkan/utils.hpp"

class SceneResources
{

public:
  using MeshID = uint;
  using InstanceID = uint;
  using TextureID = uint;
  using MaterialID = uint;

  void init(VulkanContext* ctx)
  {
    // Acquiring the texture sampler which will be used for displaying the GBuffer
    m_samplerPool.init(ctx->device);

    NVVK_CHECK(m_samplerPool.acquireSampler(m_linearSampler));
    NVVK_DBG_NAME(m_linearSampler);

    // Initialize the Sky with the pre-compiled shader
    m_skySimple.init(ctx->allocator, std::span(sky_simple_slang));
  }

  tinygltf::Model loadGltf(const std::string& filename, nvvk::StagingUploader& uploader)
  {
    auto model =
        nvsamples::loadGltfResources(nvutils::findFile(filename, nvsamples::getResourcesDirs()));
    nvsamples::importGltfData(m_resources, model, uploader);
    return model;
  }

  TextureID loadTexture(const std::string& filename, VkCommandBuffer cmd, VulkanContext* ctx)
  {
    const std::filesystem::path imageFilename =
        nvutils::findFile(filename, nvsamples::getResourcesDirs());

    nvvk::Image texture =
        nvsamples::loadAndCreateImage(cmd, ctx->stagingUploader, ctx->device, imageFilename);

    NVVK_DBG_NAME(texture.image);
    m_samplerPool.acquireSampler(texture.descriptor.sampler);
    m_textures.emplace_back(texture);
    return m_textures.size() - 1;
  }

  InstanceID addInstance(const shaderio::GltfInstance& instance)
  {
    m_resources.instances.push_back(instance);
    return m_resources.instances.size() - 1;
  }

  //--------------------------------------------------------------------------------------------------
  // Update the textures: this is called when the scene is loaded
  // Textures are updated in the descriptor set (0)
  void updateTextures(nvvk::DescriptorPack& descriptor_pack, VulkanContext* ctx)
  {
    if (m_textures.empty())
    {
      return;
    }

    // Update the descriptor set with the textures
    nvvk::WriteSetContainer write{};
    VkWriteDescriptorSet allTextures = descriptor_pack.makeWrite(shaderio::BindingPoints::eTextures,
                                                                 0, 1, uint32_t(m_textures.size()));
    nvvk::Image* allImages = m_textures.data();
    write.append(allTextures, allImages);
    vkUpdateDescriptorSets(ctx->device, write.size(), write.data(), 0, nullptr);
  }

  MaterialID addMaterial(const shaderio::GltfMetallicRoughness& material)
  {
    m_resources.materials.push_back(material);
    return m_resources.materials.size() - 1;
  }

  void finalizeSceneResources(VkCommandBuffer cmd, VulkanContext* ctx)
  {
    nvsamples::createGltfSceneInfoBuffer(
        m_resources,
        ctx->stagingUploader);                    // Create buffers for the scene data (GPU buffers)
    ctx->stagingUploader.cmdUploadAppended(cmd);  // Upload the scene information to the GPU
  }

  void clear(nvvk::ResourceAllocator* allocator)
  {

    for (auto& texture : m_textures)
    {
      allocator->destroyImage(texture);
    }
    allocator->destroyBuffer(m_resources.bSceneInfo);
    allocator->destroyBuffer(m_resources.bMeshes);
    allocator->destroyBuffer(m_resources.bMaterials);
    allocator->destroyBuffer(m_resources.bInstances);
    for (auto& gltfData : m_resources.bGltfDatas)
    {
      allocator->destroyBuffer(gltfData);
    }

    m_samplerPool.deinit();

    m_skySimple.deinit();
  }

  const nvsamples::GltfSceneResource& data() const { return m_resources; };
  nvsamples::GltfSceneResource& data() { return m_resources; };

  const std::vector<nvvk::Image>& textures() const { return m_textures; };
  const VkSampler& sampler() const { return m_linearSampler; }
  nvvk::SamplerPool& sampler_pool() { return m_samplerPool; }

  shaderio::GltfSceneInfo& scene_info() { return m_resources.sceneInfo; }
  const shaderio::GltfSceneInfo& scene_info() const { return m_resources.sceneInfo; }

  const nvshaders::SkySimple& sky() const { return m_skySimple; }

public:
  nvsamples::GltfSceneResource m_resources{};
  std::vector<nvvk::Image> m_textures;
  nvshaders::SkySimple m_skySimple{};  // Sky rendering

  nvvk::SamplerPool m_samplerPool{};  // Texture sampler pool
  VkSampler m_linearSampler{};
};