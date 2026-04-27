#include "SceneAssetManager.hpp"
#include "Image.hpp"
#include "core/timers.hpp"
#include "shaders/shared/structs.h"
#include <backends/imgui_impl_vulkan.h>
#include <core/file_operations.hpp>
#include <core/shape/primitives.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/commands.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/default_structs.hpp>
#include <nvvk/descriptors.hpp>
#include <nvvk/formats.hpp>
#include <tinygltf/tiny_gltf.h>
#include <volk.h>

// -------------------------------------------------------------------------
// Lifecycle & Initialization
// -------------------------------------------------------------------------

/**********************************************************/
VulkanSceneAssetManager::VulkanSceneAssetManager(
    VulkanContextManager *contextManager)
/**********************************************************/
{
  m_context_manager = contextManager;
  m_samplerPool.init(m_context_manager->getDevice());
  createDesctriptorLayout();
}

/**********************************************************/
void VulkanSceneAssetManager::deinit()
/**********************************************************/
{
  clear();
  m_samplerPool.deinit();
  m_descPack.deinit();
}

/**********************************************************/
void VulkanSceneAssetManager::clear()
/**********************************************************/
{
  clearSceneBuffers();
  for (auto &[id, texture] : m_textures) {
    if (texture.image != VK_NULL_HANDLE) {
      m_context_manager->getAllocator().destroyImage(texture);
    }
  }
  for (auto &data : m_data.bDatas) {
    destroyBuffer(data);
  }
  m_textures.clear();
  m_data = {};

  m_freeBufferIndices.clear();
  m_freeTextureIndices.clear();
  m_nextTextureId = 1;
}

// -------------------------------------------------------------------------
// Upload Flow Control
// -------------------------------------------------------------------------

/**********************************************************/
void VulkanSceneAssetManager::beginUploading()
/**********************************************************/
{
  if (m_cmd != VK_NULL_HANDLE) {
    throw std::runtime_error(
        "BeginUploading() called while another upload is in progress.");
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
  updateSceneResources(); // Finalize pointers in SceneResources UBO
  m_cmd = VK_NULL_HANDLE;
}

// -------------------------------------------------------------------------
// Texture & Bindless Management
// -------------------------------------------------------------------------

/**********************************************************/
IDeviceAssets::TextureID VulkanSceneAssetManager::reserveTextureSlot()
/**********************************************************/
{
  TextureID textureId;
  // Check the free list first
  if (!m_freeTextureIndices.empty()) {
    textureId = m_freeTextureIndices.back();
    m_freeTextureIndices.pop_back();
  } else {
    textureId = m_nextTextureId++;
  }
  // Ensure the slot exists in the map so addTexture can find it
  m_textures[textureId] = nvvk::Image{};
  assert(textureId < MAX_SCENE_TEXTURES);
  return textureId;
}

/**********************************************************/
bool VulkanSceneAssetManager::destroyTexture(TextureID id)
/**********************************************************/
{
  auto it = m_textures.find(id);
  if (it == m_textures.end()) {
    return false;
  }
  m_context_manager->waitForDeviceIdle();
  if (it->second.image != VK_NULL_HANDLE) {
    m_context_manager->getAllocator().destroyImage(it->second);
  }
  m_textures.erase(it);
  m_freeTextureIndices.push_back(id);
  return true;
}

/**********************************************************/
bool VulkanSceneAssetManager::registerTexture(const core::Image &image,
                                              TextureID &textureId)
/**********************************************************/
{
  if (textureId == -1 || m_textures.find(textureId) == m_textures.end()) {
    textureId = reserveTextureSlot();
  }

  if (textureId >= getMaximumNumberOfTextures()) {
    LOGE("Texture index %u exceeds maximum capacity of %u\n", textureId,
         getMaximumNumberOfTextures());
    textureId = -1;
    return false;
  }

  // Cleanup existing image at this slot (standard overwrite logic)
  nvvk::Image &slot = m_textures[textureId];
  if (slot.image != VK_NULL_HANDLE) {
    LOGD("Found existing texture(Id:%d). Destroying it to make room "
         "for new "
         "texture\n",
         textureId);
    m_context_manager->getAllocator().destroyImage(slot);
  }

  nvvk::Image texture =
      createImageFromRaw(image, m_context_manager->getStagingUploader());
  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);

  m_textures[textureId] = texture;
  return true;
}

/**********************************************************/
bool VulkanSceneAssetManager::addAndUploadTexture(const core::Image &image,
                                                  TextureID &textureId,
                                                  bool immediate)
/**********************************************************/
{
  if (registerTexture(image, textureId)) {
    if (immediate) {
      updateTextureDescriptorSets({static_cast<uint32_t>(textureId)});
    }
    return true;
  }
  return false;
}

/**********************************************************/
void VulkanSceneAssetManager::updateTextureDescriptorSets(
    const std::vector<uint32_t> &indices)
