#include "SceneResources.hpp"
#include "core/Math.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <glm/gtx/string_cast.hpp>
#include <tiny_gltf.h>
#include <tinyobjloader/tiny_obj_loader.h>

#include "models/gltf_utils.hpp"
#include "models/obj_utils.hpp"
#include "models/optimizer.hpp"

#include <core/Image.hpp>
#include <core/logger.hpp>
#include <core/path_utils.hpp>
#include <core/string_utils.h>
#include <shaders/shared/bindings.h>

#include "core/path_utils.hpp"

namespace {

/**********************************************************/
std::string
getUniqueName(const std::unordered_map<std::string, uint32_t> &nameMap,
              const std::string &baseName)
/**********************************************************/
{
  std::string candidate = baseName;
  uint32_t counter = 1;

  // Keep checking if the name exists. If it does, increment the counter.
  while (nameMap.find(candidate) != nameMap.end()) {
    candidate = baseName + "_" + std::to_string(counter++);
  }
  return core::trim(candidate);
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
SceneResourcesManager::loadModel(const std::string &filename, std::string name)
/**********************************************************/
{
  // 1. Extract Extension
  std::string ext = core::getExtension(filename);
  core::toLower(ext);

  if (name.empty()) {
    name = core::getLowercasedStem(filename);
  }

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
  return static_cast<MeshID>(m_scene_resources.meshes.size() + m_pendingMeshes);
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
  meshIDs.reserve(model.meshes.size());
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

  // Generate the optimized payload
  OptimizedPayload optimized = processAndOptimizeGltf(
      core::getLowercasedStem(filename), model, common::getCacheDir());

  // Update Counters
  m_pendingMeshes += model.meshes.size();
  m_pendingOptimizedMesh.push_back(std::move(optimized));

  return meshIDs;
}

/**********************************************************/
std::vector<MeshID> SceneResourcesManager::loadObj(const std::string &name,
                                                   const std::string &filename)
/**********************************************************/
{
  auto loadedShapes = obj::loadObjPrimitives(filename);
  if (loadedShapes.meshes.empty()) {
    return {};
  }

  auto &meshes = loadedShapes.meshes;
  auto &materials = loadedShapes.materials;

  MeshID baseID = getNextFreeMeshID();

  // 1. Process materials (unchanged)
  std::vector<MaterialID> matIdMap;
  matIdMap.reserve(materials.size());
  for (auto &material : materials) {
    if (!material.diffuseTexturePath.empty()) {
      TextureID texId =
          addTexture(material.name, core::findFile(material.diffuseTexturePath,
                                                   common::getTextureDir()));
      material.pbrData.baseColorTextureIndex = texId;
    }
    MaterialID materialId =
        addMaterial(std::move(material.pbrData), material.name);
    matIdMap.emplace_back(materialId);
  }

  std::vector<MeshID> meshIDs;
  meshIDs.reserve(meshes.size());

  // 2. Register geometry and instances
  for (size_t i = 0; i < meshes.size(); i++) {
    // Access directly from the LoadedMesh struct
    auto &shapeName = meshes[i].name;
    auto &materialIdx = meshes[i].materialIndex;

    // Registration logic
    std::string uniqueName = getUniqueName(
        m_meshMap,
        shapeName.empty() ? name + "_" + std::to_string(i) : shapeName);
    MeshID currentID = baseID + static_cast<MeshID>(i);
    m_meshMap[uniqueName] = currentID;
    meshIDs.push_back(currentID);

    // Prepare Instance
    shaderio::Instance inst;
    inst.materialIndex = (materialIdx >= 0 && materialIdx < matIdMap.size())
                             ? matIdMap[materialIdx]
                             : 0;
    inst.meshIndex = currentID;
    inst.hit_group = MaterialType::eDiffuse;
    addInstance(std::move(inst), uniqueName);
  }

  OptimizedPayload optimized =
      processAndOptimizeObj(name, meshes, common::getCacheDir());
  m_pendingMeshes += meshes.size();
  m_pendingOptimizedMesh.push_back(std::move(optimized));

  return meshIDs;
}

/**********************************************************/
TextureID SceneResourcesManager::addTexture(const std::string &name,
                                            const std::string &filename)
/**********************************************************/
{
  auto fileIt = m_fileToTextureMap.find(filename);
  if (fileIt != m_fileToTextureMap.end()) {
    if (m_textureMap.find(name) == m_textureMap.end()) {
      m_textureMap[name] = fileIt->second;
    }
    return fileIt->second;
  }

  std::string finalName = name;
  if (m_textureMap.find(name) != m_textureMap.end()) {
    LOGW("Texture name collision: '%s' already exists for a different file. "
         "Renaming...",
         name.c_str());
    finalName = getUniqueName(m_textureMap, name);
  }

  TextureID textureID = m_device_resources->reserveTextureSlot();
  m_pendingTextures.push_back({filename, textureID});
  m_fileToTextureMap[filename] = textureID;
  m_textureMap[finalName] = textureID;

  return textureID;
}

/**********************************************************/
void SceneResourcesManager::addEnvmap(const std::filesystem::path &filename,
                                      float scale, float rotation)
/**********************************************************/
{
  m_pendingEnvmap.emplace(filename, scale, rotation);
}

/**********************************************************/
InstanceID SceneResourcesManager::addInstance(shaderio::Instance &&instance,
                                              std::string name)
/**********************************************************/
{
  instance.transform = math::composeTransform(
      instance.translation, instance.rotation, instance.scale);
  name = core::trim(name);
  auto it = m_instanceMap.find(name);
  if (it != m_instanceMap.end()) {
    InstanceID existingID = it->second;
    LOGD("[SceneResourcesManager] Replacing Instance: '%s' (ID: %d)",
         name.c_str(), existingID);
    m_scene_resources.instances[existingID] = instance;
    return existingID;
  }

  m_scene_resources.instances.push_back(instance);
  InstanceID id =
      static_cast<InstanceID>(m_scene_resources.instances.size() - 1);

  if (name.empty()) {
    name = "Instance_" + std::to_string(id);
  }

  std::string uniqueName = getUniqueName(m_instanceMap, name);
  m_instanceMap[uniqueName] = id;
  m_rebuild = true;
  return id;
}

/**********************************************************/
MaterialID SceneResourcesManager::addMaterial(shaderio::Material &&material,
                                              std::string name)
/**********************************************************/
{
  name = core::trim(name);
  auto it = m_materialMap.find(name);
  if (it != m_materialMap.end()) {
    MaterialID existingID = it->second;
    LOGD("[SceneResourcesManager] Overwriting existing material: '%s' (ID: %d)",
         name.c_str(), existingID);
    m_scene_resources.materials[existingID] = material;
    return existingID;
  }

  m_scene_resources.materials.push_back(std::move(material));
  MaterialID id =
      static_cast<MaterialID>(m_scene_resources.materials.size() - 1);
  if (name.empty()) {
    name = "Material_" + std::to_string(id);
  }

  std::string uniqueName = getUniqueName(m_materialMap, name);
  m_materialMap[uniqueName] = id;
  m_rebuild = true;
  return id;
}

/**********************************************************/
void SceneResourcesManager::uploadOptimizedMesh(const OptimizedPayload &payload)
/**********************************************************/
{
  if (payload.rawBuffer.empty() || payload.primitives.empty()) {
    LOGW("Warning: Attempting to upload an empty optimized mesh.");
    return;
  }

  // 1. Upload the single unified binary blob
  std::span<const uint8_t> data = payload.rawBuffer;
  m_scene_resources.meshData.emplace_back(payload.rawBuffer);

  const auto [bufferAddr, bufferIndex] = m_device_resources->upload(data);
  const size_t startSize = m_scene_resources.meshes.size();
  m_scene_resources.meshes.reserve(startSize + payload.primitives.size());

  for (auto mesh : payload.primitives) {
    mesh.rawBufferIndex = m_scene_resources.meshData.size() - 1;
    mesh.buffer = reinterpret_cast<uint8_t *>(bufferAddr);
    m_scene_resources.meshes.emplace_back(mesh);
  }

  m_device_resources->addMeshes(payload.primitives.size(), bufferIndex);
  m_pendingMeshes -= payload.primitives.size();
}

/**********************************************************/
void SceneResourcesManager::uploadTextures()
/**********************************************************/
{
  for (const auto &[filename, id] : m_pendingTextures) {
    core::Image raw = core::loadRawImage(filename);
    if (!raw.isValid()) {
      assert(false && "Failed to load texture image!");
      continue;
    }
    m_device_resources->uploadTexture(raw, id);
  }
  m_pendingTextures.clear();
}

/**********************************************************/
void SceneResourcesManager::finalizeSceneResources()
/**********************************************************/
{
  // Start the upload
  m_device_resources->beginUploading();

  // Upload meshes
  for (const auto &mesh : m_pendingOptimizedMesh) {
    uploadOptimizedMesh(mesh);
  }
  assert(m_pendingOptimizedMesh.size() == 0 && m_pendingMeshes == 0);
  m_pendingOptimizedMesh.clear();

  // Upload textures
  uploadTextures();

  // Extract light
  uploadLights();

  m_device_resources->uploadSceneResoures(m_scene_resources);
  m_device_resources->endUploading();
  m_rebuild = true;
}

/**********************************************************/
void SceneResourcesManager::uploadLights()
/**********************************************************/
{
  if (m_pendingEnvmap) {
    const EnvmapInfo envmapInfo =
        m_lights.loadEnvmap(m_pendingEnvmap->filepath, m_pendingEnvmap->scale,
                            m_pendingEnvmap->rotation);
    m_scene_resources.sceneInfo.envmapLight =
        m_lights.uploadEnvmap(envmapInfo, m_device_resources);
    m_pendingEnvmap.reset();
  }
  m_scene_resources.sceneInfo.areaLight =
      m_lights.uploadAreaLights(m_scene_resources, m_device_resources);
  m_scene_resources.sceneInfo.totalAnalyticalPower =
      m_lights.computeAnalyticalLightContribution(m_scene_resources);
}

/**********************************************************/
void SceneResourcesManager::clear()
/**********************************************************/
{
  // Reset the core data structures
  m_scene_resources = Scene{}; // This handles instances, materials, etc.

  // Clear all naming and deduplication maps
  m_materialMap.clear();
  m_meshMap.clear();
  m_textureMap.clear();
  m_instanceMap.clear();
  m_fileToTextureMap.clear();

  // Reset pending task counters and queues
  m_pendingMeshes = 0;
  m_pendingOptimizedMesh.clear();
  m_pendingTextures.clear();

  m_pendingEnvmap.reset();
}

/**********************************************************/
void SceneResourcesManager::update(const CameraPtr &camera)
/**********************************************************/
{
  updateSceneInfo(camera);
  m_rebuild = false;
  m_dirty = false;
}

/**********************************************************/
void SceneResourcesManager::updateSceneInfo(const CameraPtr &camera)
/**********************************************************/
{
  const glm::mat4 &viewMatrix = camera->getViewMatrix();
  const glm::mat4 &projMatrix = camera->getPerspectiveMatrix();

  m_scene_resources.sceneInfo.viewMatrix = viewMatrix;
  m_scene_resources.sceneInfo.projMatrix = projMatrix;
  m_scene_resources.sceneInfo.viewProjMatrix = projMatrix * viewMatrix;
  m_scene_resources.sceneInfo.projInvMatrix = glm::inverse(projMatrix);
  m_scene_resources.sceneInfo.viewInvMatrix = glm::inverse(viewMatrix);
  m_scene_resources.sceneInfo.cameraPosition = camera->getEye();
}

/**********************************************************/
void SceneResourcesManager::onMaterialChange()
/**********************************************************/
{
  m_device_resources->beginUploading();
  m_device_resources->update(m_scene_resources.materials);
  m_device_resources->endUploading();
  setDirty(true);
}

/**********************************************************/
void SceneResourcesManager::onInstanceChange()
/**********************************************************/
{
  m_device_resources->beginUploading();
  m_device_resources->update(m_scene_resources.instances);
  m_device_resources->endUploading();
  setDirty(true);
}

/**********************************************************/
void SceneResourcesManager::setSceneInfo(shaderio::SceneInfo sceneInfo)
/**********************************************************/
{
  m_scene_resources.sceneInfo = sceneInfo;
}

/**********************************************************/
shaderio::Material *
SceneResourcesManager::getMaterialFromName(const std::string &name)
/**********************************************************/
{
  auto it = m_materialMap.find(name);
  return (it != m_materialMap.end()) ? &m_scene_resources.materials[it->second]
                                     : nullptr;
}

/**********************************************************/
const shaderio::MeshPrimitive &
SceneResourcesManager::getMeshFromIdx(uint32_t index) const
/**********************************************************/
{
  assert(index < m_scene_resources.meshes.size());
  return m_scene_resources.meshes[index];
}

/**********************************************************/
MeshID SceneResourcesManager::getMeshIDFromName(const std::string &name) const
/**********************************************************/
{
  auto it = m_meshMap.find(name);
  if (it != m_meshMap.end())
    return it->second;

  throw std::runtime_error(
      fmt::format("[SceneResourcesManager] Mesh name '{}' not found.", name));
}

/**********************************************************/
TextureID
SceneResourcesManager::getTextureIDFromName(const std::string &name) const
/**********************************************************/
{
  auto it = m_textureMap.find(name);
  if (it != m_textureMap.end())
    return it->second;

  throw std::runtime_error(fmt::format(
      "[SceneResourcesManager] Texture name '{}' not found.", name));
}
