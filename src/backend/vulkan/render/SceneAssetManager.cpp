#include "SceneAssetManager.hpp"

#include <stb/stb_image.h>
#include <volk.h>

#include <nvutils/file_operations.hpp>
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

VulkanSceneAssetManager::VulkanSceneAssetManager(VulkanContextManager* coreManager)
{
  m_core_manager = coreManager;
  // Acquiring the texture sampler which will be used for displaying the GBuffer
  m_samplerPool.init(m_core_manager->getDevice());
}

void VulkanSceneAssetManager::beginUploading()
{
  m_cmd = m_core_manager->startSingleTimeCmd();
}
void VulkanSceneAssetManager::endUploading()
{
  assert(m_cmd != VK_NULL_HANDLE);
  m_core_manager->endSingleTimeCmd(m_cmd);
  m_cmd = VK_NULL_HANDLE;
}

void VulkanSceneAssetManager::deinit()
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

VulkanSceneAssetManager::MeshID
VulkanSceneAssetManager::uploadGltfModel(const tinygltf::Model& model, gltf::Scene& resources)
{
  importGltfData(resources, m_data, model, m_core_manager->getStagingUploader());
  m_meshIdCounter++;
  return m_meshIdCounter - 1;
}

VulkanSceneAssetManager::TextureID
VulkanSceneAssetManager::uploadTexture(const std::string& filepath)
{
  nvvk::Image texture = loadAndCreateImage(m_cmd, m_core_manager->getStagingUploader(),
                                           m_core_manager->getDevice(), filepath);

  NVVK_DBG_NAME(texture.image);
  m_samplerPool.acquireSampler(texture.descriptor.sampler);
  m_textures.emplace_back(texture);
  return static_cast<TextureID>(m_textures.size() - 1);
}

void VulkanSceneAssetManager::updateDescriptors(nvvk::DescriptorPack& descriptorPack)
{
  if (m_textures.empty())
  {
    return;
  }

  nvvk::WriteSetContainer write{};
  auto write_set = descriptorPack.makeWrite(shaderio::BindingPoints::eTextures, 0, 1,
                                            static_cast<uint32_t>(m_textures.size()));

  write.append(write_set, m_textures.data());

  vkUpdateDescriptorSets(m_core_manager->getDevice(), write.size(), write.data(), 0, nullptr);
}

void VulkanSceneAssetManager::finalizeSceneResources(gltf::Scene& resources)
{
  createGltfSceneInfoBuffer(
      resources, m_data,
      m_core_manager->getStagingUploader());  // Create buffers for the scene data (GPU buffers)
  m_core_manager->getStagingUploader().cmdUploadAppended(
      m_cmd);  // Upload the scene information to the GPU

  // Update the pointers to uploaded data
  resources.sceneInfo.instances =
      (shaderio::GltfInstance*) m_data.bInstances.address;  // Address of the instance buffer
  resources.sceneInfo.meshes =
      (shaderio::GltfMesh*) m_data.bMeshes.address;  // Address of the mesh buffer
  resources.sceneInfo.materials = (shaderio::GltfMetallicRoughness*)
                                      m_data.bMaterials.address;  // Address of the material buffer
}

