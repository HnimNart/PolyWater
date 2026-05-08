#include "scene_manager.hpp"

#include <iostream>

#include <core/file_operations.hpp>

#include "core/math.hpp"
#include "core/timers.hpp"
#include "renderer/interfaces/renderer_interface.hpp"
#include "scene_data.hpp"

/**********************************************************/
SceneManager::SceneManager(std::shared_ptr<IDeviceAssets> deviceResources)
/**********************************************************/
{
  m_scene_resources.init(std::move(deviceResources));
}

/**********************************************************/
void SceneManager::clear()
/**********************************************************/
{
  m_scene_resources.clear();
}

/**********************************************************/
void SceneManager::onPreRender()
/**********************************************************/
{
  m_scene_resources.update(m_camera);
}

/**********************************************************/
Scene* SceneManager::getScenePtr()
/**********************************************************/
{
  return &gltfResources();
}

// --------------------------------------------------
// Scene / Resources
// --------------------------------------------------

/**********************************************************/
void SceneManager::buildSceneFromData(
    const SceneData& data, const std::vector<std::filesystem::path>& searchDirs)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();
  // 1. Load Meshes & Keep ID Mapping
  std::vector<MeshID> meshIdMap;
  for (const auto& val : data.meshPaths)
  {
    std::string fullPath = core::findFile(val.path, searchDirs);
    std::vector<MeshID> newMeshIds =
        m_scene_resources.loadModel(fullPath, val.name);
    meshIdMap.insert(meshIdMap.end(), newMeshIds.begin(), newMeshIds.end());
  }

  // 2. Load Textures & Keep ID Mapping
  for (const auto& val : data.texturePaths)
  {
    std::string fullPath = core::findFile(val.path, searchDirs);
    m_scene_resources.addTexture(val.name, fullPath);
  }

  // 3. Create Materials & Keep ID Mapping
  for (const auto& matData : data.materials)
  {
    shaderio::Material info{};
    info.baseColorFactor = matData.baseColor;
    info.metallicFactor = fmaxf(1e-4f, matData.metallic);
    info.roughnessFactor = fmaxf(1e-3f, matData.roughness);
    info.emission = matData.emission;
    info.ior = matData.ior;

    // Resolve Texture Index
    if (!matData.textureId.empty())
    {
      info.baseColorTextureIndex =
          m_scene_resources.getTextureIDFromName(matData.textureId);
    }
    m_scene_resources.addMaterial(std::move(info), matData.name);
  }

  // 4. Create Instances
  for (const auto& instData : data.instances)
  {
    shaderio::Instance inst{};
    inst.translation = instData.translation;
    inst.scale = instData.scale;
    inst.rotation = core::eulerToQuat(instData.rotation);
    inst.hit_group = instData.hitGroup;

    // Resolve Mesh ID
    inst.meshIndex = m_scene_resources.getMeshIDFromName(instData.meshId);
    if (inst.meshIndex == -1)
    {
      std::cerr << "Invalid Mesh Index for instance: " << instData.name
                << "[Skipping]" << std::endl;
      continue;
    }

    // Resolve Material ID
    inst.materialIndex =
        m_scene_resources.materialMap().at(instData.materialId);

    m_scene_resources.addInstance(std::move(inst), instData.name);
  }

  // 5. Fill SceneInfo (Lights & Globals)
  shaderio::SceneInfo sceneInfo = m_scene_resources.sceneInfo();
  sceneInfo.useSky = data.useSky;
  sceneInfo.backgroundColor = data.backgroundColor;
  sceneInfo.numLights = 0;

  for (const auto& l : data.lights)
  {
    if (sceneInfo.numLights >= MAX_LIGHTS)
    {
      break;
    }

    auto& light = sceneInfo.punctualLights[sceneInfo.numLights];
    light.position = l.position;
    light.color = l.color;
    light.intensity = l.intensity;
    light.type = l.type;

    sceneInfo.numLights++;
  }
  sceneInfo.numLights = std::max(1, sceneInfo.numLights);

  if (data.envmap.useEnvMap)
  {
    m_scene_resources.addEnvmap(
        core::findFile(data.envmap.path, searchDirs).string(),
        data.envmap.scale, data.envmap.rotation);
    sceneInfo.useEnv = true;
    sceneInfo.useSky = false;
  }
  m_scene_resources.setSceneInfo(sceneInfo);

  // 2. Setup Camera
  m_camera->setLookat(data.camera.eye, data.camera.center, data.camera.up);
  m_camera->setClipPlanes(data.camera.clip);
  m_camera->setClean();
}

/**********************************************************/
Scene& SceneManager::gltfResources()
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
const Scene& SceneManager::gltfResources() const
/**********************************************************/
{
  return m_scene_resources.data();
}

/**********************************************************/
SceneResourcesManager& SceneManager::sceneResourceManager()
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
const SceneResourcesManager& SceneManager::sceneResourceManager() const
/**********************************************************/
{
  return m_scene_resources;
}

/**********************************************************/
shaderio::SceneInfo& SceneManager::sceneInfo()
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