/**********************************************************/
{
  if (indices.empty()) {
    return;
  }

  nvvk::WriteSetContainer write{};
  for (uint32_t index : indices) {
    auto it = m_textures.find(index);
    if (it != m_textures.end()) {
      auto write_set =
          m_descPack.makeWrite(shaderio::BindGlobal::eTextures, 0, index, 1);
      write.append(write_set, it->second);
    }
  }

  if (write.size() > 0) {
    vkUpdateDescriptorSets(m_context_manager->getDevice(), write.size(),
                           write.data(), 0, nullptr);
  }
}

/**********************************************************/
void VulkanSceneAssetManager::uploadTextures()
/**********************************************************/
{
  if (m_textures.empty())
    return;

  std::vector<uint32_t> allIndices;
  allIndices.reserve(m_textures.size());
  for (const auto &[id, _] : m_textures) {
    allIndices.push_back(id);
  }
  updateTextureDescriptorSets(allIndices);
}

/**********************************************************/
uint64_t VulkanSceneAssetManager::getTextureHandle(TextureID id)
/**********************************************************/
{
  if (m_textures.find(id) == m_textures.end()) {
    return 0;
  }

  auto &tex = m_textures.at(id);
  if (tex.cachedDesriptor == VK_NULL_HANDLE) {
    if (tex.descriptor.imageView == VK_NULL_HANDLE ||
        tex.descriptor.sampler == VK_NULL_HANDLE)
      return 0;

    tex.cachedDesriptor = ImGui_ImplVulkan_AddTexture(
        tex.descriptor.sampler, tex.descriptor.imageView,
        tex.descriptor.imageLayout);
  }
  return reinterpret_cast<uint64_t>(tex.cachedDesriptor);
}

// -------------------------------------------------------------------------
// Geometry & Buffer Management
// -------------------------------------------------------------------------

/**********************************************************/
IDeviceAssets::BufferHandle
VulkanSceneAssetManager::upload(const std::span<const uint8_t> &data)
/**********************************************************/
{
  nvvk::Buffer bData;
  createBuffer(bData, data, meshBufferUsage);

  uint32_t bufferIndex;

  // Check if we can recycle a previously destroyed slot
  if (!m_freeBufferIndices.empty()) {
    bufferIndex = m_freeBufferIndices.back();
    m_freeBufferIndices.pop_back();
    m_data.bDatas[bufferIndex] = bData;
  } else {
    // No free slots, grow the vector
    bufferIndex = static_cast<uint32_t>(m_data.bDatas.size());
    m_data.bDatas.push_back(bData);
  }

  return {bData.address, bufferIndex};
}

/**********************************************************/
void VulkanSceneAssetManager::destroyBuffer(BufferID id)
/**********************************************************/
{
  // Safety check to prevent out-of-bounds or double-free
  if (id >= m_data.bDatas.size() ||
      m_data.bDatas[id].buffer == VK_NULL_HANDLE) {
    return;
  }
  nvvk::Buffer &buffer = m_data.bDatas[id];
  destroyBuffer(buffer);
  buffer = {};
  m_freeBufferIndices.push_back(id);
}
/**********************************************************/
void VulkanSceneAssetManager::linkMeshToBuffer(MeshID meshId,
                                               BufferID bufferIndex)
/**********************************************************/
{
  // Validate the BufferID exists
  if (bufferIndex >= m_data.bDatas.size()) {
    LOGE("linkMeshToBuffer: Attempting to link Mesh %u to non-existent Buffer "
         "%u. (Total buffers: %zu)",
         meshId, bufferIndex, m_data.bDatas.size());
    return;
  }

  // Handle Conflicts & Placement
  auto [it, inserted] =
      m_data.meshToBufferIndex.try_emplace(meshId, bufferIndex);

  if (!inserted) {
    if (it->second != bufferIndex) {
      LOGW("linkMeshToBuffer: Mesh %u is already linked to Buffer %u. "
           "Re-mapping to Buffer %u.",
           meshId, it->second, bufferIndex);
      it->second = bufferIndex;
    }
  }
}

/**********************************************************/
void VulkanSceneAssetManager::uploadSceneResoures(const Scene &resources)
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  clearSceneBuffers();
  createSceneBuffers(resources);
  uploadTextures();
}

/**********************************************************/
const nvvk::Buffer &
VulkanSceneAssetManager::getBufferFromIndex(MeshID meshIndex) const
/**********************************************************/
{
  assert(meshIndex < m_data.meshToBufferIndex.size());
  uint32_t bufferIndex = m_data.meshToBufferIndex.at(meshIndex);
  return m_data.bDatas[bufferIndex];
}

// -------------------------------------------------------------------------
// 5. Scene Updates (Dynamic)
// -------------------------------------------------------------------------

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::MeshPrimitive> &meshes)
/**********************************************************/
{
  updateBuffer(m_data.bMeshes, std::span(meshes));
}

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::Instance> &instances)
/**********************************************************/
{
  updateBuffer(m_data.bInstances, std::span(instances));
}

/**********************************************************/
void VulkanSceneAssetManager::update(
    const std::vector<shaderio::Material> &materials)
/**********************************************************/
{
  updateBuffer(m_data.bMaterials, std::span(materials));
}