// This is a utility function to convert a primitive mesh to a GltfMeshResource.
void VulkanSceneAssetManager::primitiveMeshToResource(gltf::Scene& sceneResource,
                                                      VulkanSceneGpuData& deviceResource,
                                                      nvvk::StagingUploader& stagingUploader,
                                                      const nvutils::PrimitiveMesh& primMesh)
{

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
  uint32_t bufferIndex = static_cast<uint32_t>(deviceResource.bGltfDatas.size());
  deviceResource.bGltfDatas.push_back(gltfData);

  // Upload vertices first (at offset 0)
  stagingUploader.appendBuffer(gltfData, 0, std::span(primMesh.vertices));

  // Upload triangles after vertices
  stagingUploader.appendBuffer(gltfData, verticesSize, std::span(primMesh.triangles));

  // Set up the TriangleMesh structure with proper BufferView offsets
  shaderio::GltfMesh mesh;
  mesh.triMesh.positions = {.offset = 0,
                            .count = static_cast<uint32_t>(primMesh.vertices.size()),
                            .byteStride = sizeof(nvutils::PrimitiveVertex)};

  mesh.triMesh.normals = {.offset = offsetof(nvutils::PrimitiveVertex, nrm),
                          .count = static_cast<uint32_t>(primMesh.vertices.size()),
                          .byteStride = sizeof(nvutils::PrimitiveVertex)};

  mesh.triMesh.texCoords = {.offset = offsetof(nvutils::PrimitiveVertex, tex),
                            .count = static_cast<uint32_t>(primMesh.vertices.size()),
                            .byteStride = sizeof(nvutils::PrimitiveVertex)};

  mesh.triMesh.indices = {
      .offset = static_cast<uint32_t>(verticesSize),
      .count = static_cast<uint32_t>(primMesh.triangles.size() * 3),  // 3 indices per triangle
      .byteStride = sizeof(uint32_t)};

  // Set the buffer address and index type
  mesh.gltfBuffer = (uint8_t*) gltfData.address;
  mesh.indexType = VK_INDEX_TYPE_UINT32;  // Assuming uint32_t indices
  sceneResource.meshes.push_back(mesh);

  // Update the mapping from mesh index to buffer index
  deviceResource.meshToBufferIndex.push_back(bufferIndex);
}

