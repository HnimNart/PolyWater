#include "SceneResources.hpp"

// 1. Define implementations here (and ONLY here)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tinygltf/tiny_gltf.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>

// Explicit defaults needed here because of forward declared types in smart pointers
SceneResourcesManager::SceneResourcesManager() = default;
SceneResourcesManager::~SceneResourcesManager() = default;

void SceneResourcesManager::init(std::shared_ptr<IDeviceResources> device_resource)
{
  m_device_resources = std::move(device_resource);
}

tinygltf::Model SceneResourcesManager::loadGltf(const std::string& filename)
{
  auto model = nvsamples::loadGltfResources(filename);
  auto id = static_cast<IDeviceResources::MeshID>(
      m_device_resources->upload_gltf_model(model, m_resources));

  return model;
}

void SceneResourcesManager::begin_uploading()
{
  m_device_resources->begin_uploading();
}

void SceneResourcesManager::end_uploading()
{
  m_device_resources->end_uploading();
}

uint32_t SceneResourcesManager::loadTexture(const std::string& filename)
{
  return m_device_resources->upload_texture(filename);
}

SceneResourcesManager::InstanceID
SceneResourcesManager::addInstance(const shaderio::GltfInstance& instance)
{
  m_resources.instances.push_back(instance);
  return static_cast<InstanceID>(m_resources.instances.size() - 1);
}

SceneResourcesManager::MaterialID
SceneResourcesManager::addMaterial(const shaderio::GltfMetallicRoughness& material)
{
  m_resources.materials.push_back(material);
  return static_cast<MaterialID>(m_resources.materials.size() - 1);
}

void SceneResourcesManager::finalizeSceneResources()
{
  m_device_resources->finalizeSceneResources(m_resources);
}

void SceneResourcesManager::clear()
{
}

void SceneResourcesManager::update_scene_info(CameraPtr camera)
{
  const glm::mat4& viewMatrix = camera->getViewMatrix();
  const glm::mat4& projMatrix = camera->getPerspectiveMatrix();

  m_resources.sceneInfo.viewProjMatrix =
      projMatrix * viewMatrix;  // Combine the view and projection matrices
  m_resources.sceneInfo.projInvMatrix = glm::inverse(projMatrix);
  m_resources.sceneInfo.viewInvMatrix = glm::inverse(viewMatrix);
  m_resources.sceneInfo.cameraPosition = camera->getEye();
}

const nvsamples::GltfSceneResource& SceneResourcesManager::data() const
{
  return m_resources;
}

nvsamples::GltfSceneResource& SceneResourcesManager::data()
{
  return m_resources;
}

shaderio::GltfSceneInfo& SceneResourcesManager::scene_info()
{
  return m_resources.sceneInfo;
}

const shaderio::GltfSceneInfo& SceneResourcesManager::scene_info() const
{
  return m_resources.sceneInfo;
}