/**********************************************************/
VkDeviceAddress
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
  return m_data.bSceneInfo.address;
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
      .instances =
          reinterpret_cast<shaderio::Instance *>(m_data.bInstances.address),
      .meshes =
          reinterpret_cast<shaderio::MeshPrimitive *>(m_data.bMeshes.address),
      .materials =
          reinterpret_cast<shaderio::Material *>(m_data.bMaterials.address),
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
VkDeviceAddress VulkanSceneAssetManager::getSceneResources() const
/**********************************************************/
{
  return m_data.bSceneResources.address;
}

// -------------------------------------------------------------------------
// Internal Helpers (Resource Creation)
// -------------------------------------------------------------------------

/**********************************************************/
void VulkanSceneAssetManager::createDesctriptorLayout()
/**********************************************************/
{
  VkDevice device = m_context_manager->getDevice();
  uint32_t maxTextures = getMaximumNumberOfTextures();

  nvvk::DescriptorBindings bindings;
  bindings.addBinding(
      {.binding = shaderio::BindGlobal::eTextures,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = maxTextures,
       .stageFlags = VK_SHADER_STAGE_ALL},
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

  m_descPack.init(bindings, device, 1,
                  VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                  VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                  maxTextures, &maxTextures);
}

/**********************************************************/
void VulkanSceneAssetManager::createSceneBuffers(const Scene &sceneResource)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  createBuffer(m_data.bMeshes, std::span(sceneResource.meshes), storageUsage);
  createBuffer(m_data.bInstances, std::span(sceneResource.instances),
               storageUsage);
  createBuffer(m_data.bMaterials, std::span(sceneResource.materials),
               storageUsage);
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
void VulkanSceneAssetManager::clearSceneBuffers()
/**********************************************************/
{
  destroyBuffer(m_data.bSceneInfo);
  destroyBuffer(m_data.bSceneResources);
  destroyBuffer(m_data.bMeshes);
  destroyBuffer(m_data.bMaterials);
  destroyBuffer(m_data.bInstances);
}

/**********************************************************/
void VulkanSceneAssetManager::allocBuffer(nvvk::Buffer &buffer, size_t bytes,
                                          VkBufferUsageFlags2KHR usage)
/**********************************************************/
{
  m_context_manager->getAllocator().createBuffer(buffer, bytes, usage);
}

/**********************************************************/
void VulkanSceneAssetManager::destroyBuffer(nvvk::Buffer &buffer)
/**********************************************************/
{
  if (buffer.isAllocated()) {
    m_context_manager->getAllocator().destroyBuffer(buffer);
    buffer.buffer = VK_NULL_HANDLE;
  }
}

/**********************************************************/
template <typename T>
void VulkanSceneAssetManager::createBuffer(nvvk::Buffer &buffer,
                                           const std::span<T> &dataSpan,
                                           VkBufferUsageFlags2KHR usage)
/**********************************************************/
{
  allocBuffer(buffer, dataSpan.size_bytes(), usage);
  NVVK_DBG_NAME(buffer.buffer);
  NVVK_CHECK(m_context_manager->getStagingUploader().appendBuffer(buffer, 0,
                                                                  dataSpan));
}

/**********************************************************/
template <typename T>
void VulkanSceneAssetManager::updateBuffer(nvvk::Buffer &buffer,
                                           std::span<T> &&dataSpan)
/**********************************************************/
{
  if (dataSpan.empty()) {
    return;
  }
  assert(m_cmd != VK_NULL_HANDLE && "Did you call beginUploading() first?");
  if (buffer.bufferSize != dataSpan.size_bytes()) {
    destroyBuffer(buffer);
    allocBuffer(buffer, dataSpan.size_bytes(), storageUsage);
  }
  assert(!(dataSpan.empty() || buffer.buffer == VK_NULL_HANDLE));
  NVVK_CHECK(m_context_manager->getStagingUploader().appendBuffer(buffer, 0,
                                                                  dataSpan));
};

/**********************************************************/
nvvk::Image VulkanSceneAssetManager::createImageFromRaw(
    const core::Image &raw, nvvk::StagingUploader &staging, bool sRgb)
/**********************************************************/
{
  assert(raw.isValid());
  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.extent = {uint32_t(raw.width), uint32_t(raw.height), 1};
  imageInfo.usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

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
    imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    break;
  default:
    assert(false);
    return {};
  }

  VkImageViewCreateInfo viewInfo = DEFAULT_VkImageViewCreateInfo;
  viewInfo.format = imageInfo.format;

  viewInfo.subresourceRange.aspectMask =
      (raw.format == core::ImageFormat::DEPTH32_SFLOAT)
          ? VK_IMAGE_ASPECT_DEPTH_BIT
          : VK_IMAGE_ASPECT_COLOR_BIT;

  nvvk::Image texture;
  NVVK_CHECK(staging.getResourceAllocator()->createImage(texture, imageInfo,
                                                         viewInfo));

  const std::span<const uint8_t> dataSpan(raw.pixels.data(), raw.pixels.size());

  VkImageLayout finalLayout =
      (raw.format == core::ImageFormat::DEPTH32_SFLOAT)
          ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  NVVK_CHECK(staging.appendImage(texture, dataSpan, finalLayout));
  return texture;
}
