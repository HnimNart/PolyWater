#include "SceneResources.hpp"
#include "core/Math.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <glm/gtx/string_cast.hpp>
#include <tinyobjloader/tiny_obj_loader.h>

#include <core/logger.hpp>
#include <core/path_utils.hpp>
#include <core/string_utils.h>
#include <shaders/shared/bindings.h>

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
  return common::trim(candidate);
}

} // namespace

/**********************************************************/
void SceneResourcesManager::init(std::shared_ptr<IDeviceAssets> deviceResource)
/**********************************************************/
{
  m_device_resources = std::move(deviceResource);
}

/**********************************************************/
std::vector<MeshID>
SceneResourcesManager::loadModel(const std::string &name,
                                 const std::string &filename)
/**********************************************************/
{
  // 1. Extract Extension
  std::string ext = common::getExtension(filename);
  common::toLower(ext);

  // 3. Dispatch
  if (ext == ".gltf" || ext == ".glb") {
    LOGD("Loading GLTF: %s", filename.c_str());
    return loadGltf(name, filename);
  } else if (ext == ".obj") {
    LOGD("Loading OBJ: %s", filename.c_str());
    return loadObj(name, filename);
  } else {
    LOGE("Error: Unsupported file extension '%s' for file '%s'", ext.c_str(),
         filename.c_str());
    return {};
  }
}

/**********************************************************/
MeshID SceneResourcesManager::getNextFreeMeshID()
/**********************************************************/
{
  return static_cast<MeshID>(m_resources.meshes.size() + m_pendingMeshes);
}

/**********************************************************/
std::vector<MeshID> SceneResourcesManager::loadGltf(const std::string &name,
                                                    const std::string &filename)
/**********************************************************/
{
  tinygltf::Model model = gltf::loadModel(filename);
  if (model.meshes.empty()) {
    LOGE("Error: GLTF file %s contains no meshes.", filename.c_str());
    return {};
  }

  MeshID baseID = getNextFreeMeshID();

  std::vector<MeshID> meshIDs;
  meshIDs.resize(model.meshes.size());
  for (size_t i = 0; i < model.meshes.size(); i++) {
    const auto &gltfMesh = model.meshes[i];
    std::string subMeshName = gltfMesh.name;
    if (subMeshName.empty()) {
      subMeshName = name + "_" + std::to_string(i); // Fallback if unnamed
    }

    std::string fullName = subMeshName;
    std::string uniqueName = getUniqueName(m_meshMap, fullName);
    MeshID currentID = baseID + static_cast<MeshID>(i);
    m_meshMap[uniqueName] = currentID;
    meshIDs.emplace_back(currentID);
    LOGD("Registered: %s -> ID %d", uniqueName.c_str(), currentID);
  }

  // 6. Update Counters and Storage
  m_pendingMeshes += model.meshes.size();
  m_loadOrder.push_back(
      {PendingMeshTask::Type::GLTF, m_pendingGltfModels.size()});
  m_pendingGltfModels.push_back(std::move(model));

  return meshIDs;
}

/**********************************************************/
std::vector<MeshID> SceneResourcesManager::loadObj(const std::string &name,
                                                   const std::string &filename)
/**********************************************************/
{
  // 1. Call Helper
  auto loadedShapes = obj::loadObjPrimitives(filename);
  if (loadedShapes.meshes.empty()) {
    return {};
  }

  auto &meshes = loadedShapes.meshes;
  auto &materials = loadedShapes.materials;

  MeshID baseID = getNextFreeMeshID();

  // Process materials
  std::vector<MaterialID> matIdMap;
  matIdMap.reserve(materials.size());
  for (auto &material : materials) {
    if (!material.diffuseTexturePath.empty()) {
      TextureID texId =
          loadTexture(material.name, core::findFile(material.diffuseTexturePath,
                                                    common::getTextureDir()));
      material.pbrData.baseColorTextureIndex = texId;
    }
    MaterialID materialId =
        addMaterial(std::move(material.pbrData), material.name);
    matIdMap.emplace_back(materialId);
  }

  // Process the meshes now
  std::vector<MeshID> meshIDs;
  meshIDs.reserve(meshes.size());
  for (size_t i = 0; i < meshes.size(); i++) {
    auto &[shapeName, meshPrimitive, materialIdx] = meshes[i];

    std::string subMeshName = shapeName;
    if (subMeshName.empty()) {
      subMeshName = name + "_" + std::to_string(i);
    }

    std::string uniqueName = getUniqueName(m_meshMap, subMeshName);
    MeshID currentID = baseID + static_cast<MeshID>(i);

    m_meshMap[uniqueName] = currentID;
    meshIDs.push_back(currentID);

    LOGD("Registered OBJ: %s -> ID %d", uniqueName.c_str(), currentID);
    m_loadOrder.push_back(
        {PendingMeshTask::Type::PRIMITIVE, m_pendingPrimitives.size()});
    m_pendingPrimitives.push_back(std::move(meshPrimitive));

    // Add instance
    shaderio::Instance inst;
    if (materialIdx >= 0 && materialIdx < matIdMap.size()) {
      inst.materialIndex = matIdMap[materialIdx];
    } else {
      inst.materialIndex = 0; // Use a default material ID
    }

    inst.meshIndex = currentID;
    inst.hit_group = MaterialType::eDiffuse;
    addInstance(std::move(inst), uniqueName);
  }
  m_pendingMeshes += meshes.size();

  return meshIDs;
}

