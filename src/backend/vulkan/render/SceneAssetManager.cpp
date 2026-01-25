#include "SceneAssetManager.hpp"

#include <stb/stb_image.h>
#include <volk.h>

#include <nvutils/file_operations.hpp>
#include <nvutils/primitives.hpp>
#include <nvvk/check_error.hpp>
#include <nvvk/default_structs.hpp>

// Implementation Includes
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
    VulkanContextManager* coreManager)
/**********************************************************/
{
  m_core_manager = coreManager;
  // Acquiring the texture sampler which will be used for displaying the GBuffer
  m_samplerPool.init(m_core_manager->getDevice());
}

/**********************************************************/
void VulkanSceneAssetManager::beginUploading()
/**********************************************************/
{
  m_cmd = m_core_manager->startSingleTimeCmd();
}

/**********************************************************/
void VulkanSceneAssetManager::endUploading()
/**********************************************************/
{
  assert(m_cmd != VK_NULL_HANDLE);
  m_core_manager->endSingleTimeCmd(m_cmd);
  m_cmd = VK_NULL_HANDLE;
}

/**********************************************************/
void VulkanSceneAssetManager::deinit()
/**********************************************************/
{
  for (auto& texture : m_textures)
  {
    m_core_manager->getAllocator().destroyImage(texture);
  }
  m_core_manager->getAllocator().destroyBuffer(m_data.bSceneInfo);
  m_core_manager->getAllocator().destroyBuffer(m_data.bMeshes);
  m_core_manager->getAllocator().destroyBuffer(m_data.bMaterials);
  m_core_manager->getAllocator().destroyBuffer(m_data.bInstances);
  for (auto& gltfData : m_data.bGltfDatas)
  {
    m_core_manager->getAllocator().destroyBuffer(gltfData);
  }
  m_samplerPool.deinit();
}

/**********************************************************/
VulkanSceneAssetManager::TextureID
VulkanSceneAssetManager::uploadTexture(const std::string& filepath)
/**********************************************************/
{
  nvvk::Image texture =
      loadAndCreateImage(m_cmd, m_core_manager->getStagingUploader(),
                         m_core_manager->getDevice(), filepath);

  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);
  m_textures.emplace_back(texture);
  return static_cast<TextureID>(m_textures.size() - 1);
}

/**********************************************************/
void VulkanSceneAssetManager::updateDescriptors(
    nvvk::DescriptorPack& descriptorPack)
/**********************************************************/
{
  if (m_textures.empty())
  {
    return;
  }

  nvvk::WriteSetContainer write{};
  auto write_set =
      descriptorPack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                               static_cast<uint32_t>(m_textures.size()));

  write.append(write_set, m_textures.data());
  vkUpdateDescriptorSets(m_core_manager->getDevice(), write.size(),
                         write.data(), 0, nullptr);
}

/**********************************************************/
void VulkanSceneAssetManager::finalizeSceneResources(gltf::Scene& resources)
/**********************************************************/
{
  createGltfSceneInfoBuffer(resources);
  // data (GPU buffers)
  m_core_manager->getStagingUploader().cmdUploadAppended(
      m_cmd);  // Upload the scene information to the GPU
}

/**********************************************************/
std::pair<uint8_t*, uint32_t>
VulkanSceneAssetManager::uploadPrimitiveMeshBuffer(
    const nvutils::PrimitiveMesh& primMesh, uint32_t* vertexOffset)
