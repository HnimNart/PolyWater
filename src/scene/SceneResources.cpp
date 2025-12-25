#include "SceneResources.hpp"

#include <tinygltf/tiny_gltf.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>

#include "backend/vulkan/VulkanSceneResources.hpp"

// Explicit defaults needed here because of forward declared types in smart pointers
CpuSceneResources::CpuSceneResources() = default;
CpuSceneResources::~CpuSceneResources() = default;

void CpuSceneResources::init(std::shared_ptr<VulkanSceneResources> gpu_uploader)
{
  m_gpu_uploader = std::move(gpu_uploader);
}

tinygltf::Model CpuSceneResources::loadGltf(const std::string& filename)
{
  auto model = nvsamples::loadGltfResources(filename);
  // We cast to the internal MeshID type as per original code
  auto id = static_cast<VulkanSceneResources::MeshID>(
      m_gpu_uploader->upload_gltf_model(model, m_resources));

  return model;
}

uint32_t CpuSceneResources::loadTexture(const std::string& filename, VkCommandBuffer cmd)
{
  return m_gpu_uploader->upload_texture(filename, cmd);
}

CpuSceneResources::InstanceID CpuSceneResources::addInstance(const shaderio::GltfInstance& instance)
{
  m_resources.instances.push_back(instance);
  return static_cast<InstanceID>(m_resources.instances.size() - 1);
}

CpuSceneResources::MaterialID
CpuSceneResources::addMaterial(const shaderio::GltfMetallicRoughness& material)
{
  m_resources.materials.push_back(material);
  return static_cast<MaterialID>(m_resources.materials.size() - 1);
}

void CpuSceneResources::finalizeSceneResources(VkCommandBuffer cmd)
{
  m_gpu_uploader->finalizeSceneResources(m_resources, cmd);
}

void CpuSceneResources::clear()
{
  if (m_gpu_uploader)
  {
    m_gpu_uploader->clear(m_resources);
  }
}

const nvsamples::GltfSceneResource& CpuSceneResources::data() const
{
  return m_resources;
}

nvsamples::GltfSceneResource& CpuSceneResources::data()
{
  return m_resources;
}

shaderio::GltfSceneInfo& CpuSceneResources::scene_info()
{
  return m_resources.sceneInfo;
}

const shaderio::GltfSceneInfo& CpuSceneResources::scene_info() const
{
  return m_resources.sceneInfo;
}