// This is a utility function to import the GLTF data into the scene resource.
// It is a very simple function that just imports the GLTF data into the scene resource.
// It has strong limitations, like the mesh must have only one primitive, and the primitive must be
// a triangle primitive. But it allow to import the GLTF data into the scene resource with a single
// function call, and call it again to import another scene.
void VulkanSceneAssetManager::importGltfData(gltf::Scene& sceneResource,
                                             VulkanSceneGpuData& deviceResource,
                                             const tinygltf::Model& model,
                                             nvvk::StagingUploader& stagingUploader,
                                             bool importInstance /*= false*/)
{
  SCOPED_TIMER(__FUNCTION__);

  const uint32_t meshOffset = uint32_t(sceneResource.meshes.size());

  // Lambda for element byte size calculation
  auto getElementByteSize = [](int type) -> uint32_t
  {
    return type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2U
           : type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4U
           : type == TINYGLTF_COMPONENT_TYPE_FLOAT        ? 4U
                                                          : 0U;
  };

  // Lambda for type size calculation
  auto getTypeSize = [](int type) -> uint32_t
  {
    return type == TINYGLTF_TYPE_VEC2   ? 2U
           : type == TINYGLTF_TYPE_VEC3 ? 3U
           : type == TINYGLTF_TYPE_VEC4 ? 4U
           : type == TINYGLTF_TYPE_MAT2 ? 4U * 2U
           : type == TINYGLTF_TYPE_MAT3 ? 4U * 3U
           : type == TINYGLTF_TYPE_MAT4 ? 4U * 4U
                                        : 0U;
  };

  // Lambda for extracting attributes, like positions, normals, colors, etc.
  auto extractAttribute =
      [&](const std::string& name, shaderio::BufferView& attr, const tinygltf::Primitive& primitive)
  {
    if (!primitive.attributes.contains(name))
    {
      attr.offset = -1;
      return;
    }
    const tinygltf::Accessor& acc = model.accessors[primitive.attributes.at(name)];
    const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
    assert((acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) && "Should be floats");
    attr = {
        .offset = uint32_t(bv.byteOffset + acc.byteOffset),
        .count = uint32_t(acc.count),
        .byteStride =
            uint32_t(bv.byteStride ? uint32_t(bv.byteStride)
                                   : getTypeSize(acc.type) * getElementByteSize(acc.componentType)),
    };
  };

  // Upload the scene resource to the GPU
  nvvk::Buffer bGltfData;
  uint32_t bufferIndex{};
  {
    nvvk::ResourceAllocator* allocator = stagingUploader.getResourceAllocator();

    // The GLTF buffer is used to store the geometry data (indices, positions, normals, etc.)
    // The flags are set to allow the buffer to be used as a vertex buffer, index buffer, storage
    // buffer, and for acceleration structure build input read-only.
    NVVK_CHECK(allocator->createBuffer(
        bGltfData, std::span<const unsigned char>(model.buffers[0].data).size_bytes(),
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR));  // #RT
    NVVK_CHECK(stagingUploader.appendBuffer(bGltfData, 0,
                                            std::span<const unsigned char>(model.buffers[0].data)));
    NVVK_DBG_NAME(bGltfData.buffer);

    bufferIndex = static_cast<uint32_t>(deviceResource.bGltfDatas.size());
    deviceResource.bGltfDatas.push_back(bGltfData);
  }

  for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx)
  {
    shaderio::GltfMesh mesh{};

    const tinygltf::Mesh& tinyMesh = model.meshes[meshIdx];
    const tinygltf::Primitive& primitive = tinyMesh.primitives.front();
    assert((tinyMesh.primitives.size() == 1 && primitive.mode == TINYGLTF_MODE_TRIANGLES) &&
           "Must have one triangle primitive");

    // Extract indices
    auto& accessor = model.accessors[primitive.indices];
    auto& bufferView = model.bufferViews[accessor.bufferView];
    assert((accessor.count % 3 == 0) && "Should be a multiple of 3");
    mesh.triMesh.indices = {
        .offset = uint32_t(bufferView.byteOffset + accessor.byteOffset),
        .count = uint32_t(accessor.count),
        .byteStride = uint32_t(bufferView.byteStride ? bufferView.byteStride
                                                     : getElementByteSize(accessor.componentType)),
    };
    mesh.indexType = accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT
                         ? VK_INDEX_TYPE_UINT16
                         : VK_INDEX_TYPE_UINT32;

    // Set the buffer address
    mesh.gltfBuffer = (uint8_t*) bGltfData.address;

    // Extract attributes
    extractAttribute("POSITION", mesh.triMesh.positions, primitive);
    extractAttribute("NORMAL", mesh.triMesh.normals, primitive);
    extractAttribute("COLOR_0", mesh.triMesh.colorVert, primitive);
    extractAttribute("TEXCOORD_0", mesh.triMesh.texCoords, primitive);
    extractAttribute("TANGENT", mesh.triMesh.tangents, primitive);

    sceneResource.meshes.emplace_back(mesh);

    // Update the mapping from mesh index to buffer index
    deviceResource.meshToBufferIndex.push_back(bufferIndex);
  }

  if (importInstance)
  {
    // Extract instances with proper hierarchical transformation handling
    std::function<void(const tinygltf::Node&, const glm::mat4&)> processNode =
        [&](const tinygltf::Node& node, const glm::mat4& parentTransform)
    {
      glm::mat4 nodeTransform = parentTransform;

      // Apply node transform
      if (!node.matrix.empty())
      {
        // Use matrix if available
        glm::mat4 matrix = glm::make_mat4(node.matrix.data());
        nodeTransform = parentTransform * matrix;
      }
      else
      {
        // Apply TRS if matrix is not available
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

      // Create instance for this node if it has a mesh
      if (node.mesh != -1)
      {
        const tinygltf::Mesh& tinyMesh = model.meshes[node.mesh];
        const tinygltf::Primitive& primitive = tinyMesh.primitives.front();
        assert((tinyMesh.primitives.size() == 1 && primitive.mode == TINYGLTF_MODE_TRIANGLES) &&
               "Must have one triangle primitive");
        shaderio::GltfInstance instance{};
        instance.meshIndex = node.mesh + meshOffset;
        instance.transform = nodeTransform;
        sceneResource.instances.push_back(instance);
      }

      // Process children
      for (int childIdx : node.children)
      {
        if (childIdx >= 0 && childIdx < static_cast<int>(model.nodes.size()))
        {
          processNode(model.nodes[childIdx], nodeTransform);
        }
      }
    };

    // Process all root nodes (nodes with no parent)
    for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
    {
      const tinygltf::Node& node = model.nodes[nodeIdx];
      // Check if this is a root node (not referenced as a child)
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
        processNode(node, glm::mat4(1.0f));  // Start with identity matrix
      }
    }
  }
}