/**********************************************************/
{
  auto& stagingUploader = m_core_manager->getStagingUploader();
  nvvk::ResourceAllocator* allocator = stagingUploader.getResourceAllocator();

  // Calculate buffer sizes
  size_t verticesSize = std::span(primMesh.vertices).size_bytes();
  size_t trianglesSize = std::span(primMesh.triangles).size_bytes();

  // Create buffer for the geometry data (vertices + triangles)
  nvvk::Buffer gltfData;
  allocator->createBuffer(
      gltfData, verticesSize + trianglesSize,
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

  // Upload vertices first (at offset 0)
  stagingUploader.appendBuffer(gltfData, 0, std::span(primMesh.vertices));

  // Upload triangles after vertices
  stagingUploader.appendBuffer(gltfData, verticesSize,
                               std::span(primMesh.triangles));

  if (vertexOffset)
  {
    *vertexOffset = verticesSize;
  }
  uint32_t bufferIndex = static_cast<uint32_t>(m_data.bGltfDatas.size());
  m_data.bGltfDatas.push_back(gltfData);
  return {(uint8_t*) gltfData.address, bufferIndex};
}

/**********************************************************/
std::pair<uint8_t*, uint32_t>
VulkanSceneAssetManager::uploadGltfBuffer(const tinygltf::Model& model)
/**********************************************************/
{
  nvvk::Buffer bGltfData;
  nvvk::ResourceAllocator* allocator =
      m_core_manager->getStagingUploader().getResourceAllocator();

  // We can only handle one buffer for now
  assert(model.buffers.size() == 1);

  NVVK_CHECK(allocator->createBuffer(
      bGltfData,
      std::span<const unsigned char>(model.buffers[0].data).size_bytes(),
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));

  NVVK_CHECK(m_core_manager->getStagingUploader().appendBuffer(
      bGltfData, 0, std::span<const unsigned char>(model.buffers[0].data)));
  NVVK_DBG_NAME(bGltfData.buffer);

  uint32_t bufferIndex = static_cast<uint32_t>(m_data.bGltfDatas.size());
  m_data.bGltfDatas.push_back(bGltfData);
  return {(uint8_t*) bGltfData.address, bufferIndex};
}

/**********************************************************/
void VulkanSceneAssetManager::addMeshes(size_t count, BufferID bufferIndex)
/**********************************************************/
{
  m_data.meshToBufferIndex.insert(m_data.meshToBufferIndex.end(), count,
                                  bufferIndex);
}

/**********************************************************/
void VulkanSceneAssetManager::createGltfSceneInfoBuffer(
    gltf::Scene& sceneResource)
/**********************************************************/
{
  SCOPED_TIMER(__FUNCTION__);

  auto& stagingUploader = m_core_manager->getStagingUploader();
  nvvk::ResourceAllocator* allocator = stagingUploader.getResourceAllocator();

  // 1. Define common usage flags to avoid clutter
  const VkBufferUsageFlags2KHR storageUsage =
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;

  const VkBufferUsageFlags2KHR uniformUsage =
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;

  // 2. Helper lambda to handle the generic Create -> Name -> Upload pattern
  auto uploadBuffer =
      [&](nvvk::Buffer& buffer, auto&& dataSpan, VkBufferUsageFlags2KHR usage)
  {
    if (dataSpan.empty())
      return;

    // Create the GPU buffer
    allocator->createBuffer(buffer, dataSpan.size_bytes(), usage);
    NVVK_DBG_NAME(buffer.buffer);

    // Schedule upload to GPU
    NVVK_CHECK(stagingUploader.appendBuffer(buffer, 0, dataSpan));
  };

  // 3. Process the buffers cleanly
  uploadBuffer(m_data.bMeshes, std::span(sceneResource.meshes), storageUsage);
  uploadBuffer(m_data.bInstances, std::span(sceneResource.instances),
               storageUsage);
  uploadBuffer(m_data.bMaterials, std::span(sceneResource.materials),
               storageUsage);

  // SceneInfo needs a span of size 1 created manually
  uploadBuffer(
      m_data.bSceneInfo,
      std::span<const shaderio::GltfSceneInfo>(&sceneResource.sceneInfo, 1),
      uniformUsage);
}

/**********************************************************/
nvvk::Image VulkanSceneAssetManager::loadAndCreateImage(
    VkCommandBuffer cmd, nvvk::StagingUploader& staging, VkDevice device,
    const std::filesystem::path& filename, bool sRgb)
/**********************************************************/
{
  int w, h, comp, req_comp{4};
  std::string filenameUtf8 = nvutils::utf8FromPath(filename);
  const stbi_uc* data =
      stbi_load(filenameUtf8.c_str(), &w, &h, &comp, req_comp);
  assert((data != nullptr) && "Could not load texture image!");

  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.format = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {uint32_t(w), uint32_t(h), 1};
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  nvvk::ResourceAllocator* allocator = staging.getResourceAllocator();
  const std::span dataSpan(data, w * h * req_comp);
  nvvk::Image texture;
  NVVK_CHECK(allocator->createImage(texture, imageInfo,
                                    DEFAULT_VkImageViewCreateInfo));
  NVVK_CHECK(staging.appendImage(texture, dataSpan,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  return texture;
}

/**********************************************************/
void VulkanSceneAssetManager::processGltfNodes(gltf::Scene& sceneResource,
                                               const tinygltf::Model& model,
                                               uint32_t meshOffset)
/**********************************************************/
{

  // 3. Process Nodes (Instances)
  // if (importInstance)
  // {
  //   uint32_t meshOffset =
  //       uint32_t(sceneResource.meshes.size()) -
  //       uint32_t(model.meshes.size());
  //   processGltfNodes(sceneResource, model, meshOffset);
  // }

  // Recursive lambda for node traversal
  std::function<void(const tinygltf::Node&, const glm::mat4&)> processNode =
      [&](const tinygltf::Node& node, const glm::mat4& parentTransform)
  {
    glm::mat4 nodeTransform = parentTransform;

    if (!node.matrix.empty())
    {
      glm::mat4 matrix = glm::make_mat4(node.matrix.data());
      nodeTransform = parentTransform * matrix;
    }
    else
    {
      if (!node.translation.empty())
      {
        glm::vec3 translation = glm::make_vec3(node.translation.data());
        nodeTransform = glm::translate(nodeTransform, translation);
      }
      if (!node.rotation.empty())
      {
        glm::quat rotation = glm::make_quat(node.rotation.data());
        nodeTransform = nodeTransform * glm::mat4_cast(rotation);
      }
      if (!node.scale.empty())
      {
        glm::vec3 scale = glm::make_vec3(node.scale.data());
        nodeTransform = glm::scale(nodeTransform, scale);
      }
    }

    if (node.mesh != -1)
    {
      shaderio::GltfInstance instance{};
      instance.meshIndex = node.mesh + meshOffset;
      instance.transform = nodeTransform;
      sceneResource.instances.push_back(instance);
    }

    for (int childIdx : node.children)
    {
      if (childIdx >= 0 && childIdx < static_cast<int>(model.nodes.size()))
      {
        processNode(model.nodes[childIdx], nodeTransform);
      }
    }
  };

  // Traverse root nodes
  for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
  {
    const tinygltf::Node& node = model.nodes[nodeIdx];
    bool isRootNode = true;
    for (const auto& otherNode : model.nodes)
    {
      for (int childIdx : otherNode.children)
      {
        if (childIdx == static_cast<int>(nodeIdx))
        {
          isRootNode = false;
          break;
        }
      }
      if (!isRootNode)
        break;
    }

    if (isRootNode)
    {
      processNode(node, glm::mat4(1.0f));
    }
  }
}
