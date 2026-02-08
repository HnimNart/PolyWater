#include "SceneAssetManager.hpp"

#include <stb/stb_image.h>
#include <volk.h>

#include <nvutils/file_operations.hpp>
#include <nvutils/primitives.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/default_structs.hpp>

#include <tinygltf/tiny_gltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>

#include "common/timers.hpp"
#include "scene/gltf/gltf_utils.hpp"
#include "shaders/shaderio.h"

/**********************************************************/
VulkanSceneAssetManager::VulkanSceneAssetManager(
    VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;
  m_samplerPool.init(m_context_manager->getDevice());
}

/**********************************************************/
void VulkanSceneAssetManager::beginUploading()
/**********************************************************/
{
  if (m_cmd != VK_NULL_HANDLE) {
    throw std::runtime_error(
        "BeginUploading() called while another upload is already in progress.");
  }
  m_cmd = m_context_manager->startSingleTimeCmd();
}

/**********************************************************/
void VulkanSceneAssetManager::endUploading()
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE);
  m_context_manager->getStagingUploader().cmdUploadAppended(
      m_cmd); // Upload the scene information to the GPU
  m_context_manager->endSingleTimeCmd(m_cmd);
  m_cmd = VK_NULL_HANDLE;
}

/**********************************************************/
void VulkanSceneAssetManager::deinit()
/**********************************************************/
{
  for (auto &texture : m_textures) {
    m_context_manager->getAllocator().destroyImage(texture);
  }
  m_context_manager->getAllocator().destroyBuffer(m_data.bSceneInfo);
  m_context_manager->getAllocator().destroyBuffer(m_data.bSceneResources);
  m_context_manager->getAllocator().destroyBuffer(m_data.bMeshes);
  m_context_manager->getAllocator().destroyBuffer(m_data.bMaterials);
  m_context_manager->getAllocator().destroyBuffer(m_data.bInstances);
  for (auto &gltfData : m_data.bGltfDatas) {
    m_context_manager->getAllocator().destroyBuffer(gltfData);
  }
  m_samplerPool.deinit();
}

/**********************************************************/
VulkanSceneAssetManager::TextureID
VulkanSceneAssetManager::uploadTexture(const std::string &filepath,
                                       VulkanSceneAssetManager::TextureID id)
/**********************************************************/
{
  nvvk::Image texture =
      loadAndCreateImage(m_cmd, m_context_manager->getStagingUploader(),
                         m_context_manager->getDevice(), filepath);

  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);

  if (id == -1) {
    m_textures.emplace_back(texture);
    return static_cast<TextureID>(m_textures.size() - 1);
  } else {
    assert(id < m_textures.size());
    m_textures[id] = texture;
    return id;
  }
}

/**********************************************************/
IDeviceAssets::TextureID VulkanSceneAssetManager::reserveTextureSlot()
/**********************************************************/
{
  TextureID id = static_cast<TextureID>(m_textures.size());
  m_textures.emplace_back();
  return static_cast<TextureID>(id);
}

/**********************************************************/
void VulkanSceneAssetManager::updateDescriptors(
    nvvk::DescriptorPack &descriptorPack)
/**********************************************************/
{
  if (m_textures.empty()) {
    return;
  }

  nvvk::WriteSetContainer write{};
  auto write_set =
      descriptorPack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                               static_cast<uint32_t>(m_textures.size()));

  write.append(write_set, m_textures.data());
  vkUpdateDescriptorSets(m_context_manager->getDevice(), write.size(),
                         write.data(), 0, nullptr);
}

/**********************************************************/
const nvvk::Buffer &
VulkanSceneAssetManager::getBufferFromIndex(uint32_t meshIndex) const
/**********************************************************/
{
  assert(meshIndex < m_data.meshToBufferIndex.size());
  uint32_t bufferIndex = m_data.meshToBufferIndex[meshIndex];
  return m_data.bGltfDatas[bufferIndex];
}

