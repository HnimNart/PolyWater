#include "SceneAssetManager.hpp"

#include <volk.h>

#include <core/file_operations.hpp>
#include <core/shape/primitives.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>

#include "Image.hpp"
#include "core/timers.hpp"
#include "shaders/shared/structs.h"

#include <tinygltf/tiny_gltf.h>

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
  m_context_manager->getStagingUploader().cmdUploadAppended(m_cmd);
  m_context_manager->endSingleTimeCmd(m_cmd);
  m_cmd = VK_NULL_HANDLE;
}

/**********************************************************/
void VulkanSceneAssetManager::clear()
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
  for (auto &gltfData : m_data.bDatas) {
    m_context_manager->getAllocator().destroyBuffer(gltfData);
  }
  m_textures.clear();
  m_data = {};
}

/**********************************************************/
void VulkanSceneAssetManager::deinit()
/**********************************************************/
{
  clear();
  m_samplerPool.deinit();
}

/**********************************************************/
VulkanSceneAssetManager::TextureID VulkanSceneAssetManager::uploadTexture(
    const core::Image &image, VulkanSceneAssetManager::TextureID textureID)
/**********************************************************/
{
  if (textureID == -1) {
    textureID = reserveTextureSlot();
  }
  nvvk::Image texture =
      createImageFromRaw(image, m_context_manager->getStagingUploader());
  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);

  assert(textureID >= 0 && textureID < m_textures.size());
  m_textures[textureID - 1] = texture;
  return textureID;
}

/**********************************************************/
IDeviceAssets::TextureID VulkanSceneAssetManager::reserveTextureSlot()
/**********************************************************/
{
  m_textures.emplace_back();
  TextureID textureId = static_cast<TextureID>(m_textures.size());
  assert(textureId < MAX_SCENE_TEXTURES);
  return static_cast<TextureID>(textureId);
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
  return m_data.bDatas[bufferIndex];
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
VulkanSceneAssetManager::upload(const std::span<const unsigned char> &data)
/**********************************************************/
{
  nvvk::Buffer bData;
  nvvk::ResourceAllocator *allocator =
      m_context_manager->getStagingUploader().getResourceAllocator();

  NVVK_CHECK(allocator->createBuffer(
      bData, data.size_bytes(),
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));

  NVVK_CHECK(
      m_context_manager->getStagingUploader().appendBuffer(bData, 0, data));
  NVVK_DBG_NAME(bData.buffer);

  uint32_t bufferIndex = static_cast<uint32_t>(m_data.bDatas.size());
  m_data.bDatas.push_back(bData);
  return {(uint8_t *)bData.address, bufferIndex};
}

/**********************************************************/
std::pair<void *, IDeviceAssets::BufferID>
VulkanSceneAssetManager::upload(const void *data, size_t bytes)
/**********************************************************/
{
  std::span<const unsigned char> dataSpan(
      static_cast<const unsigned char *>(data), bytes);
  auto [deviceAddress, bufferIndex] = upload(dataSpan);
  return {static_cast<void *>(deviceAddress),
          static_cast<BufferID>(bufferIndex)};
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
nvvk::Image VulkanSceneAssetManager::createImageFromRaw(
    const core::Image &raw, nvvk::StagingUploader &staging, bool sRgb)
/**********************************************************/
{
  assert(raw.isValid() && "Attempting to upload invalid image data!");

  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.extent = {uint32_t(raw.width), uint32_t(raw.height), 1};
  imageInfo.usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  // Determine Vulkan Format based on core::ImageFormat
  switch (raw.format) {
  case core::ImageFormat::RGBA8_UNORM:
    imageInfo.format =
        sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    break;
  case core::ImageFormat::RGBA32_SFLOAT:
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    break;
  case core::ImageFormat::DEPTH32_SFLOAT:
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    // Depth images often need different usage flags
    imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    break;
  default:
    assert(false && "Unsupported image format!");
    return {};
  }

  nvvk::ResourceAllocator *allocator = staging.getResourceAllocator();
  nvvk::Image texture;
  // Create the image resource
  NVVK_CHECK(allocator->createImage(texture, imageInfo,
                                    DEFAULT_VkImageViewCreateInfo));

  // 3. Upload via staging buffer
  const std::span<const uint8_t> dataSpan(raw.pixels.data(), raw.pixels.size());
  VkImageLayout finalLayout =
      (raw.format == core::ImageFormat::DEPTH32_SFLOAT)
          ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  NVVK_CHECK(staging.appendImage(texture, dataSpan, finalLayout));

  return texture;
}
