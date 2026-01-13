#include "SceneResources.hpp"

// 1. Define implementations here (and ONLY here)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tinygltf/tiny_gltf.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>

#include "backend/vulkan/VulkanSceneResources.hpp"

// Explicit defaults needed here because of forward declared types in smart pointers
SceneResources::SceneResources() = default;
SceneResources::~SceneResources() = default;

void SceneResources::init(std::shared_ptr<VulkanSceneResources> gpu_uploader)
{
  m_gpu_uploader = std::move(gpu_uploader);
}

tinygltf::Model SceneResources::loadGltf(const std::string& filename)
{
  auto model = nvsamples::loadGltfResources(filename);
  // We cast to the internal MeshID type as per original code
  auto id = static_cast<VulkanSceneResources::MeshID>(
      m_gpu_uploader->upload_gltf_model(model, m_resources));

  return model;
}

void SceneResources::begin_uploading()
{
  m_gpu_uploader->begin_uploading();
}

void SceneResources::end_uploading()
{
  m_gpu_uploader->end_uploading();
}

uint32_t SceneResources::loadTexture(const std::string& filename)
{
  return m_gpu_uploader->upload_texture(filename);
}

SceneResources::InstanceID SceneResources::addInstance(const shaderio::GltfInstance& instance)
{
  m_resources.instances.push_back(instance);
  return static_cast<InstanceID>(m_resources.instances.size() - 1);
}

SceneResources::MaterialID
SceneResources::addMaterial(const shaderio::GltfMetallicRoughness& material)
{
  m_resources.materials.push_back(material);
  return static_cast<MaterialID>(m_resources.materials.size() - 1);
}

void SceneResources::finalizeSceneResources()
{
  m_gpu_uploader->finalizeSceneResources(m_resources);
}

void SceneResources::clear()
{
  if (m_gpu_uploader)
  {
    m_gpu_uploader->clear(m_resources);
  }
}

const nvsamples::GltfSceneResource& SceneResources::data() const
{
  return m_resources;
}

nvsamples::GltfSceneResource& SceneResources::data()
{
  return m_resources;
}

shaderio::GltfSceneInfo& SceneResources::scene_info()
{
  return m_resources.sceneInfo;
}

const shaderio::GltfSceneInfo& SceneResources::scene_info() const
{
  return m_resources.sceneInfo;
}