// This function creates the scene info buffer
// It is consolidating all the mesh information into a single buffer, the same for the instances and
// materials. This is to avoid having to create multiple buffers for the scene. The scene info
// buffer is used to pass the scene information to the shader. The mesh buffer is used to pass the
// mesh information to the shader. The instance buffer is used to pass the instance information to
// the shader. The material buffer is used to pass the material information to the shader.
void VulkanSceneAssetManager::createGltfSceneInfoBuffer(gltf::Scene& sceneResource,
                                                        VulkanSceneGpuData& deviceResource,
                                                        nvvk::StagingUploader& stagingUploader)
{
  SCOPED_TIMER(__FUNCTION__);

  nvvk::ResourceAllocator* allocator = stagingUploader.getResourceAllocator();

  // Create all mesh buffers
  allocator->createBuffer(deviceResource.bMeshes, std::span(sceneResource.meshes).size_bytes(),
                          VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT);
  NVVK_DBG_NAME(deviceResource.bMeshes.buffer);
  NVVK_CHECK(stagingUploader.appendBuffer(
      deviceResource.bMeshes, 0, std::span<const shaderio::GltfMesh>(sceneResource.meshes)));

  // Create all instance buffers
  allocator->createBuffer(
      deviceResource.bInstances, std::span(sceneResource.instances).size_bytes(),
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
          VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT);
  NVVK_DBG_NAME(deviceResource.bInstances.buffer);
  NVVK_CHECK(stagingUploader.appendBuffer(
      deviceResource.bInstances, 0,
      std::span<const shaderio::GltfInstance>(sceneResource.instances)));

  // Create all material buffers
  allocator->createBuffer(
      deviceResource.bMaterials, std::span(sceneResource.materials).size_bytes(),
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
          VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT);
  NVVK_DBG_NAME(deviceResource.bMaterials.buffer);
  NVVK_CHECK(stagingUploader.appendBuffer(
      deviceResource.bMaterials, 0,
      std::span<const shaderio::GltfMetallicRoughness>(sceneResource.materials)));

  // Create the scene info buffer
  NVVK_CHECK(allocator->createBuffer(
      deviceResource.bSceneInfo,
      std::span<const shaderio::GltfSceneInfo>(&sceneResource.sceneInfo, 1).size_bytes(),
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT));
  NVVK_DBG_NAME(deviceResource.bSceneInfo.buffer);
  NVVK_CHECK(stagingUploader.appendBuffer(
      deviceResource.bSceneInfo, 0,
      std::span<const shaderio::GltfSceneInfo>(&sceneResource.sceneInfo, 1)));
}

nvvk::Image VulkanSceneAssetManager::loadAndCreateImage(VkCommandBuffer cmd,
                                                        nvvk::StagingUploader& staging,
                                                        VkDevice device,
                                                        const std::filesystem::path& filename,
                                                        bool sRgb)
{
  // Load the image from disk
  int w, h, comp, req_comp{4};
  std::string filenameUtf8 = nvutils::utf8FromPath(filename);
  const stbi_uc* data = stbi_load(filenameUtf8.c_str(), &w, &h, &comp, req_comp);
  assert((data != nullptr) && "Could not load texture image!");

  // Define how to create the image
  VkImageCreateInfo imageInfo = DEFAULT_VkImageCreateInfo;
  imageInfo.format = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {uint32_t(w), uint32_t(h), 1};
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  nvvk::ResourceAllocator* allocator = staging.getResourceAllocator();

  // Use the VMA allocator to create the image
  const std::span dataSpan(data, w * h * req_comp);
  nvvk::Image texture;
  NVVK_CHECK(allocator->createImage(texture, imageInfo, DEFAULT_VkImageViewCreateInfo));
  // texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  NVVK_CHECK(staging.appendImage(texture, dataSpan, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

  return texture;
}