/**********************************************************/
void VulkanSceneAssetManager::finalizeSceneResources(const Scene &resources)
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  createBuffers(resources);
}

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::MeshPrimitive> &meshes)
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  updateBuffer(m_data.bMeshes, std::span(meshes));
}

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::Instance> &instances)
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  updateBuffer(m_data.bInstances, std::span(instances));
}

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::Material> &materials)
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  updateBuffer(m_data.bMaterials, std::span(materials));
}

/**********************************************************/
std::pair<uint8_t *, uint32_t>
VulkanSceneAssetManager::upload(const tinygltf::Model &model)
/**********************************************************/
{
  nvvk::Buffer bGltfData;
  nvvk::ResourceAllocator *allocator =
      m_context_manager->getStagingUploader().getResourceAllocator();

  // We can only handle one buffer for now
  assert(model.buffers.size() == 1);

  NVVK_CHECK(allocator->createBuffer(
      bGltfData,
      std::span<const unsigned char>(model.buffers[0].data).size_bytes(),
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));

  NVVK_CHECK(m_context_manager->getStagingUploader().appendBuffer(
      bGltfData, 0, std::span<const unsigned char>(model.buffers[0].data)));
  NVVK_DBG_NAME(bGltfData.buffer);

  uint32_t bufferIndex = static_cast<uint32_t>(m_data.bGltfDatas.size());
  m_data.bGltfDatas.push_back(bGltfData);
  return {(uint8_t *)bGltfData.address, bufferIndex};
}

/**********************************************************/
void VulkanSceneAssetManager::addMeshes(size_t count, BufferID bufferIndex)
/**********************************************************/
{
  m_data.meshToBufferIndex.insert(m_data.meshToBufferIndex.end(), count,
                                  bufferIndex);
}

/**********************************************************/
void VulkanSceneAssetManager::createBuffers(const Scene &sceneResource)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  auto &stagingUploader = m_context_manager->getStagingUploader();
  nvvk::ResourceAllocator *allocator = stagingUploader.getResourceAllocator();

  // 1. Define common usage flags to avoid clutter
  const VkBufferUsageFlags2KHR storageUsage =
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;

  const VkBufferUsageFlags2KHR uniformUsage =
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;

  // 2. Helper lambda to handle the generic Create -> Name -> Upload pattern
  auto createBuffer = [&](nvvk::Buffer &buffer, auto &&dataSpan,
                          VkBufferUsageFlags2KHR usage) {
    if (dataSpan.empty()) {
      return;
    }
    allocator->createBuffer(buffer, dataSpan.size_bytes(), usage);
    NVVK_DBG_NAME(buffer.buffer);
    NVVK_CHECK(stagingUploader.appendBuffer(buffer, 0, dataSpan));
  };

  // 3. Process the buffers cleanly
  createBuffer(m_data.bMeshes, std::span(sceneResource.meshes), storageUsage);
  createBuffer(m_data.bInstances, std::span(sceneResource.instances),
               storageUsage);
  createBuffer(m_data.bMaterials, std::span(sceneResource.materials),
               storageUsage);

  // SceneInfo needs a span of size 1 created manually
  createBuffer(
      m_data.bSceneInfo,
      std::span<const shaderio::SceneInfo>(&sceneResource.sceneInfo, 1),
      uniformUsage);
  createBuffer(m_data.bSceneResources,
               std::span<const shaderio::SceneResources>(
                   &sceneResource.sceneResources, 1),
               uniformUsage);
}

/**********************************************************/
shaderio::SceneInfo *
VulkanSceneAssetManager::update(VkCommandBuffer cmd,
                                const shaderio::SceneInfo &sceneInfo) const
