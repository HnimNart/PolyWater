#include "SceneResources.hpp"
#include "core/Math.hpp"

#include <core/logger.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>

namespace {

/**********************************************************/
std::string getUniqueName(const std::map<std::string, uint32_t> &nameMap,
                          const std::string &baseName)
/**********************************************************/
{
  std::string candidate = baseName;
  uint32_t counter = 1;

  // Keep checking if the name exists. If it does, increment the counter.
  while (nameMap.find(candidate) != nameMap.end()) {
    candidate = baseName + "_" + std::to_string(counter++);
  }
  return candidate;
}

} // namespace

/**********************************************************/
void SceneResourcesManager::init(std::shared_ptr<IDeviceAssets> deviceResource)
/**********************************************************/
{
  m_device_resources = std::move(deviceResource);
}

/**********************************************************/
std::vector<MeshID> SceneResourcesManager::loadGltf(const std::string &name,
                                                    const std::string &filename)
/**********************************************************/
{
  tinygltf::Model model = gltf::loadModel(filename);

  if (model.meshes.empty()) {
    LOGE("Error: GLTF file %s contains no meshes.\n", filename.c_str());
    return {};
  }

  // 1. Calculate the Base ID
  MeshID baseID =
      static_cast<MeshID>(m_resources.meshes.size() + m_pendingMeshes);

  std::vector<MeshID> meshIDs;
  meshIDs.resize(model.meshes.size());
  for (size_t i = 0; i < model.meshes.size(); i++) {
    const auto &gltfMesh = model.meshes[i];
    std::string subMeshName = gltfMesh.name;
    if (subMeshName.empty()) {
      subMeshName = name + "_" + std::to_string(i); // Fallback if unnamed
    }

    // Combine user-provided name with internal mesh name
    std::string fullName = subMeshName;
    std::string uniqueName = getUniqueName(m_meshMap, fullName);
    MeshID currentID = baseID + static_cast<MeshID>(i);
    m_meshMap[uniqueName] = currentID;
    meshIDs.emplace_back(currentID);
    LOGD("Registered: %s -> ID %d\n", uniqueName.c_str(), currentID);
  }

  // 6. Update Counters and Storage
  m_pendingMeshes += model.meshes.size(); // <--- You need to track this!
  m_pendingModels.push_back(std::move(model));

  return meshIDs;
}

/**********************************************************/
TextureID SceneResourcesManager::loadTexture(const std::string &name,
                                             const std::string &filename)
/**********************************************************/
{
  IDeviceAssets::TextureID id = m_device_resources->reserveTextureSlot();
  m_pendingTextures.push_back({filename, id});
  auto retval_id = id + 1;
  std::string uniqueName = getUniqueName(m_textureMap, name);
  m_textureMap[uniqueName] = retval_id;
  return retval_id;
}
/**********************************************************/
InstanceID SceneResourcesManager::addInstance(shaderio::Instance &&instance,
                                              std::string name)
/**********************************************************/
{
  instance.transform = math::composeTransform(
      instance.translation, instance.rotation, instance.scale);
  m_resources.instances.push_back(instance);
  InstanceID id = static_cast<InstanceID>(m_resources.instances.size() - 1);

  if (name.empty()) {
    name = "Instance_" + std::to_string(id);
  }
  std::string uniqueName = getUniqueName(m_instanceMap, name);
  m_instanceMap[uniqueName] = id;
  return id;
}

/**********************************************************/
MaterialID SceneResourcesManager::addMaterial(shaderio::Material &&material,
                                              std::string name)
/**********************************************************/
{
  m_resources.materials.push_back(material);
  MaterialID id = static_cast<MaterialID>(m_resources.materials.size() - 1);

  if (name.empty()) {
    name = "Material_" + std::to_string(id);
  }
  std::string uniqueName = getUniqueName(m_materialMap, name);
  m_materialMap[uniqueName] = id;
  return id;
}

/**********************************************************/
void SceneResourcesManager::finalizeSceneResources()
/**********************************************************/
{
  // Start the Batch
  m_device_resources->beginUploading();

  // --- Process Pending Models ---
  for (const auto &model : m_pendingModels) {
    const auto [bufferAddr, bufferIndex] = m_device_resources->upload(model);
    const size_t startSize = m_resources.meshes.size();
    const auto [bmin, bmax] = gltf::computeModelBounds(model);
    m_resources.meshes.reserve(startSize + model.meshes.size());
    for (size_t i = 0; i < model.meshes.size(); i++) {
      auto mesh = gltf::extractGltfMesh(model, i);
      mesh.boxMin = bmin;
      mesh.boxMax = bmax;
      mesh.buffer = reinterpret_cast<uint8_t *>(bufferAddr);
      m_resources.meshes.emplace_back(mesh);
    }
    m_device_resources->addMeshes(model.meshes.size(), bufferIndex);
  }
  m_pendingModels.clear();
  m_pendingMeshes = 0;

  // --- Process Pending Textures ---
  for (const auto &[id, filename] : m_pendingTextures) {
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
  m_resources.instances.clear();
  m_resources.meshes.clear();
  m_resources.materials.clear();
  m_pendingMeshes = 0;
  m_pendingModels.clear();
  m_pendingTextures.clear();
  m_resources.sceneInfo = {};
  m_resources.sceneResources = {};
}

/**********************************************************/
void SceneResourcesManager::update(const CameraPtr &camera)
/**********************************************************/
{
  updateSceneInfo(camera);
}

/**********************************************************/
void SceneResourcesManager::updateSceneInfo(const CameraPtr &camera)
/**********************************************************/
{
  const glm::mat4 &viewMatrix = camera->getViewMatrix();
  const glm::mat4 &projMatrix = camera->getPerspectiveMatrix();

  m_resources.sceneInfo.viewMatrix = viewMatrix;
  m_resources.sceneInfo.projMatrix = projMatrix;
  m_resources.sceneInfo.viewProjMatrix = projMatrix * viewMatrix;
  m_resources.sceneInfo.projInvMatrix = glm::inverse(projMatrix);
  m_resources.sceneInfo.viewInvMatrix = glm::inverse(viewMatrix);
  m_resources.sceneInfo.cameraPosition = camera->getEye();
}

/**********************************************************/
void SceneResourcesManager::onMaterialChange()
/**********************************************************/
{
  m_device_resources->beginUploading();
  m_device_resources->update(m_resources.materials);
  m_device_resources->endUploading();
  setDirty(true);
}

/**********************************************************/
void SceneResourcesManager::onInstanceChange()
/**********************************************************/
{
  m_device_resources->beginUploading();
  m_device_resources->update(m_resources.instances);
  m_device_resources->endUploading();
  setDirty(true);
}

/**********************************************************/
const Scene &SceneResourcesManager::data() const
/**********************************************************/
{
  return m_resources;
}

/**********************************************************/
Scene &SceneResourcesManager::data()
/**********************************************************/
{
  return m_resources;
}

/**********************************************************/
void SceneResourcesManager::setSceneInfo(shaderio::SceneInfo sceneInfo)
/**********************************************************/
{
  m_resources.sceneInfo = std::move(sceneInfo);
}

/**********************************************************/
shaderio::SceneInfo &SceneResourcesManager::sceneInfo()
/**********************************************************/
{
  return m_resources.sceneInfo;
}

/**********************************************************/
const shaderio::SceneInfo &SceneResourcesManager::sceneInfo() const
/**********************************************************/
{
  return m_resources.sceneInfo;
}