/**********************************************************/
TextureID SceneResourcesManager::loadTexture(const std::string &name,
                                             const std::string &filename)
/**********************************************************/
{
  IDeviceAssets::TextureID id = m_device_resources->reserveTextureSlot();
  assert(id < MAX_SCENE_TEXTURES);
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
  // 1. Prepare the transform
  instance.transform = math::composeTransform(
      instance.translation, instance.rotation, instance.scale);

  // 2. Clean the name
  name = common::trim(name);

  // 3. Check for existing instance
  auto it = m_instanceMap.find(name);
  if (it != m_instanceMap.end()) {
    InstanceID existingID = it->second;
    LOGD("[SceneResourcesManager] Replacing Instance: '%s' (ID: %d)",
         name.c_str(), existingID);
    m_resources.instances[existingID] = instance;
    return existingID;
  }

  // 4. If new, push back as normal
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
  name = common::trim(name);
  auto it = m_materialMap.find(name);
  if (it != m_materialMap.end()) {
    MaterialID existingID = it->second;
    LOGD("[SceneResourcesManager] Overwriting existing material: '%s' (ID: %d)",
         name.c_str(), existingID);
    m_resources.materials[existingID] = material;
    setDirty(true);
    return existingID;
  }

  m_resources.materials.push_back(std::move(material));
  MaterialID id = static_cast<MaterialID>(m_resources.materials.size() - 1);
  if (name.empty()) {
    name = "Material_" + std::to_string(id);
  }

  std::string uniqueName = getUniqueName(m_materialMap, name);
  m_materialMap[uniqueName] = id;
  return id;
}

/**********************************************************/
void SceneResourcesManager::uploadGltfMesh(const tinygltf::Model &model)
/**********************************************************/
{
  // --- Process Pending Models ---
  assert(model.buffers.size() == 1);
  std::span<const uint8_t> data = model.buffers[0].data;
  const auto [bufferAddr, bufferIndex] = m_device_resources->upload(data);
  const size_t startSize = m_resources.meshes.size();
  m_resources.meshes.reserve(startSize + model.meshes.size());
  for (size_t i = 0; i < model.meshes.size(); i++) {
    auto mesh = gltf::extractGltfMesh(model, i);
    mesh.bbox = gltf::getMeshBounds(model, i);
    mesh.buffer = reinterpret_cast<uint8_t *>(bufferAddr);
    m_resources.meshes.emplace_back(mesh);
  }
  m_device_resources->addMeshes(model.meshes.size(), bufferIndex);
  m_pendingMeshes -= model.meshes.size();
}

/**********************************************************/
void SceneResourcesManager::uploadPrimitiveMesh(
    const core::PrimitiveMesh &meshData)
/**********************************************************/
{
  std::vector<uint8_t> stagingBuffer = obj::packMeshToBuffer(meshData);
  const auto [bufferAddr, bufferIndex] =
      m_device_resources->upload(std::span(stagingBuffer));
  shaderio::MeshPrimitive gpuMesh = obj::createGpuMeshFromPrimitive(meshData);
  gpuMesh.buffer = bufferAddr;
  gpuMesh.bbox = obj::computeMeshBounds(meshData);
  m_resources.meshes.emplace_back(gpuMesh);

  // 5. Update Device Resources (1 mesh added)
  m_device_resources->addMeshes(1, bufferIndex);
  m_pendingMeshes -= 1;
}

/**********************************************************/
void SceneResourcesManager::finalizePendingTextures()
/**********************************************************/
{
  for (const auto &[id, filename] : m_pendingTextures) {
    m_device_resources->uploadTexture(id, filename);
  }
  m_pendingTextures.clear();
}

/**********************************************************/
void SceneResourcesManager::finalizeSceneResources()
/**********************************************************/
{
  // Start the Batch
  m_device_resources->beginUploading();

  // Upload models
  size_t gltfIdx = 0;
  size_t primIdx = 0;
  for (const auto &task : m_loadOrder) {
    switch (task.type) {
    case PendingMeshTask::Type::GLTF: {
      uploadGltfMesh(m_pendingGltfModels[gltfIdx++]);
      break;
    }
    case PendingMeshTask::Type::PRIMITIVE: {
      uploadPrimitiveMesh(m_pendingPrimitives[primIdx++]);
      break;
    }
    default:
      assert(0 && "Recieved unknown task type");
    }
  }

  m_loadOrder.clear();
  m_pendingGltfModels.clear();
  m_pendingPrimitives.clear();
  assert(m_pendingMeshes == 0);

  // Upload textures
  finalizePendingTextures();
  m_device_resources->finalizeSceneResources(m_resources);
  m_device_resources->endUploading();
}

/**********************************************************/
void SceneResourcesManager::clear()
/**********************************************************/
{
  m_materialMap.clear();
  m_resources = {};
  m_meshMap = {};
  m_textureMap = {};
  m_instanceMap = {};
  m_materialMap = {};

  m_pendingMeshes = 0;
  m_loadOrder.clear();
  m_pendingGltfModels.clear();
  m_pendingPrimitives.clear();
  m_pendingTextures.clear();
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
  m_resources.sceneInfo = sceneInfo;
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
