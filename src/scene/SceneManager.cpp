#include "SceneManager.hpp"

#include <iostream>

#include <core/file_operations.hpp>

#include "SceneData.hpp"
#include "backend/interfaces/IRenderer.hpp"
#include "core/Math.hpp"

/**********************************************************/
SceneManager::SceneManager(const std::shared_ptr<IRenderer> &renderer)
/**********************************************************/
{
  m_scene_resources.init(renderer->deviceResources());
}

/**********************************************************/
void SceneManager::clear()
/**********************************************************/
{
  m_scene_resources.clear();
}

/**********************************************************/
void SceneManager::update()
/**********************************************************/
{
  m_scene_resources.update(m_camera);
}

/**********************************************************/
Scene *SceneManager::getScenePtr()
/**********************************************************/
{
  return &gltfResources();
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

/**********************************************************/
void SceneManager::buildSceneFromData(
    const SceneData &data, const std::vector<std::filesystem::path> &searchDirs)
/**********************************************************/
{
  // 1. Load Meshes & Keep ID Mapping
  // Map: Index in SceneData -> Actual MeshID in Manager
  std::vector<MeshID> meshIdMap;
  for (const auto &val : data.meshPaths) {
    std::string fullPath = core::findFile(val.path, searchDirs);
    std::vector<MeshID> newMeshIds =
        m_scene_resources.loadModel(val.name, fullPath);
    meshIdMap.insert(meshIdMap.end(), newMeshIds.begin(), newMeshIds.end());
  }

  // 2. Load Textures & Keep ID Mapping
  std::vector<TextureID> texIdMap;
  for (const auto &val : data.texturePaths) {
    std::string fullPath = core::findFile(val.path, searchDirs);
    texIdMap.push_back(m_scene_resources.loadTexture(val.name, fullPath));
  }

  // 3. Create Materials & Keep ID Mapping
  std::vector<MaterialID> matIdMap;
  for (const auto &matData : data.materials) {
    shaderio::Material info{};
    info.baseColorFactor = matData.baseColor;
    info.metallicFactor = matData.metallic;
    info.roughnessFactor = matData.roughness;
    info.ior = matData.ior;

    // Resolve Texture Index
    if (matData.textureIndex >= 0 && matData.textureIndex < texIdMap.size()) {
      info.baseColorTextureIndex =
          static_cast<int>(texIdMap[matData.textureIndex]);
    }

    matIdMap.push_back(
        m_scene_resources.addMaterial(std::move(info), matData.name));
  }

  // 4. Create Instances
  for (const auto &instData : data.instances) {
    shaderio::Instance info{};
    info.translation = instData.translation;
    info.scale = instData.scale;
    info.rotation = math::eulerToQuat(instData.rotation);
    info.hit_group = instData.hitGroup;

    // Resolve Mesh ID
    info.meshIndex = m_scene_resources.getMeshIDFromName(instData.meshId);
    if (info.meshIndex == -1) {
      std::cerr << "Invalid Mesh Index for instance: " << instData.name
                << "[Skipping]" << std::endl;
      continue;
    }

    // Resolve Material ID
    if (instData.materialIndex >= 0 &&
        instData.materialIndex < matIdMap.size()) {
      info.materialIndex = matIdMap[instData.materialIndex];
    } else {
      info.materialIndex = 0;
      // Handle default material if needed
    }

    m_scene_resources.addInstance(std::move(info), instData.name);
  }

  // 5. Fill SceneInfo (Lights & Globals)
  shaderio::SceneInfo sceneInfo = m_scene_resources.sceneInfo();
  sceneInfo.useSky = data.useSky;
  sceneInfo.backgroundColor = data.backgroundColor;
  sceneInfo.numLights = 0;

  for (const auto &l : data.lights) {
    if (sceneInfo.numLights >= MAX_LIGHTS) {
      break;
    }

    auto &light = sceneInfo.punctualLights[sceneInfo.numLights];
    light.position = l.position;
    light.color = l.color;
    light.intensity = l.intensity;
    light.type = l.type;

    sceneInfo.numLights++;
  }

  m_scene_resources.setSceneInfo(sceneInfo);

  // 2. Setup Camera
  m_camera->setLookat(data.camera.eye, data.camera.center, data.camera.up);
  m_camera->setClipPlanes(data.camera.clip);
  m_camera->setClean();
}

/**********************************************************/
Scene &SceneManager::gltfResources()
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
const Scene &SceneManager::gltfResources() const
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
SceneResourcesManager &SceneManager::sceneResourceManager()
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
const SceneResourcesManager &SceneManager::sceneResourceManager() const
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
shaderio::SceneInfo &SceneManager::sceneInfo()
/**********************************************************/
{
  return m_scene_resources.sceneInfo();
}

// --------------------------------------------------
// Camera
// --------------------------------------------------

/**********************************************************/
void SceneManager::setCamera(CameraPtr camera)
/**********************************************************/
{
  m_camera = std::move(camera);
}

/**********************************************************/
CameraPtr SceneManager::camera() const
/**********************************************************/
{
  return m_camera;
}
