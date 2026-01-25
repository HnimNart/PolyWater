#include "SceneResources.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tinygltf/tiny_gltf.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>

SceneResourcesManager::SceneResourcesManager() = default;
SceneResourcesManager::~SceneResourcesManager() = default;

void SceneResourcesManager::init(std::shared_ptr<IDeviceAssets> deviceResource)
{
  m_device_resources = std::move(deviceResource);
}

void SceneResourcesManager::beginUploading()
{
  m_device_resources->beginUploading();
}

void SceneResourcesManager::endUploading()
{
  m_device_resources->endUploading();
}

tinygltf::Model SceneResourcesManager::loadGltf(const std::string& filename)
{
  auto model = gltf::load(filename);
  auto id = static_cast<IDeviceAssets::MeshID>(
      m_device_resources->uploadGltfModel(model, m_resources));

  return model;
}

IDeviceAssets::TextureID
SceneResourcesManager::loadTexture(const std::string& filename)
{
  return m_device_resources->uploadTexture(filename);
}

SceneResourcesManager::InstanceID
SceneResourcesManager::addInstance(const shaderio::GltfInstance& instance)
{
  m_resources.instances.push_back(instance);
  return static_cast<InstanceID>(m_resources.instances.size() - 1);
}

SceneResourcesManager::MaterialID SceneResourcesManager::addMaterial(
    const shaderio::GltfMetallicRoughness& material)
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

void SceneResourcesManager::updateSceneInfo(const CameraPtr& camera)
{
  const glm::mat4& viewMatrix = camera->getViewMatrix();
  const glm::mat4& projMatrix = camera->getPerspectiveMatrix();

  m_resources.sceneInfo.viewMatrix = viewMatrix;
  m_resources.sceneInfo.projMatrix = projMatrix;
  m_resources.sceneInfo.viewProjMatrix = projMatrix * viewMatrix;
  m_resources.sceneInfo.projInvMatrix = glm::inverse(projMatrix);
  m_resources.sceneInfo.viewInvMatrix = glm::inverse(viewMatrix);
  m_resources.sceneInfo.cameraPosition = camera->getEye();
}

const gltf::Scene& SceneResourcesManager::data() const
{
  return m_resources;
}

gltf::Scene& SceneResourcesManager::data()
{
  return m_resources;
}

shaderio::GltfSceneInfo& SceneResourcesManager::sceneInfo()
{
  return m_resources.sceneInfo;
}

const shaderio::GltfSceneInfo& SceneResourcesManager::sceneInfo() const
{
  return m_resources.sceneInfo;
}
