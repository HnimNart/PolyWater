#include "SceneResources.hpp"

#include <nvutils/logger.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tinygltf/tiny_gltf.h>

#include <nvvk/check_error.hpp>
#include <nvvk/debug_util.hpp>
#include <nvvk/sampler_pool.hpp>

/**********************************************************/
void SceneResourcesManager::init(std::shared_ptr<IDeviceAssets> deviceResource)
/**********************************************************/
{
  m_device_resources = std::move(deviceResource);
}

/**********************************************************/
tinygltf::Model SceneResourcesManager::loadGltf(const std::string& filename)
/**********************************************************/
{
  tinygltf::Model model = gltf::loadModel(filename);
  m_pendingModels.push_back(model);
  return model;
}

/**********************************************************/
IDeviceAssets::TextureID
SceneResourcesManager::loadTexture(const std::string& filename)
/**********************************************************/
{
  IDeviceAssets::TextureID id = m_device_resources->reserveTextureSlot();
  m_pendingTextures.push_back({filename, id});
  return id + 1;  // Have to add one to offset the index on GPU
}

/**********************************************************/
SceneResourcesManager::InstanceID
SceneResourcesManager::addInstance(const shaderio::Instance& instance)
/**********************************************************/
{
  m_resources.instances.push_back(instance);
  return static_cast<InstanceID>(m_resources.instances.size() - 1);
}

/**********************************************************/
SceneResourcesManager::MaterialID
SceneResourcesManager::addMaterial(const shaderio::Material& material)
/**********************************************************/
{
  m_resources.materials.push_back(material);
  return static_cast<MaterialID>(m_resources.materials.size() - 1);
}

/**********************************************************/
void SceneResourcesManager::finalizeSceneResources()
/**********************************************************/
{
  // Start the Batch
  m_device_resources->beginUploading();

  // --- Process Pending Models ---
  for (const auto& model : m_pendingModels)
  {
    auto [bufferAddr, bufferIndex] = m_device_resources->upload(model);
    size_t startSize = m_resources.meshes.size();
    m_resources.meshes.reserve(startSize + model.meshes.size());
    for (size_t i = 0; i < model.meshes.size(); i++)
    {
      auto mesh = gltf::extractGltfMesh(model, i);
      mesh.gltfBuffer = reinterpret_cast<uint8_t*>(bufferAddr);
      m_resources.meshes.emplace_back(mesh);
    }
    m_device_resources->addMeshes(model.meshes.size(), bufferIndex);
  }
  m_pendingModels.clear();

  // --- Process Pending Textures ---
  for (const auto& [id, filename] : m_pendingTextures)
  {
    m_device_resources->uploadTexture(id, filename);
  }
  m_pendingTextures.clear();
  m_device_resources->finalizeSceneResources(m_resources);
  m_device_resources->endUploading();
}

/**********************************************************/
void SceneResourcesManager::clear()
/**********************************************************/
{
}

/**********************************************************/
void SceneResourcesManager::update(const CameraPtr& camera)
/**********************************************************/
{
  updateSceneInfo(camera);
}

/**********************************************************/
void SceneResourcesManager::updateSceneInfo(const CameraPtr& camera)
/**********************************************************/
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

/**********************************************************/
const Scene& SceneResourcesManager::data() const
/**********************************************************/
{
  return m_resources;
}

/**********************************************************/
Scene& SceneResourcesManager::data()
/**********************************************************/
{
  return m_resources;
}

/**********************************************************/
shaderio::SceneInfo& SceneResourcesManager::sceneInfo()
/**********************************************************/
{
  return m_resources.sceneInfo;
}

/**********************************************************/
const shaderio::SceneInfo& SceneResourcesManager::sceneInfo() const
/**********************************************************/
{
  return m_resources.sceneInfo;
}