/**********************************************************/
{
  NVVK_DBG_SCOPE(cmd);
  nvvk::cmdBufferMemoryBarrier(cmd, {m_data.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, m_data.bSceneInfo.buffer, 0,
                    sizeof(shaderio::SceneInfo), &sceneInfo);
  nvvk::cmdBufferMemoryBarrier(cmd, {m_data.bSceneInfo.buffer,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
  return reinterpret_cast<shaderio::SceneInfo *>(m_data.bSceneInfo.address);
}

/**********************************************************/
shaderio::SceneResources *VulkanSceneAssetManager::getSceneResources() const
/**********************************************************/
{
  return reinterpret_cast<shaderio::SceneResources *>(
      m_data.bSceneResources.address);
}

/**********************************************************/
void VulkanSceneAssetManager::updateSceneResources() const
/**********************************************************/
{
  VkCommandBuffer cmd = m_context_manager->startSingleTimeCmd();
  updateSceneResources(cmd);
  m_context_manager->getStagingUploader().cmdUploadAppended(cmd);
  m_context_manager->endSingleTimeCmd(cmd);
}

/**********************************************************/
void VulkanSceneAssetManager::updateSceneResources(VkCommandBuffer cmd) const
/**********************************************************/
{
  NVVK_DBG_SCOPE(cmd);
  shaderio::SceneResources resources = {
      .instances = (shaderio::Instance *)m_data.bInstances.address,
      .meshes = (shaderio::MeshPrimitive *)m_data.bMeshes.address,
      .materials = (shaderio::Material *)m_data.bMaterials.address,
  };

  nvvk::cmdBufferMemoryBarrier(cmd, {m_data.bSceneResources.buffer,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT});
  vkCmdUpdateBuffer(cmd, m_data.bSceneResources.buffer, 0,
                    sizeof(shaderio::SceneResources), &resources);
  nvvk::cmdBufferMemoryBarrier(cmd, {m_data.bSceneResources.buffer,
                                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT});
}

/**********************************************************/
template <typename T>
void VulkanSceneAssetManager::updateBuffer(nvvk::Buffer &buffer,
                                           std::span<T> &&dataSpan)
/**********************************************************/
{
  auto &stagingUploader = m_context_manager->getStagingUploader();
  if (dataSpan.empty() || buffer.buffer == VK_NULL_HANDLE) {
    return;
  }
  assert(dataSpan.size_bytes() == buffer.bufferSize);
  NVVK_CHECK(stagingUploader.appendBuffer(buffer, 0, dataSpan));
};
template void VulkanSceneAssetManager::updateBuffer<shaderio::MeshPrimitive>(
    nvvk::Buffer &, std::span<shaderio::MeshPrimitive> &&);
template void VulkanSceneAssetManager::updateBuffer<shaderio::Instance>(
    nvvk::Buffer &, std::span<shaderio::Instance> &&);
template void VulkanSceneAssetManager::updateBuffer<shaderio::Material>(
    nvvk::Buffer &, std::span<shaderio::Material> &&);

/**********************************************************/
nvvk::Image VulkanSceneAssetManager::loadAndCreateImage(
    VkCommandBuffer cmd, nvvk::StagingUploader &staging, VkDevice device,
    const std::filesystem::path &filename, bool sRgb)
/**********************************************************/
{
  int w, h, comp, req_comp{4};
  std::string filenameUtf8 = nvutils::utf8FromPath(filename);
  const stbi_uc *data =
      stbi_load(filenameUtf8.c_str(), &w, &h, &comp, req_comp);
  assert((data != nullptr) && "Could not load texture image!");

  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.format = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {uint32_t(w), uint32_t(h), 1};
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  nvvk::ResourceAllocator *allocator = staging.getResourceAllocator();
  const std::span dataSpan(data, w * h * req_comp);
  nvvk::Image texture;
  NVVK_CHECK(allocator->createImage(texture, imageInfo,
                                    DEFAULT_VkImageViewCreateInfo));
  NVVK_CHECK(staging.appendImage(texture, dataSpan,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  return texture;
}
